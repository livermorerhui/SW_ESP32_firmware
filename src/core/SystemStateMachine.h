#pragma once
#include "Types.h"
#include "EventBus.h"
#include "PlatformSnapshotOwner.h"
#include "MeasurementFacts.h"
#include "WaveControl.h"
#include "LaserStatePort.h"
#include "RuntimeProtectionPolicy.h"
#include "SafetyActionContractEvaluator.h"
#include "config/GlobalConfig.h"

class SystemStateMachine : public PlatformSnapshotOwner, public LaserStatePort {
public:
  void begin(EventBus* eb, WaveControl* waveControl);
  void attachMeasurementFacts(const MeasurementFacts* measurementFacts);
  static const SystemStateMachine* activeInstance();

  TopState state() const override;
  bool isFaultLocked() const;
  FaultCode activeFault() const override;
  bool runtimeReady() const;
  FaultCode currentReasonCode() const;
  SafetySignalKind currentSafetyEffect() const;
  PlatformSnapshot snapshot() const override;

  void onUserOn();
  void onUserOff() override;
  void onBleConnected();
  void onBleDisconnected();
  void onFallSuspected();
  void onSensorErr();
  void setFallStopEnabled(bool enabled);
  bool fallStopEnabled() const override;
  const char* fallStopModeName() const;
  void setLeaveStopEnabled(bool enabled);
  bool leaveStopEnabled() const;
  const char* leaveStopModeName() const;
  FallStopActionDecision decideFallSuspectedAction() const override;
  void applyFallSuspectedAction(const FallStopActionDecision& decision) override;
  void setMotionSamplingMode(bool enabled);
  bool motionSamplingModeEnabled() const;
  void setDegradedStartAuthorized(bool enabled);
  bool degradedStartAvailable() const;
  bool degradedStartAuthorized() const;
  void setRuntimeReady(bool ready) override;
  void setStartReadiness(bool ready, float stableWeightKg) override;
  bool startReady() const;
  bool leaveDetectionEnabled() const;
  void setSensorHealthy(bool healthy) override;
  const char* lastStopReasonText() const override;
  const char* lastStopSourceText() const override;
  SafetySignalKind lastStopSafetyEffect() const override;

  void onWeightSample(float weightKg) override;

  bool requestStart(FaultCode& reason);
  void requestStop();
  // An internal configuration/NVS fault must stop output without being
  // reported as a user-initiated stop or as a fall/leave safety event.
  void stopForConfigurationFault() override;

private:
  void setState(TopState s);
  void emitState();
  void emitFault(FaultCode code);
  void emitSafety(FaultCode code, SafetySignalKind safety);
  void emitSnapshot();
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
  void rememberStopContext(const char* stopReasonText,
                           VerificationStopSource stopSource,
                           SafetySignalKind stopEffect);
  void clearPendingStopContext();
  const char* resolvedStopReasonText(FaultCode code, const char* fallback) const;
  VerificationStopSource resolvedStopSource(VerificationStopSource fallback) const;
  DegradedStartPolicyDecision degradedStartPolicyDecision() const;
  UserLeftProtectionDecision decideUserLeftAction() const;
  bool laserConfiguredInstalled() const;
  bool effectiveRuntimeReady() const;
  bool effectiveStartReady() const;
  bool effectiveLaserAvailable() const;
  bool effectiveProtectionDegraded() const;
  bool degradedStartBypassActive() const;
  bool canEnterArmedState() const;
  void syncSnapshotDecisionContext();

  EventBus* bus = nullptr;
  WaveControl* wave = nullptr;
  const MeasurementFacts* measurement = nullptr;
  TopState st = TopState::IDLE;
  FaultCode blocking_fault_code = FaultCode::NONE;
  FaultCode pause_reason_code = FaultCode::NONE;
  FaultCode warning_fault_code = FaultCode::NONE;
  FaultCode snapshot_reason_code = FaultCode::NONE;
  SafetySignalKind snapshot_safety_effect = SafetySignalKind::NONE;

  uint32_t fault_ms = 0;
  bool clear_window_active = false;
  uint32_t clear_candidate_ms = 0;
  bool fall_stop_enabled = FALL_STOP_ENABLED_DEFAULT;
  bool motion_sampling_mode_enabled = false;
  bool degraded_start_authorized = false;
  uint32_t last_suppressed_fall_notice_ms = 0;
  bool leave_stop_enabled = LEAVE_STOP_ENABLED_DEFAULT;
  uint32_t last_suppressed_leave_notice_ms = 0;
  // runtime_ready 只表示“人是否仍在平台上”的 presence 结果。
  // 当前语义下，runtime_ready 只承载 presence，不直接承担正式 start allow。
  bool runtime_ready = false;
  // start_ready 才是正式 start readiness：
  // 只有 stable/baseline 已建立后才会置 1，并与 leave 启用时机对齐。
  bool start_ready = false;
  float start_ready_stable_weight_kg = 0.0f;
  bool sensor_healthy = false;
  bool sensor_state_known = false;
  bool has_logged_start_ready = false;
  bool last_logged_start_ready = false;
  float last_logged_start_ready_weight_kg = 0.0f;
  const char* pending_stop_reason_text = nullptr;
  VerificationStopSource pending_stop_source = VerificationStopSource::NONE;
  const char* last_stop_reason_text = "NONE";
  VerificationStopSource last_stop_source = VerificationStopSource::NONE;
  SafetySignalKind last_stop_safety_effect = SafetySignalKind::NONE;

  static SystemStateMachine* active_instance;
};
