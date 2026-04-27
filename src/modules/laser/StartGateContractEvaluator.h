#pragma once

#include <Arduino.h>
#include "config/LaserPhase2Config.h"
#include "core/Types.h"

struct StartGateContractInput {
  bool measurementValid = false;
  bool userPresent = false;
  bool baselineReadyLatched = false;
  bool stableReadyLive = false;
  float baselineReadyWeightKg = 0.0f;
  TopState topState = TopState::IDLE;
};

struct StartGateContractResult {
  bool startReady = false;
  float startReadyWeightKg = 0.0f;
  const char* reason = "not_ready";
};

class StartGateContractEvaluator {
public:
  static StartGateContractResult evaluate(
      const LaserStartGateConfig& config,
      const StartGateContractInput& input);
};
