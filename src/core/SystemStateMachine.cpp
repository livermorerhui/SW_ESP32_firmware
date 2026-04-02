#include "SystemStateMachine.h"
#include "core/LogMarkers.h"
#include "modules/laser/LaserModule.h"
#include "modules/wave/WaveModule.h"

namespace {

const char* severityName(FaultSeverity severity) {
  switch (severity) {
    case FaultSeverity::INFO_ONLY: return "INFO_ONLY";
    case FaultSeverity::WARNING_ONLY: return "WARNING_ONLY";
    case FaultSeverity::BLOCKING_FAULT: return "BLOCKING_FAULT";
  }
  return "UNKNOWN";
}

const char* originName(FaultOrigin origin) {
  switch (origin) {
    case FaultOrigin::SAFETY_RUNTIME: return "SAFETY_RUNTIME";
    case FaultOrigin::MEASUREMENT_CAPABILITY: return "MEASUREMENT_CAPABILITY";
    case FaultOrigin::COMMUNICATION_SESSION: return "COMMUNICATION_SESSION";
    case FaultOrigin::COMMAND_INPUT: return "COMMAND_INPUT";
  }
  return "UNKNOWN";
}

FaultOrigin faultOrigin(FaultCode code) {
  switch (code) {
    case FaultCode::USER_LEFT_PLATFORM:
    case FaultCode::FALL_SUSPECTED:
      return FaultOrigin::SAFETY_RUNTIME;
    case FaultCode::BLE_DISCONNECTED:
      return FaultOrigin::COMMUNICATION_SESSION;
    case FaultCode::MEASUREMENT_UNAVAILABLE:
      return FaultOrigin::MEASUREMENT_CAPABILITY;
    case FaultCode::INVALID_PARAM:
    case FaultCode::NOT_ARMED:
    case FaultCode::FAULT_LOCKED:
    case FaultCode::NONE:
      return FaultOrigin::COMMAND_INPUT;
  }
  return FaultOrigin::COMMAND_INPUT;
}

}  // namespace

SystemStateMachine* SystemStateMachine::active_instance = nullptr;

void SystemStateMachine::begin(EventBus* eb, WaveModule* waveModule) {
  active_instance = this;
  bus = eb;
  wave = waveModule;
  laser = nullptr;
  st = TopState::IDLE;
  blocking_fault_code = FaultCode::NONE;
  pause_reason_code = FaultCode::NONE;
  warning_fault_code = FaultCode::NONE;
  snapshot_reason_code = FaultCode::NONE;
  snapshot_safety_effect = SafetySignalKind::NONE;
  fault_ms = 0;
  clear_window_active = false;
  clear_candidate_ms = 0;
  fall_stop_enabled = FALL_STOP_ENABLED_DEFAULT;
  motion_sampling_mode_enabled = false;
  last_suppressed_fall_notice_ms = 0;
  runtime_ready = false;
  start_ready = false;
  start_ready_stable_weight_kg = 0.0f;
  sensor_healthy = false;
  sensor_state_known = false;
  pending_stop_reason_text = nullptr;
  pending_stop_source = VerificationStopSource::NONE;
  last_stop_reason_text = "NONE";
  last_stop_source = VerificationStopSource::NONE;
  syncSnapshotDecisionContext();
  emitState();
}

void SystemStateMachine::attachLaserModule(const LaserModule* laserModule) {
  laser = laserModule;
}

const SystemStateMachine* SystemStateMachine::activeInstance() {
  return active_instance;
}

TopState SystemStateMachine::state() const {
  return st;
}

bool SystemStateMachine::isFaultLocked() const {
  return blocking_fault_code != FaultCode::NONE;
}

FaultCode SystemStateMachine::activeFault() const {
  return visibleReasonCode();
}

bool SystemStateMachine::runtimeReady() const {
  return effectiveRuntimeReady();
}

FaultCode SystemStateMachine::currentReasonCode() const {
  return snapshot_reason_code;
}

SafetySignalKind SystemStateMachine::currentSafetyEffect() const {
  return snapshot_safety_effect;
}

bool SystemStateMachine::laserConfiguredInstalled() const {
  return laser ? laser->laserInstalled() : false;
}

bool SystemStateMachine::laserlessRuntimeStrategyActive() const {
  return !laserConfiguredInstalled();
}

bool SystemStateMachine::effectiveRuntimeReady() const {
  return laserlessRuntimeStrategyActive() ? true : runtime_ready;
}

bool SystemStateMachine::effectiveStartReady() const {
  return laserlessRuntimeStrategyActive() ? true : start_ready;
}

bool SystemStateMachine::effectiveLaserAvailable() const {
  return laser ? laser->laserAvailable() : false;
}

bool SystemStateMachine::effectiveProtectionDegraded() const {
  if (!laserConfiguredInstalled()) {
    return true;
  }
  return !effectiveLaserAvailable();
}

PlatformSnapshot SystemStateMachine::snapshot() const {
  PlatformSnapshot out{};
  out.topState = st;
  out.userPresent = laser ? laser->isUserPresent() : false;
  out.runtimeReady = effectiveRuntimeReady();
  out.startReady = effectiveStartReady();
  out.baselineReady = laser ? laser->baselineReady() : false;
  out.waveOutputActive = wave ? wave->isOutputActive() : false;
  out.currentReasonCode = snapshot_reason_code;
  out.currentSafetyEffect = snapshot_safety_effect;
  out.stableWeightKg = laser ? laser->stableWeightKg() : 0.0f;
  out.platformModel = laser ? laser->platformModel() : PlatformModel::PLUS;
  out.laserInstalled = laserConfiguredInstalled();
  out.laserAvailable = effectiveLaserAvailable();
  out.protectionDegraded = effectiveProtectionDegraded();

  if (wave) {
    float ignoredIntensityNormalized = 0.0f;
    wave->getSummaryParams(out.currentFrequencyHz, out.currentIntensity, ignoredIntensityNormalized);
  }

  return out;
}

bool SystemStateMachine::fallStopEnabled() const {
  return fall_stop_enabled;
}

const char* SystemStateMachine::fallStopModeName() const {
  return fall_stop_enabled ? "ENABLED_STOP" : "DETECT_ONLY";
}

bool SystemStateMachine::motionSamplingModeEnabled() const {
  return motion_sampling_mode_enabled;
}

bool SystemStateMachine::startReady() const {
  return effectiveStartReady();
}

bool SystemStateMachine::leaveDetectionEnabled() const {
  // leave 判定必须和正式 start readiness 对齐。
  // 这里不用 runtime_ready 直接做 owner，避免再次把 presence 误当成“已站稳可运行”。
  // 这只是本阶段的固件侧收口，不代表 stable/runtime/leave 全语义治理已经结束。
  return st == TopState::RUNNING && laserConfiguredInstalled() && start_ready;
}

const char* SystemStateMachine::lastStopReasonText() const {
  return last_stop_reason_text ? last_stop_reason_text : "NONE";
}

const char* SystemStateMachine::lastStopSourceText() const {
  return verificationStopSourceName(last_stop_source);
}

void SystemStateMachine::rememberStopContext(
    const char* stopReasonText,
    VerificationStopSource stopSource) {
  last_stop_reason_text = stopReasonText ? stopReasonText : "NONE";
  last_stop_source = stopSource;
}

void SystemStateMachine::clearPendingStopContext() {
  pending_stop_reason_text = nullptr;
  pending_stop_source = VerificationStopSource::NONE;
}

const char* SystemStateMachine::resolvedStopReasonText(
    FaultCode code,
    const char* fallback) const {
  if (pending_stop_reason_text && pending_stop_reason_text[0] != '\0') {
    return pending_stop_reason_text;
  }
  if (fallback && fallback[0] != '\0') {
    return fallback;
  }
  if (code != FaultCode::NONE) {
    return faultCodeName(code);
  }
  return "MANUAL_STOP";
}

VerificationStopSource SystemStateMachine::resolvedStopSource(
    VerificationStopSource fallback) const {
  if (pending_stop_source != VerificationStopSource::NONE) {
    return pending_stop_source;
  }
  if (fallback != VerificationStopSource::NONE) {
    return fallback;
  }
  return VerificationStopSource::USER_MANUAL_OTHER;
}

bool SystemStateMachine::canEnterArmedState() const {
  if (pause_reason_code != FaultCode::NONE) {
    return false;
  }
  return effectiveRuntimeReady() && effectiveStartReady();
}

void SystemStateMachine::emitStopEvent(
    FaultCode code,
    SafetySignalKind safety,
    TopState targetState,
    const char* stopReasonText,
    VerificationStopSource stopSource) {
  if (!bus) return;

  Event e{};
  e.type = EventType::STOP;
  e.fault = code;
  e.safety = safety;
  e.state = targetState;
  e.ts_ms = millis();
  strlcpy(e.stopReasonText, stopReasonText ? stopReasonText : "NONE", sizeof(e.stopReasonText));
  strlcpy(e.stopSourceText, verificationStopSourceName(stopSource), sizeof(e.stopSourceText));
  bus->publish(e);
}

void SystemStateMachine::emitState() {
  if (!bus) return;

  Event e{};
  e.type = EventType::STATE;
  e.state = st;
  e.ts_ms = millis();

  bus->publish(e);
}

void SystemStateMachine::emitFault(FaultCode code) {
  if (!bus) return;

  Event e{};
  e.type = EventType::FAULT;
  e.fault = code;
  e.ts_ms = millis();

  bus->publish(e);
}

void SystemStateMachine::emitSafety(FaultCode code, SafetySignalKind safety) {
  if (!bus) return;

  Event e{};
  e.type = EventType::SAFETY;
  e.fault = code;
  e.safety = safety;
  e.state = st;
  e.waveStopped = wave ? !wave->isOutputActive() : (st != TopState::RUNNING);
  e.ts_ms = millis();

  bus->publish(e);
}

FaultCode SystemStateMachine::visibleReasonCode() const {
  if (blocking_fault_code != FaultCode::NONE) return blocking_fault_code;
  if (pause_reason_code != FaultCode::NONE) return pause_reason_code;
  return warning_fault_code;
}

SafetySignalKind SystemStateMachine::visibleSafetySignal() const {
  if (blocking_fault_code != FaultCode::NONE) return SafetySignalKind::ABNORMAL_STOP;
  if (pause_reason_code != FaultCode::NONE) return SafetySignalKind::RECOVERABLE_PAUSE;
  if (warning_fault_code != FaultCode::NONE) return SafetySignalKind::WARNING_ONLY;
  return SafetySignalKind::NONE;
}

void SystemStateMachine::emitVisibleSignals() {
  const FaultCode visible = visibleReasonCode();
  const SafetySignalKind safety = visibleSafetySignal();
  syncSnapshotDecisionContext();
  emitFault(visible);
  emitSafety(visible, safety);
}

void SystemStateMachine::syncSnapshotDecisionContext() {
  const FaultCode visible = visibleReasonCode();
  const SafetySignalKind safety = visibleSafetySignal();

  if (visible != FaultCode::NONE || safety != SafetySignalKind::NONE) {
    snapshot_reason_code = visible;
    snapshot_safety_effect = safety;
    return;
  }

  snapshot_reason_code = FaultCode::NONE;
  snapshot_safety_effect = SafetySignalKind::NONE;
}

void SystemStateMachine::setState(TopState s) {
  if (st == s) return;

  TopState prev = st;
  st = s;

  // SystemStateMachine is the only gate allowed to start/stop output.
  if (wave) {
    if (st == TopState::RUNNING) {
      Serial.printf(
          "[LAYER:STATE_OWNER] transition=%s->%s action=wave.start blocking_fault=%s pause_reason=%s\n",
          topStateName(prev),
          topStateName(st),
          faultCodeName(blocking_fault_code),
          faultCodeName(pause_reason_code));
      wave->start();
    } else {
      Serial.printf(
          "[LAYER:STATE_OWNER] transition=%s->%s action=wave.stopSoft blocking_fault=%s pause_reason=%s\n",
          topStateName(prev),
          topStateName(st),
          faultCodeName(blocking_fault_code),
          faultCodeName(pause_reason_code));
      wave->stopSoft();
    }
  }

  syncSnapshotDecisionContext();
  Serial.printf("%s [FSM] STATE %s -> %s\n", LogMarker::kFsm, topStateName(prev), topStateName(st));
  emitState();
}

void SystemStateMachine::setWarningFault(FaultCode code, const char* detail) {
  if (warning_fault_code == code) return;

  warning_fault_code = code;
  Serial.printf("%s [FSM] SAFETY reason=%s effect=%s detail=%s\n",
                LogMarker::kSafety,
                faultCodeName(code),
                safetySignalName(SafetySignalKind::WARNING_ONLY),
                detail ? detail : "n/a");
  Serial.printf("%s [FAULT] WARN name=%s origin=%s severity=%s detail=%s\n",
                LogMarker::kSafety,
                faultCodeName(code),
                originName(faultOrigin(code)),
                severityName(FaultSeverity::WARNING_ONLY),
                detail ? detail : "n/a");

  if (blocking_fault_code == FaultCode::NONE && pause_reason_code == FaultCode::NONE) {
    emitVisibleSignals();
  }
}

void SystemStateMachine::clearWarningFault(FaultCode code, const char* detail) {
  if (warning_fault_code != code) return;

  warning_fault_code = FaultCode::NONE;
  Serial.printf("%s [FAULT] CLEAR severity=%s name=%s detail=%s\n",
                LogMarker::kClear,
                severityName(FaultSeverity::WARNING_ONLY),
                faultCodeName(code),
                detail ? detail : "n/a");

  if (blocking_fault_code == FaultCode::NONE && pause_reason_code == FaultCode::NONE) {
    emitVisibleSignals();
  }
}

void SystemStateMachine::enterBlockingFault(FaultCode code, const char* detail) {
  uint32_t now = millis();
  bool isNewFault = (blocking_fault_code != code);
  const bool wasRunning = (st == TopState::RUNNING);

  blocking_fault_code = code;
  fault_ms = now;
  clear_window_active = false;
  clear_candidate_ms = 0;
  pause_reason_code = FaultCode::NONE;

  if (isNewFault) {
    Serial.printf("%s [FSM] SAFETY reason=%s effect=%s detail=%s\n",
                  LogMarker::kSafety,
                  faultCodeName(code),
                  safetySignalName(SafetySignalKind::ABNORMAL_STOP),
                  detail ? detail : "n/a");
    Serial.printf("%s [FAULT] BLOCK name=%s origin=%s severity=%s detail=%s\n",
                  LogMarker::kFaultBlock,
                  faultCodeName(code),
                  originName(faultOrigin(code)),
                  severityName(FaultSeverity::BLOCKING_FAULT),
                  detail ? detail : "n/a");
    Serial.printf("%s [FSM] FAULT ENTER reason=%s detail=%s\n",
                  LogMarker::kFaultBlock,
                  faultCodeName(code),
                  detail ? detail : "n/a");
  }

  if (wasRunning) {
    const char* stopReasonText = resolvedStopReasonText(code, faultCodeName(code));
    const VerificationStopSource stopSource =
        resolvedStopSource(VerificationStopSource::FORMAL_SAFETY_OTHER);
    rememberStopContext(stopReasonText, stopSource);
    emitStopEvent(code, SafetySignalKind::ABNORMAL_STOP, TopState::FAULT_STOP, stopReasonText, stopSource);
  }
  clearPendingStopContext();

  setState(TopState::FAULT_STOP);
  if (isNewFault) {
    emitVisibleSignals();
  }
}

void SystemStateMachine::enterRecoverablePause(FaultCode code, const char* detail) {
  if (pause_reason_code == code) return;

  pause_reason_code = code;
  clear_window_active = false;
  clear_candidate_ms = 0;

  Serial.printf("%s [FSM] SAFETY reason=%s effect=%s detail=%s\n",
                LogMarker::kSafety,
                faultCodeName(code),
                safetySignalName(SafetySignalKind::RECOVERABLE_PAUSE),
                detail ? detail : "n/a");

  if (code == FaultCode::USER_LEFT_PLATFORM) {
    Serial.printf("%s [LEAVE] confirmed action=RECOVERABLE_PAUSE\n", LogMarker::kSafety);
  }

  if (st == TopState::RUNNING) {
    Serial.printf("%s [FSM] RECOVERABLE_PAUSE closing running path source=%s\n",
                  LogMarker::kSafety,
                  faultCodeName(code));
    requestStop();
  }

  syncReadyState();
  emitVisibleSignals();
}

bool SystemStateMachine::recoveryConditionMet() const {
  switch (blocking_fault_code) {
    case FaultCode::USER_LEFT_PLATFORM:
      return runtime_ready && start_ready;
    case FaultCode::FALL_SUSPECTED:
      return !runtime_ready;
    case FaultCode::BLE_DISCONNECTED:
      return true;
    case FaultCode::MEASUREMENT_UNAVAILABLE:
      return sensor_state_known && sensor_healthy;
    default:
      return true;
  }
}

const char* SystemStateMachine::recoveryDetail() const {
  switch (blocking_fault_code) {
    case FaultCode::USER_LEFT_PLATFORM:
      if (!runtime_ready) return "runtime_ready=0";
      return start_ready ? "baseline_ready=1" : "baseline_ready=0";
    case FaultCode::FALL_SUSPECTED:
      return runtime_ready ? "await_runtime_clear" : "runtime_ready=0";
    case FaultCode::BLE_DISCONNECTED:
      return "disconnect_cooldown_complete";
    case FaultCode::MEASUREMENT_UNAVAILABLE:
      return sensor_healthy ? "sensor_healthy=1" : "sensor_healthy=0";
    default:
      return "n/a";
  }
}

void SystemStateMachine::clearBlockingFault(const char* detail) {
  FaultCode cleared = blocking_fault_code;
  blocking_fault_code = FaultCode::NONE;
  clear_window_active = false;
  clear_candidate_ms = 0;

  Serial.printf("%s [FAULT] CLEAR severity=%s name=%s detail=%s\n",
                LogMarker::kClear,
                severityName(FaultSeverity::BLOCKING_FAULT),
                faultCodeName(cleared),
                detail ? detail : "n/a");
  Serial.printf("%s [FSM] FAULT CLEAR reason=%s detail=%s\n",
                LogMarker::kClear,
                faultCodeName(cleared),
                detail ? detail : "n/a");

  syncReadyState();
  emitVisibleSignals();
}

void SystemStateMachine::clearRecoverablePause(FaultCode code, const char* detail) {
  if (pause_reason_code != code) return;

  pause_reason_code = FaultCode::NONE;
  Serial.printf("%s [FSM] SAFETY CLEAR reason=%s effect=%s detail=%s\n",
                LogMarker::kClear,
                faultCodeName(code),
                safetySignalName(SafetySignalKind::RECOVERABLE_PAUSE),
                detail ? detail : "n/a");

  syncReadyState();
  emitVisibleSignals();
}

void SystemStateMachine::maybeClearBlockingFault(uint32_t now) {
  if (blocking_fault_code == FaultCode::NONE || st != TopState::FAULT_STOP) return;

  if (now - fault_ms < FAULT_COOLDOWN_MS) {
    clear_window_active = false;
    return;
  }

  if (!recoveryConditionMet()) {
    clear_window_active = false;
    return;
  }

  if (!clear_window_active) {
    clear_window_active = true;
    clear_candidate_ms = now;
    return;
  }

  if (now - clear_candidate_ms >= CLEAR_CONFIRM_MS) {
    clearBlockingFault(recoveryDetail());
  }
}

void SystemStateMachine::syncReadyState() {
  if (blocking_fault_code != FaultCode::NONE) {
    setState(TopState::FAULT_STOP);
    return;
  }

  if (st == TopState::RUNNING) return;
  if (pause_reason_code != FaultCode::NONE) {
    setState(TopState::IDLE);
    return;
  }
  setState(canEnterArmedState() ? TopState::ARMED : TopState::IDLE);
}

void SystemStateMachine::onUserOn() {
  setRuntimeReady(true);
}

void SystemStateMachine::onUserOff() {
  runtime_ready = false;

  if (!leaveDetectionEnabled()) {
    Serial.printf(
        "%s [LEAVE] suppress enabled=0 reason=%s state=%s baseline_ready=%d stable_weight_kg=%.2f runtime_ready=%d\n",
        LogMarker::kSafety,
        start_ready ? "not_running" : "baseline_not_ready",
        topStateName(st),
        start_ready ? 1 : 0,
        start_ready_stable_weight_kg,
        runtime_ready ? 1 : 0);
    syncReadyState();
    return;
  }

  Serial.printf(
      "%s [LEAVE] trigger enabled=1 state=%s baseline_ready=%d stable_weight_kg=%.2f runtime_ready=%d policy=%s\n",
      LogMarker::kSafety,
      topStateName(st),
      start_ready ? 1 : 0,
      start_ready_stable_weight_kg,
      runtime_ready ? 1 : 0,
      SAFETY_POLICY_USER_LEFT_RECOVERABLE_PAUSE ? "RECOVERABLE_PAUSE" : "BLOCKING_FAULT");

  if (SAFETY_POLICY_USER_LEFT_RECOVERABLE_PAUSE) {
    enterRecoverablePause(FaultCode::USER_LEFT_PLATFORM, "user_left_platform");
    return;
  }

  enterBlockingFault(FaultCode::USER_LEFT_PLATFORM, "user_left_platform");
}

void SystemStateMachine::onBleConnected() {
  clearWarningFault(FaultCode::BLE_DISCONNECTED, "ble_connected");
  if (sensor_state_known && !sensor_healthy) {
    setWarningFault(FaultCode::MEASUREMENT_UNAVAILABLE, "sensor_still_unhealthy");
  }
}

void SystemStateMachine::onBleDisconnected() {
  if (SAFETY_POLICY_DISCONNECT_STOPS_WAVE) {
    enterBlockingFault(FaultCode::BLE_DISCONNECTED, "ble_disconnected");
    return;
  }

  setWarningFault(FaultCode::BLE_DISCONNECTED, "ble_disconnected");
}

void SystemStateMachine::onFallSuspected() {
  applyFallSuspectedAction(decideFallSuspectedAction());
}

void SystemStateMachine::setFallStopEnabled(bool enabled) {
  if (fall_stop_enabled == enabled) return;

  fall_stop_enabled = enabled;
  last_suppressed_fall_notice_ms = 0;
  Serial.printf("%s [FALL_STOP] enabled=%d mode=%s\n",
                LogMarker::kSafety,
                enabled ? 1 : 0,
                fallStopModeName());
}

FallStopActionDecision SystemStateMachine::decideFallSuspectedAction() const {
  FallStopActionDecision decision{};
  decision.stopCandidateDetected = true;
  decision.fallStopEnabled = fall_stop_enabled;
  decision.stopReason = FaultCode::FALL_SUSPECTED;

  if (!fall_stop_enabled) {
    decision.shouldExecuteStop = false;
    decision.stopSuppressedBySwitch = true;
    decision.safetySignal = SafetySignalKind::WARNING_ONLY;
    decision.detail = "fall_stop_disabled";
    return decision;
  }

  decision.shouldExecuteStop = true;
  if (SAFETY_POLICY_FALL_ABNORMAL_STOP) {
    decision.safetySignal = SafetySignalKind::ABNORMAL_STOP;
    decision.detail = "fall_stop_active";
    return decision;
  }

  decision.safetySignal = SafetySignalKind::RECOVERABLE_PAUSE;
  decision.detail = "fall_pause_override";
  return decision;
}

void SystemStateMachine::applyFallSuspectedAction(const FallStopActionDecision& decision) {
  if (!decision.stopCandidateDetected) return;

  if (decision.stopSuppressedBySwitch) {
    const uint32_t now = millis();
    if (last_suppressed_fall_notice_ms == 0 ||
        now - last_suppressed_fall_notice_ms >= MOTION_SAMPLING_SUPPRESSED_FALL_NOTICE_INTERVAL_MS) {
      last_suppressed_fall_notice_ms = now;
      emitSafety(decision.stopReason, SafetySignalKind::WARNING_ONLY);
    }
    return;
  }

  last_suppressed_fall_notice_ms = 0;
  pending_stop_reason_text = decision.verificationStopReason;
  pending_stop_source = decision.verificationStopSource;
  if (decision.safetySignal == SafetySignalKind::ABNORMAL_STOP) {
    enterBlockingFault(decision.stopReason, decision.detail);
    return;
  }

  enterRecoverablePause(decision.stopReason, decision.detail);
}

void SystemStateMachine::setMotionSamplingMode(bool enabled) {
  if (motion_sampling_mode_enabled == enabled) return;

  motion_sampling_mode_enabled = enabled;
  last_suppressed_fall_notice_ms = 0;
  Serial.printf("[MOTION_SAMPLE_MODE] enabled=%s\n", enabled ? "true" : "false");
}

void SystemStateMachine::onSensorErr() {
  setSensorHealthy(false);
}

void SystemStateMachine::setRuntimeReady(bool ready) {
  const bool changed = (runtime_ready != ready);
  runtime_ready = ready;

  if (changed) {
    Serial.printf(
        "%s [READY] runtime_ready=%d user_present=%d baseline_ready=%d stable_weight_kg=%.2f note=presence_only\n",
        LogMarker::kFsm,
        runtime_ready ? 1 : 0,
        runtime_ready ? 1 : 0,
        start_ready ? 1 : 0,
        start_ready_stable_weight_kg);
  }

  if (pause_reason_code == FaultCode::USER_LEFT_PLATFORM && runtime_ready && start_ready) {
    clearRecoverablePause(FaultCode::USER_LEFT_PLATFORM, "runtime_ready=1 baseline_ready=1");
    return;
  }

  uint32_t now = millis();
  if (blocking_fault_code != FaultCode::NONE) {
    maybeClearBlockingFault(now);
    return;
  }

  syncReadyState();
}

void SystemStateMachine::setStartReadiness(bool ready, float stableWeightKg) {
  const float nextStableWeightKg = ready ? stableWeightKg : 0.0f;
  const bool changed =
      (start_ready != ready) ||
      (ready && start_ready_stable_weight_kg != nextStableWeightKg);

  start_ready = ready;
  start_ready_stable_weight_kg = nextStableWeightKg;

  if (changed) {
    Serial.printf(
        "%s [READY] baseline_ready=%d stable_weight_kg=%.2f runtime_ready=%d leave_enabled=%d note=formal_start_gate\n",
        LogMarker::kBaselineReady,
        start_ready ? 1 : 0,
        start_ready_stable_weight_kg,
        runtime_ready ? 1 : 0,
        leaveDetectionEnabled() ? 1 : 0);
  }

  if (pause_reason_code == FaultCode::USER_LEFT_PLATFORM && runtime_ready && start_ready) {
    clearRecoverablePause(FaultCode::USER_LEFT_PLATFORM, "runtime_ready=1 baseline_ready=1");
    return;
  }

  uint32_t now = millis();
  if (blocking_fault_code != FaultCode::NONE) {
    maybeClearBlockingFault(now);
    return;
  }

  syncReadyState();
}

void SystemStateMachine::setSensorHealthy(bool healthy) {
  if (!laserConfiguredInstalled()) {
    sensor_state_known = false;
    sensor_healthy = false;

    if (blocking_fault_code == FaultCode::MEASUREMENT_UNAVAILABLE) {
      clearBlockingFault("laser_not_installed");
      return;
    }

    clearWarningFault(FaultCode::MEASUREMENT_UNAVAILABLE, "laser_not_installed");
    syncReadyState();
    return;
  }

  bool changed = (!sensor_state_known || sensor_healthy != healthy);
  sensor_state_known = true;
  sensor_healthy = healthy;

  if (!healthy && changed) {
    if (SAFETY_POLICY_MEASUREMENT_UNAVAILABLE_STOPS_WAVE && st == TopState::RUNNING) {
      enterBlockingFault(FaultCode::MEASUREMENT_UNAVAILABLE, "sensor_unhealthy");
      return;
    }

    setWarningFault(FaultCode::MEASUREMENT_UNAVAILABLE, "sensor_unhealthy");
    return;
  }

  if (healthy) {
    clearWarningFault(FaultCode::MEASUREMENT_UNAVAILABLE, "sensor_recovered");
  }

  uint32_t now = millis();
  if (blocking_fault_code != FaultCode::NONE) {
    maybeClearBlockingFault(now);
    return;
  }

  syncReadyState();
}

bool SystemStateMachine::requestStart(FaultCode& reason) {
  uint32_t now = millis();
  if (blocking_fault_code != FaultCode::NONE) {
    maybeClearBlockingFault(now);
  }

  const PlatformSnapshot startSnapshot = snapshot();
  const bool runtimeReadyNow = effectiveRuntimeReady();
  const bool startReadyNow = effectiveStartReady();
  Serial.printf(
      "[LAYER:START_GATE] phase=entry top_state=%s runtime_ready=%d start_ready=%d baseline_ready=%d user_present=%d blocking_fault=%s pause_reason=%s warning=%s platform_model=%s laser_installed=%d laser_available=%d protection_degraded=%d\n",
      topStateName(st),
      runtimeReadyNow ? 1 : 0,
      startReadyNow ? 1 : 0,
      startSnapshot.baselineReady ? 1 : 0,
      startSnapshot.userPresent ? 1 : 0,
      faultCodeName(blocking_fault_code),
      faultCodeName(pause_reason_code),
      faultCodeName(warning_fault_code),
      platformModelName(startSnapshot.platformModel),
      startSnapshot.laserInstalled ? 1 : 0,
      startSnapshot.laserAvailable ? 1 : 0,
      startSnapshot.protectionDegraded ? 1 : 0);

  if (blocking_fault_code != FaultCode::NONE) {
    reason = FaultCode::FAULT_LOCKED;
    Serial.printf(
        "[LAYER:START_GATE] decision=reject reason=FAULT_LOCKED top_state=%s runtime_ready=%d start_ready=%d\n",
        topStateName(st),
        runtimeReadyNow ? 1 : 0,
        startReadyNow ? 1 : 0);
    Serial.printf("%s [FSM] START REJECT reason=FAULT_LOCKED detail=%s active_fault=%s\n",
                  LogMarker::kFsm,
                  recoveryDetail(),
                  faultCodeName(blocking_fault_code));
    return false;
  }

  if (pause_reason_code != FaultCode::NONE) {
    reason = FaultCode::NOT_ARMED;
    Serial.printf(
        "[LAYER:START_GATE] decision=reject reason=NOT_ARMED detail=pause_reason=%s top_state=%s runtime_ready=%d start_ready=%d baseline_ready=%d\n",
        faultCodeName(pause_reason_code),
        topStateName(st),
        runtimeReadyNow ? 1 : 0,
        startReadyNow ? 1 : 0,
        startSnapshot.baselineReady ? 1 : 0);
    Serial.printf(
        "%s [FSM] START REJECT reason=NOT_ARMED detail=pause_reason=%s baseline_ready=%d stable_weight_kg=%.2f runtime_ready=%d userPresent=%d leave_enabled=%d\n",
                  LogMarker::kFsm,
                  faultCodeName(pause_reason_code),
                  startReadyNow ? 1 : 0,
                  start_ready_stable_weight_kg,
                  runtimeReadyNow ? 1 : 0,
                  startSnapshot.userPresent ? 1 : 0,
                  leaveDetectionEnabled() ? 1 : 0);
    return false;
  }

  if (!runtimeReadyNow || !startReadyNow) {
    reason = FaultCode::NOT_ARMED;
    const char* detail = !runtimeReadyNow ? "user_not_present" : "baseline_not_ready";
    Serial.printf(
        "[LAYER:START_GATE] decision=reject reason=NOT_ARMED detail=%s top_state=%s runtime_ready=%d start_ready=%d baseline_ready=%d\n",
        detail,
        topStateName(st),
        runtimeReadyNow ? 1 : 0,
        startReadyNow ? 1 : 0,
        startSnapshot.baselineReady ? 1 : 0);
    Serial.printf(
        "%s [FSM] START REJECT reason=NOT_ARMED detail=%s baseline_ready=%d stable_weight_kg=%.2f runtime_ready=%d userPresent=%d leave_enabled=%d warning=%s\n",
        LogMarker::kFsm,
        detail,
        startReadyNow ? 1 : 0,
        start_ready_stable_weight_kg,
        runtimeReadyNow ? 1 : 0,
        startSnapshot.userPresent ? 1 : 0,
        leaveDetectionEnabled() ? 1 : 0,
        faultCodeName(warning_fault_code));
    return false;
  }

  Serial.printf(
      "[LAYER:START_GATE] decision=allow top_state=%s runtime_ready=%d start_ready=%d baseline_ready=%d platform_model=%s laser_installed=%d laser_available=%d protection_degraded=%d\n",
      topStateName(st),
      runtimeReadyNow ? 1 : 0,
      startReadyNow ? 1 : 0,
      startSnapshot.baselineReady ? 1 : 0,
      platformModelName(startSnapshot.platformModel),
      startSnapshot.laserInstalled ? 1 : 0,
      startSnapshot.laserAvailable ? 1 : 0,
      startSnapshot.protectionDegraded ? 1 : 0);
  Serial.printf(
      "%s [FSM] START ALLOW baseline_ready=%d stable_weight_kg=%.2f runtime_ready=%d userPresent=%d leave_enabled=%d warning=%s\n",
                LogMarker::kFsm,
                startReadyNow ? 1 : 0,
                start_ready_stable_weight_kg,
                runtimeReadyNow ? 1 : 0,
                startSnapshot.userPresent ? 1 : 0,
                leaveDetectionEnabled() ? 1 : 0,
                faultCodeName(warning_fault_code));
  rememberStopContext("NONE", VerificationStopSource::NONE);
  setState(TopState::RUNNING);
  reason = FaultCode::NONE;
  return true;
}

void SystemStateMachine::requestStop() {
  const bool wasRunning = (st == TopState::RUNNING);
  Serial.printf(
      "%s [FSM] STOP REQUEST state=%s baseline_ready=%d stable_weight_kg=%.2f runtime_ready=%d active_block=%s active_visible=%s\n",
                LogMarker::kFsm,
                topStateName(st),
                start_ready ? 1 : 0,
                start_ready_stable_weight_kg,
                runtime_ready ? 1 : 0,
                faultCodeName(blocking_fault_code),
                faultCodeName(visibleReasonCode()));

  if (blocking_fault_code != FaultCode::NONE) {
    Serial.printf("%s [FSM] STOP while fault latched active_fault=%s\n",
                  LogMarker::kFsm,
                  faultCodeName(blocking_fault_code));
    if (wave) wave->stopSoft();
    setState(TopState::FAULT_STOP);
    return;
  }

  TopState target = canEnterArmedState() ? TopState::ARMED : TopState::IDLE;
  if (wasRunning) {
    const FaultCode stopCode =
        (pause_reason_code != FaultCode::NONE) ? pause_reason_code : FaultCode::NONE;
    const char* stopReasonText = resolvedStopReasonText(
        stopCode,
        (pause_reason_code != FaultCode::NONE) ? faultCodeName(pause_reason_code) : "MANUAL_STOP");
    const VerificationStopSource stopSource = resolvedStopSource(
        (pause_reason_code != FaultCode::NONE)
            ? VerificationStopSource::FORMAL_SAFETY_OTHER
            : VerificationStopSource::USER_MANUAL_OTHER);
    const SafetySignalKind stopEffect =
        (pause_reason_code != FaultCode::NONE) ? SafetySignalKind::RECOVERABLE_PAUSE : SafetySignalKind::NONE;
    rememberStopContext(stopReasonText, stopSource);
    emitStopEvent(stopCode, stopEffect, target, stopReasonText, stopSource);
  }
  clearPendingStopContext();
  if (st == target) {
    if (wave) wave->stopSoft();
    Serial.printf("%s [FSM] STOP RESULT state=%s\n", LogMarker::kFsm, topStateName(st));
    return;
  }

  setState(target);
}

void SystemStateMachine::onWeightSample(float w) {
  (void)w;
  maybeClearBlockingFault(millis());
}
