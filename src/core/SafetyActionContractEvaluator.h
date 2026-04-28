#pragma once

#include "Types.h"
#include "config/GlobalConfig.h"

struct FallStopActionDecision {
  bool stopCandidateDetected = false;
  bool shouldExecuteStop = false;
  bool stopSuppressedBySwitch = false;
  bool fallStopEnabled = FALL_STOP_ENABLED_DEFAULT;
  FaultCode stopReason = FaultCode::FALL_SUSPECTED;
  SafetySignalKind safetySignal = SafetySignalKind::WARNING_ONLY;
  // verification contract 里的 stop_reason / stop_source。
  // 允许上游候选方提供更精确的停波语义，但最终动作仍由状态机执行。
  const char* verificationStopReason = nullptr;
  VerificationStopSource verificationStopSource = VerificationStopSource::NONE;
  const char* detail = "fall_detect_only";
};

class SafetyActionContractEvaluator {
public:
  static FallStopActionDecision decideFallSuspected(
      bool fallStopEnabled,
      bool fallAbnormalStopPolicy = SAFETY_POLICY_FALL_ABNORMAL_STOP);

  static const char* resolveStopReasonText(
      const char* pendingStopReasonText,
      FaultCode code,
      const char* fallback);

  static VerificationStopSource resolveStopSource(
      VerificationStopSource pendingStopSource,
      VerificationStopSource fallback);
};
