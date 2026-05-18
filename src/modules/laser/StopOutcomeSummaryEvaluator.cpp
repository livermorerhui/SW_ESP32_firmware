#include "modules/laser/StopOutcomeSummaryEvaluator.h"

StopOutcomeSummaryDecision StopOutcomeSummaryEvaluator::evaluate(
    FaultCode stopReason,
    SafetySignalKind stopEffect,
    const char* stopReasonText,
    const char* stopSourceText) {
  StopOutcomeSummaryDecision decision{};
  decision.stopReasonText = (stopReasonText && stopReasonText[0] != '\0')
      ? stopReasonText
      : ((stopReason != FaultCode::NONE) ? faultCodeName(stopReason) : "NONE");
  decision.stopSourceText = (stopSourceText && stopSourceText[0] != '\0')
      ? stopSourceText
      : "NONE";

  switch (stopEffect) {
    case SafetySignalKind::ABNORMAL_STOP:
      decision.kind = StopOutcomeSummaryKind::ABORT_SUMMARY;
      decision.result = "ABNORMAL_STOP";
      return decision;
    case SafetySignalKind::RECOVERABLE_PAUSE:
      decision.kind = StopOutcomeSummaryKind::STOP_SUMMARY;
      decision.result = "RECOVERABLE_PAUSE";
      return decision;
    case SafetySignalKind::WARNING_ONLY:
      decision.kind = StopOutcomeSummaryKind::STOP_SUMMARY;
      decision.result = "WARNING_ONLY";
      return decision;
    case SafetySignalKind::NONE:
      decision.kind = StopOutcomeSummaryKind::STOP_SUMMARY;
      decision.result = "NORMAL";
      return decision;
  }

  decision.kind = StopOutcomeSummaryKind::STOP_SUMMARY;
  decision.result = "NORMAL";
  return decision;
}
