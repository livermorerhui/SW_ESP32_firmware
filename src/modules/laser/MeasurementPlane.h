#pragma once

#include <Arduino.h>
#include "config/GlobalConfig.h"
#include "core/EventBus.h"

struct MeasurementPlaneRecordResult {
  bool shouldPublish = false;
  Event event{};
};

class MeasurementPlane {
public:
  void reset(const char* reason, bool logReset);
  MeasurementPlaneRecordResult record(
      uint32_t now,
      bool valid,
      float distance,
      float weight,
      const char* reason);
  void notePublished(const MeasurementPlaneRecordResult& result);
  void logLatest(const char* trigger);

private:
  void pushWeightSample(float weight);
  bool currentMa12(float& out) const;
  void logSummary(
      uint32_t now,
      bool valid,
      float distance,
      float weight,
      bool ma12Ready,
      float ma12,
      const char* reason,
      const char* trigger);

  float ma12WeightBuffer[MEASUREMENT_MA12_WINDOW]{};
  uint8_t ma12Head = 0;
  uint8_t ma12Count = 0;
  uint32_t measurementSequence = 0;
  uint32_t logStartedAtMs = 0;
  uint32_t logSamples = 0;
  uint32_t lastInvalidEventMs = 0;
  const char* lastInvalidEventReason = nullptr;
  bool hasLatestSample = false;
  bool latestSampleValid = false;
  float latestSampleDistance = 0.0f;
  float latestSampleWeight = 0.0f;
  bool latestSampleMa12Ready = false;
  float latestSampleMa12 = 0.0f;
  const char* latestSampleReason = nullptr;
  bool hasLoggedSummary = false;
  bool lastLoggedSummaryValid = false;
  const char* lastLoggedSummaryReason = nullptr;
};
