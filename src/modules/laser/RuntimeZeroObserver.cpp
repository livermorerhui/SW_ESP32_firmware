#include "modules/laser/RuntimeZeroObserver.h"

#include <math.h>

namespace {
RuntimeZeroDecision makeDecision(
    RuntimeZeroDecisionReason reason,
    uint8_t requiredSamples,
    bool eligible,
    bool shouldResetWindow = false) {
  RuntimeZeroDecision decision{};
  decision.reason = reason;
  decision.requiredSamples = requiredSamples;
  decision.eligible = eligible;
  decision.shouldResetWindow = shouldResetWindow;
  return decision;
}

RuntimeZeroDecisionStats computeWindowStats(
    const float* values,
    uint8_t head,
    uint8_t count,
    uint8_t capacity,
    uint8_t sampleCount,
    float calibrationZero) {
  RuntimeZeroDecisionStats stats{};
  if (!values || capacity == 0 || count == 0 || sampleCount == 0 || count < sampleCount) {
    return stats;
  }

  const uint8_t startOffset = count - sampleCount;
  const uint8_t oldestIndex = (count == capacity) ? head : 0;
  float sum = 0.0f;
  float minValue = INFINITY;
  float maxValue = -INFINITY;

  for (uint8_t i = 0; i < sampleCount; ++i) {
    const uint8_t index = static_cast<uint8_t>((oldestIndex + startOffset + i) % capacity);
    const float value = values[index];
    if (!isfinite(value)) {
      return stats;
    }
    sum += value;
    if (value < minValue) minValue = value;
    if (value > maxValue) maxValue = value;
  }

  stats.mean = sum / static_cast<float>(sampleCount);
  float sumSqDiff = 0.0f;
  for (uint8_t i = 0; i < sampleCount; ++i) {
    const uint8_t index = static_cast<uint8_t>((oldestIndex + startOffset + i) % capacity);
    const float diff = values[index] - stats.mean;
    sumSqDiff += diff * diff;
  }

  stats.stddev = sqrtf(sumSqDiff / static_cast<float>(sampleCount));
  stats.range = maxValue - minValue;
  stats.driftFromCalibration = fabsf(stats.mean - calibrationZero);
  return stats;
}
}

RuntimeZeroDecision RuntimeZeroObserver::evaluateEligibility(
    const LaserRuntimeZeroThresholdConfig& config,
    const RuntimeZeroEligibilityInput& input) const {
  const uint8_t requiredSamples =
      config.refreshSamples > WINDOW_N ? WINDOW_N : config.refreshSamples;

  if (!config.refreshEnabled || requiredSamples == 0) {
    return makeDecision(RuntimeZeroDecisionReason::REFRESH_DISABLED, requiredSamples, false);
  }
  if (input.topState == TopState::RUNNING) {
    return makeDecision(RuntimeZeroDecisionReason::FROZEN_RUNNING, requiredSamples, false);
  }
  if (input.userPresent) {
    return makeDecision(RuntimeZeroDecisionReason::FROZEN_USER_PRESENT, requiredSamples, false);
  }
  if (input.stableCandidate) {
    return makeDecision(RuntimeZeroDecisionReason::FROZEN_STABLE_CANDIDATE, requiredSamples, false);
  }
  if (input.occupiedCycleActive || (input.baselineReadyLatched && input.occupiedCycleActive)) {
    return makeDecision(RuntimeZeroDecisionReason::FROZEN_OCCUPIED_CYCLE, requiredSamples, false);
  }
  if (input.effectiveZeroLocked) {
    return makeDecision(RuntimeZeroDecisionReason::FROZEN_EFFECTIVE_ZERO_LOCKED, requiredSamples, false);
  }

  const bool emptyWindowEligible =
      isfinite(input.distance) &&
      isfinite(input.weight) &&
      input.topState != TopState::RUNNING &&
      input.weight <= config.emptyWindowMaxWeightKg;
  if (!emptyWindowEligible) {
    return makeDecision(RuntimeZeroDecisionReason::NOT_EMPTY_WINDOW, requiredSamples, false, true);
  }

  return makeDecision(RuntimeZeroDecisionReason::WINDOW_NOT_READY, requiredSamples, true);
}

RuntimeZeroDecision RuntimeZeroObserver::evaluateWindow(
    const LaserRuntimeZeroThresholdConfig& config,
    const RuntimeZeroWindowInput& input) const {
  const uint8_t requiredSamples =
      config.refreshSamples > WINDOW_N ? WINDOW_N : config.refreshSamples;

  if (requiredSamples == 0) {
    return makeDecision(RuntimeZeroDecisionReason::REFRESH_DISABLED, requiredSamples, false);
  }

  if (input.count < requiredSamples) {
    return makeDecision(RuntimeZeroDecisionReason::WINDOW_NOT_READY, requiredSamples, true);
  }

  RuntimeZeroDecision decision =
      makeDecision(RuntimeZeroDecisionReason::WINDOW_UNSTABLE, requiredSamples, true);
  decision.stats = computeWindowStats(
      input.values,
      input.head,
      input.count,
      input.capacity,
      requiredSamples,
      input.calibrationZero);

  if (!isfinite(decision.stats.mean) ||
      !isfinite(decision.stats.stddev) ||
      !isfinite(decision.stats.range) ||
      decision.stats.stddev > config.refreshStdDevDistance ||
      decision.stats.range > config.refreshRangeDistance) {
    return decision;
  }

  if (decision.stats.driftFromCalibration > config.driftGuardDistance) {
    decision.reason = RuntimeZeroDecisionReason::DRIFT_GUARD;
    return decision;
  }

  decision.reason = RuntimeZeroDecisionReason::REFRESHED;
  decision.shouldRefresh = true;
  return decision;
}

void RuntimeZeroObserver::noteDecision(uint32_t now, const RuntimeZeroDecision& decision) {
  const uint8_t index = reasonIndex(decision.reason);
  if (reasonCounts[index] < 0xFFFF) {
    reasonCounts[index]++;
  }

  if (summaryWindowStartedAtMs == 0) {
    summaryWindowStartedAtMs = now;
    return;
  }

  if ((now - summaryWindowStartedAtMs) < kDiagnosticSummaryIntervalMs) {
    return;
  }

  logSummary(now);
}

const char* RuntimeZeroObserver::reasonName(RuntimeZeroDecisionReason reason) {
  switch (reason) {
    case RuntimeZeroDecisionReason::REFRESH_DISABLED:
      return "DISABLED";
    case RuntimeZeroDecisionReason::FROZEN_RUNNING:
      return "FROZEN_RUNNING";
    case RuntimeZeroDecisionReason::FROZEN_USER_PRESENT:
      return "FROZEN_USER_PRESENT";
    case RuntimeZeroDecisionReason::FROZEN_STABLE_CANDIDATE:
      return "FROZEN_STABLE_CANDIDATE";
    case RuntimeZeroDecisionReason::FROZEN_OCCUPIED_CYCLE:
      return "FROZEN_OCCUPIED_CYCLE";
    case RuntimeZeroDecisionReason::FROZEN_EFFECTIVE_ZERO_LOCKED:
      return "FROZEN_EFFECTIVE_ZERO_LOCKED";
    case RuntimeZeroDecisionReason::NOT_EMPTY_WINDOW:
      return "NOT_EMPTY_WINDOW";
    case RuntimeZeroDecisionReason::WINDOW_NOT_READY:
      return "WINDOW_NOT_READY";
    case RuntimeZeroDecisionReason::WINDOW_UNSTABLE:
      return "WINDOW_UNSTABLE";
    case RuntimeZeroDecisionReason::DRIFT_GUARD:
      return "DRIFT_GUARD";
    case RuntimeZeroDecisionReason::REFRESHED:
      return "REFRESHED";
    case RuntimeZeroDecisionReason::COUNT:
      break;
  }
  return "UNKNOWN";
}

uint8_t RuntimeZeroObserver::reasonIndex(RuntimeZeroDecisionReason reason) {
  const uint8_t index = static_cast<uint8_t>(reason);
  return index < kReasonCount ? index : 0;
}

void RuntimeZeroObserver::logSummary(uint32_t now) {
  const uint32_t windowMs = now - summaryWindowStartedAtMs;
  Serial.printf(
      "[ZERO_RUNTIME_DIAG] window_ms=%lu disabled=%u frozen_running=%u frozen_user_present=%u "
      "frozen_stable_candidate=%u frozen_occupied_cycle=%u frozen_effective_zero_locked=%u "
      "not_empty_window=%u window_not_ready=%u window_unstable=%u drift_guard=%u refreshed=%u\n",
      static_cast<unsigned long>(windowMs),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::REFRESH_DISABLED)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::FROZEN_RUNNING)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::FROZEN_USER_PRESENT)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::FROZEN_STABLE_CANDIDATE)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::FROZEN_OCCUPIED_CYCLE)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::FROZEN_EFFECTIVE_ZERO_LOCKED)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::NOT_EMPTY_WINDOW)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::WINDOW_NOT_READY)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::WINDOW_UNSTABLE)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::DRIFT_GUARD)]),
      static_cast<unsigned>(reasonCounts[reasonIndex(RuntimeZeroDecisionReason::REFRESHED)]));

  for (uint8_t i = 0; i < kReasonCount; ++i) {
    reasonCounts[i] = 0;
  }
  summaryWindowStartedAtMs = now;
}
