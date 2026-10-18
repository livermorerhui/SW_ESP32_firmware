#include "modules/laser/MeasurementPlane.h"
#include "core/VerboseSerial.h"

void MeasurementPlane::reset(const char* reason, bool logReset) {
  ma12Head = 0;
  ma12Count = 0;
  logStartedAtMs = 0;
  logSamples = 0;
  lastInvalidEventMs = 0;
  lastInvalidEventReason = nullptr;
  if (logReset) {
    SW_VERBOSE_SERIAL_PRINTF(
        "[LAYER:MEASUREMENT_PLANE] action=reset reason=%s ma12_window=%u\n",
        reason ? reason : "unspecified",
        static_cast<unsigned>(MEASUREMENT_MA12_WINDOW));
  }
}

MeasurementPlaneRecordResult MeasurementPlane::record(
    uint32_t now,
    bool valid,
    float distance,
    float weight,
    const char* reason) {
  return record(now, valid, valid, distance, weight, reason, reason, nullptr);
}

MeasurementPlaneRecordResult MeasurementPlane::record(
    uint32_t now,
    bool distanceValid,
    bool weightValid,
    float distance,
    float weight,
    const char* distanceReason,
    const char* weightReason,
    const char* sensorBackend) {
  const bool shouldEmitInvalid =
      !distanceValid &&
      (lastInvalidEventReason != distanceReason ||
       (now - lastInvalidEventMs) >= MEASUREMENT_INVALID_KEEPALIVE_MS);
  if (!distanceValid && !shouldEmitInvalid) {
    return {};
  }

  if (weightValid) {
    pushWeightSample(weight);
  }

  float ma12 = NAN;
  const bool ma12Ready = weightValid && currentMa12(ma12);
  latestSampleValid = distanceValid;
  latestDistanceValid = distanceValid;
  latestWeightValid = weightValid;
  latestSampleDistance = distance;
  latestSampleWeight = weight;
  latestSampleMa12Ready = ma12Ready;
  latestSampleMa12 = ma12Ready ? ma12 : 0.0f;
  latestSampleReason = distanceValid ? weightReason : distanceReason;
  hasLatestSample = true;

  MeasurementPlaneRecordResult result{};
  result.shouldPublish = true;
  result.event.type = EventType::STREAM;
  result.event.ts_ms = now;
  result.event.sampleSeq = ++measurementSequence;
  result.event.measurementValid = distanceValid;
  result.event.distanceValid = distanceValid;
  result.event.weightValid = weightValid;
  result.event.distance = distance;
  result.event.weightKg = weight;
  result.event.ma12Ready = ma12Ready;
  result.event.ma12WeightKg = ma12Ready ? ma12 : 0.0f;
  if (sensorBackend) {
    strlcpy(result.event.sensorBackend, sensorBackend, sizeof(result.event.sensorBackend));
  }
  if (!distanceValid) {
    strlcpy(
        result.event.measurementReason,
        distanceReason ? distanceReason : "INVALID",
        sizeof(result.event.measurementReason));
    strlcpy(
        result.event.distanceReason,
        distanceReason ? distanceReason : "INVALID",
        sizeof(result.event.distanceReason));
    lastInvalidEventMs = now;
    lastInvalidEventReason = distanceReason;
  } else {
    if (!weightValid) {
      strlcpy(
          result.event.weightReason,
          weightReason ? weightReason : "WEIGHT_INVALID",
          sizeof(result.event.weightReason));
      strlcpy(
          result.event.measurementReason,
          weightReason ? weightReason : "WEIGHT_INVALID",
          sizeof(result.event.measurementReason));
    }
    lastInvalidEventReason = nullptr;
  }

  return result;
}

void MeasurementPlane::notePublished(const MeasurementPlaneRecordResult& result) {
  if (!result.shouldPublish) {
    return;
  }

  const Event& e = result.event;
  logSamples++;
  if (logStartedAtMs == 0) {
    logStartedAtMs = e.ts_ms;
  }

  const bool shouldLogSummary =
      !hasLoggedSummary ||
      lastLoggedSummaryValid != e.measurementValid ||
      lastLoggedSummaryReason != latestSampleReason;
  if (shouldLogSummary) {
    logSummary(
        e.ts_ms,
        e.measurementValid,
        e.distance,
        e.weightKg,
        e.ma12Ready,
        e.ma12Ready ? e.ma12WeightKg : 0.0f,
        latestSampleReason,
        "measurement_edge");
  } else if (DEBUG_MEASUREMENT_PLANE_VERBOSE) {
    const uint32_t elapsedMs = e.ts_ms - logStartedAtMs;
    if (elapsedMs >= MEASUREMENT_PLANE_LOG_INTERVAL_MS) {
      logSummary(
          e.ts_ms,
          e.measurementValid,
          e.distance,
          e.weightKg,
          e.ma12Ready,
          e.ma12Ready ? e.ma12WeightKg : 0.0f,
          latestSampleReason,
          "verbose_periodic");
    }
  }
}

void MeasurementPlane::logLatest(const char* trigger) {
  if (!hasLatestSample) {
    return;
  }
  logSummary(
      millis(),
      latestSampleValid,
      latestSampleDistance,
      latestSampleWeight,
      latestSampleMa12Ready,
      latestSampleMa12,
      latestSampleReason,
      trigger);
}

void MeasurementPlane::pushWeightSample(float weight) {
  ma12WeightBuffer[ma12Head] = weight;
  ma12Head = (ma12Head + 1) % MEASUREMENT_MA12_WINDOW;
  if (ma12Count < MEASUREMENT_MA12_WINDOW) {
    ma12Count++;
  }
}

bool MeasurementPlane::currentMa12(float& out) const {
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

void MeasurementPlane::logSummary(
    uint32_t now,
    bool valid,
    float distance,
    float weight,
    bool ma12Ready,
    float ma12,
    const char* reason,
    const char* trigger) {
  const uint32_t elapsedMs = now - logStartedAtMs;
  const float approxRateHz = elapsedMs > 0
      ? (static_cast<float>(logSamples) * 1000.0f / static_cast<float>(elapsedMs))
      : 0.0f;
  SW_VERBOSE_SERIAL_PRINTF(
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
    SW_VERBOSE_SERIAL_PRINTF(
        "[LAYER:MEASUREMENT_CARRIER] carrier=EVT:STREAM queue_policy=ordered_no_overwrite seq=%lu valid=%d "
        "reason=%s\n",
        static_cast<unsigned long>(measurementSequence),
        valid ? 1 : 0,
        valid ? "NONE" : (reason ? reason : "INVALID"));
  }
  if (DEBUG_MEASUREMENT_PLANE_VERBOSE && ma12Ready) {
    SW_VERBOSE_SERIAL_PRINTF(
        "[LAYER:MA12] seq=%lu window=%u value=%.2f input=weightKg\n",
        static_cast<unsigned long>(measurementSequence),
        static_cast<unsigned>(MEASUREMENT_MA12_WINDOW),
        ma12);
  }
  hasLoggedSummary = true;
  lastLoggedSummaryValid = valid;
  lastLoggedSummaryReason = valid ? nullptr : reason;
  logStartedAtMs = now;
  logSamples = 0;
}
