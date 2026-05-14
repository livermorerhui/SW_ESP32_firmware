#pragma once

#include "Types.h"

struct DegradedStartPolicyInput {
  bool laserConfiguredInstalled = false;
  bool measurementFaultConfirmed = false;
  bool degradedStartAuthorized = false;
  bool runtimeReady = false;
  bool startReady = false;
};

struct DegradedStartPolicyDecision {
  bool laserlessRuntimeStrategyActive = true;
  bool degradedStartAvailable = false;
  bool degradedStartEnabled = false;
  bool effectiveRuntimeReady = true;
  bool effectiveStartReady = true;
  bool protectionDegraded = true;
};

enum class UserLeftProtectionAction : uint8_t {
  NOT_ELIGIBLE,
  WARNING_ONLY,
  RECOVERABLE_PAUSE,
  BLOCKING_FAULT
};

struct UserLeftProtectionInput {
  TopState topState = TopState::IDLE;
  bool laserConfiguredInstalled = false;
  bool startReady = false;
  bool leaveStopEnabled = true;
  bool recoverablePausePolicy = true;
};

struct UserLeftProtectionDecision {
  UserLeftProtectionAction action = UserLeftProtectionAction::NOT_ELIGIBLE;
  bool eligible = false;
  SafetySignalKind safetySignal = SafetySignalKind::NONE;
  const char* suppressReason = "not_eligible";
  const char* detail = "user_left_platform";
};

class RuntimeProtectionPolicy {
public:
  static DegradedStartPolicyDecision evaluateDegradedStart(
      const DegradedStartPolicyInput& input);

  static UserLeftProtectionDecision decideUserLeft(
      const UserLeftProtectionInput& input);
};
