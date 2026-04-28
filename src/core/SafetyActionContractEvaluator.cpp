#include "SafetyActionContractEvaluator.h"

FallStopActionDecision SafetyActionContractEvaluator::decideFallSuspected(
    bool fallStopEnabled,
    bool fallAbnormalStopPolicy) {
  FallStopActionDecision decision{};
  decision.stopCandidateDetected = true;
  decision.fallStopEnabled = fallStopEnabled;
  decision.stopReason = FaultCode::FALL_SUSPECTED;

  if (!fallStopEnabled) {
    decision.shouldExecuteStop = false;
    decision.stopSuppressedBySwitch = true;
    decision.safetySignal = SafetySignalKind::WARNING_ONLY;
    decision.detail = "fall_stop_disabled";
    return decision;
  }

  decision.shouldExecuteStop = true;
  if (fallAbnormalStopPolicy) {
    decision.safetySignal = SafetySignalKind::ABNORMAL_STOP;
    decision.detail = "fall_stop_active";
    return decision;
  }

  decision.safetySignal = SafetySignalKind::RECOVERABLE_PAUSE;
  decision.detail = "fall_pause_override";
  return decision;
}

const char* SafetyActionContractEvaluator::resolveStopReasonText(
    const char* pendingStopReasonText,
    FaultCode code,
    const char* fallback) {
  if (pendingStopReasonText && pendingStopReasonText[0] != '\0') {
    return pendingStopReasonText;
  }
  if (fallback && fallback[0] != '\0') {
    return fallback;
  }
  if (code != FaultCode::NONE) {
    return faultCodeName(code);
  }
  return "MANUAL_STOP";
}

VerificationStopSource SafetyActionContractEvaluator::resolveStopSource(
    VerificationStopSource pendingStopSource,
    VerificationStopSource fallback) {
  if (pendingStopSource != VerificationStopSource::NONE) {
    return pendingStopSource;
  }
  if (fallback != VerificationStopSource::NONE) {
    return fallback;
  }
  return VerificationStopSource::USER_MANUAL_OTHER;
}
