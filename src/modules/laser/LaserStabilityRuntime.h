#pragma once

#include <Arduino.h>

#include "config/GlobalConfig.h"

struct StableContractState {
  bool userPresent = false;
  bool stableCandidate = false;
  bool stableReadyLive = false;
  bool baselineReadyLatched = false;
  bool startReady = false;
  float stableReadyWeightKg = 0.0f;
  float stableReadyDistance = 0.0f;
  float baselineReadyWeightKg = 0.0f;
  float baselineReadyDistance = 0.0f;
  float startReadyWeightKg = 0.0f;
  uint32_t stableReadyAtMs = 0;
  uint32_t baselineReadyAtMs = 0;
  const char* startReadyBridge = "not_ready";
};

class LaserStabilityRuntime {
public:
  enum class StableState : uint8_t {
    UNSTABLE,
    STABLE_CANDIDATE,
    STABLE_LATCHED
  };

  void resetTracking() {
    stableState = StableState::UNSTABLE;
    stableBaselineDistance = 0.0f;
    stableBaselineDistanceMm = 0.0f;
    stableBaselineWeight = 0.0f;
    stableLatchedAtMs = 0;
    invalidStableSamples = 0;
    stableConfirmCount = 0;
    stableCandidateStartedAtMs = 0;
    stableEarlyCheckpointLogged = false;
    stableExitConfirmCount = 0;
    stableExitPendingReason = nullptr;
    bufHead = 0;
    bufCount = 0;
  }

  void resetSignalFilter() {
    stableFilterValid = false;
    stableFilteredDistance = 0.0f;
    stableFilteredWeight = 0.0f;
  }

  void beginCandidate(uint32_t now) {
    stableState = StableState::STABLE_CANDIDATE;
    bufHead = 0;
    bufCount = 0;
    stableConfirmCount = 0;
    stableCandidateStartedAtMs = now;
    stableEarlyCheckpointLogged = false;
  }

  void pushSample(float distance, float weight) {
    weightBuffer[bufHead] = weight;
    distanceBuffer[bufHead] = distance;
    bufHead = (bufHead + 1) % WINDOW_N;
    if (bufCount < WINDOW_N) ++bufCount;
  }

  float weightBuffer[WINDOW_N]{};
  float distanceBuffer[WINDOW_N]{};
  int bufHead = 0;
  int bufCount = 0;
  StableState stableState = StableState::UNSTABLE;
  float stableBaselineDistance = 0.0f;
  float stableBaselineDistanceMm = 0.0f;
  float stableBaselineWeight = 0.0f;
  uint32_t stableLatchedAtMs = 0;
  uint8_t invalidStableSamples = 0;
  uint8_t stableConfirmCount = 0;
  uint32_t stableCandidateStartedAtMs = 0;
  bool stableEarlyCheckpointLogged = false;
  bool stableFilterValid = false;
  float stableFilteredDistance = 0.0f;
  float stableFilteredWeight = 0.0f;
  StableContractState stableContract{};
  uint8_t presenceEnterConfirmCount = 0;
  uint8_t presenceExitConfirmCount = 0;
  uint8_t invalidPresenceSamples = 0;
  uint8_t stableExitConfirmCount = 0;
  const char* stableExitPendingReason = nullptr;
};
