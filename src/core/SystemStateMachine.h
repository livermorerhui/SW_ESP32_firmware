#pragma once
#include "Types.h"
#include "EventBus.h"
#include "config/GlobalConfig.h"

class WaveModule;

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
  void setRuntimeReady(bool ready);
  void setSensorHealthy(bool healthy);

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

  EventBus* bus = nullptr;
  WaveModule* wave = nullptr;
  TopState st = TopState::IDLE;
  FaultCode blocking_fault_code = FaultCode::NONE;
  FaultCode pause_reason_code = FaultCode::NONE;
  FaultCode warning_fault_code = FaultCode::NONE;

  uint32_t fault_ms = 0;
  bool clear_window_active = false;
  uint32_t clear_candidate_ms = 0;
  bool runtime_ready = false;
  bool sensor_healthy = false;
  bool sensor_state_known = false;
};
