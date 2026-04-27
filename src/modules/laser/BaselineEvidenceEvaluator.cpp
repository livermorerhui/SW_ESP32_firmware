#include "modules/laser/BaselineEvidenceEvaluator.h"

#include <math.h>

BaselineEvidenceResult BaselineEvidenceEvaluator::evaluate(
    const LaserStableThresholdConfig& config,
    const BaselineEvidenceInput& input) {
  BaselineEvidenceResult result{};
  result.nextStableConfirmCount = input.currentStableConfirmCount;

  if (!input.metrics.valid ||
      !isfinite(input.metrics.stddev) ||
      !isfinite(input.metrics.range) ||
      !isfinite(input.metrics.drift)) {
    result.reason = "window_not_ready";
    return result;
  }

  result.windowReady = true;
  result.stddevOk = input.metrics.stddev < config.enterStdDevKg;
  result.rangeOk = input.metrics.range < config.enterRangeKg;
  result.driftOk = input.metrics.drift < config.enterDriftKg;
  result.stableEligible = result.stddevOk && result.rangeOk && result.driftOk;

  if (!result.stableEligible) {
    result.nextStableConfirmCount = 0;
    result.reason = "stable_window_hold";
    return result;
  }

  if (result.nextStableConfirmCount < 0xFF) {
    result.nextStableConfirmCount++;
  }
  result.baselineEligible =
      result.nextStableConfirmCount >= config.enterConfirmWindows;
  result.reason = result.baselineEligible ? "baseline_eligible" : "stable_confirm_pending";
  return result;
}
