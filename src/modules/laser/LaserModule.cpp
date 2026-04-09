#include "LaserModule.h"
#include "core/LogMarkers.h"
#include <math.h>
#include <string.h>
#include "modules/wave/WaveModule.h"

namespace {
constexpr uint32_t kLaserLoopIntervalNoLaserMs = 200UL;
constexpr uint32_t kLaserLoopIntervalUnavailableIdleMs = 250UL;
constexpr uint32_t kLaserUnavailableIdleReadBackoffMs = 3000UL;
constexpr uint32_t kModbusReadFailureSummaryIntervalMs = 10000UL;

struct WindowStats {
  float mean = NAN;
  float stddev = NAN;
  float range = NAN;
};

struct StableWindowMetrics {
  bool valid = false;
  float mean = NAN;
  float stddev = NAN;
  float range = NAN;
  float drift = NAN;
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

StableWindowMetrics computeStableWindowMetrics(
    const float* values,
    int head,
    int count,
    int capacity,
    int sampleCount) {
  StableWindowMetrics metrics{};
  if (!values || sampleCount <= 1 || count < sampleCount) {
    return metrics;
  }

  const int startOffset = count - sampleCount;
  const WindowStats full = computeRingWindowStats(
      values,
      head,
      count,
      capacity,
      startOffset,
      sampleCount);
  if (!isfinite(full.mean) || !isfinite(full.stddev) || !isfinite(full.range)) {
    return metrics;
  }

  const int firstHalfCount = sampleCount / 2;
  const int secondHalfCount = sampleCount - firstHalfCount;
  if (firstHalfCount <= 0 || secondHalfCount <= 0) {
    return metrics;
  }

  const WindowStats firstHalf = computeRingWindowStats(
      values,
      head,
      count,
      capacity,
      startOffset,
      firstHalfCount);
  const WindowStats secondHalf = computeRingWindowStats(
      values,
      head,
      count,
      capacity,
      startOffset + firstHalfCount,
      secondHalfCount);
  if (!isfinite(firstHalf.mean) || !isfinite(secondHalf.mean)) {
    return metrics;
  }

  metrics.valid = true;
  metrics.mean = full.mean;
  metrics.stddev = full.stddev;
  metrics.range = full.range;
  metrics.drift = fabsf(secondHalf.mean - firstHalf.mean);
  return metrics;
}

float computeRingTrimmedMean(
    const float* values,
    int head,
    int count,
    int capacity,
    int sampleCount,
    int trimCount) {
  if (!values || sampleCount <= 0 || count < sampleCount) {
    return NAN;
  }

  const int startOffset = count - sampleCount;
  const int oldestIndex = (count == capacity) ? head : 0;
  float ordered[WINDOW_N]{};
  for (int i = 0; i < sampleCount; ++i) {
    const int index = (oldestIndex + startOffset + i) % capacity;
    ordered[i] = values[index];
  }

  for (int i = 1; i < sampleCount; ++i) {
    const float key = ordered[i];
    int j = i - 1;
    while (j >= 0 && ordered[j] > key) {
      ordered[j + 1] = ordered[j];
      --j;
    }
    ordered[j + 1] = key;
  }

  int keepStart = trimCount;
  int keepEnd = sampleCount - trimCount;
  if (keepStart >= keepEnd) {
    keepStart = 0;
    keepEnd = sampleCount;
  }

  float sum = 0.0f;
  int kept = 0;
  for (int i = keepStart; i < keepEnd; ++i) {
    sum += ordered[i];
    ++kept;
  }

  return kept > 0 ? (sum / kept) : NAN;
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

bool LaserModule::measurementBypassActive() const {
  return !deviceConfig.laserInstalled;
}

void LaserModule::logConfigTruth(const char* source, const char* reason) {
  const bool measurementBypass = measurementBypassActive();
  Serial.printf(
      "[LAYER:CONFIG_TRUTH] measurement_bypass=%d platform_model=%s laser_installed=%d laser_available=%d protection_degraded=%d source=%s",
      measurementBypass ? 1 : 0,
      platformModelName(deviceConfig.platformModel),
      deviceConfig.laserInstalled ? 1 : 0,
      laserAvailable() ? 1 : 0,
      protectionDegraded() ? 1 : 0,
      source ? source : "unknown");
  if (reason && reason[0] != '\0') {
    Serial.printf(" reason=%s", reason);
  }
  Serial.printf("\n");
  hasLoggedMeasurementBypassState = true;
  lastLoggedMeasurementBypassState = measurementBypass;
}

void LaserModule::begin(EventBus* eb, SystemStateMachine* fsm, WaveModule* waveModule) {
  bus = eb;
  sm = fsm;
  wave = waveModule;
  if (sm) sm->attachLaserModule(this);
  lastObservedTopState = sm ? sm->state() : TopState::IDLE;
  phase2Thresholds = phase2ThresholdConfig();

  preferences.begin("scale_cal", false);
  loadDeviceConfig();
  zeroDistance = preferences.getFloat("zero", -22.0f);
  scaleFactor  = preferences.getFloat("factor", 1.0f);
  loadCalibrationModel();

  Serial.printf("\n=== LaserModule boot ===\nZero=%.2f K=%.4f\n", zeroDistance, scaleFactor);
  Serial.printf("[DEVICE] CONFIG platform_model=%s laser_installed=%d\n",
      platformModelName(deviceConfig.platformModel),
      deviceConfig.laserInstalled ? 1 : 0);
  logConfigTruth("boot");
  Serial.printf("[CAL] MODEL type=%s ref=%.2f c0=%.6f c1=%.6f c2=%.6f\n",
      calibrationModelTypeName(calibrationModel.type),
      calibrationModel.referenceDistance,
      calibrationModel.coefficients[0],
      calibrationModel.coefficients[1],
      calibrationModel.coefficients[2]);

  Serial1.begin(MODBUS_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
  node.begin(MODBUS_SLAVE_ID, Serial1);

  needSendParams = true;
  rhythmStateJudge.configure(phase2Thresholds.rhythm);
  rhythmStateJudge.reset("boot");
  refreshEffectiveZero();
  resetStableSignalFilter();
  syncStableLiveContract(millis());
  clearStableContractBridge();
  resetMeasurementPlane("boot", false);
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
  resetMeasurementPlane("scale_cal", true);
  releaseOccupiedCycle("scale_cal", millis());
  rhythmStateJudge.reset("scale_cal_reset");
  clearStableContractBridge();
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

bool LaserModule::isUserPresent() const {
  return stableContract.userPresent;
}

bool LaserModule::baselineReady() const {
  return stableContract.baselineReadyLatched;
}

float LaserModule::stableWeightKg() const {
  return stableContract.stableReadyLive ? stableBaselineWeight : 0.0f;
}

PlatformModel LaserModule::platformModel() const {
  return deviceConfig.platformModel;
}

bool LaserModule::laserInstalled() const {
  return deviceConfig.laserInstalled;
}

bool LaserModule::laserAvailable() const {
  return deviceConfig.laserInstalled && lastMeasurementValid;
}

bool LaserModule::protectionDegraded() const {
  if (!deviceConfig.laserInstalled) {
    return true;
  }
  return !laserAvailable();
}

void LaserModule::getDeviceConfig(DeviceConfigSnapshot& out) const {
  out = deviceConfig;
}

bool LaserModule::setDeviceConfig(
    PlatformModel nextPlatformModel,
    bool nextLaserInstalled,
    String& reason) {
  if (!isKnownPlatformModel(nextPlatformModel)) {
    reason = "INVALID_PLATFORM_MODEL";
    return false;
  }

  if (nextLaserInstalled != platformModelImpliesLaserInstalled(nextPlatformModel)) {
    reason = "CONFIG_CONFLICT";
    return false;
  }

  const bool changed =
      deviceConfig.platformModel != nextPlatformModel ||
      deviceConfig.laserInstalled != nextLaserInstalled;

  deviceConfig.platformModel = nextPlatformModel;
  deviceConfig.laserInstalled = nextLaserInstalled;
  saveDeviceConfig();

  if (changed) {
    applyDeviceConfigRuntimeEffects("device_set_config");
  }

  Serial.printf("[DEVICE] CONFIG APPLY source=device_set_config platform_model=%s laser_installed=%d\n",
      platformModelName(deviceConfig.platformModel),
      deviceConfig.laserInstalled ? 1 : 0);
  return true;
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
  resetMeasurementPlane("cal_model", true);
  releaseOccupiedCycle("cal_model", millis());
  rhythmStateJudge.reset("cal_model_reset");
  clearStableContractBridge();
  if (sm) sm->setStartReadiness(false, 0.0f);
  needSendParams = true;
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

void LaserModule::resetMeasurementPlane(const char* reason, bool logReset) {
  ma12Head = 0;
  ma12Count = 0;
  measurementPlaneLogStartedAtMs = 0;
  measurementPlaneLogSamples = 0;
  lastInvalidMeasurementEventMs = 0;
  lastInvalidMeasurementEventReason = nullptr;
  if (logReset) {
    Serial.printf(
        "[LAYER:MEASUREMENT_PLANE] action=reset reason=%s ma12_window=%u\n",
        reason ? reason : "unspecified",
        static_cast<unsigned>(MEASUREMENT_MA12_WINDOW));
  }
}

void LaserModule::pushMeasurementWeightSample(float weight) {
  ma12WeightBuffer[ma12Head] = weight;
  ma12Head = (ma12Head + 1) % MEASUREMENT_MA12_WINDOW;
  if (ma12Count < MEASUREMENT_MA12_WINDOW) {
    ma12Count++;
  }
}

bool LaserModule::currentMa12(float& out) const {
  if (ma12Count < MEASUREMENT_MA12_WINDOW) {
    out = NAN;
    return false;
  }

  float sum = 0.0f;
  for (uint8_t i = 0; i < MEASUREMENT_MA12_WINDOW; ++i) {
    sum += ma12WeightBuffer[i];
  }
  out = sum / static_cast<float>(MEASUREMENT_MA12_WINDOW);
  return true;
}

void LaserModule::publishMeasurementSample(
    uint32_t now,
    bool valid,
    float distance,
    float weight,
    const char* reason) {
  const bool shouldEmitInvalid =
      !valid &&
      (lastInvalidMeasurementEventReason != reason ||
       (now - lastInvalidMeasurementEventMs) >= MEASUREMENT_INVALID_KEEPALIVE_MS);
  if (!valid && !shouldEmitInvalid) {
    return;
  }

  float ma12 = NAN;
  const bool ma12Ready = valid && currentMa12(ma12);
  latestMeasurementSampleValid = valid;
  latestMeasurementSampleDistance = distance;
  latestMeasurementSampleWeight = weight;
  latestMeasurementSampleMa12Ready = ma12Ready;
  latestMeasurementSampleMa12 = ma12Ready ? ma12 : 0.0f;
  latestMeasurementSampleReason = valid ? nullptr : reason;
  hasLatestMeasurementSample = true;

  Event e{};
  e.type = EventType::STREAM;
  e.ts_ms = now;
  e.sampleSeq = ++measurementSequence;
  e.measurementValid = valid;
  e.distance = distance;
  e.weightKg = weight;
  e.ma12Ready = ma12Ready;
  e.ma12WeightKg = ma12Ready ? ma12 : 0.0f;
  if (!valid) {
    strlcpy(e.measurementReason, reason ? reason : "INVALID", sizeof(e.measurementReason));
    lastInvalidMeasurementEventMs = now;
    lastInvalidMeasurementEventReason = reason;
  } else {
    lastInvalidMeasurementEventReason = nullptr;
  }

  if (bus) {
    bus->publish(e);
  }

  measurementPlaneLogSamples++;
  if (measurementPlaneLogStartedAtMs == 0) {
    measurementPlaneLogStartedAtMs = now;
  }

  const bool shouldLogSummary =
      !hasLoggedMeasurementSummary ||
      lastLoggedMeasurementSummaryValid != valid ||
      lastLoggedMeasurementSummaryReason != latestMeasurementSampleReason;
  if (shouldLogSummary) {
    logMeasurementPlaneSummary(
        now,
        valid,
        distance,
        weight,
        ma12Ready,
        ma12Ready ? ma12 : 0.0f,
        latestMeasurementSampleReason,
        "measurement_edge");
  } else if (DEBUG_MEASUREMENT_PLANE_VERBOSE) {
    const uint32_t elapsedMs = now - measurementPlaneLogStartedAtMs;
    if (elapsedMs >= MEASUREMENT_PLANE_LOG_INTERVAL_MS) {
      logMeasurementPlaneSummary(
          now,
          valid,
          distance,
          weight,
          ma12Ready,
          ma12Ready ? ma12 : 0.0f,
          latestMeasurementSampleReason,
          "verbose_periodic");
    }
  }
}

void LaserModule::logMeasurementPlaneSummary(
    uint32_t now,
    bool valid,
    float distance,
    float weight,
    bool ma12Ready,
    float ma12,
    const char* reason,
    const char* trigger) {
  const uint32_t elapsedMs = now - measurementPlaneLogStartedAtMs;
  const float approxRateHz = elapsedMs > 0
      ? (static_cast<float>(measurementPlaneLogSamples) * 1000.0f / static_cast<float>(elapsedMs))
      : 0.0f;
  Serial.printf(
      "[LAYER:MEASUREMENT_PLANE] seq=%lu valid=%d distance=%.2f weight=%.2f ma12_ready=%d ma12=%.2f "
      "reason=%s target_read_interval_ms=%lu approx_rate_hz=%.2f trigger=%s\n",
      static_cast<unsigned long>(measurementSequence),
      valid ? 1 : 0,
      distance,
      weight,
      ma12Ready ? 1 : 0,
      ma12Ready ? ma12 : 0.0f,
      valid ? "NONE" : (reason ? reason : "INVALID"),
      static_cast<unsigned long>(LASER_MEASUREMENT_READ_INTERVAL_MS),
      approxRateHz,
      trigger ? trigger : "unknown");
  if (DEBUG_MEASUREMENT_PLANE_VERBOSE) {
    Serial.printf(
        "[LAYER:MEASUREMENT_CARRIER] carrier=EVT:STREAM queue_policy=ordered_no_overwrite seq=%lu valid=%d "
        "reason=%s\n",
        static_cast<unsigned long>(measurementSequence),
        valid ? 1 : 0,
        valid ? "NONE" : (reason ? reason : "INVALID"));
  }
  if (DEBUG_MEASUREMENT_PLANE_VERBOSE && ma12Ready) {
    Serial.printf(
        "[LAYER:MA12] seq=%lu window=%u value=%.2f input=weightKg\n",
        static_cast<unsigned long>(measurementSequence),
        static_cast<unsigned>(MEASUREMENT_MA12_WINDOW),
        ma12);
  }
  hasLoggedMeasurementSummary = true;
  lastLoggedMeasurementSummaryValid = valid;
  lastLoggedMeasurementSummaryReason = valid ? nullptr : reason;
  measurementPlaneLogStartedAtMs = now;
  measurementPlaneLogSamples = 0;
}

void LaserModule::logLatestMeasurementPlaneSummary(const char* trigger) {
  if (!hasLatestMeasurementSample) {
    return;
  }
  logMeasurementPlaneSummary(
      millis(),
      latestMeasurementSampleValid,
      latestMeasurementSampleDistance,
      latestMeasurementSampleWeight,
      latestMeasurementSampleMa12Ready,
      latestMeasurementSampleMa12,
      latestMeasurementSampleReason,
      trigger);
}

void LaserModule::loadDeviceConfig() {
  uint8_t storedModel = preferences.getUChar(
      "cfg_model",
      static_cast<uint8_t>(PlatformModel::PLUS));
  PlatformModel parsedModel = static_cast<PlatformModel>(storedModel);
  if (!isKnownPlatformModel(parsedModel)) {
    parsedModel = PlatformModel::PLUS;
  }

  const uint8_t storedLaserInstalled = preferences.getUChar("cfg_laser", 1);
  const bool normalizedLaserInstalled = platformModelImpliesLaserInstalled(parsedModel);

  deviceConfig.platformModel = parsedModel;
  deviceConfig.laserInstalled = normalizedLaserInstalled;

  if (storedLaserInstalled != static_cast<uint8_t>(normalizedLaserInstalled)) {
    saveDeviceConfig();
  }
}

void LaserModule::saveDeviceConfig() {
  preferences.putUChar("cfg_model", static_cast<uint8_t>(deviceConfig.platformModel));
  preferences.putUChar("cfg_laser", deviceConfig.laserInstalled ? 1 : 0);
}

void LaserModule::applyDeviceConfigRuntimeEffects(const char* source) {
  releaseOccupiedCycle(source, millis());
  resetStableTracking(source, true);
  resetRuntimeZero(source);
  resetMeasurementPlane(source, true);
  rhythmStateJudge.reset(source ? source : "device_config");
  clearStableContractBridge();
  stableContract.userPresent = false;
  presenceEnterConfirmCount = 0;
  presenceExitConfirmCount = 0;
  invalidPresenceSamples = 0;
  stableExitConfirmCount = 0;
  stableExitPendingReason = nullptr;
  latestWeightKg = 0.0f;
  lastMeasurementValid = false;
  lastInvalidReason = deviceConfig.laserInstalled ? nullptr : "LASER_NOT_INSTALLED";
  lastLogDist = -999.0f;

  logConfigTruth(source ? source : "device_config");

  if (!sm) return;

  sm->setRuntimeReady(false);
  sm->setStartReadiness(false, 0.0f);
  sm->setSensorHealthy(false);
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
  dualZero.calibrationZeroDistance = calibrationModel.referenceDistance;
  refreshEffectiveZero();
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
  resetRuntimeZero(source);

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

float LaserModule::evaluateCalibrationWeight(
    const CalibrationModel& model,
    float distance,
    float zeroReferenceDistance) const {
  const float x = distance - zeroReferenceDistance;
  return model.coefficients[0] * x * x +
      model.coefficients[1] * x +
      model.coefficients[2];
}

float LaserModule::evaluateCalibrationWeight(const CalibrationModel& model, float distance) const {
  return evaluateCalibrationWeight(model, distance, model.referenceDistance);
}

float LaserModule::evaluateCalibrationWeight(float distance) const {
  float weight = evaluateCalibrationWeight(calibrationModel, distance, dualZero.effectiveZeroDistance);
  if (!isfinite(weight)) return NAN;
  if (weight < 0.0f) return 0.0f;
  return weight;
}

float LaserModule::computeUnlockedEffectiveZeroDistance() const {
  float calibrationZero = dualZero.calibrationZeroDistance;
  if (!isfinite(calibrationZero)) {
    calibrationZero = calibrationModel.referenceDistance;
  }
  if (!isfinite(calibrationZero)) {
    calibrationZero = zeroDistance;
  }

  float effectiveZero = calibrationZero;
  if (phase2Thresholds.runtimeZero.applyToWeightConversion &&
      dualZero.runtimeZeroValid &&
      isfinite(dualZero.runtimeZeroDistance)) {
    float runtimeDelta = dualZero.runtimeZeroDistance - calibrationZero;
    const float clamp = phase2Thresholds.runtimeZero.clampMaxOffsetFromCalibration;
    if (runtimeDelta > clamp) runtimeDelta = clamp;
    if (runtimeDelta < -clamp) runtimeDelta = -clamp;
    effectiveZero = calibrationZero + runtimeDelta;
  }

  if (!isfinite(effectiveZero)) {
    return 0.0f;
  }
  return effectiveZero;
}

float LaserModule::computeEffectiveZeroDistance() const {
  if (dualZero.effectiveZeroLocked) {
    return isfinite(dualZero.effectiveZeroDistance) ? dualZero.effectiveZeroDistance : 0.0f;
  }
  return computeUnlockedEffectiveZeroDistance();
}

void LaserModule::refreshEffectiveZero() {
  dualZero.effectiveZeroDistance = computeEffectiveZeroDistance();
}

void LaserModule::resetStableSignalFilter() {
  stableFilterValid = false;
  stableFilteredDistance = 0.0f;
  stableFilteredWeight = 0.0f;
}

bool LaserModule::updateStableSignalFilter(
    float distance,
    float& filteredDistance,
    float& filteredWeight) {
  if (!isfinite(distance)) {
    resetStableSignalFilter();
    filteredDistance = NAN;
    filteredWeight = NAN;
    return false;
  }

  const float alpha = phase2Thresholds.stable.filterDistanceAlpha;
  if (!stableFilterValid || !isfinite(stableFilteredDistance)) {
    stableFilteredDistance = distance;
    stableFilterValid = true;
  } else {
    stableFilteredDistance =
        stableFilteredDistance + alpha * (distance - stableFilteredDistance);
  }

  stableFilteredWeight = evaluateCalibrationWeight(stableFilteredDistance);
  if (!isfinite(stableFilteredWeight)) {
    resetStableSignalFilter();
    filteredDistance = NAN;
    filteredWeight = NAN;
    return false;
  }

  filteredDistance = stableFilteredDistance;
  filteredWeight = stableFilteredWeight;
  return true;
}

bool LaserModule::runtimeZeroRefreshFrozen(TopState currentTopState) const {
  return currentTopState == TopState::RUNNING ||
      stableContract.userPresent ||
      stableContract.stableCandidate ||
      dualZero.occupiedCycleActive ||
      dualZero.effectiveZeroLocked ||
      (stableContract.baselineReadyLatched && dualZero.occupiedCycleActive);
}

void LaserModule::lockEffectiveZeroForOccupiedCycle(uint32_t now, const char* reason) {
  if (dualZero.effectiveZeroLocked) {
    return;
  }

  dualZero.occupiedCycleActive = true;
  dualZero.effectiveZeroLocked = true;
  dualZero.effectiveZeroDistance = computeUnlockedEffectiveZeroDistance();
  dualZero.effectiveZeroUsesRuntime =
      phase2Thresholds.runtimeZero.applyToWeightConversion &&
      dualZero.runtimeZeroValid &&
      fabsf(dualZero.effectiveZeroDistance - dualZero.calibrationZeroDistance) > 0.0001f;
  dualZero.effectiveZeroLockedAtMs = now;

  Serial.printf(
      "[ZERO_EFFECTIVE] action=lock reason=%s occupied_cycle=1 source=%s effective_zero=%.2f calibration_zero=%.2f runtime_zero=%.2f\n",
      reason ? reason : "occupied_cycle_start",
      dualZero.effectiveZeroUsesRuntime ? "runtime_zero" : "calibration_zero",
      dualZero.effectiveZeroDistance,
      dualZero.calibrationZeroDistance,
      dualZero.runtimeZeroValid ? dualZero.runtimeZeroDistance : dualZero.calibrationZeroDistance);
}

void LaserModule::releaseOccupiedCycle(const char* reason, uint32_t now) {
  const bool wasLocked = dualZero.effectiveZeroLocked || dualZero.occupiedCycleActive;
  dualZero.occupiedCycleActive = false;
  dualZero.effectiveZeroLocked = false;
  dualZero.effectiveZeroUsesRuntime = false;
  dualZero.effectiveZeroLockedAtMs = 0;
  resetStableSignalFilter();
  refreshEffectiveZero();

  if (wasLocked) {
    Serial.printf(
        "[ZERO_EFFECTIVE] action=unlock reason=%s occupied_cycle=0 effective_zero=%.2f now_ms=%lu\n",
        reason ? reason : "occupied_cycle_clear",
        dualZero.effectiveZeroDistance,
        static_cast<unsigned long>(now));
  }
}

void LaserModule::resetRuntimeZero(const char* reason) {
  const bool hadRuntimeZero = dualZero.runtimeZeroValid;
  dualZero.runtimeZeroDistance = 0.0f;
  dualZero.runtimeZeroValid = false;
  dualZero.effectiveZeroUsesRuntime = false;
  dualZero.runtimeZeroCapturedAtMs = 0;
  dualZero.lastRuntimeZeroCandidateDistance = 0.0f;
  dualZero.runtimeZeroWindowStartedAtMs = 0;
  runtimeZeroHead = 0;
  runtimeZeroCount = 0;
  refreshEffectiveZero();

  if (hadRuntimeZero) {
    Serial.printf(
        "[ZERO_RUNTIME] CLEAR reason=%s calibration_zero=%.2f effective_zero=%.2f\n",
        reason ? reason : "unspecified",
        dualZero.calibrationZeroDistance,
        dualZero.effectiveZeroDistance);
  }
}

void LaserModule::observeRuntimeZero(float distance, float weight, uint32_t now) {
  const LaserRuntimeZeroThresholdConfig& runtimeZeroCfg = phase2Thresholds.runtimeZero;
  const uint8_t requiredSamples = runtimeZeroCfg.refreshSamples > WINDOW_N
      ? WINDOW_N
      : runtimeZeroCfg.refreshSamples;
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;

  if (!runtimeZeroCfg.refreshEnabled ||
      requiredSamples == 0 ||
      runtimeZeroRefreshFrozen(currentTopState)) {
    return;
  }

  const bool emptyWindowEligible =
      isfinite(distance) &&
      isfinite(weight) &&
      !stableContract.userPresent &&
      currentTopState != TopState::RUNNING &&
      weight <= runtimeZeroCfg.emptyWindowMaxWeightKg;
  if (!emptyWindowEligible) {
    runtimeZeroHead = 0;
    runtimeZeroCount = 0;
    dualZero.runtimeZeroWindowStartedAtMs = 0;
    return;
  }

  if (runtimeZeroCount == 0) {
    dualZero.runtimeZeroWindowStartedAtMs = now;
  }

  runtimeZeroBuffer[runtimeZeroHead] = distance;
  runtimeZeroHead = (runtimeZeroHead + 1) % WINDOW_N;
  if (runtimeZeroCount < WINDOW_N) {
    runtimeZeroCount++;
  }

  if (runtimeZeroCount < requiredSamples) {
    return;
  }

  const WindowStats stats = computeRingWindowStats(
      runtimeZeroBuffer,
      runtimeZeroHead,
      runtimeZeroCount,
      WINDOW_N,
      runtimeZeroCount - requiredSamples,
      requiredSamples);
  if (!isfinite(stats.mean) ||
      !isfinite(stats.stddev) ||
      !isfinite(stats.range) ||
      stats.stddev > runtimeZeroCfg.refreshStdDevDistance ||
      stats.range > runtimeZeroCfg.refreshRangeDistance) {
    return;
  }

  dualZero.lastRuntimeZeroCandidateDistance = stats.mean;
  const float driftFromCalibration = fabsf(stats.mean - dualZero.calibrationZeroDistance);
  if (driftFromCalibration > runtimeZeroCfg.driftGuardDistance) {
    return;
  }

  const bool changed =
      !dualZero.runtimeZeroValid ||
      fabsf(dualZero.runtimeZeroDistance - stats.mean) > 0.0001f;
  dualZero.runtimeZeroDistance = stats.mean;
  dualZero.runtimeZeroValid = true;
  dualZero.runtimeZeroCapturedAtMs = now;
  dualZero.effectiveZeroUsesRuntime = runtimeZeroCfg.applyToWeightConversion;
  refreshEffectiveZero();

  if (changed) {
    Serial.printf(
        "[ZERO_RUNTIME] REFRESH runtime_zero=%.2f calibration_zero=%.2f effective_zero=%.2f "
        "samples=%u std=%.4f range=%.4f window_ms=%lu apply_to_weight=%d\n",
        dualZero.runtimeZeroDistance,
        dualZero.calibrationZeroDistance,
        dualZero.effectiveZeroDistance,
        static_cast<unsigned>(requiredSamples),
        stats.stddev,
        stats.range,
        static_cast<unsigned long>(
            dualZero.runtimeZeroWindowStartedAtMs > 0 && now >= dualZero.runtimeZeroWindowStartedAtMs
                ? (now - dualZero.runtimeZeroWindowStartedAtMs)
                : 0),
        runtimeZeroCfg.applyToWeightConversion ? 1 : 0);
  }
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

  if (reason && strcmp(reason, "READ_FAIL") == 0) {
    lastMeasurementValid = false;
    lastInvalidReason = reason;
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

void LaserModule::noteModbusReadFailure(uint8_t result, uint32_t now) {
  if (!hasLoggedModbusReadFailure || lastModbusReadFailureCode != result) {
    clearModbusReadFailureBurst(now, true);
    Serial.printf("❌ Modbus read fail (0x%02X)\n", result);
    hasLoggedModbusReadFailure = true;
    lastModbusReadFailureCode = result;
    lastModbusReadFailureLogMs = now;
    suppressedModbusReadFailureCount = 0;
    return;
  }

  suppressedModbusReadFailureCount += 1;
  const uint32_t windowMs = now - lastModbusReadFailureLogMs;
  if (windowMs < kModbusReadFailureSummaryIntervalMs) {
    return;
  }

  if (suppressedModbusReadFailureCount > 0) {
    Serial.printf(
        "❌ Modbus read fail (0x%02X) suppressed=%lu window_ms=%lu\n",
        result,
        static_cast<unsigned long>(suppressedModbusReadFailureCount),
        static_cast<unsigned long>(windowMs));
  }

  hasLoggedModbusReadFailure = true;
  lastModbusReadFailureCode = result;
  lastModbusReadFailureLogMs = now;
  suppressedModbusReadFailureCount = 0;
}

void LaserModule::clearModbusReadFailureBurst(uint32_t now, bool flushSummary) {
  if (flushSummary && hasLoggedModbusReadFailure && suppressedModbusReadFailureCount > 0) {
    Serial.printf(
        "❌ Modbus read fail (0x%02X) suppressed=%lu window_ms=%lu\n",
        lastModbusReadFailureCode,
        static_cast<unsigned long>(suppressedModbusReadFailureCount),
        static_cast<unsigned long>(now - lastModbusReadFailureLogMs));
  }
  hasLoggedModbusReadFailure = false;
  lastModbusReadFailureCode = 0;
  lastModbusReadFailureLogMs = 0;
  suppressedModbusReadFailureCount = 0;
}

void LaserModule::pushStableSample(float distance, float weight) {
  distanceBuffer[bufHead] = distance;
  weightBuffer[bufHead] = weight;
  bufHead = (bufHead + 1) % WINDOW_N;
  if (bufCount < WINDOW_N) bufCount++;
}

bool LaserModule::updatePresenceState(float weight) {
  const LaserPresenceThresholdConfig& presence = phase2Thresholds.presence;
  const uint8_t confirmSamples = presence.confirmSamples == 0 ? 1 : presence.confirmSamples;
  const bool previousUserPresent = stableContract.userPresent;

  if (weight >= presence.enterThresholdKg) {
    if (presenceEnterConfirmCount < 0xFF) {
      presenceEnterConfirmCount++;
    }
    presenceExitConfirmCount = 0;
    if (!stableContract.userPresent && presenceEnterConfirmCount >= confirmSamples) {
      stableContract.userPresent = true;
      invalidPresenceSamples = 0;
    }
  } else if (weight <= presence.exitThresholdKg) {
    if (presenceExitConfirmCount < 0xFF) {
      presenceExitConfirmCount++;
    }
    presenceEnterConfirmCount = 0;
    if (stableContract.userPresent && presenceExitConfirmCount >= confirmSamples) {
      stableContract.userPresent = false;
    }
  } else {
    presenceEnterConfirmCount = 0;
    presenceExitConfirmCount = 0;
  }

  return previousUserPresent != stableContract.userPresent;
}

void LaserModule::noteInvalidPresenceSample(uint32_t now, const char* reason) {
  const LaserPresenceThresholdConfig& presence = phase2Thresholds.presence;
  if (presence.invalidExitSamples == 0) {
    return;
  }

  if (invalidPresenceSamples < 0xFF) {
    invalidPresenceSamples++;
  }

  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  if (currentTopState == TopState::RUNNING) {
    return;
  }

  if (!stableContract.userPresent || invalidPresenceSamples < presence.invalidExitSamples) {
    return;
  }

  stableContract.userPresent = false;
  presenceEnterConfirmCount = 0;
  presenceExitConfirmCount = 0;
  Serial.printf(
      "[PRESENCE] exit reason=%s invalid_samples=%u now_ms=%lu\n",
      reason ? reason : "measurement_invalid",
      static_cast<unsigned>(invalidPresenceSamples),
      static_cast<unsigned long>(now));
}

void LaserModule::syncStableLiveContract(uint32_t now) {
  stableContract.stableCandidate = (stableState == StableState::STABLE_CANDIDATE);
  stableContract.stableReadyLive = (stableState == StableState::STABLE_LATCHED);

  if (stableContract.stableReadyLive) {
    stableContract.stableReadyWeightKg = stableBaselineWeight;
    stableContract.stableReadyDistance = stableBaselineDistance;
    stableContract.stableReadyAtMs = stableLatchedAtMs ? stableLatchedAtMs : now;
  } else {
    stableContract.stableReadyWeightKg = 0.0f;
    stableContract.stableReadyDistance = 0.0f;
    stableContract.stableReadyAtMs = 0;
  }
}

void LaserModule::latchBaselineReadyFromStable(uint32_t now, float distance, float weight) {
  stableContract.baselineReadyLatched = true;
  stableContract.baselineReadyWeightKg = weight;
  stableContract.baselineReadyDistance = distance;
  stableContract.baselineReadyAtMs = now;
}

void LaserModule::syncStartReadyContract(
    uint32_t now,
    TopState currentTopState,
    const RhythmStateUpdateResult& result) {
  (void)now;
  (void)result;

  const LaserStartGateConfig& startGate = phase2Thresholds.startGate;
  const bool measurementValid = !startGate.requireMeasurementValid || lastMeasurementValid;
  const bool userPresentOk = !startGate.requireUserPresent || stableContract.userPresent;
  // baselineReadyLatched is the durable owner-side evidence that a valid
  // pre-start baseline has been established for the current occupied cycle.
  // start_ready stays separate from baseline_ready because it still requires
  // user presence and measurement health, but it no longer collapses back to a
  // short idle live-stable window once that baseline has been accepted.
  const bool baselineReadyOk =
      !startGate.requireBaselineReady ||
      (stableContract.baselineReadyLatched &&
       stableContract.baselineReadyWeightKg >= startGate.minimumBaselineWeightKg);
  const bool liveStableOk =
      !startGate.requireLiveStableWhenIdle ||
      currentTopState == TopState::RUNNING ||
      stableContract.stableReadyLive;

  bool startReady = false;
  const char* reason = "not_ready";

  if (!measurementValid) {
    reason = "measurement_invalid";
  } else if (!userPresentOk) {
    reason = "user_not_present";
  } else if (!baselineReadyOk) {
    reason = "baseline_not_ready";
  } else if (!liveStableOk) {
    reason = "live_stable_not_ready";
  } else if (currentTopState == TopState::RUNNING &&
             startGate.runningMaintainsReadyWithoutLiveStable) {
    startReady = true;
    reason = "running_contract_hold";
  } else {
    startReady = true;
    reason = "idle_contract_ready";
  }

  stableContract.startReady = startReady;
  stableContract.startReadyWeightKg =
      startReady ? stableContract.baselineReadyWeightKg : 0.0f;
  stableContract.startReadyBridge = reason;
}

void LaserModule::syncStableContractBridge(
    uint32_t now,
    const RhythmStateUpdateResult& result) {
  syncStableLiveContract(now);
  if (!stableContract.baselineReadyLatched && result.evidence.baselineReady) {
    latchBaselineReadyFromStable(
        result.evidence.baselineCapturedAtMs ? result.evidence.baselineCapturedAtMs : now,
        result.evidence.baselineDistance,
        result.evidence.baselineWeightKg);
  } else if (stableContract.baselineReadyLatched &&
             result.evidence.baselineReady &&
             stableContract.baselineReadyWeightKg <= 0.0f) {
    stableContract.baselineReadyWeightKg = result.evidence.baselineWeightKg;
    stableContract.baselineReadyDistance = result.evidence.baselineDistance;
  }
  syncStartReadyContract(now, sm ? sm->state() : TopState::IDLE, result);
}

void LaserModule::clearStableContractBridge() {
  stableContract.stableCandidate = false;
  stableContract.stableReadyLive = false;
  stableContract.baselineReadyLatched = false;
  stableContract.startReady = false;
  stableContract.stableReadyWeightKg = 0.0f;
  stableContract.stableReadyDistance = 0.0f;
  stableContract.baselineReadyWeightKg = 0.0f;
  stableContract.baselineReadyDistance = 0.0f;
  stableContract.startReadyWeightKg = 0.0f;
  stableContract.stableReadyAtMs = 0;
  stableContract.baselineReadyAtMs = 0;
  stableContract.startReadyBridge = "not_ready";
}

void LaserModule::resetStableTracking(const char* reason, bool logIfActive) {
  const bool exitedStableStage = stableState != StableState::UNSTABLE;
  if (logIfActive && stableState != StableState::UNSTABLE) {
    Serial.printf("[STABLE] CLEAR reason=%s\n", reason ? reason : "unspecified");
  }

  stableState = StableState::UNSTABLE;
  stableBaselineDistance = 0.0f;
  stableBaselineDistanceMm = 0.0f;
  stableBaselineWeight = 0.0f;
  stableLatchedAtMs = 0;
  invalidStableSamples = 0;
  stableConfirmCount = 0;
  stableCandidateStartedAtMs = 0;
  stableEarlyCheckpointLogged = false;
  stableExitConfirmCount = 0;
  stableExitPendingReason = nullptr;
  bufHead = 0;
  bufCount = 0;
  syncStableLiveContract(millis());

  if (exitedStableStage) {
    logLatestMeasurementPlaneSummary("stable_exit");
  }
}

void LaserModule::beginStableCandidate(float distance, float weight) {
  if (stableState != StableState::STABLE_CANDIDATE) {
    // 本轮约 3 秒体感优化优先落在 stable build，而不是 baseline_ready/start_ready 后半段。
    // 这里记录 build 入口，便于现场直接看出候选开始到 latch 的真实耗时。
    Serial.printf(
        "[STABLE] CANDIDATE state_eval_interval_ms=%lu early_samples=%u legacy_window=%d\n",
        static_cast<unsigned long>(phase2Thresholds.stable.evalIntervalStableBuildMs),
        static_cast<unsigned int>(phase2Thresholds.stable.earlyAcceptSamples),
        phase2Thresholds.stable.enterWindowSamples);
    bufHead = 0;
    bufCount = 0;
    stableConfirmCount = 0;
    stableCandidateStartedAtMs = millis();
    stableEarlyCheckpointLogged = false;
    logLatestMeasurementPlaneSummary("stable_enter");
  }

  stableState = StableState::STABLE_CANDIDATE;
  invalidStableSamples = 0;
  pushStableSample(distance, weight);
  syncStableLiveContract(millis());
}

bool LaserModule::shouldClearLatchedStable(float distance, float weight, const char*& reason) const {
  if (weight < phase2Thresholds.stable.exitLeaveThresholdKg) {
    reason = "leave_threshold";
    return true;
  }

  if (fabsf(weight - stableBaselineWeight) >= phase2Thresholds.stable.exitWeightDeltaKg) {
    reason = "weight_delta";
    return true;
  }

  if (fabsf(distance - stableBaselineDistance) >= phase2Thresholds.stable.exitDistanceDelta) {
    reason = "distance_delta";
    return true;
  }

  reason = nullptr;
  return false;
}

bool LaserModule::shouldUseFastStableBuildReadInterval() const {
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  const bool baselineReady = stableContract.baselineReadyLatched;
  return currentTopState != TopState::RUNNING &&
      (stableState != StableState::STABLE_LATCHED || !baselineReady);
}

void LaserModule::latchStable(uint32_t now, const char* mode, float stddev) {
  const int sampleCount = min<int>(bufCount, phase2Thresholds.stable.enterWindowSamples);
  const int trimCount = min<int>(phase2Thresholds.stable.trimmedMeanDropSamples, sampleCount / 2);
  float finalWeight = computeRingTrimmedMean(
      weightBuffer,
      bufHead,
      bufCount,
      WINDOW_N,
      sampleCount,
      trimCount);
  float finalDistance = computeRingTrimmedMean(
      distanceBuffer,
      bufHead,
      bufCount,
      WINDOW_N,
      sampleCount,
      trimCount);
  if (!isfinite(finalWeight)) {
    finalWeight = getMean(weightBuffer);
  }
  if (!isfinite(finalDistance)) {
    finalDistance = getMean(distanceBuffer);
  }
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
  stableConfirmCount = 0;
  stableEarlyCheckpointLogged = false;
  stableExitConfirmCount = 0;
  stableExitPendingReason = nullptr;
  syncStableLiveContract(now);

  Serial.printf(
      "[STABLE] LATCH mode=%s samples=%d std=%.3f weight=%.2f dist=%.2f build_ms=%lu\n",
      mode ? mode : "unknown",
      bufCount,
      stddev,
      finalWeight,
      finalDistance,
      static_cast<unsigned long>(buildLatencyMs));
  logLatestMeasurementPlaneSummary("stable_latched");
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  const bool shouldCapturePrimaryBaseline =
      currentTopState != TopState::RUNNING &&
      !stableContract.baselineReadyLatched;

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
  latchBaselineReadyFromStable(now, finalDistance, finalWeight);
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
  e.startReady = stableContract.startReady;
  e.baselineReady = result.evidence.baselineReady;
  e.stableWeightKg = result.evidence.baselineWeightKg;
  e.mainMa12WeightKg = result.evidence.ma12WeightKg;
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
  runSummary.ma12WeightKgRange = RangeTracker{};
  runSummary.ma12DistanceRange = RangeTracker{};
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
  if (computeRunAverage(12, avgWeight, avgDistance)) {
    includeRange(runSummary.ma12WeightKgRange, avgWeight);
    includeRange(runSummary.ma12DistanceRange, avgDistance);
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
  char ma12WeightRange[32];
  char ma12DistanceRange[32];
  formatRange(runSummary.weightKgRange, weightRange, sizeof(weightRange));
  formatRange(runSummary.distanceRange, distanceRange, sizeof(distanceRange));
  formatRange(runSummary.ma3WeightKgRange, ma3WeightRange, sizeof(ma3WeightRange));
  formatRange(runSummary.ma3DistanceRange, ma3DistanceRange, sizeof(ma3DistanceRange));
  formatRange(runSummary.ma5WeightKgRange, ma5WeightRange, sizeof(ma5WeightRange));
  formatRange(runSummary.ma5DistanceRange, ma5DistanceRange, sizeof(ma5DistanceRange));
  formatRange(runSummary.ma12WeightKgRange, ma12WeightRange, sizeof(ma12WeightRange));
  formatRange(runSummary.ma12DistanceRange, ma12DistanceRange, sizeof(ma12DistanceRange));

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
      "ma12_weight_range_kg=%s ma12_distance_range=%s "
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
      ma12WeightRange,
      ma12DistanceRange,
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
  runSummary.ma12WeightKgRange = RangeTracker{};
  runSummary.ma12DistanceRange = RangeTracker{};
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
  resetStableSignalFilter();
  noteInvalidPresenceSample(millis(), reason);
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;

  if (currentTopState != TopState::RUNNING) {
    stableContract.startReady = false;
    stableContract.startReadyWeightKg = 0.0f;
    stableContract.startReadyBridge = "measurement_invalid";
    if (sm) sm->setStartReadiness(false, 0.0f);
  }

  if (stableState == StableState::STABLE_CANDIDATE) {
    resetStableTracking(reason, true);
  } else if (stableState == StableState::STABLE_LATCHED) {
    if (invalidStableSamples < 0xFF) invalidStableSamples++;
    if (invalidStableSamples >= phase2Thresholds.stable.invalidGraceSamples) {
      resetStableTracking(reason, true);
    }
  }

  if (!stableContract.userPresent && currentTopState != TopState::RUNNING) {
    if (!stableContract.baselineReadyLatched) {
      releaseOccupiedCycle(reason, millis());
    }
    if (sm) {
      sm->setRuntimeReady(false);
      sm->setStartReadiness(false, 0.0f);
    }
  }
}

void LaserModule::updateStableState(float distance, float weight, uint32_t now) {
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  invalidStableSamples = 0;

  if (currentTopState == TopState::RUNNING && stableState == StableState::STABLE_CANDIDATE) {
    resetStableTracking("running_started", true);
    return;
  }

  if (stableState == StableState::STABLE_LATCHED) {
    const char* clearReason = nullptr;
    if (shouldClearLatchedStable(distance, weight, clearReason)) {
      if (stableExitPendingReason == clearReason) {
        if (stableExitConfirmCount < 0xFF) {
          stableExitConfirmCount++;
        }
      } else {
        stableExitPendingReason = clearReason;
        stableExitConfirmCount = 1;
      }

      if (stableExitConfirmCount >= phase2Thresholds.stable.exitConfirmSamples) {
        resetStableTracking(clearReason, true);
      }
      if (currentTopState != TopState::RUNNING &&
          stableContract.userPresent &&
          weight > phase2Thresholds.presence.enterThresholdKg) {
        beginStableCandidate(distance, weight);
      }
    } else {
      stableExitConfirmCount = 0;
      stableExitPendingReason = nullptr;
    }
    return;
  }

  if (currentTopState == TopState::RUNNING || !stableContract.userPresent) {
    if (stableState == StableState::STABLE_CANDIDATE) {
      resetStableTracking(
          currentTopState == TopState::RUNNING ? "running_started" : "user_not_present",
          true);
    }
    return;
  }

  if (weight <= phase2Thresholds.presence.enterThresholdKg) {
    if (stableState == StableState::STABLE_CANDIDATE) {
      resetStableTracking("below_entry_threshold", true);
    }
    return;
  }

  beginStableCandidate(distance, weight);
  const StableWindowMetrics metrics = computeStableWindowMetrics(
      weightBuffer,
      bufHead,
      bufCount,
      WINDOW_N,
      phase2Thresholds.stable.enterWindowSamples);
  if (!metrics.valid) {
    return;
  }

  const bool stableEligible =
      metrics.stddev < phase2Thresholds.stable.enterStdDevKg &&
      metrics.range < phase2Thresholds.stable.enterRangeKg &&
      metrics.drift < phase2Thresholds.stable.enterDriftKg;
  if (!stableEligible) {
    stableConfirmCount = 0;
    if (!stableEarlyCheckpointLogged) {
      stableEarlyCheckpointLogged = true;
      Serial.printf(
          "[STABLE] HOLD mode=combined_window samples=%d std=%.3f range=%.3f drift=%.3f std_ok=%d range_ok=%d drift_ok=%d\n",
          bufCount,
          metrics.stddev,
          metrics.range,
          metrics.drift,
          metrics.stddev < phase2Thresholds.stable.enterStdDevKg ? 1 : 0,
          metrics.range < phase2Thresholds.stable.enterRangeKg ? 1 : 0,
          metrics.drift < phase2Thresholds.stable.enterDriftKg ? 1 : 0);
    }
    return;
  }

  stableEarlyCheckpointLogged = false;
  if (stableConfirmCount < 0xFF) {
    stableConfirmCount++;
  }
  if (stableConfirmCount < phase2Thresholds.stable.enterConfirmWindows) {
    return;
  }

  latchStable(now, "combined_window", metrics.stddev);
}

void LaserModule::logRhythmStateUpdate(const RhythmStateUpdateResult& result) const {
  if (result.formalEventCandidate) {
    Serial.printf(
        "%s [EVENT_AUX] formal_event_candidate=1 action_owner=baseline_main stable_weight_kg=%.2f ma12_weight_kg=%.2f deviation_kg=%.2f ratio=%.4f\n",
        LogMarker::kResearch,
        result.evidence.baselineWeightKg,
        result.evidence.ma12WeightKg,
        result.evidence.deviationKg,
        result.evidence.ratio);
  }

  if (result.advisory.shouldLog) {
    Serial.printf(
        "%s [RISK_ADVISORY] advisory_state=%s advisory_type=%s advisory_level=%s advisory_reason=%s "
        "stable_weight_kg=%.2f ma12_weight_kg=%.2f deviation_kg=%.2f ratio=%.4f peak_ratio=%.4f "
        "abnormal_duration_ms=%lu danger_duration_ms=%lu event_aux_seen=%d final_stop=0 action=advisory_only\n",
        LogMarker::kSafety,
        result.advisory.active ? "ACTIVE" : "NONE",
        riskAdvisoryTypeName(result.advisory.type),
        riskAdvisoryLevelName(result.advisory.level),
        result.advisory.reason ? result.advisory.reason : "none",
        result.evidence.baselineWeightKg,
        result.evidence.ma12WeightKg,
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
      "%s [BASELINE_MAIN_STATE] prev=%s next=%s cause=%s reason=%s stable_weight_kg=%.2f ma12_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu user_present=%d\n",
      LogMarker::kResearch,
      previousStatusName,
      nextStatusName,
      result.logCause ? result.logCause : "transition",
      result.reason ? result.reason : "n/a",
      result.evidence.baselineWeightKg,
      result.evidence.ma12WeightKg,
      result.evidence.deviationKg,
      result.evidence.ratio,
      static_cast<unsigned long>(result.evidence.abnormalDurationMs),
      static_cast<unsigned long>(result.evidence.dangerDurationMs),
      stableContract.userPresent ? 1 : 0);

  if (!result.shouldStopByDanger) {
    return;
  }

  const char* stopSource =
      result.evidence.directDangerBandTriggered
          ? "ratio_above_danger_band"
          : "abnormal_exceeded_recovery_and_danger_hold";
  Serial.printf(
      "%s [AUTO_STOP_BY_DANGER] stop_reason=%s stop_source=%s stop_detail=%s stable_weight_kg=%.2f ma12_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu fall_stop_enabled=%d\n",
      LogMarker::kSafety,
      result.stopReason ? result.stopReason : "danger_stop",
      verificationStopSourceName(VerificationStopSource::BASELINE_MAIN_LOGIC),
      stopSource,
      result.evidence.baselineWeightKg,
      result.evidence.ma12WeightKg,
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
      "%s [AUTO_STOP_SUPPRESSED] stop_reason=%s stop_source=%s stop_detail=%s stable_weight_kg=%.2f ma12_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu "
      "fall_stop_enabled=%d should_execute_stop=%d stop_suppressed_by_switch=%d detail=%s\n",
      LogMarker::kSafety,
      rhythmResult.stopReason ? rhythmResult.stopReason : "danger_stop",
      verificationStopSourceName(VerificationStopSource::BASELINE_MAIN_LOGIC),
      stopSource,
      rhythmResult.evidence.baselineWeightKg,
      rhythmResult.evidence.ma12WeightKg,
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
  uint32_t lastStateEval = 0;
  uint32_t nextReadEligibleAtMs = 0;
#if DIAG_DISABLE_LASER_SAFETY
  bool diagBannerPrinted = false;
#endif

  while (true) {
    const bool noLaserConfigured = !deviceConfig.laserInstalled;
    const TopState loopTopState = sm ? sm->state() : TopState::IDLE;
    const bool unavailableIdleBackoffActive =
        deviceConfig.laserInstalled &&
        nextReadEligibleAtMs != 0 &&
        loopTopState != TopState::RUNNING;
    const uint32_t loopDelayMs =
        noLaserConfigured
            ? kLaserLoopIntervalNoLaserMs
            : (unavailableIdleBackoffActive ? kLaserLoopIntervalUnavailableIdleMs : 20UL);
    vTaskDelay(pdMS_TO_TICKS(loopDelayMs));

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
    const uint32_t readIntervalMs = LASER_MEASUREMENT_READ_INTERVAL_MS;
    if (now - lastRead < readIntervalMs) continue;
    if (nextReadEligibleAtMs != 0 && now < nextReadEligibleAtMs) continue;
    lastRead = now;

    if (!deviceConfig.laserInstalled) {
      clearModbusReadFailureBurst(now, true);
      nextReadEligibleAtMs = 0;
      lastMeasurementValid = false;
      lastInvalidReason = "LASER_NOT_INSTALLED";
      resetMeasurementPlane("no_laser_config", false);
      if (!hasLoggedMeasurementBypassState || !lastLoggedMeasurementBypassState) {
        logConfigTruth("measurement_bypass_changed", "no_laser_config");
      }
      continue;
    }

    if (!hasLoggedMeasurementBypassState || lastLoggedMeasurementBypassState) {
      logConfigTruth("measurement_bypass_changed");
    }

    const TopState readAttemptTopState = sm ? sm->state() : TopState::IDLE;
    uint8_t result = node.readInputRegisters(REG_DISTANCE, 1);
    if (result != node.ku8MBSuccess) {
      noteModbusReadFailure(result, now);
      noteDistanceValidity(false, 0, 0, NAN, false, "READ_FAIL", now);
      if (sm) sm->setSensorHealthy(false);
      nextReadEligibleAtMs =
          readAttemptTopState == TopState::RUNNING ? 0 : (now + kLaserUnavailableIdleReadBackoffMs);
      resetMeasurementPlane("modbus_read_fail", false);
      handleInvalidMeasurement("modbus_read_fail");
      publishMeasurementSample(now, false, 0.0f, 0.0f, "READ_FAIL");
      continue;
    }

    clearModbusReadFailureBurst(now, true);
    nextReadEligibleAtMs = 0;
    if (sm) sm->setSensorHealthy(true);

    const uint16_t rawRegister = node.getResponseBuffer(0);
    const int16_t signedDistanceRaw = static_cast<int16_t>(rawRegister);
    const float scaledDistance = signedDistanceRaw * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;

    const char* validityReason = nullptr;
    if (isDistanceSentinelRaw(rawRegister, signedDistanceRaw, validityReason)) {
      noteDistanceValidity(false, rawRegister, signedDistanceRaw, scaledDistance, true, validityReason, now);
      if (sm) sm->setSensorHealthy(false);
      resetMeasurementPlane("distance_sentinel", false);
      handleInvalidMeasurement("distance_sentinel");
      publishMeasurementSample(now, false, 0.0f, 0.0f, validityReason);
      continue;
    }

    if (!isDistanceValidRaw(signedDistanceRaw, validityReason)) {
      noteDistanceValidity(false, rawRegister, signedDistanceRaw, scaledDistance, false, validityReason, now);
      if (sm) sm->setSensorHealthy(false);
      resetMeasurementPlane("distance_out_of_range", false);
      handleInvalidMeasurement("distance_out_of_range");
      publishMeasurementSample(now, false, 0.0f, 0.0f, validityReason);
      continue;
    }

    noteDistanceValidity(true, rawRegister, signedDistanceRaw, scaledDistance, false, nullptr, now);

    float dist = scaledDistance;
    if (!isfinite(dist)) {
      noteDistanceValidity(false, rawRegister, signedDistanceRaw, dist, false, "DISTANCE_NONFINITE", now);
      if (sm) sm->setSensorHealthy(false);
      resetMeasurementPlane("distance_invalid", false);
      handleInvalidMeasurement("distance_invalid");
      publishMeasurementSample(now, false, 0.0f, 0.0f, "DISTANCE_NONFINITE");
      continue;
    }

    if (needZero) {
      zeroDistance = dist;
      preferences.putFloat("zero", zeroDistance);
      calibrationModel.referenceDistance = zeroDistance;
      dualZero.calibrationZeroDistance = zeroDistance;
      saveCalibrationModel();
      releaseOccupiedCycle("zero", now);
      resetRuntimeZero("zero");
      resetStableTracking("zero", true);
      rhythmStateJudge.reset("zero_reset");
      clearStableContractBridge();
      resetMeasurementPlane("zero_reset", true);
      if (sm) sm->setStartReadiness(false, 0.0f);
      needZero = false;
      Serial.printf("🔘 ZERO done: %.2f\n", zeroDistance);
      lastLogDist = -999.0f;
      needSendParams = true;
    }

    float weight = evaluateCalibrationWeight(dist);
    if (!isfinite(weight)) {
      if (sm) sm->setSensorHealthy(false);
      resetMeasurementPlane("weight_invalid", false);
      handleInvalidMeasurement("weight_invalid");
      publishMeasurementSample(now, false, dist, 0.0f, "WEIGHT_INVALID");
      continue;
    }
    latestWeightKg = weight;
    invalidPresenceSamples = 0;
    pushMeasurementWeightSample(weight);
    publishMeasurementSample(now, true, dist, weight, nullptr);

    // ===== Safety supervisor: user on/off =====
    // WP2 promotes presence into the formal occupied-cycle owner.
    const bool presenceChanged = updatePresenceState(weight);
    if (presenceChanged && stableContract.userPresent) {
      lockEffectiveZeroForOccupiedCycle(now, "presence_enter");
    }
    if (presenceChanged && !stableContract.userPresent) {
#if !DIAG_DISABLE_LASER_SAFETY
      if (sm) sm->onUserOff();
#endif
    }
    if (sm) sm->setRuntimeReady(stableContract.userPresent);
    observeRuntimeZero(dist, weight, now);

    // ===== Fault clear feed (Gemini rule) =====
    if (sm) sm->onWeightSample(weight);

    const uint32_t stateEvalIntervalMs =
        shouldUseFastStableBuildReadInterval()
            ? phase2Thresholds.stable.evalIntervalStableBuildMs
            : phase2Thresholds.stable.evalIntervalDefaultMs;
    if (now - lastStateEval < stateEvalIntervalMs) {
      continue;
    }
    lastStateEval = now;

    float stableEvalDistance = dist;
    float stableEvalWeight = weight;
    if (!updateStableSignalFilter(dist, stableEvalDistance, stableEvalWeight)) {
      handleInvalidMeasurement("stable_filter_invalid");
      continue;
    }

    updateStableState(stableEvalDistance, stableEvalWeight, now);

    // LaserModule remains the measurement owner. The dedicated rhythm-state
    // judge consumes normalized context only and never owns final actions.
    TopState currentTopState = sm ? sm->state() : TopState::IDLE;

    RhythmStateJudgeInput rhythmInput{};
    rhythmInput.nowMs = now;
    rhythmInput.distance = dist;
    rhythmInput.weightKg = weight;
    rhythmInput.sampleValid = true;
    rhythmInput.userPresent = stableContract.userPresent;
    rhythmInput.topState = currentTopState;
    const RhythmStateUpdateResult& rhythmResult = rhythmStateJudge.update(rhythmInput);
    syncStableContractBridge(now, rhythmResult);
    if (sm) {
      // baseline_ready 才是正式 start/leave gate；
      // runtime_ready 仍仅表示人在平台上，不再单独承担“可开始”语义。
      sm->setStartReadiness(
          stableContract.startReady,
          stableContract.startReadyWeightKg);
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

    if (!stableContract.userPresent &&
        currentTopState != TopState::RUNNING &&
        stableContract.baselineReadyLatched) {
      // stable_weight：稳定体重。
      // 只有确认离台后，才清空本轮主判断基线。
      // 这里同步清掉正式 start readiness，避免只靠首秒屏蔽或阈值调高来掩盖问题。
      releaseOccupiedCycle("user_left_platform_confirmed", now);
      rhythmStateJudge.reset("user_left_platform_confirmed");
      clearStableContractBridge();
      if (sm) sm->setStartReadiness(false, 0.0f);
    } else if (!stableContract.userPresent &&
               currentTopState != TopState::RUNNING &&
               dualZero.occupiedCycleActive &&
               !stableContract.baselineReadyLatched) {
      releaseOccupiedCycle("occupied_cycle_clear_without_baseline", now);
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
