#include "modules/laser/RunSummaryCollector.h"

#include <math.h>
#include "core/LogMarkers.h"

void RunSummaryCollector::start(
    const RunSummaryStartSnapshot& snapshot,
    const RhythmStateUpdateResult& rhythmResult) {
  state.active = true;
  state.testId = state.nextTestId++;
  state.startedAtMs = snapshot.now;
  state.samples = 0;
  state.baselineReady = snapshot.baselineReady;
  state.freqHz = snapshot.freqHz;
  state.intensity = snapshot.intensity;
  state.intensityNormalized = snapshot.intensityNormalized;
  state.baselineWeightKg = snapshot.baselineWeightKg;
  state.baselineDistance = snapshot.baselineDistance;
  state.fallStopEnabled = snapshot.fallStopEnabled;
  state.lastRhythmStatus = rhythmResult.status;
  state.lastRhythmReason = rhythmResult.reason ? rhythmResult.reason : "n/a";
  state.weightKgRange = RangeTracker{};
  state.distanceRange = RangeTracker{};
  state.ma3WeightKgRange = RangeTracker{};
  state.ma3DistanceRange = RangeTracker{};
  state.ma5WeightKgRange = RangeTracker{};
  state.ma5DistanceRange = RangeTracker{};
  state.ma12WeightKgRange = RangeTracker{};
  state.ma12DistanceRange = RangeTracker{};
  state.advisoryCount = 0;
  state.lastAdvisoryType = RiskAdvisoryType::NONE;
  state.lastAdvisoryLevel = RiskAdvisoryLevel::NONE;
  state.lastAdvisoryReason = "none";
  state.recentHead = 0;
  state.recentCount = 0;

  // Test-summary logs stay event-driven: baseline latch, run start, and run end.
  Serial.printf(
      "%s [TEST_START] test_id=%lu freq_hz=%.2f intensity=%d intensity_norm=%.3f "
      "baseline_ready=%d stable_weight_kg=%.2f stable_distance=%.2f fall_stop_enabled=%d\n",
      LogMarker::kTestStart,
      static_cast<unsigned long>(state.testId),
      state.freqHz,
      state.intensity,
      state.intensityNormalized,
      state.baselineReady ? 1 : 0,
      state.baselineWeightKg,
      state.baselineDistance,
      state.fallStopEnabled ? 1 : 0);
}

void RunSummaryCollector::accumulate(
    uint32_t now,
    float distance,
    float weight,
    const RhythmStateUpdateResult& rhythmResult) {
  (void)now;

  if (!state.active) return;

  state.samples++;
  state.lastRhythmStatus = rhythmResult.status;
  state.lastRhythmReason = rhythmResult.reason ? rhythmResult.reason : "n/a";
  includeRange(state.weightKgRange, weight);
  includeRange(state.distanceRange, distance);

  static constexpr uint8_t kCapacity =
      sizeof(state.recentWeightKg) / sizeof(state.recentWeightKg[0]);
  state.recentWeightKg[state.recentHead] = weight;
  state.recentDistance[state.recentHead] = distance;
  state.recentHead = (state.recentHead + 1) % kCapacity;
  if (state.recentCount < kCapacity) {
    state.recentCount++;
  }

  float avgWeight = 0.0f;
  float avgDistance = 0.0f;
  if (computeAverage(3, avgWeight, avgDistance)) {
    includeRange(state.ma3WeightKgRange, avgWeight);
    includeRange(state.ma3DistanceRange, avgDistance);
  }
  if (computeAverage(5, avgWeight, avgDistance)) {
    includeRange(state.ma5WeightKgRange, avgWeight);
    includeRange(state.ma5DistanceRange, avgDistance);
  }
  if (computeAverage(12, avgWeight, avgDistance)) {
    includeRange(state.ma12WeightKgRange, avgWeight);
    includeRange(state.ma12DistanceRange, avgDistance);
  }

  if (rhythmResult.advisory.shouldLog) {
    if (state.advisoryCount < 0xFFFF) {
      state.advisoryCount++;
    }
    state.lastAdvisoryType = rhythmResult.advisory.type;
    state.lastAdvisoryLevel = rhythmResult.advisory.level;
    state.lastAdvisoryReason = rhythmResult.advisory.reason ? rhythmResult.advisory.reason : "none";
  }
}

void RunSummaryCollector::finish(
    const RunSummaryStopSnapshot& snapshot,
    const RhythmStateUpdateResult& rhythmResult) {
  if (!state.active) return;

  state.lastRhythmStatus = rhythmResult.status;
  state.lastRhythmReason = rhythmResult.reason ? rhythmResult.reason : "n/a";

  char weightRange[32];
  char distanceRange[32];
  char ma3WeightRange[32];
  char ma3DistanceRange[32];
  char ma5WeightRange[32];
  char ma5DistanceRange[32];
  char ma12WeightRange[32];
  char ma12DistanceRange[32];
  formatRange(state.weightKgRange, weightRange, sizeof(weightRange));
  formatRange(state.distanceRange, distanceRange, sizeof(distanceRange));
  formatRange(state.ma3WeightKgRange, ma3WeightRange, sizeof(ma3WeightRange));
  formatRange(state.ma3DistanceRange, ma3DistanceRange, sizeof(ma3DistanceRange));
  formatRange(state.ma5WeightKgRange, ma5WeightRange, sizeof(ma5WeightRange));
  formatRange(state.ma5DistanceRange, ma5DistanceRange, sizeof(ma5DistanceRange));
  formatRange(state.ma12WeightKgRange, ma12WeightRange, sizeof(ma12WeightRange));
  formatRange(state.ma12DistanceRange, ma12DistanceRange, sizeof(ma12DistanceRange));

  const bool abnormalStop = (snapshot.stopReason != FaultCode::NONE);
  const uint32_t durationMs =
      (snapshot.now >= state.startedAtMs) ? (snapshot.now - state.startedAtMs) : 0;
  const char* summaryMarker = abnormalStop ? LogMarker::kTestAbort : LogMarker::kTestStop;
  const char* stopReasonText = snapshot.stopReasonText
      ? snapshot.stopReasonText
      : (abnormalStop ? faultCodeName(snapshot.stopReason) : "NONE");
  const char* stopSourceText = snapshot.stopSourceText ? snapshot.stopSourceText : "NONE";

  Serial.printf(
      "%s [%s] test_id=%lu result=%s stop_reason=%s stop_source=%s freq_hz=%.2f intensity=%d intensity_norm=%.3f "
      "baseline_ready=%d stable_weight_kg=%.2f stable_distance=%.2f "
      "fall_stop_enabled=%d "
      "weight_range_kg=%s distance_range=%s "
      "ma3_weight_range_kg=%s ma3_distance_range=%s "
      "ma5_weight_range_kg=%s ma5_distance_range=%s "
      "ma12_weight_range_kg=%s ma12_distance_range=%s "
      "main_status=%s main_reason=%s advisory_count=%u last_advisory_type=%s "
      "last_advisory_level=%s last_advisory_reason=%s final_abnormal_duration_ms=%lu "
      "final_danger_duration_ms=%lu duration_ms=%lu samples=%lu\n",
      summaryMarker,
      abnormalStop ? "ABORT_SUMMARY" : "STOP_SUMMARY",
      static_cast<unsigned long>(state.testId),
      abnormalStop ? "ABNORMAL_STOP" : "NORMAL",
      stopReasonText,
      stopSourceText,
      state.freqHz,
      state.intensity,
      state.intensityNormalized,
      state.baselineReady ? 1 : 0,
      state.baselineWeightKg,
      state.baselineDistance,
      snapshot.fallStopEnabled ? 1 : 0,
      weightRange,
      distanceRange,
      ma3WeightRange,
      ma3DistanceRange,
      ma5WeightRange,
      ma5DistanceRange,
      ma12WeightRange,
      ma12DistanceRange,
      rhythmStateName(state.lastRhythmStatus),
      state.lastRhythmReason ? state.lastRhythmReason : "n/a",
      static_cast<unsigned int>(state.advisoryCount),
      riskAdvisoryTypeName(state.lastAdvisoryType),
      riskAdvisoryLevelName(state.lastAdvisoryLevel),
      state.lastAdvisoryReason ? state.lastAdvisoryReason : "none",
      static_cast<unsigned long>(rhythmResult.evidence.abnormalDurationMs),
      static_cast<unsigned long>(rhythmResult.evidence.dangerDurationMs),
      static_cast<unsigned long>(durationMs),
      static_cast<unsigned long>(state.samples));

  resetAfterFinish();
}

void RunSummaryCollector::includeRange(RangeTracker& range, float value) {
  if (!isfinite(value)) return;
  if (!range.valid) {
    range.valid = true;
    range.min = value;
    range.max = value;
    return;
  }
  if (value < range.min) range.min = value;
  if (value > range.max) range.max = value;
}

void RunSummaryCollector::formatRange(const RangeTracker& range, char* buffer, size_t bufferSize) {
  if (!range.valid) {
    snprintf(buffer, bufferSize, "n/a");
    return;
  }
  snprintf(buffer, bufferSize, "%.2f..%.2f", range.min, range.max);
}

bool RunSummaryCollector::computeAverage(uint8_t window, float& avgWeight, float& avgDistance) const {
  static constexpr uint8_t kCapacity =
      sizeof(state.recentWeightKg) / sizeof(state.recentWeightKg[0]);

  if (!state.active || window == 0 || window > kCapacity || state.recentCount < window) {
    return false;
  }

  float weightSum = 0.0f;
  float distanceSum = 0.0f;
  uint8_t index = state.recentHead;

  for (uint8_t i = 0; i < window; ++i) {
    index = (index == 0) ? static_cast<uint8_t>(kCapacity - 1) : static_cast<uint8_t>(index - 1);
    weightSum += state.recentWeightKg[index];
    distanceSum += state.recentDistance[index];
  }

  avgWeight = weightSum / window;
  avgDistance = distanceSum / window;
  return true;
}

void RunSummaryCollector::resetAfterFinish() {
  const uint32_t nextTestId = state.nextTestId;
  state = State{};
  state.nextTestId = nextTestId;
}
