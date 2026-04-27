#pragma once

#include <Arduino.h>

#include "config/LaserPhase2Config.h"

struct BaselineEvidenceMetrics {
  bool valid = false;
  float stddev = NAN;
  float range = NAN;
  float drift = NAN;
};

struct BaselineEvidenceInput {
  BaselineEvidenceMetrics metrics{};
  uint8_t currentStableConfirmCount = 0;
};

struct BaselineEvidenceResult {
  bool windowReady = false;
  bool stableEligible = false;
  bool baselineEligible = false;
  bool stddevOk = false;
  bool rangeOk = false;
  bool driftOk = false;
  uint8_t nextStableConfirmCount = 0;
  const char* reason = "window_not_ready";
};

class BaselineEvidenceEvaluator {
public:
  static BaselineEvidenceResult evaluate(
      const LaserStableThresholdConfig& config,
      const BaselineEvidenceInput& input);
};
