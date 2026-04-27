#include "LaserModule.h"
#include "core/LogMarkers.h"
#include <math.h>
#include <string.h>
#include "modules/wave/WaveModule.h"

namespace {
constexpr uint32_t kLaserLoopIntervalNoLaserMs = 200UL;
constexpr uint32_t kLaserLoopIntervalUnavailableIdleMs = 250UL;
constexpr uint32_t kLaserUnavailableIdleReadBackoffMs = 3000UL;

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
  const LegacyScaleParams legacyParams = calibrationModelStore.loadLegacyParams(preferences);
  zeroDistance = legacyParams.zeroDistance;
  scaleFactor  = legacyParams.scaleFactor;
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

  measurementReader.begin();

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

  if (!isLaserInstallAllowedForPlatformModel(nextPlatformModel, nextLaserInstalled)) {
    reason = "LASER_INSTALL_CONSTRAINT_VIOLATION";
    Serial.printf(
        "[DEVICE] CONFIG REJECT source=device_set_config platform_model=%s laser_installed=%d constraint=%s\n",
        platformModelName(nextPlatformModel),
        nextLaserInstalled ? 1 : 0,
        laserInstallConstraintName(platformModelLaserInstallConstraint(nextPlatformModel)));
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
  measurementPlane.reset(reason, logReset);
}

void LaserModule::publishMeasurementSample(
    uint32_t now,
    bool valid,
    float distance,
    float weight,
    const char* reason) {
  const MeasurementPlaneRecordResult sample =
      measurementPlane.record(now, valid, distance, weight, reason);
  if (!sample.shouldPublish) {
    return;
  }
  if (bus) {
    bus->publish(sample.event);
  }
  measurementPlane.notePublished(sample);
}

void LaserModule::logLatestMeasurementPlaneSummary(const char* trigger) {
  measurementPlane.logLatest(trigger);
}

void LaserModule::loadDeviceConfig() {
  const DeviceConfigLoadResult result = deviceConfigStore.load(preferences);
  deviceConfig = result.config;

  if (result.laserInstalledNormalized) {
    Serial.printf(
        "[DEVICE] CONFIG NORMALIZE source=boot platform_model=%s stored_laser_installed=%d normalized_laser_installed=%d constraint=%s\n",
        platformModelName(deviceConfig.platformModel),
        result.storedLaserInstalled ? 1 : 0,
        deviceConfig.laserInstalled ? 1 : 0,
        laserInstallConstraintName(platformModelLaserInstallConstraint(deviceConfig.platformModel)));
    saveDeviceConfig();
  }
}

void LaserModule::saveDeviceConfig() {
  deviceConfigStore.save(preferences, deviceConfig);
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
  CalibrationModel model = calibrationModelStore.loadModel(preferences, zeroDistance, scaleFactor);

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
  calibrationModelStore.saveModel(preferences, calibrationModel);
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
  if (!calibrationModelStore.isModelFinite(model)) {
    reason = "INVALID_MODEL";
    return false;
  }

  if (!calibrationModelStore.isModelMonotonic(model)) {
    reason = "NON_MONOTONIC";
    return false;
  }

  calibrationModel = model;
  syncLegacyParamsFromModel();
  resetRuntimeZero(source);

  if (persist) {
    calibrationModelStore.saveLegacyParams(preferences, zeroDistance, scaleFactor);
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
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  RuntimeZeroEligibilityInput eligibilityInput{};
  eligibilityInput.distance = distance;
  eligibilityInput.weight = weight;
  eligibilityInput.topState = currentTopState;
  eligibilityInput.userPresent = stableContract.userPresent;
  eligibilityInput.stableCandidate = stableContract.stableCandidate;
  eligibilityInput.occupiedCycleActive = dualZero.occupiedCycleActive;
  eligibilityInput.effectiveZeroLocked = dualZero.effectiveZeroLocked;
  eligibilityInput.baselineReadyLatched = stableContract.baselineReadyLatched;
  const RuntimeZeroDecision eligibility =
      runtimeZeroObserver.evaluateEligibility(runtimeZeroCfg, eligibilityInput);
  if (!eligibility.eligible) {
    if (eligibility.shouldResetWindow) {
      runtimeZeroHead = 0;
      runtimeZeroCount = 0;
      dualZero.runtimeZeroWindowStartedAtMs = 0;
    }
    runtimeZeroObserver.noteDecision(now, eligibility);
    return;
  }

  if (eligibility.shouldResetWindow) {
    runtimeZeroHead = 0;
    runtimeZeroCount = 0;
    dualZero.runtimeZeroWindowStartedAtMs = 0;
  }

  if (runtimeZeroCount == 0) {
    dualZero.runtimeZeroWindowStartedAtMs = now;
  }

  runtimeZeroBuffer[runtimeZeroHead] = distance;
  runtimeZeroHead = (runtimeZeroHead + 1) % WINDOW_N;
  if (runtimeZeroCount < WINDOW_N) {
    runtimeZeroCount++;
  }

  RuntimeZeroWindowInput windowInput{};
  windowInput.values = runtimeZeroBuffer;
  windowInput.head = runtimeZeroHead;
  windowInput.count = runtimeZeroCount;
  windowInput.capacity = WINDOW_N;
  windowInput.calibrationZero = dualZero.calibrationZeroDistance;
  const RuntimeZeroDecision decision =
      runtimeZeroObserver.evaluateWindow(runtimeZeroCfg, windowInput);
  if (!decision.shouldRefresh) {
    runtimeZeroObserver.noteDecision(now, decision);
    return;
  }

  dualZero.lastRuntimeZeroCandidateDistance = decision.stats.mean;
  const bool changed =
      !dualZero.runtimeZeroValid ||
      fabsf(dualZero.runtimeZeroDistance - decision.stats.mean) > 0.0001f;
  dualZero.runtimeZeroDistance = decision.stats.mean;
  dualZero.runtimeZeroValid = true;
  dualZero.runtimeZeroCapturedAtMs = now;
  dualZero.effectiveZeroUsesRuntime = runtimeZeroCfg.applyToWeightConversion;
  refreshEffectiveZero();
  runtimeZeroObserver.noteDecision(now, decision);

  if (changed) {
    Serial.printf(
        "[ZERO_RUNTIME] REFRESH runtime_zero=%.2f calibration_zero=%.2f effective_zero=%.2f "
        "samples=%u std=%.4f range=%.4f window_ms=%lu apply_to_weight=%d\n",
        dualZero.runtimeZeroDistance,
        dualZero.calibrationZeroDistance,
        dualZero.effectiveZeroDistance,
        static_cast<unsigned>(decision.requiredSamples),
        decision.stats.stddev,
        decision.stats.range,
        static_cast<unsigned long>(
            dualZero.runtimeZeroWindowStartedAtMs > 0 && now >= dualZero.runtimeZeroWindowStartedAtMs
                ? (now - dualZero.runtimeZeroWindowStartedAtMs)
                : 0),
        runtimeZeroCfg.applyToWeightConversion ? 1 : 0);
  }
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

void LaserModule::pushStableSample(float distance, float weight) {
  distanceBuffer[bufHead] = distance;
  weightBuffer[bufHead] = weight;
  bufHead = (bufHead + 1) % WINDOW_N;
  if (bufCount < WINDOW_N) bufCount++;
}

bool LaserModule::updatePresenceState(float weight) {
  const LaserPresenceThresholdConfig& presence = phase2Thresholds.presence;

  if (weight >= presence.enterThresholdKg) {
    if (presenceEnterConfirmCount < 0xFF) {
      presenceEnterConfirmCount++;
    }
    presenceExitConfirmCount = 0;
  } else if (weight <= presence.exitThresholdKg) {
    if (presenceExitConfirmCount < 0xFF) {
      presenceExitConfirmCount++;
    }
    presenceEnterConfirmCount = 0;
  } else {
    presenceEnterConfirmCount = 0;
    presenceExitConfirmCount = 0;
  }

  PresenceContractInput input{};
  input.weightKg = weight;
  input.currentUserPresent = stableContract.userPresent;
  input.enterConfirmCount = presenceEnterConfirmCount;
  input.exitConfirmCount = presenceExitConfirmCount;
  const PresenceContractResult result =
      PresenceContractEvaluator::evaluate(presence, input);

  stableContract.userPresent = result.nextUserPresent;
  if (result.changed && stableContract.userPresent) {
    invalidPresenceSamples = 0;
  }
  return result.changed;
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
  (void)result;

  // baselineReadyLatched is the durable owner-side evidence that a valid
  // pre-start baseline has been established for the current occupied cycle.
  // start_ready stays separate from baseline_ready because it still requires
  // user presence and measurement health, but it no longer collapses back to a
  // short idle live-stable window once that baseline has been accepted.
  StartGateContractInput input{};
  input.measurementValid = lastMeasurementValid;
  input.userPresent = stableContract.userPresent;
  input.baselineReadyLatched = stableContract.baselineReadyLatched;
  input.stableReadyLive = stableContract.stableReadyLive;
  input.baselineReadyWeightKg = stableContract.baselineReadyWeightKg;
  input.topState = currentTopState;
  const StartGateContractResult evaluation =
      StartGateContractEvaluator::evaluate(phase2Thresholds.startGate, input);

  stableContract.startReady = evaluation.startReady;
  stableContract.startReadyWeightKg = evaluation.startReadyWeightKg;
  stableContract.startReadyBridge = evaluation.reason;
  observeStartGateDiagnostics(now, input, evaluation);
}

void LaserModule::resetStartGateDiagnosticsWindow(uint32_t now) {
  startGateDiagWindowStartedAtMs = now;
  startGateDiagEvaluations = 0;
  startGateDiagReady = 0;
  startGateDiagMeasurementInvalid = 0;
  startGateDiagUserNotPresent = 0;
  startGateDiagBaselineNotReady = 0;
  startGateDiagLiveStableNotReady = 0;
  startGateDiagRunningHold = 0;
  startGateDiagIdleReady = 0;
}

void LaserModule::observeStartGateDiagnostics(
    uint32_t now,
    const StartGateContractInput& input,
    const StartGateContractResult& evaluation) {
  if (!START_GATE_DIAG_ENABLED) {
    return;
  }

  if (startGateDiagWindowStartedAtMs == 0) {
    resetStartGateDiagnosticsWindow(now);
  }

  ++startGateDiagEvaluations;
  if (evaluation.startReady) {
    ++startGateDiagReady;
  }

  const char* reason = evaluation.reason ? evaluation.reason : "unknown";
  if (strcmp(reason, "measurement_invalid") == 0) {
    ++startGateDiagMeasurementInvalid;
  } else if (strcmp(reason, "user_not_present") == 0) {
    ++startGateDiagUserNotPresent;
  } else if (strcmp(reason, "baseline_not_ready") == 0) {
    ++startGateDiagBaselineNotReady;
  } else if (strcmp(reason, "live_stable_not_ready") == 0) {
    ++startGateDiagLiveStableNotReady;
  } else if (strcmp(reason, "running_contract_hold") == 0) {
    ++startGateDiagRunningHold;
  } else if (strcmp(reason, "idle_contract_ready") == 0) {
    ++startGateDiagIdleReady;
  }

  if (startGateDiagLastLogMs == 0 ||
      now - startGateDiagLastLogMs >= START_GATE_DIAG_LOG_INTERVAL_MS) {
    logStartGateDiagnostics(now, "periodic", input, evaluation);
    startGateDiagLastLogMs = now;
    resetStartGateDiagnosticsWindow(now);
  }
}

void LaserModule::logStartGateDiagnostics(
    uint32_t now,
    const char* trigger,
    const StartGateContractInput& input,
    const StartGateContractResult& evaluation) {
  const uint32_t windowMs =
      startGateDiagWindowStartedAtMs == 0 ? 0 : (now - startGateDiagWindowStartedAtMs);
  Serial.printf(
      "[START_GATE_DIAG] trigger=%s window_ms=%lu evals=%lu ready=%lu "
      "reason=%s start_ready=%d start_weight=%.2f "
      "measurement_valid=%d user_present=%d baseline_latched=%d stable_live=%d "
      "baseline_weight=%.2f top_state=%s "
      "measurement_invalid=%lu user_not_present=%lu baseline_not_ready=%lu "
      "live_stable_not_ready=%lu running_hold=%lu idle_ready=%lu\n",
      trigger ? trigger : "unknown",
      static_cast<unsigned long>(windowMs),
      static_cast<unsigned long>(startGateDiagEvaluations),
      static_cast<unsigned long>(startGateDiagReady),
      evaluation.reason ? evaluation.reason : "unknown",
      evaluation.startReady ? 1 : 0,
      evaluation.startReadyWeightKg,
      input.measurementValid ? 1 : 0,
      input.userPresent ? 1 : 0,
      input.baselineReadyLatched ? 1 : 0,
      input.stableReadyLive ? 1 : 0,
      input.baselineReadyWeightKg,
      topStateName(input.topState),
      static_cast<unsigned long>(startGateDiagMeasurementInvalid),
      static_cast<unsigned long>(startGateDiagUserNotPresent),
      static_cast<unsigned long>(startGateDiagBaselineNotReady),
      static_cast<unsigned long>(startGateDiagLiveStableNotReady),
      static_cast<unsigned long>(startGateDiagRunningHold),
      static_cast<unsigned long>(startGateDiagIdleReady));
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
  RunSummaryStartSnapshot snapshot{};
  snapshot.now = now;
  snapshot.baselineReady = rhythmResult.evidence.baselineReady;
  snapshot.baselineWeightKg =
      rhythmResult.evidence.baselineReady ? rhythmResult.evidence.baselineWeightKg : stableBaselineWeight;
  snapshot.baselineDistance =
      rhythmResult.evidence.baselineReady ? rhythmResult.evidence.baselineDistance : stableBaselineDistance;
  snapshot.fallStopEnabled = sm ? sm->fallStopEnabled() : FALL_STOP_ENABLED_DEFAULT;
  if (wave) {
    wave->getSummaryParams(snapshot.freqHz, snapshot.intensity, snapshot.intensityNormalized);
  }
  runSummaryCollector.start(snapshot, rhythmResult);
}

void LaserModule::accumulateRunSummary(uint32_t now,
                                       float distance,
                                       float weight,
                                       const RhythmStateUpdateResult& rhythmResult) {
  runSummaryCollector.accumulate(now, distance, weight, rhythmResult);
}

void LaserModule::finishRunSummary(uint32_t now,
                                   FaultCode stopReason,
                                   const RhythmStateUpdateResult& rhythmResult) {
  const bool abnormalStop = (stopReason != FaultCode::NONE);
  RunSummaryStopSnapshot snapshot{};
  snapshot.now = now;
  snapshot.stopReason = stopReason;
  snapshot.stopReasonText = sm ? sm->lastStopReasonText() : (abnormalStop ? faultCodeName(stopReason) : "NONE");
  snapshot.stopSourceText = sm ? sm->lastStopSourceText() : "NONE";
  snapshot.fallStopEnabled = sm ? sm->fallStopEnabled() : FALL_STOP_ENABLED_DEFAULT;
  runSummaryCollector.finish(snapshot, rhythmResult);
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
    if (!runSummaryCollector.active()) {
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

  BaselineEvidenceInput baselineInput{};
  baselineInput.metrics.valid = metrics.valid;
  baselineInput.metrics.stddev = metrics.stddev;
  baselineInput.metrics.range = metrics.range;
  baselineInput.metrics.drift = metrics.drift;
  baselineInput.currentStableConfirmCount = stableConfirmCount;
  const BaselineEvidenceResult baselineResult =
      BaselineEvidenceEvaluator::evaluate(phase2Thresholds.stable, baselineInput);
  if (!baselineResult.stableEligible) {
    stableConfirmCount = baselineResult.nextStableConfirmCount;
    if (!stableEarlyCheckpointLogged) {
      stableEarlyCheckpointLogged = true;
      Serial.printf(
          "[STABLE] HOLD mode=combined_window samples=%d std=%.3f range=%.3f drift=%.3f std_ok=%d range_ok=%d drift_ok=%d\n",
          bufCount,
          metrics.stddev,
          metrics.range,
          metrics.drift,
          baselineResult.stddevOk ? 1 : 0,
          baselineResult.rangeOk ? 1 : 0,
          baselineResult.driftOk ? 1 : 0);
    }
    return;
  }

  stableEarlyCheckpointLogged = false;
  stableConfirmCount = baselineResult.nextStableConfirmCount;
  if (!baselineResult.baselineEligible) {
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
      measurementReader.clearFailureBurst(now, true);
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
    const uint32_t nextReadBackoffMs =
        nextReadEligibleAtMs != 0 && nextReadEligibleAtMs > now
            ? (nextReadEligibleAtMs - now)
            : 0;
    const MeasurementReadResult readResult =
        measurementReader.read(readAttemptTopState, deviceConfig.laserInstalled, nextReadBackoffMs);
    now = readResult.readCompletedAtMs;
    if (!readResult.transportOk) {
      noteDistanceValidity(false, 0, 0, NAN, false, "READ_FAIL", now);
      if (sm) sm->setSensorHealthy(false);
      nextReadEligibleAtMs =
          readAttemptTopState == TopState::RUNNING ? 0 : (now + kLaserUnavailableIdleReadBackoffMs);
      resetMeasurementPlane("modbus_read_fail", false);
      handleInvalidMeasurement("modbus_read_fail");
      publishMeasurementSample(now, false, 0.0f, 0.0f, "READ_FAIL");
      continue;
    }

    nextReadEligibleAtMs = 0;
    if (sm) sm->setSensorHealthy(true);

    const char* validityReason = readResult.invalidReason;
    if (!readResult.validDistance) {
      noteDistanceValidity(
          false,
          readResult.rawRegister,
          readResult.signedRaw,
          readResult.scaledDistance,
          readResult.sentinel,
          validityReason,
          now);
      if (sm) sm->setSensorHealthy(false);
      resetMeasurementPlane(readResult.sentinel ? "distance_sentinel" : "distance_out_of_range", false);
      handleInvalidMeasurement(readResult.sentinel ? "distance_sentinel" : "distance_out_of_range");
      publishMeasurementSample(now, false, 0.0f, 0.0f, validityReason);
      continue;
    }

    noteDistanceValidity(
        true,
        readResult.rawRegister,
        readResult.signedRaw,
        readResult.scaledDistance,
        false,
        nullptr,
        now);

    float dist = readResult.scaledDistance;
    if (!isfinite(dist)) {
      noteDistanceValidity(
          false,
          readResult.rawRegister,
          readResult.signedRaw,
          dist,
          false,
          "DISTANCE_NONFINITE",
          now);
      if (sm) sm->setSensorHealthy(false);
      resetMeasurementPlane("distance_invalid", false);
      handleInvalidMeasurement("distance_invalid");
      publishMeasurementSample(now, false, 0.0f, 0.0f, "DISTANCE_NONFINITE");
      continue;
    }

    if (needZero) {
      zeroDistance = dist;
      calibrationModel.referenceDistance = zeroDistance;
      dualZero.calibrationZeroDistance = zeroDistance;
      calibrationModelStore.saveLegacyParams(preferences, zeroDistance, scaleFactor);
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
    MotionSafetyShadowInput shadowInput{};
    shadowInput.nowMs = now;
    shadowInput.sampleValid = true;
    shadowInput.topState = currentTopState;
    shadowInput.userPresent = stableContract.userPresent;
    shadowInput.baselineReady = rhythmResult.evidence.baselineReady;
    shadowInput.distance = dist;
    shadowInput.weightKg = weight;
    shadowInput.baselineDistance = rhythmResult.evidence.baselineDistance;
    shadowInput.baselineWeightKg = rhythmResult.evidence.baselineWeightKg;
    motionSafetyShadow.update(shadowInput);
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
