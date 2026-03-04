#pragma once
#include "Types.h"
#include "EventBus.h"
#include "config/GlobalConfig.h"

class SystemStateMachine {
public:
  void begin(EventBus* eb);

  TopState state() const;
  bool isFaultLocked() const;

  void onUserOn();
  void onUserOff();
  void onFallSuspected();
  void onSensorErr();

  void onWeightSample(float weightKg);

  bool requestStart(FaultCode& reason);
  void requestStop();

private:
  void setState(TopState s);
  void emitState();
  void emitFault(FaultCode code);

  EventBus* bus = nullptr;
  TopState st = TopState::IDLE;

  uint32_t fault_ms = 0;
  bool clear_window_active = false;
  uint32_t clear_candidate_ms = 0;
};