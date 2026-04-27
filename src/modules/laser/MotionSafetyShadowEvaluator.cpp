#include "MotionSafetyShadowEvaluator.h"

#include <math.h>
#include <string.h>

#include "config/GlobalConfig.h"

namespace {
constexpr float kLeaveLowWeightRatio = 0.25f;
constexpr float kLeaveLowWeightKg = 5.0f;
constexpr float kLeaveDistanceMigration = 4.0f;
constexpr uint8_t kLeaveConfirmSamples = 5;

constexpr float kFallDeepLowWeightRatio = 0.60f;
constexpr float kFallTailLowRatio = 0.75f;
constexpr uint8_t kFallLowWeightMinSamples = 5;
constexpr float kFallMigrationDeltaWeightKg = 20.0f;
constexpr float kFallMigrationDeltaDistance = 2.0f;
constexpr float kFallMigrationRecoveryMax = 0.75f;
constexpr float kFallSupportDeltaRatio = 0.25f;
constexpr float kFallSupportMigrationDeltaDistance = 1.8f;
constexpr float kFallSupportRecoveryMax = 0.90f;
constexpr float kFallSupportDeviationMin = 0.25f;
constexpr float kFallFastDropRateKgPerSec = 25.0f;
constexpr uint8_t kFallFastDropConfirmSamples = 2;
constexpr float kFallFastDropRecoveryMax = 0.96f;

bool baselineChanged(float previousWeight, float previousDistance, float nextWeight, float nextDistance) {
  return fabsf(previousWeight - nextWeight) >= 0.5f ||
         fabsf(previousDistance - nextDistance) >= 0.5f;
}
}  // namespace

const char* MotionSafetyShadowEvaluator::mappedReasonName(MappedReason reason) {
  switch (reason) {
    case MappedReason::NONE:
      return "NONE";
    case MappedReason::USER_LEFT_PLATFORM:
      return "USER_LEFT_PLATFORM";
    case MappedReason::FALL_SUSPECTED:
      return "FALL_SUSPECTED";
  }
  return "UNKNOWN";
}

const char* MotionSafetyShadowEvaluator::mappedEffectName(MappedReason reason) {
  switch (reason) {
    case MappedReason::NONE:
      return "NONE";
    case MappedReason::USER_LEFT_PLATFORM:
      return "RECOVERABLE_PAUSE";
    case MappedReason::FALL_SUSPECTED:
      return "ABNORMAL_STOP";
  }
  return "UNKNOWN";
}

void MotionSafetyShadowEvaluator::resetWindow() {
  windowHead = 0;
  windowCount = 0;
  minMaWeightKg = 0.0f;
  minMaDistance = 0.0f;
  maxBaselineDeviationRatio = 0.0f;
  leaveLowRun = 0;
  fallLowRun = 0;
  fastDropCount = 0;
  haveLastSample = false;
  lastWeightKg = 0.0f;
  lastDistance = 0.0f;
  lastSampleMs = 0;
}

void MotionSafetyShadowEvaluator::reset(const char* reason) {
  resetWindow();
  active = false;
  baselineWeightKg = 0.0f;
  baselineDistance = 0.0f;
  lastLoggedReason = MappedReason::NONE;
  lastLoggedDetail = "NONE";
  lastLogMs = 0;
  if (MOTION_SAFETY_SHADOW_VERBOSE_RESET_LOG) {
    Serial.printf("[MOTION_SHADOW] action=reset reason=%s note=observe_only\n",
                  reason ? reason : "unspecified");
  }
}

void MotionSafetyShadowEvaluator::pushSample(float weightKg, float distance) {
  weightWindow[windowHead] = weightKg;
  distanceWindow[windowHead] = distance;
  windowHead = (windowHead + 1) % kMovingAverageWindow;
  if (windowCount < kMovingAverageWindow) {
    windowCount++;
  }
}

bool MotionSafetyShadowEvaluator::currentMovingAverage(float& weightKg, float& distance) const {
  if (windowCount < kMovingAverageWindow) {
    return false;
  }

  float weightSum = 0.0f;
  float distanceSum = 0.0f;
  for (uint8_t i = 0; i < windowCount; ++i) {
    weightSum += weightWindow[i];
    distanceSum += distanceWindow[i];
  }
  weightKg = weightSum / static_cast<float>(windowCount);
  distance = distanceSum / static_cast<float>(windowCount);
  return true;
}

void MotionSafetyShadowEvaluator::update(const MotionSafetyShadowInput& input) {
  if (!MOTION_SAFETY_SHADOW_RUNTIME_ENABLED) {
    return;
  }

  const bool canObserve =
      input.sampleValid &&
      input.topState == TopState::RUNNING &&
      input.userPresent &&
      input.baselineReady &&
      isfinite(input.baselineWeightKg) &&
      input.baselineWeightKg > 0.0f &&
      isfinite(input.baselineDistance) &&
      isfinite(input.weightKg) &&
      isfinite(input.distance);
  if (!canObserve) {
    if (active) {
      reset("not_observable");
    }
    return;
  }

  if (!active || baselineChanged(
          baselineWeightKg,
          baselineDistance,
          input.baselineWeightKg,
          input.baselineDistance)) {
    resetWindow();
    active = true;
    baselineWeightKg = input.baselineWeightKg;
    baselineDistance = input.baselineDistance;
    lastLoggedReason = MappedReason::NONE;
    lastLoggedDetail = "NONE";
    lastLogMs = 0;
  }

  if (haveLastSample && input.nowMs > lastSampleMs) {
    const float dtSec = static_cast<float>(input.nowMs - lastSampleMs) / 1000.0f;
    const float weightRateKgPerSec = (input.weightKg - lastWeightKg) / dtSec;
    const float distanceDelta = input.distance - lastDistance;
    if (weightRateKgPerSec <= -kFallFastDropRateKgPerSec && distanceDelta < 0.0f) {
      if (fastDropCount < 0xFF) fastDropCount++;
    } else {
      fastDropCount = 0;
    }
  }
  haveLastSample = true;
  lastWeightKg = input.weightKg;
  lastDistance = input.distance;
  lastSampleMs = input.nowMs;

  pushSample(input.weightKg, input.distance);

  float maWeightKg = 0.0f;
  float maDistance = 0.0f;
  if (!currentMovingAverage(maWeightKg, maDistance)) {
    return;
  }

  if (minMaWeightKg == 0.0f || maWeightKg < minMaWeightKg) {
    minMaWeightKg = maWeightKg;
  }
  if (minMaDistance == 0.0f || maDistance < minMaDistance) {
    minMaDistance = maDistance;
  }

  const float recoveryRatio = maWeightKg / baselineWeightKg;
  const float weightDropKg = baselineWeightKg - minMaWeightKg;
  const float distanceMigration = baselineDistance - minMaDistance;
  const float baselineDeviationRatio =
      fabsf(maWeightKg - baselineWeightKg) / baselineWeightKg;
  if (baselineDeviationRatio > maxBaselineDeviationRatio) {
    maxBaselineDeviationRatio = baselineDeviationRatio;
  }

  const bool leaveLow =
      maWeightKg <= kLeaveLowWeightKg ||
      maWeightKg <= baselineWeightKg * kLeaveLowWeightRatio;
  if (leaveLow) {
    if (leaveLowRun < 0xFF) leaveLowRun++;
  } else {
    leaveLowRun = 0;
  }

  const bool fallLow = maWeightKg <= baselineWeightKg * kFallDeepLowWeightRatio;
  if (fallLow) {
    if (fallLowRun < 0xFF) fallLowRun++;
  } else {
    fallLowRun = 0;
  }

  const bool leaveConfirmed =
      leaveLowRun >= kLeaveConfirmSamples ||
      (weightDropKg >= baselineWeightKg * 0.55f &&
       distanceMigration >= kLeaveDistanceMigration &&
       recoveryRatio <= kFallMigrationRecoveryMax);

  const bool deepLow =
      fallLowRun >= kFallLowWeightMinSamples &&
      maWeightKg <= baselineWeightKg * kFallTailLowRatio;
  const bool migrationUnrecovered =
      weightDropKg >= kFallMigrationDeltaWeightKg &&
      recoveryRatio <= kFallMigrationRecoveryMax &&
      distanceMigration >= kFallMigrationDeltaDistance;
  const bool partialSupportMigration =
      weightDropKg >= baselineWeightKg * kFallSupportDeltaRatio &&
      distanceMigration >= kFallSupportMigrationDeltaDistance &&
      recoveryRatio <= kFallSupportRecoveryMax &&
      maxBaselineDeviationRatio >= kFallSupportDeviationMin;
  const bool fastDrop =
      fastDropCount >= kFallFastDropConfirmSamples &&
      (deepLow || migrationUnrecovered || partialSupportMigration ||
       recoveryRatio <= kFallFastDropRecoveryMax);

  const bool strongFallCandidate =
      deepLow || migrationUnrecovered || partialSupportMigration;
  const bool fastDropOnly = fastDrop && !strongFallCandidate;
  const bool leaveLikeAmbiguous =
      strongFallCandidate &&
      !leaveConfirmed &&
      weightDropKg >= baselineWeightKg * kFallSupportDeltaRatio &&
      distanceMigration >= kFallSupportMigrationDeltaDistance &&
      recoveryRatio <= kFallSupportRecoveryMax;
  const bool fallConfirmed = strongFallCandidate && !leaveConfirmed && !leaveLikeAmbiguous;

  MappedReason mappedReason = MappedReason::NONE;
  const char* detail = "NONE";
  if (fastDropOnly) {
    detail = "REVIEW_FAST_DROP_ONLY";
  } else if (leaveLikeAmbiguous) {
    detail = "REVIEW_LEAVE_FALL_AMBIGUOUS";
  } else if (fallConfirmed) {
    mappedReason = MappedReason::FALL_SUSPECTED;
    if (deepLow) {
      detail = "DANGER_DEEP_LOW_LOAD";
    } else if (migrationUnrecovered) {
      detail = "DANGER_MIGRATION_UNRECOVERED";
    } else if (partialSupportMigration) {
      detail = "DANGER_MIGRATION_PARTIAL_SUPPORT";
    }
  } else if (leaveConfirmed) {
    mappedReason = MappedReason::USER_LEFT_PLATFORM;
    detail = "LEFT_PLATFORM_CONFIRMED";
  }

  maybeLog(
      input,
      mappedReason,
      detail,
      maWeightKg,
      maDistance,
      weightDropKg,
      distanceMigration,
      recoveryRatio,
      leaveConfirmed,
      fallConfirmed);
}

void MotionSafetyShadowEvaluator::maybeLog(
    const MotionSafetyShadowInput& input,
    MappedReason mappedReason,
    const char* detail,
    float maWeightKg,
    float maDistance,
    float weightDropKg,
    float distanceMigration,
    float recoveryRatio,
    bool leaveConfirmed,
    bool fallConfirmed) {
  if (mappedReason == MappedReason::NONE) {
    const bool reviewDetail =
        detail &&
        (strcmp(detail, "REVIEW_LEAVE_FALL_AMBIGUOUS") == 0 ||
         strcmp(detail, "REVIEW_FAST_DROP_ONLY") == 0);
    if (!reviewDetail) {
      return;
    }
  }

  const bool reasonChanged = mappedReason != lastLoggedReason;
  const bool detailChanged = strcmp(detail ? detail : "", lastLoggedDetail ? lastLoggedDetail : "") != 0;
  const bool periodic =
      lastLogMs == 0 ||
      input.nowMs - lastLogMs >= MOTION_SAFETY_SHADOW_LOG_INTERVAL_MS;
  if (!reasonChanged && !detailChanged && !periodic) {
    return;
  }

  Serial.printf(
      "[MOTION_SHADOW] action=observe_only top_state=%s user_present=%d baseline_ready=%d "
      "mapped_reason=%s effect_if_enabled=%s effect_if_disabled=%s detail=%s "
      "stable_weight=%.2f stable_distance=%.2f ma_weight=%.2f ma_distance=%.2f "
      "weight_drop=%.2f distance_migration=%.2f recovery_ratio=%.4f "
      "leave_confirmed=%d fall_confirmed=%d note=no_runtime_action\n",
      topStateName(input.topState),
      input.userPresent ? 1 : 0,
      input.baselineReady ? 1 : 0,
      mappedReasonName(mappedReason),
      mappedEffectName(mappedReason),
      mappedReason == MappedReason::FALL_SUSPECTED ? "WARNING_ONLY" : mappedEffectName(mappedReason),
      detail ? detail : "NONE",
      input.baselineWeightKg,
      input.baselineDistance,
      maWeightKg,
      maDistance,
      weightDropKg,
      distanceMigration,
      recoveryRatio,
      leaveConfirmed ? 1 : 0,
      fallConfirmed ? 1 : 0);

  lastLoggedReason = mappedReason;
  lastLoggedDetail = detail ? detail : "NONE";
  lastLogMs = input.nowMs;
}
