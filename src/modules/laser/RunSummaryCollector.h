#pragma once

#include <Arduino.h>
#include "config/GlobalConfig.h"
#include "core/Types.h"
#include "modules/laser/RhythmStateJudge.h"

struct RunSummaryStartSnapshot {
  uint32_t now = 0;
  bool baselineReady = false;
  float baselineWeightKg = 0.0f;
  float baselineDistance = 0.0f;
  float freqHz = 0.0f;
  int intensity = 0;
  float intensityNormalized = 0.0f;
  bool fallStopEnabled = FALL_STOP_ENABLED_DEFAULT;
};

struct RunSummaryStopSnapshot {
  uint32_t now = 0;
  FaultCode stopReason = FaultCode::NONE;
  const char* stopReasonText = "NONE";
  const char* stopSourceText = "NONE";
  bool fallStopEnabled = FALL_STOP_ENABLED_DEFAULT;
};

class RunSummaryCollector {
public:
  bool active() const { return state.active; }
  void start(const RunSummaryStartSnapshot& snapshot, const RhythmStateUpdateResult& rhythmResult);
  void accumulate(uint32_t now, float distance, float weight, const RhythmStateUpdateResult& rhythmResult);
  void finish(const RunSummaryStopSnapshot& snapshot, const RhythmStateUpdateResult& rhythmResult);

private:
  struct RangeTracker {
    bool valid = false;
    float min = 0.0f;
    float max = 0.0f;
  };

  struct State {
    bool active = false;
    uint32_t nextTestId = 1;
    uint32_t testId = 0;
    uint32_t startedAtMs = 0;
    uint32_t samples = 0;
    bool baselineReady = false;
    float freqHz = 0.0f;
    int intensity = 0;
    float intensityNormalized = 0.0f;
    float baselineWeightKg = 0.0f;
    float baselineDistance = 0.0f;
    bool fallStopEnabled = FALL_STOP_ENABLED_DEFAULT;
    RhythmStateStatus lastRhythmStatus = RhythmStateStatus::BASELINE_PENDING;
    const char* lastRhythmReason = "baseline_pending";
    RangeTracker weightKgRange{};
    RangeTracker distanceRange{};
    RangeTracker ma3WeightKgRange{};
    RangeTracker ma3DistanceRange{};
    RangeTracker ma5WeightKgRange{};
    RangeTracker ma5DistanceRange{};
    RangeTracker ma12WeightKgRange{};
    RangeTracker ma12DistanceRange{};
    uint16_t advisoryCount = 0;
    RiskAdvisoryType lastAdvisoryType = RiskAdvisoryType::NONE;
    RiskAdvisoryLevel lastAdvisoryLevel = RiskAdvisoryLevel::NONE;
    const char* lastAdvisoryReason = "none";
    float recentWeightKg[12]{};
    float recentDistance[12]{};
    uint8_t recentHead = 0;
    uint8_t recentCount = 0;
  };

  static void includeRange(RangeTracker& range, float value);
  static void formatRange(const RangeTracker& range, char* buffer, size_t bufferSize);
  bool computeAverage(uint8_t window, float& avgWeight, float& avgDistance) const;
  void resetAfterFinish();

  State state{};
};
