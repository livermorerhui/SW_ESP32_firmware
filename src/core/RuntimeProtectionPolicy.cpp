#include "core/RuntimeProtectionPolicy.h"

DegradedStartPolicyDecision RuntimeProtectionPolicy::evaluateDegradedStart(
    const DegradedStartPolicyInput& input) {
  DegradedStartPolicyDecision decision{};
  decision.noLaserStartBypassActive = !input.laserConfiguredInstalled;
  decision.degradedStartAvailable =
      input.laserConfiguredInstalled && input.measurementFaultConfirmed;
  decision.degradedStartEnabled =
      decision.degradedStartAvailable && input.degradedStartAuthorized;

  const bool bypassActive =
      decision.noLaserStartBypassActive || decision.degradedStartEnabled;
  decision.effectiveRuntimeReady = bypassActive ? true : input.runtimeReady;
  decision.effectiveStartReady = bypassActive ? true : input.startReady;
  decision.protectionDegraded =
      !input.laserConfiguredInstalled || input.measurementFaultConfirmed;
  return decision;
}

UserLeftProtectionDecision RuntimeProtectionPolicy::decideUserLeft(
    const UserLeftProtectionInput& input) {
  UserLeftProtectionDecision decision{};
  decision.eligible =
      input.topState == TopState::RUNNING &&
      input.laserConfiguredInstalled &&
      input.startReady;

  if (!decision.eligible) {
    decision.action = UserLeftProtectionAction::NOT_ELIGIBLE;
    decision.safetySignal = SafetySignalKind::NONE;
    decision.suppressReason = input.startReady ? "not_running" : "baseline_not_ready";
    return decision;
  }

  if (!input.leaveStopEnabled) {
    decision.action = UserLeftProtectionAction::WARNING_ONLY;
    decision.safetySignal = SafetySignalKind::WARNING_ONLY;
    decision.suppressReason = "leave_stop_disabled";
    return decision;
  }

  decision.action = input.recoverablePausePolicy
      ? UserLeftProtectionAction::RECOVERABLE_PAUSE
      : UserLeftProtectionAction::BLOCKING_FAULT;
  decision.safetySignal = input.recoverablePausePolicy
      ? SafetySignalKind::RECOVERABLE_PAUSE
      : SafetySignalKind::ABNORMAL_STOP;
  decision.suppressReason = "action_enabled";
  return decision;
}
