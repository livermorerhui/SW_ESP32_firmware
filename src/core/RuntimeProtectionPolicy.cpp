#include "core/RuntimeProtectionPolicy.h"

DegradedStartPolicyDecision RuntimeProtectionPolicy::evaluateDegradedStart(
    const DegradedStartPolicyInput& input) {
  DegradedStartPolicyDecision decision{};
  const bool degradedCondition =
      input.configurationValid &&
      (!input.laserConfiguredInstalled ||
       !input.laserProfileConfigured ||
       (input.measurementStartupResolved && !input.laserAvailable));
  const bool noProtectionBaselineRequired =
      input.configurationValid &&
      input.measurementStartupResolved &&
      input.laserAvailable &&
      !input.protectedStartRequired;
  // Do not publish a degraded result during reader startup or a configuration
  // transition. Once startup is resolved, Base, Plus-without-profile, and a
  // confirmed unavailable Plus laser all use the same local ESP32 decision.
  const bool degradedPathActive =
      input.measurementStartupResolved && degradedCondition;
  const bool bypassActive = degradedPathActive || noProtectionBaselineRequired;
  decision.noLaserStartBypassActive = degradedPathActive;
  decision.startConfigurationBypassActive = bypassActive;
  decision.degradedStartAvailable =
      degradedPathActive && input.laserConfiguredInstalled;
  // This field is retained as a compatibility/diagnostic projection. It no
  // longer depends on an APP authorization command.
  decision.degradedStartEnabled = decision.degradedStartAvailable;
  decision.effectiveRuntimeReady = bypassActive ? true : input.runtimeReady;
  decision.effectiveStartReady = bypassActive ? true : input.startReady;
  decision.protectionDegraded = degradedPathActive;
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
