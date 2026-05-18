#pragma once

#include "core/Types.h"

enum class StopOutcomeSummaryKind : uint8_t {
  STOP_SUMMARY,
  ABORT_SUMMARY
};

struct StopOutcomeSummaryDecision {
  StopOutcomeSummaryKind kind = StopOutcomeSummaryKind::STOP_SUMMARY;
  const char* result = "NORMAL";
  const char* stopReasonText = "NONE";
  const char* stopSourceText = "NONE";
};

class StopOutcomeSummaryEvaluator {
public:
  static StopOutcomeSummaryDecision evaluate(
      FaultCode stopReason,
      SafetySignalKind stopEffect,
      const char* stopReasonText,
      const char* stopSourceText);
};
