#include "modules/laser/PresenceContractEvaluator.h"

PresenceContractResult PresenceContractEvaluator::evaluate(
    const LaserPresenceThresholdConfig& config,
    const PresenceContractInput& input) {
  const uint8_t confirmSamples = config.confirmSamples == 0 ? 1 : config.confirmSamples;

  PresenceContractResult result{};
  result.nextUserPresent = input.currentUserPresent;
  result.changed = false;

  if (input.weightKg >= config.enterThresholdKg) {
    if (!input.currentUserPresent && input.enterConfirmCount >= confirmSamples) {
      result.nextUserPresent = true;
      result.changed = true;
      result.reason = "enter_confirmed";
    } else {
      result.reason = input.currentUserPresent ? "present_hold" : "enter_pending";
    }
    return result;
  }

  if (input.weightKg <= config.exitThresholdKg) {
    if (input.currentUserPresent && input.exitConfirmCount >= confirmSamples) {
      result.nextUserPresent = false;
      result.changed = true;
      result.reason = "exit_confirmed";
    } else {
      result.reason = input.currentUserPresent ? "exit_pending" : "absent_hold";
    }
    return result;
  }

  result.reason = "threshold_deadband";
  return result;
}
