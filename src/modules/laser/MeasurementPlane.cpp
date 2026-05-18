#include "modules/laser/MeasurementPlane.h"

void MeasurementPlane::reset(const char* reason, bool logReset) {
  ma12Head = 0;
  ma12Count = 0;
  logStartedAtMs = 0;
  logSamples = 0;
  lastInvalidEventMs = 0;
  lastInvalidEventReason = nullptr;
  if (logReset) {
    Serial.printf(
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
  const bool shouldEmitInvalid =
      !valid &&
      (lastInvalidEventReason != reason ||
       (now - lastInvalidEventMs) >= MEASUREMENT_INVALID_KEEPALIVE_MS);
  if (!valid && !shouldEmitInvalid) {
    return {};
  }

  if (valid) {
    pushWeightSample(weight);
  }

  float ma12 = NAN;
  const bool ma12Ready = valid && currentMa12(ma12);
  latestSampleValid = valid;
  latestSampleDistance = distance;
  latestSampleWeight = weight;
  latestSampleMa12Ready = ma12Ready;
  latestSampleMa12 = ma12Ready ? ma12 : 0.0f;
  latestSampleReason = valid ? nullptr : reason;
  hasLatestSample = true;

  MeasurementPlaneRecordResult result{};
  result.shouldPublish = true;
  result.event.type = EventType::STREAM;
  result.event.ts_ms = now;
  result.event.sampleSeq = ++measurementSequence;
  result.event.measurementValid = valid;
  result.event.distance = distance;
  result.event.weightKg = weight;
  result.event.ma12Ready = ma12Ready;
  result.event.ma12WeightKg = ma12Ready ? ma12 : 0.0f;
  if (!valid) {
    strlcpy(
        result.event.measurementReason,
        reason ? reason : "INVALID",
        sizeof(result.event.measurementReason));
    lastInvalidEventMs = now;
    lastInvalidEventReason = reason;
  } else {
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
  hasLoggedSummary = true;
  lastLoggedSummaryValid = valid;
  lastLoggedSummaryReason = valid ? nullptr : reason;
  logStartedAtMs = now;
  logSamples = 0;
}
