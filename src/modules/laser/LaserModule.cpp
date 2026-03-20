#include "LaserModule.h"
#include <math.h>

const char* LaserModule::calibrationModelTypeName(CalibrationModelType type) {
  switch (type) {
    case CalibrationModelType::LINEAR:
      return "LINEAR";
    case CalibrationModelType::QUADRATIC:
      return "QUADRATIC";
  }
  return "UNKNOWN";
}

void LaserModule::begin(EventBus* eb, SystemStateMachine* fsm) {
  bus = eb;
  sm = fsm;

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

  lastMs = millis();
  needSendParams = true;
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
  bufHead = 0;
  bufCount = 0;
}

void LaserModule::beginStableCandidate(float distance, float weight) {
  if (stableState != StableState::STABLE_CANDIDATE) {
    Serial.println("[STABLE] CANDIDATE");
    bufHead = 0;
    bufCount = 0;
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

void LaserModule::latchStable(uint32_t now) {
  float finalWeight = getMean(weightBuffer);
  float finalDistance = getMean(distanceBuffer);

  stableState = StableState::STABLE_LATCHED;
  stableBaselineWeight = finalWeight;
  stableBaselineDistance = finalDistance;
  stableBaselineDistanceMm = finalDistance * LASER_DISTANCE_RUNTIME_DIVISOR;
  stableLatchedAtMs = now;
  invalidStableSamples = 0;

  Serial.printf("[STABLE] LATCH weight=%.2f dist=%.2f\n", finalWeight, finalDistance);
  Serial.printf("[STABLE] EMIT weight=%.2f\n", finalWeight);

  Event e{};
  e.type = EventType::STABLE_WEIGHT;
  e.v1 = finalWeight;
  e.ts_ms = now;
  if (bus) bus->publish(e);
}

void LaserModule::handleInvalidMeasurement(const char* reason) {
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

  if (bufCount == WINDOW_N && getStdDev(weightBuffer) < STD_TH) {
    latchStable(now);
  }
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
    if (now - lastRead < 200) continue;
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

    // ===== Fall suspected (rate) =====
    float dt = (now - lastMs) / 1000.0f;
    if (dt <= 0) dt = 0.001f;
    float rate = fabsf(weight - lastWeight) / dt;
    if (sm && sm->state() == TopState::RUNNING && rate > FALL_DW_DT_SUSPECT_TH) {
#if !DIAG_DISABLE_LASER_SAFETY
        sm->onFallSuspected();
#endif
    }
    lastWeight = weight;
    lastMs = now;

    // ===== Fault clear feed (Gemini rule) =====
    if (sm) sm->onWeightSample(weight);

    updateStableState(dist, weight, now);

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
