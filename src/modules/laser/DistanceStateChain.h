#pragma once

#include <Arduino.h>

// DistanceStateChain is deliberately independent of the weight calibration.
// It consumes normalized physical distance (runtime units are mm / 100, as in
// the existing firmware) and supplies presence/stability evidence even when a
// weight model is missing or invalid.
struct DistanceStateChainConfig {
  float emptyEnterDelta = 0.03f;
  float emptyExitDelta = 0.01f;
  float stableRange = 0.01f;
  float stableDrift = 0.01f;
  float baselineExitDelta = 0.02f;
  uint8_t confirmSamples = 2;
  uint8_t stableSamples = 10;
};

struct DistanceStateChainResult {
  bool valid = false;
  bool userPresent = false;
  bool presenceChanged = false;
  bool stable = false;
  bool baselineReady = false;
  float distance = NAN;
  float emptyReference = NAN;
  float baselineDistance = NAN;
  float deltaFromEmpty = NAN;
  float deltaFromBaseline = NAN;
  float speed = 0.0f;
  const char* reason = "invalid";
};

class DistanceStateChain {
public:
  void configure(const DistanceStateChainConfig& config);
  void reset(const char* reason);
  void clearEmptyReference();
  void setEmptyReference(float distance, bool valid);
  DistanceStateChainResult update(float distance, uint32_t nowMs, bool running);
  const DistanceStateChainResult& last() const { return last_; }

private:
  DistanceStateChainConfig config_{};
  float samples_[16]{};
  uint8_t head_ = 0;
  uint8_t count_ = 0;
  uint8_t enterCount_ = 0;
  uint8_t exitCount_ = 0;
  bool hasEmptyReference_ = false;
  bool userPresent_ = false;
  bool baselineReady_ = false;
  float emptyReference_ = NAN;
  float baselineDistance_ = NAN;
  float previousDistance_ = NAN;
  uint32_t previousAtMs_ = 0;
  DistanceStateChainResult last_{};
};
