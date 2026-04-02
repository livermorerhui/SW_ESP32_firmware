#pragma once

#include <Arduino.h>

#include "GlobalConfig.h"
#include "RhythmStateConfig.h"

// Phase 2 WP1 keeps the currently validated behavior by bridging existing
// constants into one formal threshold matrix. Later work packages can migrate
// individual paths without re-hunting scattered literals.

struct LaserPresenceThresholdConfig {
  float enterThresholdKg = MIN_WEIGHT;
  float exitThresholdKg = LEAVE_TH;
  uint8_t confirmSamples = 1;
  uint8_t invalidExitSamples = 4;
};

struct LaserStableThresholdConfig {
  uint32_t evalIntervalDefaultMs = LASER_STATE_EVAL_INTERVAL_DEFAULT_MS;
  uint32_t evalIntervalStableBuildMs = LASER_STATE_EVAL_INTERVAL_STABLE_BUILD_MS;
  uint8_t enterWindowSamples = WINDOW_N;
  float enterStdDevKg = STD_TH;
  uint8_t earlyAcceptSamples = STABLE_EARLY_LATCH_SAMPLES;
  float earlyStrictStdDevKg = STABLE_EARLY_STRICT_STD_TH;
  float earlyStrictLatestDeltaKg = STABLE_EARLY_STRICT_LATEST_DELTA_TH;
  float earlyGuardedStdDevKg = STABLE_EARLY_GUARDED_STD_TH;
  float earlyGuardedLatestDeltaKg = STABLE_EARLY_GUARDED_LATEST_DELTA_TH;
  uint8_t earlyGuardedRecentSamples = STABLE_EARLY_GUARDED_RECENT_SAMPLES;
  float earlyGuardedRecentStdDevKg = STABLE_EARLY_GUARDED_RECENT_STD_TH;
  float earlyGuardedRecentRangeKg = STABLE_EARLY_GUARDED_RECENT_RANGE_TH;
  float earlyGuardedRecentMeanDeltaKg = STABLE_EARLY_GUARDED_RECENT_MEAN_DELTA_TH;
  float exitLeaveThresholdKg = LEAVE_TH;
  float exitWeightDeltaKg = STABLE_REARM_WEIGHT_DELTA_TH;
  float exitDistanceDelta = STABLE_REARM_DISTANCE_DELTA_TH;
  uint8_t exitLeaveConfirmSamples = STABLE_EXIT_CONFIRM_SAMPLES_LEAVE;
  uint8_t exitMovementConfirmSamples = STABLE_EXIT_CONFIRM_SAMPLES_MOVEMENT;
  uint8_t invalidGraceSamples = STABLE_INVALID_GRACE_SAMPLES;
};

struct LaserRuntimeZeroThresholdConfig {
  bool refreshEnabled = true;
  bool applyToWeightConversion = true;
  float emptyWindowMaxWeightKg = LEAVE_TH;
  uint8_t refreshSamples = WINDOW_N;
  float refreshStdDevDistance = 0.08f;
  float refreshRangeDistance = 0.20f;
  float driftGuardDistance = STABLE_REARM_DISTANCE_DELTA_TH;
  float clampMaxOffsetFromCalibration = STABLE_REARM_DISTANCE_DELTA_TH;
};

struct LaserStartGateConfig {
  bool requireUserPresent = true;
  bool requireBaselineReady = true;
  // formal pre-start ready must stay recoverable while the user remains
  // valid-on-platform after baseline capture; live-stable is evidence to
  // establish readiness, not a perpetual idle-time consumer gate.
  bool requireLiveStableWhenIdle = false;
  bool requireMeasurementValid = true;
  bool runningMaintainsReadyWithoutLiveStable = true;
  float minimumBaselineWeightKg = MIN_WEIGHT;
};

struct LaserPhase2ThresholdConfig {
  LaserPresenceThresholdConfig presence{};
  LaserStableThresholdConfig stable{};
  LaserRuntimeZeroThresholdConfig runtimeZero{};
  LaserStartGateConfig startGate{};
  RhythmStateJudgeParams rhythm{};
};

inline const LaserPhase2ThresholdConfig& phase2ThresholdConfig() {
  static const LaserPhase2ThresholdConfig config{};
  return config;
}
