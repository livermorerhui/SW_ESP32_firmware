#include "modules/laser/StartGateContractEvaluator.h"

StartGateContractResult StartGateContractEvaluator::evaluate(
    const LaserStartGateConfig& config,
    const StartGateContractInput& input) {
  const bool measurementValid = !config.requireMeasurementValid || input.measurementValid;
  const bool userPresentOk = !config.requireUserPresent || input.userPresent;
  const bool baselineReadyOk =
      !config.requireBaselineReady ||
      (input.baselineReadyLatched &&
       input.baselineReadyWeightKg >= config.minimumBaselineWeightKg);
  const bool liveStableOk =
      !config.requireLiveStableWhenIdle ||
      input.topState == TopState::RUNNING ||
      input.stableReadyLive;

  StartGateContractResult result{};
  if (!measurementValid) {
    result.reason = "measurement_invalid";
  } else if (!userPresentOk) {
    result.reason = "user_not_present";
  } else if (!baselineReadyOk) {
    result.reason = "baseline_not_ready";
  } else if (!liveStableOk) {
    result.reason = "live_stable_not_ready";
  } else if (input.topState == TopState::RUNNING &&
             config.runningMaintainsReadyWithoutLiveStable) {
    result.startReady = true;
    result.startReadyWeightKg = input.baselineReadyWeightKg;
    result.reason = "running_contract_hold";
  } else {
    result.startReady = true;
    result.startReadyWeightKg = input.baselineReadyWeightKg;
    result.reason = "idle_contract_ready";
  }

  return result;
}
