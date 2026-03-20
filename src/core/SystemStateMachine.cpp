#include "SystemStateMachine.h"
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

void SystemStateMachine::begin(EventBus* eb, WaveModule* waveModule) {
  bus = eb;
  wave = waveModule;
  st = TopState::IDLE;
  blocking_fault_code = FaultCode::NONE;
  pause_reason_code = FaultCode::NONE;
  warning_fault_code = FaultCode::NONE;
  fault_ms = 0;
  clear_window_active = false;
  clear_candidate_ms = 0;
  runtime_ready = false;
  sensor_healthy = false;
  sensor_state_known = false;
  emitState();
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
  e.waveStopped = (st != TopState::RUNNING);
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
  emitFault(visible);
  emitSafety(visible, safety);
}

void SystemStateMachine::setState(TopState s) {
  if (st == s) return;

  TopState prev = st;
  st = s;

  // SystemStateMachine is the only gate allowed to start/stop output.
  if (wave) {
    if (st == TopState::RUNNING) wave->start();
    else wave->stopSoft();
  }

  Serial.printf("[FSM] STATE %s -> %s\n", topStateName(prev), topStateName(st));
  emitState();
}

void SystemStateMachine::setWarningFault(FaultCode code, const char* detail) {
  if (warning_fault_code == code) return;

  warning_fault_code = code;
  Serial.printf("[FSM] SAFETY reason=%s effect=%s detail=%s\n",
                faultCodeName(code),
                safetySignalName(SafetySignalKind::WARNING_ONLY),
                detail ? detail : "n/a");
  Serial.printf("[FAULT] WARN name=%s origin=%s severity=%s detail=%s\n",
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
  Serial.printf("[FAULT] CLEAR severity=%s name=%s detail=%s\n",
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

  blocking_fault_code = code;
  fault_ms = now;
  clear_window_active = false;
  clear_candidate_ms = 0;
  pause_reason_code = FaultCode::NONE;

  if (isNewFault) {
    Serial.printf("[FSM] SAFETY reason=%s effect=%s detail=%s\n",
                  faultCodeName(code),
                  safetySignalName(SafetySignalKind::ABNORMAL_STOP),
                  detail ? detail : "n/a");
    Serial.printf("[FAULT] BLOCK name=%s origin=%s severity=%s detail=%s\n",
                  faultCodeName(code),
                  originName(faultOrigin(code)),
                  severityName(FaultSeverity::BLOCKING_FAULT),
                  detail ? detail : "n/a");
    Serial.printf("[FSM] FAULT ENTER reason=%s detail=%s\n",
                  faultCodeName(code),
                  detail ? detail : "n/a");
  }

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

  Serial.printf("[FSM] SAFETY reason=%s effect=%s detail=%s\n",
                faultCodeName(code),
                safetySignalName(SafetySignalKind::RECOVERABLE_PAUSE),
                detail ? detail : "n/a");

  syncReadyState();
  emitVisibleSignals();
}

bool SystemStateMachine::recoveryConditionMet() const {
  switch (blocking_fault_code) {
    case FaultCode::USER_LEFT_PLATFORM:
      return runtime_ready;
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
      return runtime_ready ? "runtime_ready=1" : "runtime_ready=0";
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

  Serial.printf("[FAULT] CLEAR severity=%s name=%s detail=%s\n",
                severityName(FaultSeverity::BLOCKING_FAULT),
                faultCodeName(cleared),
                detail ? detail : "n/a");
  Serial.printf("[FSM] FAULT CLEAR reason=%s detail=%s\n",
                faultCodeName(cleared),
                detail ? detail : "n/a");

  syncReadyState();
  emitVisibleSignals();
}

void SystemStateMachine::clearRecoverablePause(FaultCode code, const char* detail) {
  if (pause_reason_code != code) return;

  pause_reason_code = FaultCode::NONE;
  Serial.printf("[FSM] SAFETY CLEAR reason=%s effect=%s detail=%s\n",
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
  setState(runtime_ready ? TopState::ARMED : TopState::IDLE);
}

void SystemStateMachine::onUserOn() {
  setRuntimeReady(true);
}

void SystemStateMachine::onUserOff() {
  runtime_ready = false;

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
  if (SAFETY_POLICY_FALL_ABNORMAL_STOP) {
    enterBlockingFault(FaultCode::FALL_SUSPECTED, "fall_stop_active");
    return;
  }

  enterRecoverablePause(FaultCode::FALL_SUSPECTED, "fall_pause_override");
}

void SystemStateMachine::onSensorErr() {
  setSensorHealthy(false);
}

void SystemStateMachine::setRuntimeReady(bool ready) {
  runtime_ready = ready;

  if (pause_reason_code == FaultCode::USER_LEFT_PLATFORM && ready) {
    clearRecoverablePause(FaultCode::USER_LEFT_PLATFORM, "runtime_ready=1");
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

  if (blocking_fault_code != FaultCode::NONE) {
    reason = FaultCode::FAULT_LOCKED;
    Serial.printf("[FSM] START REJECT reason=FAULT_LOCKED detail=%s active_fault=%s\n",
                  recoveryDetail(),
                  faultCodeName(blocking_fault_code));
    return false;
  }

  if (pause_reason_code != FaultCode::NONE) {
    reason = FaultCode::NOT_ARMED;
    Serial.printf("[FSM] START REJECT reason=NOT_ARMED detail=pause_reason=%s\n",
                  faultCodeName(pause_reason_code));
    return false;
  }

  Serial.printf("[FSM] START ALLOW runtime_ready=%d warning=%s\n",
                runtime_ready ? 1 : 0,
                faultCodeName(warning_fault_code));
  setState(TopState::RUNNING);
  reason = FaultCode::NONE;
  return true;
}

void SystemStateMachine::requestStop() {
  Serial.printf("[FSM] STOP REQUEST state=%s runtime_ready=%d active_block=%s active_visible=%s\n",
                topStateName(st),
                runtime_ready ? 1 : 0,
                faultCodeName(blocking_fault_code),
                faultCodeName(visibleReasonCode()));

  if (blocking_fault_code != FaultCode::NONE) {
    Serial.printf("[FSM] STOP while fault latched active_fault=%s\n", faultCodeName(blocking_fault_code));
    if (wave) wave->stopSoft();
    setState(TopState::FAULT_STOP);
    return;
  }

  TopState target = runtime_ready ? TopState::ARMED : TopState::IDLE;
  if (st == target) {
    if (wave) wave->stopSoft();
    Serial.printf("[FSM] STOP RESULT state=%s\n", topStateName(st));
    return;
  }

  setState(target);
}

void SystemStateMachine::onWeightSample(float w) {
  (void)w;
  maybeClearBlockingFault(millis());
}
