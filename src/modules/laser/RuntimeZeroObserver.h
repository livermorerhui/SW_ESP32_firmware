#pragma once

#include <Arduino.h>
#include "config/LaserPhase2Config.h"
#include "core/SystemStateMachine.h"

enum class RuntimeZeroDecisionReason : uint8_t {
  REFRESH_DISABLED = 0,
  FROZEN_RUNNING,
  FROZEN_USER_PRESENT,
  FROZEN_STABLE_CANDIDATE,
  FROZEN_OCCUPIED_CYCLE,
  FROZEN_EFFECTIVE_ZERO_LOCKED,
  NOT_EMPTY_WINDOW,
  WINDOW_NOT_READY,
  WINDOW_UNSTABLE,
  DRIFT_GUARD,
  REFRESHED,
  COUNT
};

struct RuntimeZeroEligibilityInput {
  float distance = NAN;
  float weight = NAN;
  TopState topState = TopState::IDLE;
  bool userPresent = false;
  bool stableCandidate = false;
  bool occupiedCycleActive = false;
  bool effectiveZeroLocked = false;
  bool baselineReadyLatched = false;
};

struct RuntimeZeroWindowInput {
  const float* values = nullptr;
  uint8_t head = 0;
  uint8_t count = 0;
  uint8_t capacity = 0;
  float calibrationZero = 0.0f;
};

struct RuntimeZeroDecisionStats {
  float mean = NAN;
  float stddev = NAN;
  float range = NAN;
  float driftFromCalibration = NAN;
};

struct RuntimeZeroDecision {
  RuntimeZeroDecisionReason reason = RuntimeZeroDecisionReason::REFRESH_DISABLED;
  uint8_t requiredSamples = 0;
  bool eligible = false;
  bool shouldResetWindow = false;
  bool shouldRefresh = false;
  RuntimeZeroDecisionStats stats{};
};

class RuntimeZeroObserver {
public:
  RuntimeZeroDecision evaluateEligibility(
      const LaserRuntimeZeroThresholdConfig& config,
      const RuntimeZeroEligibilityInput& input) const;

  RuntimeZeroDecision evaluateWindow(
      const LaserRuntimeZeroThresholdConfig& config,
      const RuntimeZeroWindowInput& input) const;

  void noteDecision(uint32_t now, const RuntimeZeroDecision& decision);

  static const char* reasonName(RuntimeZeroDecisionReason reason);

private:
  static constexpr uint32_t kDiagnosticSummaryIntervalMs = 10000UL;
  static constexpr uint8_t kReasonCount =
      static_cast<uint8_t>(RuntimeZeroDecisionReason::COUNT);

  uint16_t reasonCounts[kReasonCount]{};
  uint32_t summaryWindowStartedAtMs = 0;

  static uint8_t reasonIndex(RuntimeZeroDecisionReason reason);
  void logSummary(uint32_t now);
};
