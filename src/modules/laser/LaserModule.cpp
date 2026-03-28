#include "LaserModule.h"
#include "core/LogMarkers.h"
#include <math.h>
#include "modules/wave/WaveModule.h"

namespace {
struct WindowStats {
  float mean = NAN;
  float stddev = NAN;
  float range = NAN;
};

WindowStats computeRingWindowStats(
    const float* values,
    int head,
    int count,
    int capacity,
    int startOffset,
    int sampleCount) {
  WindowStats stats{};
  if (!values || count <= 0 || sampleCount <= 0 || startOffset < 0 ||
      startOffset + sampleCount > count) {
    return stats;
  }

  const int oldestIndex = (count == capacity) ? head : 0;
  float sum = 0.0f;
  float minValue = INFINITY;
  float maxValue = -INFINITY;
  for (int i = 0; i < sampleCount; ++i) {
    const int index = (oldestIndex + startOffset + i) % capacity;
    const float value = values[index];
    sum += value;
    if (value < minValue) minValue = value;
    if (value > maxValue) maxValue = value;
  }

  stats.mean = sum / sampleCount;
  float sumSqDiff = 0.0f;
  for (int i = 0; i < sampleCount; ++i) {
    const int index = (oldestIndex + startOffset + i) % capacity;
    const float diff = values[index] - stats.mean;
    sumSqDiff += diff * diff;
  }

  stats.stddev = sqrtf(sumSqDiff / sampleCount);
  stats.range = maxValue - minValue;
  return stats;
}
}  // namespace

const char* LaserModule::calibrationModelTypeName(CalibrationModelType type) {
  switch (type) {
    case CalibrationModelType::LINEAR:
      return "LINEAR";
    case CalibrationModelType::QUADRATIC:
      return "QUADRATIC";
  }
  return "UNKNOWN";
}

void LaserModule::begin(EventBus* eb, SystemStateMachine* fsm, WaveModule* waveModule) {
  bus = eb;
  sm = fsm;
  wave = waveModule;
  lastObservedTopState = sm ? sm->state() : TopState::IDLE;

  preferences.begin("scale_cal", false);
  zeroDistance = preferences.getFloat("zero", -22.0f);
  scaleFactor  = preferences.getFloat("factor", 1.0f);
  loadCalibrationModel();

  Serial.printf("\n=== LaserModule boot ===\nZero=%.2f K=%.4f\n", zeroDistance, scaleFactor);
  Serial.printf("[CAL] MODEL type=%s ref=%.2f c0=%.6f c1=%.6f c2=%.6f\n",
      calibrationModelTypeName(calibrationModel.type),
      calibrationModel.referenceDistance,
      calibrationModel.coefficients[0],
      calibrationModel.coefficients[1],
      calibrationModel.coefficients[2]);

  Serial1.begin(MODBUS_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
  node.begin(MODBUS_SLAVE_ID, Serial1);

  needSendParams = true;
  rhythmStateJudge.reset("boot");
}

void LaserModule::startTask() {
  xTaskCreatePinnedToCore(taskThunk, "LaserTask", 4096, this, 2, NULL, 1);
  Serial.println("[OK] LaserModule started");
}

void LaserModule::triggerZero() {
  needZero = true;
}

void LaserModule::setParams(float zero, float factor) {
  // Guard calibration factor from invalid/non-positive input.
  if (factor <= 0.0f) factor = 1.0f;

  CalibrationModel model{};
  model.type = CalibrationModelType::LINEAR;
  model.referenceDistance = zero;
  model.coefficients[0] = 0.0f;
  model.coefficients[1] = factor;
  model.coefficients[2] = 0.0f;

  String reason;
  if (!applyCalibrationModel(model, true, "scale_cal", reason)) {
    Serial.printf("[CAL] SET legacy linear rejected reason=%s\n", reason.c_str());
    return;
  }

  resetStableTracking("scale_cal", true);
  rhythmStateJudge.reset("scale_cal_reset");
  if (sm) sm->setStartReadiness(false, 0.0f);
  needSendParams = true;
}

void LaserModule::getParams(float &zero, float &factor) const {
  zero = zeroDistance;
  factor = scaleFactor;
}

float LaserModule::getWeightKg() const {
  return latestWeightKg;
}

bool LaserModule::getCalibrationModel(CalibrationModel& out) const {
  out = calibrationModel;
  return true;
}

bool LaserModule::setCalibrationModel(const CalibrationModel& model, String& reason) {
  if (!applyCalibrationModel(model, true, "cal_set_model", reason)) {
    return false;
  }

  resetStableTracking("cal_model", true);
  rhythmStateJudge.reset("cal_model_reset");
  if (sm) sm->setStartReadiness(false, 0.0f);
  needSendParams = true;
  hasStreamSample = false;
  return true;
}

bool LaserModule::captureCalibrationPoint(float referenceWeightKg, CalibrationCapture& out, String& reason) {
  if (!isfinite(referenceWeightKg) || referenceWeightKg < 0.0f) {
    reason = "INVALID_REFERENCE_WEIGHT";
    return false;
  }

  if (!lastMeasurementValid) {
    reason = "INVALID_SAMPLE";
    return false;
  }

  if (stableState != StableState::STABLE_LATCHED) {
    reason = "NOT_STABLE";
    return false;
  }

  if (!isfinite(stableBaselineDistanceMm) || !isfinite(stableBaselineWeight)) {
    reason = "INVALID_STABLE_BASELINE";
    return false;
  }

  calibrationCaptureCounter++;
  out.index = calibrationCaptureCounter;
  out.ts_ms = stableLatchedAtMs ? stableLatchedAtMs : millis();
  out.distanceMm = stableBaselineDistanceMm;
  out.referenceWeightKg = referenceWeightKg;
  out.predictedWeightKg = stableBaselineWeight;
  out.stableFlag = true;
  out.validFlag = true;

  Serial.printf("[CAL] CAPTURE idx=%lu dist_mm=%.2f ref_kg=%.2f pred_kg=%.2f\n",
      (unsigned long)out.index, out.distanceMm, out.referenceWeightKg, out.predictedWeightKg);

  return true;
}

float LaserModule::getMean(const float* values) const {
  float sum = 0;
  for (int i = 0; i < bufCount; i++) sum += values[i];
  return (bufCount == 0) ? 0 : (sum / bufCount);
}

float LaserModule::getStdDev(const float* values) const {
  if (bufCount < 2) return 999.0f;
  float mean = getMean(values);
  float sumSqDiff = 0;
  for (int i = 0; i < bufCount; i++) sumSqDiff += pow(values[i] - mean, 2);
  return sqrt(sumSqDiff / bufCount);
}

void LaserModule::taskThunk(void* arg) {
  static_cast<LaserModule*>(arg)->taskLoop();
}

bool LaserModule::shouldEmitStream(float distance, float weight, uint32_t now) const {
  if (!hasStreamSample) return true;
  if (now - lastStreamTime >= STREAM_KEEPALIVE_MS) return true;
  if (fabsf(distance - lastStreamDistance) >= STREAM_DISTANCE_DELTA_TH) return true;
  if (fabsf(weight - lastStreamWeight) >= STREAM_WEIGHT_DELTA_TH) return true;
  return false;
}

void LaserModule::noteStreamSent(float distance, float weight, uint32_t now) {
  hasStreamSample = true;
  lastStreamTime = now;
  lastStreamDistance = distance;
  lastStreamWeight = weight;
}

void LaserModule::loadCalibrationModel() {
  uint8_t storedType = preferences.getUChar("mdl_t", 0);
  CalibrationModel model{};

  if (storedType == static_cast<uint8_t>(CalibrationModelType::LINEAR) ||
      storedType == static_cast<uint8_t>(CalibrationModelType::QUADRATIC)) {
    model.type = static_cast<CalibrationModelType>(storedType);
    model.referenceDistance = preferences.getFloat("mdl_ref", zeroDistance);
    model.coefficients[0] = preferences.getFloat("mdl_c0", 0.0f);
    model.coefficients[1] = preferences.getFloat("mdl_c1", scaleFactor);
    model.coefficients[2] = preferences.getFloat("mdl_c2", 0.0f);
  } else {
    model.type = CalibrationModelType::LINEAR;
    model.referenceDistance = zeroDistance;
    model.coefficients[0] = 0.0f;
    model.coefficients[1] = scaleFactor;
    model.coefficients[2] = 0.0f;
  }

  String reason;
  if (!applyCalibrationModel(model, false, "boot", reason)) {
    CalibrationModel fallback{};
    fallback.type = CalibrationModelType::LINEAR;
    fallback.referenceDistance = zeroDistance;
    fallback.coefficients[0] = 0.0f;
    fallback.coefficients[1] = (scaleFactor > 0.0f) ? scaleFactor : 1.0f;
    fallback.coefficients[2] = 0.0f;

    String unusedReason;
    applyCalibrationModel(fallback, false, "boot_fallback", unusedReason);
  }
}

void LaserModule::saveCalibrationModel() {
  preferences.putUChar("mdl_t", static_cast<uint8_t>(calibrationModel.type));
  preferences.putFloat("mdl_ref", calibrationModel.referenceDistance);
  preferences.putFloat("mdl_c0", calibrationModel.coefficients[0]);
  preferences.putFloat("mdl_c1", calibrationModel.coefficients[1]);
  preferences.putFloat("mdl_c2", calibrationModel.coefficients[2]);
}

void LaserModule::syncLegacyParamsFromModel() {
  zeroDistance = calibrationModel.referenceDistance;
  scaleFactor = (calibrationModel.type == CalibrationModelType::LINEAR)
      ? calibrationModel.coefficients[1]
      : 0.0f;
}

bool LaserModule::applyCalibrationModel(
    const CalibrationModel& model,
    bool persist,
    const char* source,
    String& reason) {
  if (!isCalibrationModelFinite(model)) {
    reason = "INVALID_MODEL";
    return false;
  }

  if (!isCalibrationModelMonotonic(model)) {
    reason = "NON_MONOTONIC";
    return false;
  }

  calibrationModel = model;
  syncLegacyParamsFromModel();

  if (persist) {
    preferences.putFloat("zero", zeroDistance);
    preferences.putFloat("factor", scaleFactor);
    saveCalibrationModel();
  }

  Serial.printf("[CAL] MODEL APPLY source=%s type=%s ref=%.2f c0=%.6f c1=%.6f c2=%.6f\n",
      source ? source : "unknown",
      calibrationModelTypeName(calibrationModel.type),
      calibrationModel.referenceDistance,
      calibrationModel.coefficients[0],
      calibrationModel.coefficients[1],
      calibrationModel.coefficients[2]);
  return true;
}

bool LaserModule::isCalibrationModelFinite(const CalibrationModel& model) const {
  const uint8_t type = static_cast<uint8_t>(model.type);
  if (type != static_cast<uint8_t>(CalibrationModelType::LINEAR) &&
      type != static_cast<uint8_t>(CalibrationModelType::QUADRATIC)) {
    return false;
  }

  if (!isfinite(model.referenceDistance)) return false;
  if (!isfinite(model.coefficients[0])) return false;
  if (!isfinite(model.coefficients[1])) return false;
  if (!isfinite(model.coefficients[2])) return false;
  return true;
}

bool LaserModule::isCalibrationModelMonotonic(const CalibrationModel& model) const {
  const float minDistance = LASER_VALID_MEASUREMENT_MIN_RAW * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  const float maxDistance = LASER_VALID_MEASUREMENT_MAX_RAW * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  const float xMin = minDistance - model.referenceDistance;
  const float xMax = maxDistance - model.referenceDistance;
  const float kSlopeTolerance = -0.0001f;

  if (model.type == CalibrationModelType::LINEAR) {
    return model.coefficients[1] >= kSlopeTolerance;
  }

  const float dMin = 2.0f * model.coefficients[0] * xMin + model.coefficients[1];
  const float dMax = 2.0f * model.coefficients[0] * xMax + model.coefficients[1];
  return dMin >= kSlopeTolerance && dMax >= kSlopeTolerance;
}

float LaserModule::evaluateCalibrationWeight(const CalibrationModel& model, float distance) const {
  const float x = distance - model.referenceDistance;
  return model.coefficients[0] * x * x +
      model.coefficients[1] * x +
      model.coefficients[2];
}

float LaserModule::evaluateCalibrationWeight(float distance) const {
  float weight = evaluateCalibrationWeight(calibrationModel, distance);
  if (!isfinite(weight)) return NAN;
  if (weight < 0.0f) return 0.0f;
  return weight;
}

bool LaserModule::isDistanceSentinelRaw(uint16_t rawRegister, int16_t signedRaw, const char*& reason) const {
  (void)signedRaw;

  if (rawRegister == LASER_SENTINEL_OVER_RANGE_RAW) {
    reason = "SENTINEL_OVER_RANGE";
    return true;
  }

  reason = nullptr;
  return false;
}

bool LaserModule::isDistanceValidRaw(int16_t signedRaw, const char*& reason) const {
  if (signedRaw < LASER_VALID_MEASUREMENT_MIN_RAW) {
    reason = "OUT_OF_RANGE_LOW";
    return false;
  }

  if (signedRaw > LASER_VALID_MEASUREMENT_MAX_RAW) {
    reason = "OUT_OF_RANGE_HIGH";
    return false;
  }

  reason = nullptr;
  return true;
}

void LaserModule::noteDistanceValidity(
    bool valid,
    uint16_t rawRegister,
    int16_t signedRaw,
    float scaledDistance,
    bool sentinel,
    const char* reason,
    uint32_t now) {
  if (valid) {
    if (!lastMeasurementValid) {
      Serial.printf("[LASER] VALID raw_u16=%u raw_i16=%d scaled=%.2f\n",
          (unsigned int)rawRegister,
          (int)signedRaw,
          scaledDistance);
    }
    lastMeasurementValid = true;
    lastInvalidReason = nullptr;
    return;
  }

  const bool shouldLog =
      lastMeasurementValid ||
      lastInvalidReason != reason ||
      (now - lastValidityLogMs) >= LASER_INVALID_LOG_INTERVAL_MS;

  if (shouldLog) {
    if (isfinite(scaledDistance)) {
      Serial.printf("[LASER] INVALID raw_u16=%u raw_i16=%d scaled=%.2f sentinel=%d reason=%s\n",
          (unsigned int)rawRegister,
          (int)signedRaw,
          scaledDistance,
          sentinel ? 1 : 0,
          reason ? reason : "UNKNOWN");
    } else {
      Serial.printf("[LASER] INVALID sentinel=%d reason=%s\n",
          sentinel ? 1 : 0,
          reason ? reason : "UNKNOWN");
    }
    lastValidityLogMs = now;
  }

  lastMeasurementValid = false;
  lastInvalidReason = reason;
}

void LaserModule::pushStableSample(float distance, float weight) {
  distanceBuffer[bufHead] = distance;
  weightBuffer[bufHead] = weight;
  bufHead = (bufHead + 1) % WINDOW_N;
  if (bufCount < WINDOW_N) bufCount++;
}

void LaserModule::resetStableTracking(const char* reason, bool logIfActive) {
  if (logIfActive && stableState != StableState::UNSTABLE) {
    Serial.printf("[STABLE] CLEAR reason=%s\n", reason ? reason : "unspecified");
  }

  stableState = StableState::UNSTABLE;
  stableBaselineDistance = 0.0f;
  stableBaselineDistanceMm = 0.0f;
  stableBaselineWeight = 0.0f;
  stableLatchedAtMs = 0;
  invalidStableSamples = 0;
  stableCandidateStartedAtMs = 0;
  stableEarlyCheckpointLogged = false;
  bufHead = 0;
  bufCount = 0;
}

void LaserModule::beginStableCandidate(float distance, float weight) {
  if (stableState != StableState::STABLE_CANDIDATE) {
    // 本轮约 3 秒体感优化优先落在 stable build，而不是 baseline_ready/start_ready 后半段。
    // 这里记录 build 入口，便于现场直接看出候选开始到 latch 的真实耗时。
    Serial.printf(
        "[STABLE] CANDIDATE read_interval_ms=%lu early_samples=%u legacy_window=%d\n",
        static_cast<unsigned long>(LASER_READ_INTERVAL_STABLE_BUILD_MS),
        static_cast<unsigned int>(STABLE_EARLY_LATCH_SAMPLES),
        WINDOW_N);
    bufHead = 0;
    bufCount = 0;
    stableCandidateStartedAtMs = millis();
    stableEarlyCheckpointLogged = false;
  }

  stableState = StableState::STABLE_CANDIDATE;
  invalidStableSamples = 0;
  pushStableSample(distance, weight);
}

bool LaserModule::shouldClearLatchedStable(float distance, float weight, const char*& reason) const {
  if (weight < LEAVE_TH) {
    reason = "leave_threshold";
    return true;
  }

  if (fabsf(weight - stableBaselineWeight) >= STABLE_REARM_WEIGHT_DELTA_TH) {
    reason = "weight_delta";
    return true;
  }

  if (fabsf(distance - stableBaselineDistance) >= STABLE_REARM_DISTANCE_DELTA_TH) {
    reason = "distance_delta";
    return true;
  }

  reason = nullptr;
  return false;
}

bool LaserModule::shouldUseFastStableBuildReadInterval() const {
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  const bool baselineReady = rhythmStateJudge.lastResult().evidence.baselineReady;
  return currentTopState != TopState::RUNNING &&
      (stableState != StableState::STABLE_LATCHED || !baselineReady);
}

bool LaserModule::shouldLatchStableEarly(
    float latestWeight,
    float& stddev,
    float& latestDelta) const {
  if (bufCount != STABLE_EARLY_LATCH_SAMPLES) {
    stddev = NAN;
    latestDelta = NAN;
    return false;
  }

  const float mean = getMean(weightBuffer);
  stddev = getStdDev(weightBuffer);
  latestDelta = fabsf(latestWeight - mean);
  return stddev < STABLE_EARLY_STRICT_STD_TH &&
      latestDelta < STABLE_EARLY_STRICT_LATEST_DELTA_TH;
}

void LaserModule::latchStable(uint32_t now, const char* mode, float stddev) {
  float finalWeight = getMean(weightBuffer);
  float finalDistance = getMean(distanceBuffer);
  const uint32_t buildLatencyMs =
      stableCandidateStartedAtMs > 0 && now >= stableCandidateStartedAtMs
          ? (now - stableCandidateStartedAtMs)
          : 0;

  stableState = StableState::STABLE_LATCHED;
  stableBaselineWeight = finalWeight;
  stableBaselineDistance = finalDistance;
  stableBaselineDistanceMm = finalDistance * LASER_DISTANCE_RUNTIME_DIVISOR;
  stableLatchedAtMs = now;
  invalidStableSamples = 0;
  stableEarlyCheckpointLogged = false;

  Serial.printf(
      "[STABLE] LATCH mode=%s samples=%d std=%.3f weight=%.2f dist=%.2f build_ms=%lu\n",
      mode ? mode : "unknown",
      bufCount,
      stddev,
      finalWeight,
      finalDistance,
      static_cast<unsigned long>(buildLatencyMs));
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  const bool shouldCapturePrimaryBaseline =
      currentTopState != TopState::RUNNING &&
      !rhythmStateJudge.lastResult().evidence.baselineReady;

  if (!shouldCapturePrimaryBaseline) {
    return;
  }

  Serial.printf("[STABLE] EMIT weight=%.2f\n", finalWeight);

  Event e{};
  e.type = EventType::STABLE_WEIGHT;
  e.v1 = finalWeight;
  e.ts_ms = now;
  if (bus) bus->publish(e);

  // stable_weight：稳定体重。
  // 只在未律动且稳定站立时锁定一次，直到确认离台后才允许清空。
  rhythmStateJudge.refreshBaselineFromStable(finalDistance, finalWeight, now);
  emitBaselineReadyLog(now);
}

void LaserModule::emitBaselineReadyLog(uint32_t now) const {
  const RhythmStateEvidence& evidence = rhythmStateJudge.lastResult().evidence;
  Serial.printf(
      "%s [BASELINE_READY] stable_weight_kg=%.2f stable_distance=%.2f captured_ms=%lu\n",
      LogMarker::kBaselineReady,
      evidence.baselineWeightKg,
      evidence.baselineDistance,
      static_cast<unsigned long>(evidence.baselineCapturedAtMs ? evidence.baselineCapturedAtMs : now));
}

void LaserModule::publishBaselineMainVerification(
    uint32_t now,
    const RhythmStateUpdateResult& result) const {
  if (!bus) return;

  Event e{};
  e.type = EventType::BASELINE_MAIN;
  e.ts_ms = now;
  e.baselineReady = result.evidence.baselineReady;
  e.stableWeightKg = result.evidence.baselineWeightKg;
  e.ma7WeightKg = result.evidence.ma7WeightKg;
  e.deviationKg = result.evidence.deviationKg;
  e.ratio = result.evidence.ratio;
  e.abnormalDurationMs = result.evidence.abnormalDurationMs;
  e.dangerDurationMs = result.evidence.dangerDurationMs;
  strlcpy(e.mainState, rhythmStateName(result.status), sizeof(e.mainState));
  strlcpy(
      e.stopReasonText,
      result.stopReason ? result.stopReason : "NONE",
      sizeof(e.stopReasonText));
  strlcpy(
      e.stopSourceText,
      result.shouldStopByDanger ? verificationStopSourceName(VerificationStopSource::BASELINE_MAIN_LOGIC) : "NONE",
      sizeof(e.stopSourceText));
  bus->publish(e);
}

void LaserModule::startRunSummary(uint32_t now, const RhythmStateUpdateResult& rhythmResult) {
  runSummary.active = true;
  runSummary.testId = runSummary.nextTestId++;
  runSummary.startedAtMs = now;
  runSummary.samples = 0;
  runSummary.baselineReady = rhythmResult.evidence.baselineReady;
  runSummary.freqHz = 0.0f;
  runSummary.intensity = 0;
  runSummary.intensityNormalized = 0.0f;
  runSummary.baselineWeightKg =
      rhythmResult.evidence.baselineReady ? rhythmResult.evidence.baselineWeightKg : stableBaselineWeight;
  runSummary.baselineDistance =
      rhythmResult.evidence.baselineReady ? rhythmResult.evidence.baselineDistance : stableBaselineDistance;
  runSummary.fallStopEnabled = sm ? sm->fallStopEnabled() : FALL_STOP_ENABLED_DEFAULT;
  runSummary.lastRhythmStatus = rhythmResult.status;
  runSummary.lastRhythmReason = rhythmResult.reason ? rhythmResult.reason : "n/a";
  runSummary.weightKgRange = RangeTracker{};
  runSummary.distanceRange = RangeTracker{};
  runSummary.ma3WeightKgRange = RangeTracker{};
  runSummary.ma3DistanceRange = RangeTracker{};
  runSummary.ma5WeightKgRange = RangeTracker{};
  runSummary.ma5DistanceRange = RangeTracker{};
  runSummary.ma7WeightKgRange = RangeTracker{};
  runSummary.ma7DistanceRange = RangeTracker{};
  runSummary.advisoryCount = 0;
  runSummary.lastAdvisoryType = RiskAdvisoryType::NONE;
  runSummary.lastAdvisoryLevel = RiskAdvisoryLevel::NONE;
  runSummary.lastAdvisoryReason = "none";
  runSummary.recentHead = 0;
  runSummary.recentCount = 0;

  if (wave) {
    wave->getSummaryParams(runSummary.freqHz,
                           runSummary.intensity,
                           runSummary.intensityNormalized);
  }

  // Test-summary logs stay event-driven: baseline latch, run start, and run end.
  Serial.printf(
      "%s [TEST_START] test_id=%lu freq_hz=%.2f intensity=%d intensity_norm=%.3f "
      "baseline_ready=%d stable_weight_kg=%.2f stable_distance=%.2f fall_stop_enabled=%d\n",
      LogMarker::kTestStart,
      static_cast<unsigned long>(runSummary.testId),
      runSummary.freqHz,
      runSummary.intensity,
      runSummary.intensityNormalized,
      runSummary.baselineReady ? 1 : 0,
      runSummary.baselineWeightKg,
      runSummary.baselineDistance,
      runSummary.fallStopEnabled ? 1 : 0);
}

bool LaserModule::computeRunAverage(uint8_t window, float& avgWeight, float& avgDistance) const {
  static constexpr uint8_t kCapacity =
      sizeof(runSummary.recentWeightKg) / sizeof(runSummary.recentWeightKg[0]);

  if (!runSummary.active || window == 0 || window > kCapacity || runSummary.recentCount < window) {
    return false;
  }

  float weightSum = 0.0f;
  float distanceSum = 0.0f;
  uint8_t index = runSummary.recentHead;

  for (uint8_t i = 0; i < window; ++i) {
    index = (index == 0) ? static_cast<uint8_t>(kCapacity - 1) : static_cast<uint8_t>(index - 1);
    weightSum += runSummary.recentWeightKg[index];
    distanceSum += runSummary.recentDistance[index];
  }

  avgWeight = weightSum / window;
  avgDistance = distanceSum / window;
  return true;
}

void LaserModule::accumulateRunSummary(uint32_t now,
                                       float distance,
                                       float weight,
                                       const RhythmStateUpdateResult& rhythmResult) {
  (void)now;

  if (!runSummary.active) return;

  auto includeRange = [](RangeTracker& range, float value) {
    if (!isfinite(value)) return;
    if (!range.valid) {
      range.valid = true;
      range.min = value;
      range.max = value;
      return;
    }
    if (value < range.min) range.min = value;
    if (value > range.max) range.max = value;
  };

  runSummary.samples++;
  runSummary.lastRhythmStatus = rhythmResult.status;
  runSummary.lastRhythmReason = rhythmResult.reason ? rhythmResult.reason : "n/a";
  includeRange(runSummary.weightKgRange, weight);
  includeRange(runSummary.distanceRange, distance);

  static constexpr uint8_t kCapacity =
      sizeof(runSummary.recentWeightKg) / sizeof(runSummary.recentWeightKg[0]);
  runSummary.recentWeightKg[runSummary.recentHead] = weight;
  runSummary.recentDistance[runSummary.recentHead] = distance;
  runSummary.recentHead = (runSummary.recentHead + 1) % kCapacity;
  if (runSummary.recentCount < kCapacity) {
    runSummary.recentCount++;
  }

  float avgWeight = 0.0f;
  float avgDistance = 0.0f;
  if (computeRunAverage(3, avgWeight, avgDistance)) {
    includeRange(runSummary.ma3WeightKgRange, avgWeight);
    includeRange(runSummary.ma3DistanceRange, avgDistance);
  }
  if (computeRunAverage(5, avgWeight, avgDistance)) {
    includeRange(runSummary.ma5WeightKgRange, avgWeight);
    includeRange(runSummary.ma5DistanceRange, avgDistance);
  }
  if (computeRunAverage(7, avgWeight, avgDistance)) {
    includeRange(runSummary.ma7WeightKgRange, avgWeight);
    includeRange(runSummary.ma7DistanceRange, avgDistance);
  }

  if (rhythmResult.advisory.shouldLog) {
    if (runSummary.advisoryCount < 0xFFFF) {
      runSummary.advisoryCount++;
    }
    runSummary.lastAdvisoryType = rhythmResult.advisory.type;
    runSummary.lastAdvisoryLevel = rhythmResult.advisory.level;
    runSummary.lastAdvisoryReason = rhythmResult.advisory.reason ? rhythmResult.advisory.reason : "none";
  }
}

void LaserModule::finishRunSummary(uint32_t now,
                                   FaultCode stopReason,
                                   const RhythmStateUpdateResult& rhythmResult) {
  if (!runSummary.active) return;

  auto formatRange = [](const RangeTracker& range, char* buffer, size_t bufferSize) {
    if (!range.valid) {
      snprintf(buffer, bufferSize, "n/a");
      return;
    }
    snprintf(buffer, bufferSize, "%.2f..%.2f", range.min, range.max);
  };

  runSummary.lastRhythmStatus = rhythmResult.status;
  runSummary.lastRhythmReason = rhythmResult.reason ? rhythmResult.reason : "n/a";

  char weightRange[32];
  char distanceRange[32];
  char ma3WeightRange[32];
  char ma3DistanceRange[32];
  char ma5WeightRange[32];
  char ma5DistanceRange[32];
  char ma7WeightRange[32];
  char ma7DistanceRange[32];
  formatRange(runSummary.weightKgRange, weightRange, sizeof(weightRange));
  formatRange(runSummary.distanceRange, distanceRange, sizeof(distanceRange));
  formatRange(runSummary.ma3WeightKgRange, ma3WeightRange, sizeof(ma3WeightRange));
  formatRange(runSummary.ma3DistanceRange, ma3DistanceRange, sizeof(ma3DistanceRange));
  formatRange(runSummary.ma5WeightKgRange, ma5WeightRange, sizeof(ma5WeightRange));
  formatRange(runSummary.ma5DistanceRange, ma5DistanceRange, sizeof(ma5DistanceRange));
  formatRange(runSummary.ma7WeightKgRange, ma7WeightRange, sizeof(ma7WeightRange));
  formatRange(runSummary.ma7DistanceRange, ma7DistanceRange, sizeof(ma7DistanceRange));

  const bool abnormalStop = (stopReason != FaultCode::NONE);
  const uint32_t durationMs = (now >= runSummary.startedAtMs) ? (now - runSummary.startedAtMs) : 0;
  const char* summaryMarker = abnormalStop ? LogMarker::kTestAbort : LogMarker::kTestStop;
  const char* stopReasonText = sm ? sm->lastStopReasonText() : (abnormalStop ? faultCodeName(stopReason) : "NONE");
  const char* stopSourceText = sm ? sm->lastStopSourceText() : "NONE";

  Serial.printf(
      "%s [%s] test_id=%lu result=%s stop_reason=%s stop_source=%s freq_hz=%.2f intensity=%d intensity_norm=%.3f "
      "baseline_ready=%d stable_weight_kg=%.2f stable_distance=%.2f "
      "fall_stop_enabled=%d "
      "weight_range_kg=%s distance_range=%s "
      "ma3_weight_range_kg=%s ma3_distance_range=%s "
      "ma5_weight_range_kg=%s ma5_distance_range=%s "
      "ma7_weight_range_kg=%s ma7_distance_range=%s "
      "main_status=%s main_reason=%s advisory_count=%u last_advisory_type=%s "
      "last_advisory_level=%s last_advisory_reason=%s final_abnormal_duration_ms=%lu "
      "final_danger_duration_ms=%lu duration_ms=%lu samples=%lu\n",
      summaryMarker,
      abnormalStop ? "ABORT_SUMMARY" : "STOP_SUMMARY",
      static_cast<unsigned long>(runSummary.testId),
      abnormalStop ? "ABNORMAL_STOP" : "NORMAL",
      stopReasonText,
      stopSourceText,
      runSummary.freqHz,
      runSummary.intensity,
      runSummary.intensityNormalized,
      runSummary.baselineReady ? 1 : 0,
      runSummary.baselineWeightKg,
      runSummary.baselineDistance,
      sm && sm->fallStopEnabled() ? 1 : 0,
      weightRange,
      distanceRange,
      ma3WeightRange,
      ma3DistanceRange,
      ma5WeightRange,
      ma5DistanceRange,
      ma7WeightRange,
      ma7DistanceRange,
      rhythmStateName(runSummary.lastRhythmStatus),
      runSummary.lastRhythmReason ? runSummary.lastRhythmReason : "n/a",
      static_cast<unsigned int>(runSummary.advisoryCount),
      riskAdvisoryTypeName(runSummary.lastAdvisoryType),
      riskAdvisoryLevelName(runSummary.lastAdvisoryLevel),
      runSummary.lastAdvisoryReason ? runSummary.lastAdvisoryReason : "none",
      static_cast<unsigned long>(rhythmResult.evidence.abnormalDurationMs),
      static_cast<unsigned long>(rhythmResult.evidence.dangerDurationMs),
      static_cast<unsigned long>(durationMs),
      static_cast<unsigned long>(runSummary.samples));

  runSummary.active = false;
  runSummary.testId = 0;
  runSummary.startedAtMs = 0;
  runSummary.samples = 0;
  runSummary.baselineReady = false;
  runSummary.freqHz = 0.0f;
  runSummary.intensity = 0;
  runSummary.intensityNormalized = 0.0f;
  runSummary.baselineWeightKg = 0.0f;
  runSummary.baselineDistance = 0.0f;
  runSummary.fallStopEnabled = FALL_STOP_ENABLED_DEFAULT;
  runSummary.lastRhythmStatus = RhythmStateStatus::BASELINE_PENDING;
  runSummary.lastRhythmReason = "baseline_pending";
  runSummary.weightKgRange = RangeTracker{};
  runSummary.distanceRange = RangeTracker{};
  runSummary.ma3WeightKgRange = RangeTracker{};
  runSummary.ma3DistanceRange = RangeTracker{};
  runSummary.ma5WeightKgRange = RangeTracker{};
  runSummary.ma5DistanceRange = RangeTracker{};
  runSummary.ma7WeightKgRange = RangeTracker{};
  runSummary.ma7DistanceRange = RangeTracker{};
  runSummary.advisoryCount = 0;
  runSummary.lastAdvisoryType = RiskAdvisoryType::NONE;
  runSummary.lastAdvisoryLevel = RiskAdvisoryLevel::NONE;
  runSummary.lastAdvisoryReason = "none";
  runSummary.recentHead = 0;
  runSummary.recentCount = 0;
}

void LaserModule::handleRunSummaryState(TopState currentTopState,
                                        uint32_t now,
                                        float distance,
                                        float weight,
                                        const RhythmStateUpdateResult& rhythmResult) {
  const TopState previousTopState = lastObservedTopState;

  if (previousTopState != currentTopState) {
    if (previousTopState == TopState::RUNNING && currentTopState != TopState::RUNNING) {
      finishRunSummary(now, sm ? sm->activeFault() : FaultCode::NONE, rhythmResult);
    }

    if (currentTopState == TopState::RUNNING) {
      startRunSummary(now, rhythmResult);
    }

    lastObservedTopState = currentTopState;
  }

  if (currentTopState == TopState::RUNNING) {
    if (!runSummary.active) {
      startRunSummary(now, rhythmResult);
    }
    accumulateRunSummary(now, distance, weight, rhythmResult);
  }
}

void LaserModule::handleInvalidMeasurement(const char* reason) {
  rhythmStateJudge.noteInvalidMeasurement();

  if (stableState == StableState::STABLE_CANDIDATE) {
    resetStableTracking(reason, true);
    return;
  }

  if (stableState != StableState::STABLE_LATCHED) return;

  if (invalidStableSamples < 0xFF) invalidStableSamples++;
  if (invalidStableSamples >= STABLE_INVALID_GRACE_SAMPLES) {
    resetStableTracking(reason, true);
  }
}

void LaserModule::updateStableState(float distance, float weight, uint32_t now) {
  invalidStableSamples = 0;

  if (stableState == StableState::STABLE_LATCHED) {
    const char* clearReason = nullptr;
    if (shouldClearLatchedStable(distance, weight, clearReason)) {
      resetStableTracking(clearReason, true);
      if (weight > MIN_WEIGHT) {
        beginStableCandidate(distance, weight);
      }
    }
    return;
  }

  if (weight <= MIN_WEIGHT) {
    if (stableState == StableState::STABLE_CANDIDATE) {
      resetStableTracking("below_entry_threshold", true);
    }
    return;
  }

  beginStableCandidate(distance, weight);

  float earlyStdDev = NAN;
  float earlyLatestDelta = NAN;
  if (shouldLatchStableEarly(weight, earlyStdDev, earlyLatestDelta)) {
    Serial.printf(
        "[STABLE] EARLY_ACCEPT profile=strict_core samples=%d early_std=%.3f early_latest_delta=%.3f\n",
        bufCount,
        earlyStdDev,
        earlyLatestDelta);
    latchStable(now, "early_strict", earlyStdDev);
    return;
  }

  if (bufCount == STABLE_EARLY_LATCH_SAMPLES && !stableEarlyCheckpointLogged) {
    const WindowStats recentWeightStats = computeRingWindowStats(
        weightBuffer,
        bufHead,
        bufCount,
        WINDOW_N,
        bufCount - STABLE_EARLY_GUARDED_RECENT_SAMPLES,
        STABLE_EARLY_GUARDED_RECENT_SAMPLES);
    const float recentMeanDelta =
        isfinite(recentWeightStats.mean) ? fabsf(recentWeightStats.mean - getMean(weightBuffer)) : NAN;
    const bool guardedStdOk = earlyStdDev < STABLE_EARLY_GUARDED_STD_TH;
    const bool guardedLatestOk = earlyLatestDelta < STABLE_EARLY_GUARDED_LATEST_DELTA_TH;
    const bool guardedRecentStdOk = recentWeightStats.stddev < STABLE_EARLY_GUARDED_RECENT_STD_TH;
    const bool guardedRecentRangeOk = recentWeightStats.range < STABLE_EARLY_GUARDED_RECENT_RANGE_TH;
    const bool guardedRecentMeanOk = recentMeanDelta < STABLE_EARLY_GUARDED_RECENT_MEAN_DELTA_TH;
    const bool guardedEarlyEligible =
        guardedStdOk &&
        guardedLatestOk &&
        guardedRecentStdOk &&
        guardedRecentRangeOk &&
        guardedRecentMeanOk;

    // 这是 baseline build 时延点位的最后一次固件侧优化尝试：
    // 现场主问题是 early_strict 9 样本命中率不足，而不是 legacy fallback 不存在。
    // 因此这里只增加一个 guarded 补充条件：整窗仍需接近 legacy 标准，
    // 同时 recent tail 必须明显更稳、且 tail 均值不能偏离整窗均值太多。
    // 如果现场之后仍主要走 legacy_full_window，就应停止继续在这个点位细抠。
    if (guardedEarlyEligible) {
      stableEarlyCheckpointLogged = true;
      Serial.printf(
          "[STABLE] EARLY_ACCEPT profile=tail_guard samples=%d early_std=%.3f early_latest_delta=%.3f "
          "tail_std=%.3f tail_range=%.3f tail_mean_delta=%.3f exit_rule=final_attempt_guarded\n",
          bufCount,
          earlyStdDev,
          earlyLatestDelta,
          recentWeightStats.stddev,
          recentWeightStats.range,
          recentMeanDelta);
      latchStable(now, "early_strict", earlyStdDev);
      return;
    }

    stableEarlyCheckpointLogged = true;
    Serial.printf(
        "[STABLE] HOLD mode=legacy_wait samples=%d early_std=%.3f early_latest_delta=%.3f "
        "strict_std_ok=%d strict_latest_ok=%d guarded_std_ok=%d guarded_latest_ok=%d "
        "tail_std=%.3f tail_range=%.3f tail_mean_delta=%.3f "
        "tail_std_ok=%d tail_range_ok=%d tail_mean_ok=%d exit_rule=stop_if_legacy_still_dominates\n",
        bufCount,
        earlyStdDev,
        earlyLatestDelta,
        earlyStdDev < STABLE_EARLY_STRICT_STD_TH ? 1 : 0,
        earlyLatestDelta < STABLE_EARLY_STRICT_LATEST_DELTA_TH ? 1 : 0,
        guardedStdOk ? 1 : 0,
        guardedLatestOk ? 1 : 0,
        recentWeightStats.stddev,
        recentWeightStats.range,
        recentMeanDelta,
        guardedRecentStdOk ? 1 : 0,
        guardedRecentRangeOk ? 1 : 0,
        guardedRecentMeanOk ? 1 : 0);
  }

  const float fullWindowStdDev = getStdDev(weightBuffer);
  if (bufCount == WINDOW_N && fullWindowStdDev < STD_TH) {
    latchStable(now, "legacy_full_window", fullWindowStdDev);
  }
}

void LaserModule::logRhythmStateUpdate(const RhythmStateUpdateResult& result) const {
  if (result.formalEventCandidate) {
    Serial.printf(
        "%s [EVENT_AUX] formal_event_candidate=1 action_owner=baseline_main stable_weight_kg=%.2f ma7_weight_kg=%.2f deviation_kg=%.2f ratio=%.4f\n",
        LogMarker::kResearch,
        result.evidence.baselineWeightKg,
        result.evidence.ma7WeightKg,
        result.evidence.deviationKg,
        result.evidence.ratio);
  }

  if (result.advisory.shouldLog) {
    Serial.printf(
        "%s [RISK_ADVISORY] advisory_state=%s advisory_type=%s advisory_level=%s advisory_reason=%s "
        "stable_weight_kg=%.2f ma7_weight_kg=%.2f deviation_kg=%.2f ratio=%.4f peak_ratio=%.4f "
        "abnormal_duration_ms=%lu danger_duration_ms=%lu event_aux_seen=%d final_stop=0 action=advisory_only\n",
        LogMarker::kSafety,
        result.advisory.active ? "ACTIVE" : "NONE",
        riskAdvisoryTypeName(result.advisory.type),
        riskAdvisoryLevelName(result.advisory.level),
        result.advisory.reason ? result.advisory.reason : "none",
        result.evidence.baselineWeightKg,
        result.evidence.ma7WeightKg,
        result.evidence.deviationKg,
        result.evidence.ratio,
        result.advisory.peakRatio,
        static_cast<unsigned long>(result.advisory.abnormalDurationMs),
        static_cast<unsigned long>(result.advisory.dangerDurationMs),
        result.advisory.eventAuxSeen ? 1 : 0);
  }

  if (!result.logShouldEmit) return;

  const char* previousStatusName =
      result.hasPreviousLoggedStatus ? rhythmStateName(result.previousLoggedStatus) : "UNSET";
  const char* nextStatusName = rhythmStateName(result.status);

  Serial.printf(
      "%s [BASELINE_MAIN_STATE] prev=%s next=%s cause=%s reason=%s stable_weight_kg=%.2f ma7_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu user_present=%d\n",
      LogMarker::kResearch,
      previousStatusName,
      nextStatusName,
      result.logCause ? result.logCause : "transition",
      result.reason ? result.reason : "n/a",
      result.evidence.baselineWeightKg,
      result.evidence.ma7WeightKg,
      result.evidence.deviationKg,
      result.evidence.ratio,
      static_cast<unsigned long>(result.evidence.abnormalDurationMs),
      static_cast<unsigned long>(result.evidence.dangerDurationMs),
      userPresent ? 1 : 0);

  if (!result.shouldStopByDanger) {
    return;
  }

  const char* stopSource =
      result.evidence.directDangerBandTriggered
          ? "ratio_above_danger_band"
          : "abnormal_exceeded_recovery_and_danger_hold";
  Serial.printf(
      "%s [AUTO_STOP_BY_DANGER] stop_reason=%s stop_source=%s stop_detail=%s stable_weight_kg=%.2f ma7_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu fall_stop_enabled=%d\n",
      LogMarker::kSafety,
      result.stopReason ? result.stopReason : "danger_stop",
      verificationStopSourceName(VerificationStopSource::BASELINE_MAIN_LOGIC),
      stopSource,
      result.evidence.baselineWeightKg,
      result.evidence.ma7WeightKg,
      result.evidence.deviationKg,
      result.evidence.ratio,
      static_cast<unsigned long>(result.evidence.abnormalDurationMs),
      static_cast<unsigned long>(result.evidence.dangerDurationMs),
      sm && sm->fallStopEnabled() ? 1 : 0);
}

void LaserModule::logFallStopSuppressed(
    const RhythmStateUpdateResult& rhythmResult,
    const FallStopActionDecision& actionDecision) const {
  const char* stopSource =
      rhythmResult.evidence.directDangerBandTriggered
          ? "ratio_above_danger_band"
          : "abnormal_exceeded_recovery_and_danger_hold";
  Serial.printf(
      "%s [AUTO_STOP_SUPPRESSED] stop_reason=%s stop_source=%s stop_detail=%s stable_weight_kg=%.2f ma7_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu "
      "fall_stop_enabled=%d should_execute_stop=%d stop_suppressed_by_switch=%d detail=%s\n",
      LogMarker::kSafety,
      rhythmResult.stopReason ? rhythmResult.stopReason : "danger_stop",
      verificationStopSourceName(VerificationStopSource::BASELINE_MAIN_LOGIC),
      stopSource,
      rhythmResult.evidence.baselineWeightKg,
      rhythmResult.evidence.ma7WeightKg,
      rhythmResult.evidence.deviationKg,
      rhythmResult.evidence.ratio,
      static_cast<unsigned long>(rhythmResult.evidence.abnormalDurationMs),
      static_cast<unsigned long>(rhythmResult.evidence.dangerDurationMs),
      actionDecision.fallStopEnabled ? 1 : 0,
      actionDecision.shouldExecuteStop ? 1 : 0,
      actionDecision.stopSuppressedBySwitch ? 1 : 0,
      actionDecision.detail ? actionDecision.detail : "n/a");
}

void LaserModule::handleFallStopCandidate(const RhythmStateUpdateResult& rhythmResult) {
  if (!rhythmResult.shouldStopByDanger || !sm) return;

  FallStopActionDecision actionDecision = sm->decideFallSuspectedAction();
  actionDecision.verificationStopReason = rhythmResult.stopReason;
  actionDecision.verificationStopSource = VerificationStopSource::BASELINE_MAIN_LOGIC;
  if (actionDecision.stopSuppressedBySwitch) {
    logFallStopSuppressed(rhythmResult, actionDecision);
  }
  sm->applyFallSuspectedAction(actionDecision);
}

void LaserModule::taskLoop() {
  uint32_t lastRead = 0;
#if DIAG_DISABLE_LASER_SAFETY
  bool diagBannerPrinted = false;
#endif

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(20));

#if DIAG_DISABLE_LASER_SAFETY
    if (!diagBannerPrinted) {
      Serial.println("[DIAG] Laser safety triggers disabled");
      diagBannerPrinted = true;
    }
#endif

    if (needSendParams) {
      // 发布参数事件（由 BLE Transport 发给 App）
      float z, k; getParams(z, k);
      Event e{}; e.type = EventType::PARAMS; e.v1 = z; e.v2 = k; e.ts_ms = millis();
      if (bus) bus->publish(e);
      needSendParams = false;
    }

    uint32_t now = millis();
    const uint32_t readIntervalMs =
        shouldUseFastStableBuildReadInterval()
            ? LASER_READ_INTERVAL_STABLE_BUILD_MS
            : LASER_READ_INTERVAL_DEFAULT_MS;
    if (now - lastRead < readIntervalMs) continue;
    lastRead = now;

    uint8_t result = node.readInputRegisters(REG_DISTANCE, 1);
    if (result != node.ku8MBSuccess) {
      static uint32_t lastErr = 0;
      if (now - lastErr > 1000) {
        Serial.printf("❌ Modbus read fail (0x%02X)\n", result);
        lastErr = now;
      }
      noteDistanceValidity(false, 0, 0, NAN, false, "READ_FAIL", now);
      if (sm) sm->setSensorHealthy(false);
      handleInvalidMeasurement("modbus_read_fail");
      continue;
    }

    if (sm) sm->setSensorHealthy(true);

    const uint16_t rawRegister = node.getResponseBuffer(0);
    const int16_t signedDistanceRaw = static_cast<int16_t>(rawRegister);
    const float scaledDistance = signedDistanceRaw * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;

    const char* validityReason = nullptr;
    if (isDistanceSentinelRaw(rawRegister, signedDistanceRaw, validityReason)) {
      noteDistanceValidity(false, rawRegister, signedDistanceRaw, scaledDistance, true, validityReason, now);
      handleInvalidMeasurement("distance_sentinel");
      continue;
    }

    if (!isDistanceValidRaw(signedDistanceRaw, validityReason)) {
      noteDistanceValidity(false, rawRegister, signedDistanceRaw, scaledDistance, false, validityReason, now);
      handleInvalidMeasurement("distance_out_of_range");
      continue;
    }

    noteDistanceValidity(true, rawRegister, signedDistanceRaw, scaledDistance, false, nullptr, now);

    float dist = scaledDistance;
    if (!isfinite(dist)) {
      noteDistanceValidity(false, rawRegister, signedDistanceRaw, dist, false, "DISTANCE_NONFINITE", now);
      handleInvalidMeasurement("distance_invalid");
      continue;
    }

    if (needZero) {
      zeroDistance = dist;
      preferences.putFloat("zero", zeroDistance);
      calibrationModel.referenceDistance = zeroDistance;
      saveCalibrationModel();
      resetStableTracking("zero", true);
      rhythmStateJudge.reset("zero_reset");
      if (sm) sm->setStartReadiness(false, 0.0f);
      needZero = false;
      Serial.printf("🔘 ZERO done: %.2f\n", zeroDistance);
      lastLogDist = -999.0f;
      needSendParams = true;
      hasStreamSample = false;
    }

    float weight = evaluateCalibrationWeight(dist);
    if (!isfinite(weight)) {
      handleInvalidMeasurement("weight_invalid");
      continue;
    }
    latestWeightKg = weight;

    // ===== Safety supervisor: user on/off =====
    // Use hysteresis window and edge trigger to avoid repeated fault spam.
    if (weight > MIN_WEIGHT && !userPresent) {
      userPresent = true;
    } else if (weight < LEAVE_TH && userPresent) {
      userPresent = false;
#if !DIAG_DISABLE_LASER_SAFETY
      if (sm) sm->onUserOff();
#endif
    }
    if (sm) sm->setRuntimeReady(userPresent);

    // ===== Fault clear feed (Gemini rule) =====
    if (sm) sm->onWeightSample(weight);

    updateStableState(dist, weight, now);

    // LaserModule remains the measurement owner. The dedicated rhythm-state
    // judge consumes normalized context only and never owns final actions.
    TopState currentTopState = sm ? sm->state() : TopState::IDLE;

    RhythmStateJudgeInput rhythmInput{};
    rhythmInput.nowMs = now;
    rhythmInput.distance = dist;
    rhythmInput.weightKg = weight;
    rhythmInput.sampleValid = true;
    rhythmInput.userPresent = userPresent;
    rhythmInput.topState = currentTopState;
    const RhythmStateUpdateResult& rhythmResult = rhythmStateJudge.update(rhythmInput);
    if (sm) {
      // baseline_ready 才是正式 start/leave gate；
      // runtime_ready 仍仅表示人在平台上，不再单独承担“可开始”语义。
      sm->setStartReadiness(
          rhythmResult.evidence.baselineReady,
          rhythmResult.evidence.baselineWeightKg);
    }
    logRhythmStateUpdate(rhythmResult);
    publishBaselineMainVerification(now, rhythmResult);
    if (rhythmResult.shouldStopByDanger) {
#if !DIAG_DISABLE_LASER_SAFETY
      handleFallStopCandidate(rhythmResult);
#endif
    }

    currentTopState = sm ? sm->state() : currentTopState;
    handleRunSummaryState(currentTopState, now, dist, weight, rhythmResult);

    if (!userPresent &&
        currentTopState != TopState::RUNNING &&
        rhythmStateJudge.lastResult().evidence.baselineReady) {
      // stable_weight：稳定体重。
      // 只有确认离台后，才清空本轮主判断基线。
      // 这里同步清掉正式 start readiness，避免只靠首秒屏蔽或阈值调高来掩盖问题。
      rhythmStateJudge.reset("user_left_platform_confirmed");
      if (sm) sm->setStartReadiness(false, 0.0f);
    }

    if (bus && shouldEmitStream(dist, weight, now)) {
      Event e{}; e.type = EventType::STREAM; e.v1 = dist; e.v2 = weight; e.ts_ms = millis();
      bus->publish(e);
      noteStreamSent(dist, weight, now);
    }

    // ===== Silent log =====
    if (fabsf(dist - lastLogDist) > LOG_TH) {
#if DEBUG_LASER_STREAM
      Serial.printf(">> Dist %6.2f -> %6.2f | W %5.2f\n",
        lastLogDist == -999.0f ? 0 : lastLogDist, dist, weight);
#endif
      lastLogDist = dist;
    }
  }
}
