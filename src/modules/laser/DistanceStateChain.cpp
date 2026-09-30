#include "modules/laser/DistanceStateChain.h"

#include <math.h>

void DistanceStateChain::configure(const DistanceStateChainConfig& config) {
  config_ = config;
  if (config_.confirmSamples == 0) config_.confirmSamples = 1;
  if (config_.stableSamples == 0) config_.stableSamples = 1;
  if (config_.stableSamples > 16) config_.stableSamples = 16;
}

void DistanceStateChain::reset(const char* reason) {
  (void)reason;
  head_ = 0;
  count_ = 0;
  enterCount_ = 0;
  exitCount_ = 0;
  userPresent_ = false;
  baselineReady_ = false;
  previousDistance_ = NAN;
  previousAtMs_ = 0;
  baselineDistance_ = NAN;
  last_ = DistanceStateChainResult{};
  last_.reason = "reset";
}

void DistanceStateChain::clearEmptyReference() {
  hasEmptyReference_ = false;
  emptyReference_ = NAN;
  userPresent_ = false;
  baselineReady_ = false;
  baselineDistance_ = NAN;
  enterCount_ = 0;
  exitCount_ = 0;
}

void DistanceStateChain::setEmptyReference(float distance, bool valid) {
  if (!valid || !isfinite(distance)) {
    hasEmptyReference_ = false;
    emptyReference_ = NAN;
    return;
  }
  hasEmptyReference_ = true;
  emptyReference_ = distance;
  userPresent_ = false;
  baselineReady_ = false;
  baselineDistance_ = NAN;
  enterCount_ = 0;
  exitCount_ = 0;
}

DistanceStateChainResult DistanceStateChain::update(
    float distance,
    uint32_t nowMs,
    bool running) {
  DistanceStateChainResult result{};
  result.distance = distance;
  result.emptyReference = emptyReference_;
  result.baselineDistance = baselineDistance_;
  result.userPresent = userPresent_;
  result.baselineReady = baselineReady_;

  if (!isfinite(distance)) {
    result.reason = "distance_invalid";
    last_ = result;
    return result;
  }

  if (!hasEmptyReference_) {
    // A missing calibration must not invent a user weight. It may establish a
    // temporary empty reference for the distance-only chain; CAL:SET_MODEL
    // replaces it with the explicit APP-provided point.
    hasEmptyReference_ = true;
    emptyReference_ = distance;
  }
  result.valid = true;
  result.emptyReference = emptyReference_;
  result.deltaFromEmpty = fabsf(distance - emptyReference_);

  const uint8_t confirm = config_.confirmSamples;
  const bool shouldEnter = result.deltaFromEmpty >= config_.emptyEnterDelta;
  const bool shouldExit = result.deltaFromEmpty <= config_.emptyExitDelta;
  if (!userPresent_) {
    if (shouldEnter) {
      if (enterCount_ < 0xFF) ++enterCount_;
      exitCount_ = 0;
      if (enterCount_ >= confirm) {
        userPresent_ = true;
        baselineReady_ = false;
        baselineDistance_ = NAN;
        result.presenceChanged = true;
      }
    } else {
      enterCount_ = 0;
    }
  } else if (shouldExit) {
    if (exitCount_ < 0xFF) ++exitCount_;
    enterCount_ = 0;
    if (exitCount_ >= confirm) {
      userPresent_ = false;
      baselineReady_ = false;
      baselineDistance_ = NAN;
      result.presenceChanged = true;
    }
  } else {
    exitCount_ = 0;
  }

  samples_[head_] = distance;
  head_ = static_cast<uint8_t>((head_ + 1) % 16);
  if (count_ < 16) ++count_;

  float minValue = distance;
  float maxValue = distance;
  float oldest = distance;
  const uint8_t window = count_ < config_.stableSamples ? count_ : config_.stableSamples;
  for (uint8_t i = 0; i < window; ++i) {
    const uint8_t index = static_cast<uint8_t>((head_ + 16 - 1 - i) % 16);
    const float value = samples_[index];
    if (value < minValue) minValue = value;
    if (value > maxValue) maxValue = value;
    oldest = value;
  }
  const bool stable = window >= config_.stableSamples &&
      (maxValue - minValue) <= config_.stableRange &&
      fabsf(distance - oldest) <= config_.stableDrift;

  if (userPresent_ && stable && !baselineReady_ && !running) {
    baselineDistance_ = (minValue + maxValue) * 0.5f;
    baselineReady_ = true;
  }
  if (baselineReady_ && isfinite(baselineDistance_) &&
      fabsf(distance - baselineDistance_) >= config_.baselineExitDelta &&
      !running) {
    baselineReady_ = false;
  }

  if (previousAtMs_ != 0 && nowMs > previousAtMs_) {
    result.speed = (distance - previousDistance_) /
        (static_cast<float>(nowMs - previousAtMs_) / 1000.0f);
  }
  previousDistance_ = distance;
  previousAtMs_ = nowMs;

  result.userPresent = userPresent_;
  result.stable = stable;
  result.baselineReady = baselineReady_;
  result.baselineDistance = baselineDistance_;
  result.deltaFromBaseline = isfinite(baselineDistance_)
      ? distance - baselineDistance_
      : NAN;
  result.reason = userPresent_ ? (stable ? "present_stable" : "present_unstable")
                               : "empty_or_absent";
  last_ = result;
  return result;
}
