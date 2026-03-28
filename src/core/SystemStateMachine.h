#pragma once
#include "Types.h"
#include "EventBus.h"
#include "config/GlobalConfig.h"

class WaveModule;

struct FallStopActionDecision {
  bool stopCandidateDetected = false;
  bool shouldExecuteStop = false;
  bool stopSuppressedBySwitch = false;
  bool fallStopEnabled = FALL_STOP_ENABLED_DEFAULT;
  FaultCode stopReason = FaultCode::FALL_SUSPECTED;
  SafetySignalKind safetySignal = SafetySignalKind::WARNING_ONLY;
  // verification contract 里的 stop_reason / stop_source。
  // 允许上游候选方提供更精确的停波语义，但最终动作仍由状态机执行。
  const char* verificationStopReason = nullptr;
  VerificationStopSource verificationStopSource = VerificationStopSource::NONE;
  const char* detail = "fall_detect_only";
};

class SystemStateMachine {
public:
  void begin(EventBus* eb, WaveModule* waveModule);

  TopState state() const;
  bool isFaultLocked() const;
  FaultCode activeFault() const;

  void onUserOn();
  void onUserOff();
  void onBleConnected();
  void onBleDisconnected();
  void onFallSuspected();
  void onSensorErr();
  void setFallStopEnabled(bool enabled);
  bool fallStopEnabled() const;
  const char* fallStopModeName() const;
  FallStopActionDecision decideFallSuspectedAction() const;
  void applyFallSuspectedAction(const FallStopActionDecision& decision);
  void setMotionSamplingMode(bool enabled);
  bool motionSamplingModeEnabled() const;
  void setRuntimeReady(bool ready);
  void setStartReadiness(bool ready, float stableWeightKg);
  bool startReady() const;
  bool leaveDetectionEnabled() const;
  void setSensorHealthy(bool healthy);
  const char* lastStopReasonText() const;
  const char* lastStopSourceText() const;

  void onWeightSample(float weightKg);

  bool requestStart(FaultCode& reason);
  void requestStop();

private:
  void setState(TopState s);
  void emitState();
  void emitFault(FaultCode code);
  void emitSafety(FaultCode code, SafetySignalKind safety);
  void emitVisibleSignals();
  void enterBlockingFault(FaultCode code, const char* detail);
  void enterRecoverablePause(FaultCode code, const char* detail);
  void maybeClearBlockingFault(uint32_t now);
  void clearBlockingFault(const char* detail);
  void setWarningFault(FaultCode code, const char* detail);
  void clearWarningFault(FaultCode code, const char* detail);
  void clearRecoverablePause(FaultCode code, const char* detail);
  void syncReadyState();
  bool recoveryConditionMet() const;
  const char* recoveryDetail() const;
  FaultCode visibleReasonCode() const;
  SafetySignalKind visibleSafetySignal() const;
  void emitStopEvent(FaultCode code,
                     SafetySignalKind safety,
                     TopState targetState,
                     const char* stopReasonText,
                     VerificationStopSource stopSource);
  void rememberStopContext(const char* stopReasonText, VerificationStopSource stopSource);
  void clearPendingStopContext();
  const char* resolvedStopReasonText(FaultCode code, const char* fallback) const;
  VerificationStopSource resolvedStopSource(VerificationStopSource fallback) const;
  bool canEnterArmedState() const;

  EventBus* bus = nullptr;
  WaveModule* wave = nullptr;
  TopState st = TopState::IDLE;
  FaultCode blocking_fault_code = FaultCode::NONE;
  FaultCode pause_reason_code = FaultCode::NONE;
  FaultCode warning_fault_code = FaultCode::NONE;

  uint32_t fault_ms = 0;
  bool clear_window_active = false;
  uint32_t clear_candidate_ms = 0;
  bool fall_stop_enabled = FALL_STOP_ENABLED_DEFAULT;
  bool motion_sampling_mode_enabled = false;
  uint32_t last_suppressed_fall_notice_ms = 0;
  // runtime_ready 只表示“人是否仍在平台上”的 presence 结果。
  // 本轮修复后，它不再直接承担正式 start allow 的语义。
  bool runtime_ready = false;
  // start_ready 才是正式 start readiness：
  // 只有 stable/baseline 已建立后才会置 1，并与 leave 启用时机对齐。
  bool start_ready = false;
  float start_ready_stable_weight_kg = 0.0f;
  bool sensor_healthy = false;
  bool sensor_state_known = false;
  const char* pending_stop_reason_text = nullptr;
  VerificationStopSource pending_stop_source = VerificationStopSource::NONE;
  const char* last_stop_reason_text = "NONE";
  VerificationStopSource last_stop_source = VerificationStopSource::NONE;
};
