#pragma once

#include <Arduino.h>
#include "core/Types.h"

struct MotionSafetyShadowInput {
  uint32_t nowMs = 0;
  bool sampleValid = false;
  TopState topState = TopState::IDLE;
  bool userPresent = false;
  bool baselineReady = false;
  float distance = 0.0f;
  float weightKg = 0.0f;
  float baselineDistance = 0.0f;
  float baselineWeightKg = 0.0f;
};

class MotionSafetyShadowEvaluator {
public:
  void reset(const char* reason);
  void update(const MotionSafetyShadowInput& input);

private:
  static constexpr uint8_t kMovingAverageWindow = 5;

  enum class MappedReason : uint8_t {
    NONE,
    USER_LEFT_PLATFORM,
    FALL_SUSPECTED
  };

  static const char* mappedReasonName(MappedReason reason);
  static const char* mappedEffectName(MappedReason reason);
  void resetWindow();
  void pushSample(float weightKg, float distance);
  bool currentMovingAverage(float& weightKg, float& distance) const;
  void maybeLog(
      const MotionSafetyShadowInput& input,
      MappedReason mappedReason,
      const char* detail,
      float maWeightKg,
      float maDistance,
      float weightDropKg,
      float distanceMigration,
      float recoveryRatio,
      bool leaveConfirmed,
      bool fallConfirmed);

  bool active = false;
  float baselineWeightKg = 0.0f;
  float baselineDistance = 0.0f;
  float weightWindow[kMovingAverageWindow]{};
  float distanceWindow[kMovingAverageWindow]{};
  uint8_t windowHead = 0;
  uint8_t windowCount = 0;
  float minMaWeightKg = 0.0f;
  float minMaDistance = 0.0f;
  float maxBaselineDeviationRatio = 0.0f;
  uint8_t leaveLowRun = 0;
  uint8_t fallLowRun = 0;
  uint8_t fastDropCount = 0;
  bool haveLastSample = false;
  float lastWeightKg = 0.0f;
  float lastDistance = 0.0f;
  uint32_t lastSampleMs = 0;
  uint32_t lastLogMs = 0;
  MappedReason lastLoggedReason = MappedReason::NONE;
  const char* lastLoggedDetail = "NONE";
};
