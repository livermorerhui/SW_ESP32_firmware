#pragma once
#include "Types.h"

enum class EventType : uint8_t {
  STATE,
  FAULT,
  SAFETY,
  STABLE_WEIGHT,
  PARAMS,
  STREAM
};

struct Event {
  EventType type;
  TopState state = TopState::IDLE;
  FaultCode fault = FaultCode::NONE;
  SafetySignalKind safety = SafetySignalKind::NONE;
  bool waveStopped = false;
  float v1 = 0;     // weight/dist/zero
  float v2 = 0;     // weight/factor
  uint32_t ts_ms = 0;
};

class EventSink {
public:
  virtual void onEvent(const Event& e) = 0;
  virtual ~EventSink() = default;
};

class EventBus {
public:
  void setSink(EventSink* s) { sink = s; }
  void publish(const Event& e) { if (sink) sink->onEvent(e); }
private:
  EventSink* sink = nullptr;
};
