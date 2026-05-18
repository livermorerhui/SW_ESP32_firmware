#pragma once

#include <Arduino.h>

#include "config/LaserPhase2Config.h"

struct PresenceContractInput {
  float weightKg = 0.0f;
  bool currentUserPresent = false;
  uint8_t enterConfirmCount = 0;
  uint8_t exitConfirmCount = 0;
};

struct PresenceContractResult {
  bool nextUserPresent = false;
  bool changed = false;
  const char* reason = "unchanged";
};

struct PresenceCounterInput {
  float weightKg = 0.0f;
  bool currentUserPresent = false;
  uint8_t currentEnterConfirmCount = 0;
  uint8_t currentExitConfirmCount = 0;
};

struct PresenceCounterResult {
  uint8_t nextEnterConfirmCount = 0;
  uint8_t nextExitConfirmCount = 0;
  PresenceContractResult decision{};
};

class PresenceContractEvaluator {
public:
  static PresenceContractResult evaluate(
      const LaserPresenceThresholdConfig& config,
      const PresenceContractInput& input);

  static PresenceCounterResult evaluateWithCounters(
      const LaserPresenceThresholdConfig& config,
      const PresenceCounterInput& input);
};
