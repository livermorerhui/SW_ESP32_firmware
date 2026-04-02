#pragma once
#include "Types.h"

enum class EventType : uint8_t {
  STATE,
  WAVE_OUTPUT,
  FAULT,
  SAFETY,
  STABLE_WEIGHT,
  PARAMS,
  STREAM,
  BASELINE_MAIN,
  STOP
};

struct Event {
  EventType type;
  TopState state = TopState::IDLE;
  FaultCode fault = FaultCode::NONE;
  SafetySignalKind safety = SafetySignalKind::NONE;
  bool waveStopped = false;
  bool waveOutputActive = false;
  float v1 = 0;     // weight/dist/zero
  float v2 = 0;     // weight/factor
  uint32_t ts_ms = 0;

  // baseline-main verification contract。
  // 只有 EventType::BASELINE_MAIN / STOP 会使用这些字段。
  bool startReady = false;
  bool baselineReady = false;
  float stableWeightKg = 0.0f;
  float ma7WeightKg = 0.0f;
  float deviationKg = 0.0f;
  float ratio = 0.0f;
  uint32_t abnormalDurationMs = 0;
  uint32_t dangerDurationMs = 0;
  char mainState[24] = {0};
  char stopReasonText[48] = {0};
  char stopSourceText[32] = {0};

  // Formal continuous measurement plane payload.
  uint32_t sampleSeq = 0;
  bool measurementValid = false;
  bool ma12Ready = false;
  float distance = 0.0f;
  float weightKg = 0.0f;
  float ma12WeightKg = 0.0f;
  char measurementReason[32] = {0};
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
