#include "RhythmStateJudge.h"

#include <math.h>
#include <string.h>

namespace {

constexpr uint8_t kPrimaryMovingAverageWindow = 12;

const char* validReasonOrDefault(const char* reason) {
  return (reason && reason[0] != '\0') ? reason : "baseline_pending";
}

bool sameString(const char* lhs, const char* rhs) {
  return strcmp(validReasonOrDefault(lhs), validReasonOrDefault(rhs)) == 0;
}

}  // namespace

const char* rhythmStateName(RhythmStateStatus status) {
  switch (status) {
    case RhythmStateStatus::BASELINE_PENDING:
      return "BASELINE_PENDING";
    case RhythmStateStatus::NORMAL:
      return "NORMAL";
    case RhythmStateStatus::ABNORMAL_RECOVERABLE:
      return "ABNORMAL_RECOVERABLE";
    case RhythmStateStatus::DANGER_CANDIDATE:
      return "DANGER_CANDIDATE";
    case RhythmStateStatus::DANGER:
      return "DANGER";
    case RhythmStateStatus::STOPPED_BY_DANGER:
      return "STOPPED_BY_DANGER";
  }
  return "UNKNOWN";
}

const char* riskAdvisoryTypeName(RiskAdvisoryType type) {
  switch (type) {
    case RiskAdvisoryType::NONE:
      return "NONE";
    case RiskAdvisoryType::LOAD_SHIFT_LARGE:
      return "LOAD_SHIFT_LARGE";
    case RiskAdvisoryType::WEIGHT_DEVIATION_HIGH:
      return "WEIGHT_DEVIATION_HIGH";
    case RiskAdvisoryType::RECOVERY_SLOW:
      return "RECOVERY_SLOW";
    case RiskAdvisoryType::EVENT_LIKE_BUT_RECOVERED:
      return "EVENT_LIKE_BUT_RECOVERED";
  }
  return "UNKNOWN";
}

const char* riskAdvisoryLevelName(RiskAdvisoryLevel level) {
  switch (level) {
    case RiskAdvisoryLevel::NONE:
      return "NONE";
    case RiskAdvisoryLevel::INFO:
      return "INFO";
    case RiskAdvisoryLevel::WARN:
      return "WARN";
  }
  return "UNKNOWN";
}

void RhythmStateJudge::configure(const RhythmStateJudgeParams& newParams) {
  params = newParams;
  clampParams();
}

void RhythmStateJudge::clampParams() {
  if (params.research.movingAverageWindow == 0) {
    params.research.movingAverageWindow = 1;
  }
  if (params.research.movingAverageWindow > kMaxMovingAverageWindow) {
    params.research.movingAverageWindow = kMaxMovingAverageWindow;
  }

  if (params.research.tailWindow == 0) {
    params.research.tailWindow = 1;
  }

  if (params.fall.confirmSamples == 0) {
    params.fall.confirmSamples = 1;
  }
  if (params.fall.minimumCumulativeWeightDropKg < 0.0f) {
    params.fall.minimumCumulativeWeightDropKg = 0.0f;
  }
  if (params.fall.minimumCumulativeDistanceDrop < 0.0f) {
    params.fall.minimumCumulativeDistanceDrop = 0.0f;
  }
  if (params.fall.holdSamplesAfterThreshold == 0) {
    params.fall.holdSamplesAfterThreshold = 1;
  }
  if (params.fall.sustainDropRatioAfterThreshold < 0.0f) {
    params.fall.sustainDropRatioAfterThreshold = 0.0f;
  }
  if (params.fall.sustainDropRatioAfterThreshold > 1.0f) {
    params.fall.sustainDropRatioAfterThreshold = 1.0f;
  }

  if (params.baseline.safeBandRatio < 0.0f) {
    params.baseline.safeBandRatio = 0.0f;
  }
  if (params.baseline.dangerBandRatio < params.baseline.safeBandRatio) {
    params.baseline.dangerBandRatio = params.baseline.safeBandRatio;
  }
  if (params.baseline.abnormalRecoveryTimeMs == 0) {
    params.baseline.abnormalRecoveryTimeMs = 1;
  }
  if (params.baseline.dangerHoldTimeMs == 0) {
    params.baseline.dangerHoldTimeMs = 1;
  }

  if (params.advisory.advisoryBandRatio < params.baseline.safeBandRatio) {
    params.advisory.advisoryBandRatio = params.baseline.safeBandRatio;
  }
  if (params.advisory.advisoryNearDangerBandRatio < params.advisory.advisoryBandRatio) {
    params.advisory.advisoryNearDangerBandRatio = params.advisory.advisoryBandRatio;
  }
  if (params.advisory.advisoryNearDangerBandRatio > params.baseline.dangerBandRatio) {
    params.advisory.advisoryNearDangerBandRatio = params.baseline.dangerBandRatio;
  }
  if (params.advisory.advisoryRecoveryTimeMs == 0) {
    params.advisory.advisoryRecoveryTimeMs = 1;
  }
  if (params.advisory.repeatThrottleMs == 0) {
    params.advisory.repeatThrottleMs = 1;
  }
}

void RhythmStateJudge::clearFormalFallCandidate() {
  fallCandidateActive = false;
  fallHoldPending = false;
  fallCandidateStartWeightKg = 0.0f;
  fallCandidateStartDistance = 0.0f;
  fallConfirmCount = 0;
  fallHoldCount = 0;
}

void RhythmStateJudge::resetMotionEvaluation() {
  maHead = 0;
  maCount = 0;
  maWeightSum = 0.0f;
  maDistanceSum = 0.0f;
  abnormalStartedAtMs = 0;
  dangerStartedAtMs = 0;
  resetAdvisoryTracking();

  result.evidence.ma12WeightKg = 0.0f;
  result.evidence.ma12Distance = 0.0f;
  result.evidence.deviationKg = 0.0f;
  result.evidence.ratio = 0.0f;
  result.evidence.abnormalDurationMs = 0;
  result.evidence.dangerDurationMs = 0;
  result.evidence.directDangerBandTriggered = false;
  result.evidence.dangerFromUnrecoveredAbnormal = false;
  result.advisory = RiskAdvisoryState{};
  result.advisory.reason = "none";
}

void RhythmStateJudge::resetAdvisoryTracking() {
  advisoryExcursionActive = false;
  advisoryEventAuxSeen = false;
  advisoryNearDangerSeen = false;
  advisoryPeakRatio = 0.0f;
  advisoryLastAbnormalDurationMs = 0;
  advisoryLastDangerDurationMs = 0;
}

void RhythmStateJudge::reset(const char* reason) {
  clampParams();
  result = RhythmStateUpdateResult{};
  result.reason = validReasonOrDefault(reason);
  resetMotionEvaluation();
  lastWeightKg = 0.0f;
  lastDistance = 0.0f;
  lastSampleMs = 0;
  clearFormalFallCandidate();
  dangerStopLatched = false;
  latchedStopReason = nullptr;
  hasLoggedStatus = false;
  lastLoggedStatus = RhythmStateStatus::BASELINE_PENDING;
  lastLoggedReason = "baseline_pending";
  lastLogMs = 0;
  lastAdvisoryLoggedType = RiskAdvisoryType::NONE;
  lastAdvisoryLogMs = 0;
}

void RhythmStateJudge::noteInvalidMeasurement() {
  clearFormalFallCandidate();
  lastSampleMs = 0;
}

void RhythmStateJudge::refreshBaselineFromStable(float stableDistance, float stableWeightKg, uint32_t nowMs) {
  if (!isfinite(stableDistance) || !isfinite(stableWeightKg) || stableWeightKg <= 0.0f) {
    return;
  }

  resetMotionEvaluation();
  dangerStopLatched = false;
  latchedStopReason = nullptr;

  result.status = RhythmStateStatus::NORMAL;
  result.reason = "baseline_ready";
  result.previousStatus = RhythmStateStatus::BASELINE_PENDING;
  result.stateChanged = true;
  result.evidence.baselineReady = true;
  result.evidence.baselineWeightKg = stableWeightKg;
  result.evidence.baselineDistance = stableDistance;
  result.evidence.baselineCapturedAtMs = nowMs;
}

void RhythmStateJudge::fillAdvisoryState(
    RiskAdvisoryType type,
    RiskAdvisoryLevel level,
    const char* reason,
    uint32_t nowMs,
    uint32_t abnormalDurationMs,
    uint32_t dangerDurationMs) {
  result.advisory.active = (type != RiskAdvisoryType::NONE);
  result.advisory.type = type;
  result.advisory.level = level;
  result.advisory.reason = (reason && reason[0] != '\0') ? reason : "none";
  result.advisory.eventAuxSeen = advisoryEventAuxSeen;
  result.advisory.triggeredAtMs = nowMs;
  result.advisory.peakRatio = advisoryPeakRatio;
  result.advisory.abnormalDurationMs = abnormalDurationMs;
  result.advisory.dangerDurationMs = dangerDurationMs;
  result.advisory.shouldLog = false;

  if (type == RiskAdvisoryType::NONE) {
    return;
  }

  const bool sameType = (lastAdvisoryLoggedType == type);
  const bool withinThrottle =
      sameType && (nowMs - lastAdvisoryLogMs) < params.advisory.repeatThrottleMs;
  if (!withinThrottle) {
    result.advisory.shouldLog = true;
    lastAdvisoryLoggedType = type;
    lastAdvisoryLogMs = nowMs;
  }
}

void RhythmStateJudge::updateFormalFallCandidate(const RhythmStateJudgeInput& input) {
  result.formalEventCandidate = false;

  if (!input.sampleValid) {
    noteInvalidMeasurement();
    return;
  }

  if (lastSampleMs == 0) {
    lastSampleMs = input.nowMs;
    lastWeightKg = input.weightKg;
    lastDistance = input.distance;
    return;
  }

  float dt = static_cast<float>(input.nowMs - lastSampleMs) / 1000.0f;
  if (dt <= 0.0f) dt = 0.001f;

  const float weightDeltaKg = input.weightKg - lastWeightKg;
  const float distanceDelta = input.distance - lastDistance;
  const float signedWeightRateKgPerSec = weightDeltaKg / dt;
  const bool shouldTrack =
      input.topState == TopState::RUNNING && input.userPresent;
  const bool directionConsistentDrop =
      signedWeightRateKgPerSec < -params.fall.suspectRateThresholdKgPerSec &&
      distanceDelta < 0.0f;

  if (!shouldTrack) {
    clearFormalFallCandidate();
  } else if (fallHoldPending) {
    const float sustainedWeightDropKg =
        fallCandidateStartWeightKg - input.weightKg;
    const float sustainedDistanceDrop =
        fallCandidateStartDistance - input.distance;
    const float requiredWeightHoldKg =
        params.fall.minimumCumulativeWeightDropKg *
        params.fall.sustainDropRatioAfterThreshold;
    const float requiredDistanceHold =
        params.fall.minimumCumulativeDistanceDrop *
        params.fall.sustainDropRatioAfterThreshold;

    if (sustainedWeightDropKg < requiredWeightHoldKg ||
        sustainedDistanceDrop < requiredDistanceHold) {
      clearFormalFallCandidate();
    } else {
      if (fallHoldCount < 0xFF) {
        fallHoldCount++;
      }
      if (fallHoldCount >= params.fall.holdSamplesAfterThreshold) {
        result.formalEventCandidate = true;
        clearFormalFallCandidate();
      }
    }
  } else {
    if (!directionConsistentDrop) {
      clearFormalFallCandidate();
    } else {
      if (!fallCandidateActive) {
        fallCandidateActive = true;
        fallCandidateStartWeightKg = lastWeightKg;
        fallCandidateStartDistance = lastDistance;
        fallConfirmCount = 1;
      } else if (fallConfirmCount < 0xFF) {
        fallConfirmCount++;
      }

      const float cumulativeWeightDropKg =
          fallCandidateStartWeightKg - input.weightKg;
      const float cumulativeDistanceDrop =
          fallCandidateStartDistance - input.distance;

      if (fallConfirmCount >= params.fall.confirmSamples &&
          cumulativeWeightDropKg >= params.fall.minimumCumulativeWeightDropKg &&
          cumulativeDistanceDrop >= params.fall.minimumCumulativeDistanceDrop) {
        fallHoldPending = true;
        fallHoldCount = 0;
      }
    }
  }

  lastSampleMs = input.nowMs;
  lastWeightKg = input.weightKg;
  lastDistance = input.distance;
}

void RhythmStateJudge::updatePrimaryState(const RhythmStateJudgeInput& input) {
  result.shouldStopByDanger = false;
  result.stopReason = nullptr;

  if (!result.evidence.baselineReady) {
    resetMotionEvaluation();
    result.status = RhythmStateStatus::BASELINE_PENDING;
    result.reason = "baseline_pending";
    return;
  }

  if (dangerStopLatched) {
    result.status = RhythmStateStatus::STOPPED_BY_DANGER;
    result.reason = validReasonOrDefault(latchedStopReason);
    result.stopReason = latchedStopReason;
    return;
  }

  const bool shouldEvaluateMotion =
      input.sampleValid && input.userPresent && input.topState == TopState::RUNNING;
  if (!shouldEvaluateMotion) {
    resetMotionEvaluation();
    result.status = RhythmStateStatus::NORMAL;
    result.reason = input.userPresent ? "baseline_ready_idle" : "user_not_present";
    return;
  }

  if (maCount == kPrimaryMovingAverageWindow) {
    maWeightSum -= maWeightWindow[maHead];
    maDistanceSum -= maDistanceWindow[maHead];
  } else {
    maCount++;
  }

  maWeightWindow[maHead] = input.weightKg;
  maDistanceWindow[maHead] = input.distance;
  maWeightSum += input.weightKg;
  maDistanceSum += input.distance;
  maHead = (maHead + 1) % kPrimaryMovingAverageWindow;

  result.evidence.ma12WeightKg = maWeightSum / maCount;
  result.evidence.ma12Distance = maDistanceSum / maCount;

  const float stableWeightKg = result.evidence.baselineWeightKg;
  const float deviationKg = result.evidence.ma12WeightKg - stableWeightKg;
  const float ratio =
      (stableWeightKg > 0.0f) ? fabsf(deviationKg) / stableWeightKg : 0.0f;
  result.evidence.deviationKg = deviationKg;
  result.evidence.ratio = ratio;
  result.evidence.directDangerBandTriggered = false;
  result.evidence.dangerFromUnrecoveredAbnormal = false;

  if (ratio <= params.baseline.safeBandRatio) {
    abnormalStartedAtMs = 0;
    dangerStartedAtMs = 0;
    result.evidence.abnormalDurationMs = 0;
    result.evidence.dangerDurationMs = 0;
    result.status = RhythmStateStatus::NORMAL;
    result.reason = "ratio_within_safe_band";
    return;
  }

  if (abnormalStartedAtMs == 0) {
    abnormalStartedAtMs = input.nowMs;
  }
  result.evidence.abnormalDurationMs = input.nowMs - abnormalStartedAtMs;

  if (ratio > params.baseline.dangerBandRatio) {
    if (dangerStartedAtMs == 0) {
      dangerStartedAtMs = input.nowMs;
    }
    result.evidence.dangerDurationMs = input.nowMs - dangerStartedAtMs;
    result.evidence.directDangerBandTriggered = true;
    result.status = RhythmStateStatus::DANGER;
    result.reason = "ratio_exceeds_danger_band";
  } else if (result.evidence.abnormalDurationMs <= params.baseline.abnormalRecoveryTimeMs) {
    dangerStartedAtMs = 0;
    result.evidence.dangerDurationMs = 0;
    result.status = RhythmStateStatus::ABNORMAL_RECOVERABLE;
    result.reason = "abnormal_within_recovery_window";
  } else {
    if (dangerStartedAtMs == 0) {
      dangerStartedAtMs = input.nowMs;
    }
    result.evidence.dangerDurationMs = input.nowMs - dangerStartedAtMs;
    result.evidence.dangerFromUnrecoveredAbnormal = true;
    if (result.evidence.dangerDurationMs >= params.baseline.dangerHoldTimeMs) {
      result.status = RhythmStateStatus::DANGER;
      result.reason = "abnormal_persisted_into_danger";
    } else {
      result.status = RhythmStateStatus::DANGER_CANDIDATE;
      result.reason = "abnormal_exceeds_recovery_window";
    }
  }

  if (result.status != RhythmStateStatus::DANGER ||
      result.evidence.dangerDurationMs < params.baseline.dangerHoldTimeMs) {
    return;
  }

  dangerStopLatched = true;
  latchedStopReason = result.evidence.directDangerBandTriggered
      ? "RATIO_EXCEEDS_DANGER_BAND_HOLD"
      : "ABNORMAL_UNRECOVERED_HOLD_TIMEOUT";
  result.shouldStopByDanger = true;
  result.stopReason = latchedStopReason;
  result.status = RhythmStateStatus::STOPPED_BY_DANGER;
  result.reason = latchedStopReason;
}

void RhythmStateJudge::updateRiskAdvisory(const RhythmStateJudgeInput& input) {
  result.advisory = RiskAdvisoryState{};
  result.advisory.reason = "none";

  const bool shouldEvaluateMotion =
      result.evidence.baselineReady &&
      input.sampleValid &&
      input.userPresent &&
      input.topState == TopState::RUNNING &&
      result.status != RhythmStateStatus::STOPPED_BY_DANGER &&
      !result.shouldStopByDanger;
  if (!shouldEvaluateMotion) {
    resetAdvisoryTracking();
    return;
  }

  if (result.formalEventCandidate) {
    advisoryEventAuxSeen = true;
  }

  const float ratio = result.evidence.ratio;
  if (ratio > params.baseline.safeBandRatio) {
    advisoryExcursionActive = true;
    advisoryLastAbnormalDurationMs = result.evidence.abnormalDurationMs;
    advisoryLastDangerDurationMs = result.evidence.dangerDurationMs;
    if (ratio >= params.advisory.advisoryNearDangerBandRatio) {
      advisoryNearDangerSeen = true;
    }
    if (ratio >= advisoryPeakRatio) {
      advisoryPeakRatio = ratio;
    }
  }

  if (ratio <= params.baseline.safeBandRatio) {
    if (!advisoryExcursionActive) {
      return;
    }

    if (advisoryEventAuxSeen) {
      fillAdvisoryState(
          RiskAdvisoryType::EVENT_LIKE_BUT_RECOVERED,
          RiskAdvisoryLevel::WARN,
          "event_candidate_recovered_without_stop",
          input.nowMs,
          advisoryLastAbnormalDurationMs,
          advisoryLastDangerDurationMs);
    } else if (advisoryLastAbnormalDurationMs >= params.advisory.advisoryRecoveryTimeMs) {
      fillAdvisoryState(
          RiskAdvisoryType::RECOVERY_SLOW,
          RiskAdvisoryLevel::WARN,
          "abnormal_recovered_but_too_slow",
          input.nowMs,
          advisoryLastAbnormalDurationMs,
          advisoryLastDangerDurationMs);
    } else if (advisoryNearDangerSeen) {
      fillAdvisoryState(
          RiskAdvisoryType::WEIGHT_DEVIATION_HIGH,
          RiskAdvisoryLevel::WARN,
          "near_danger_band_but_recovered",
          input.nowMs,
          advisoryLastAbnormalDurationMs,
          advisoryLastDangerDurationMs);
    }

    resetAdvisoryTracking();
    return;
  }

  if (ratio >= params.advisory.advisoryNearDangerBandRatio) {
    fillAdvisoryState(
        RiskAdvisoryType::WEIGHT_DEVIATION_HIGH,
        RiskAdvisoryLevel::WARN,
        "ratio_near_danger_band",
        input.nowMs,
        result.evidence.abnormalDurationMs,
        result.evidence.dangerDurationMs);
    return;
  }

  if (ratio >= params.advisory.advisoryBandRatio) {
    fillAdvisoryState(
        RiskAdvisoryType::LOAD_SHIFT_LARGE,
        RiskAdvisoryLevel::INFO,
        "ratio_exceeds_advisory_band",
        input.nowMs,
        result.evidence.abnormalDurationMs,
        result.evidence.dangerDurationMs);
  }
}

void RhythmStateJudge::updateLogDecision(uint32_t nowMs) {
  (void)nowMs;
  result.logShouldEmit = false;
  result.logCause = nullptr;
  result.hasPreviousLoggedStatus = hasLoggedStatus;
  result.previousLoggedStatus = lastLoggedStatus;
  result.previousLoggedReason = lastLoggedReason;

  const char* currentReason = validReasonOrDefault(result.reason);
  const bool reasonChanged =
      !hasLoggedStatus || !sameString(lastLoggedReason, currentReason);

  if (!hasLoggedStatus) {
    result.logShouldEmit = true;
    result.logCause = "initial";
  } else if (result.stateChanged) {
    result.logShouldEmit = true;
    result.logCause = "transition";
  } else if (result.shouldStopByDanger) {
    result.logShouldEmit = true;
    result.logCause = "danger_stop";
  } else if (!result.evidence.baselineReady && reasonChanged) {
    result.logShouldEmit = true;
    result.logCause = "reason_change";
  }

  if (!result.logShouldEmit) {
    return;
  }

  hasLoggedStatus = true;
  lastLoggedStatus = result.status;
  lastLoggedReason = currentReason;
  lastLogMs = nowMs;
}

void RhythmStateJudge::copyEvidenceToResult() {
  if (result.evidence.baselineReady) {
    return;
  }

  result.evidence.baselineWeightKg = 0.0f;
  result.evidence.baselineDistance = 0.0f;
  result.evidence.baselineCapturedAtMs = 0;
  result.evidence.ma12WeightKg = 0.0f;
  result.evidence.ma12Distance = 0.0f;
  result.evidence.deviationKg = 0.0f;
  result.evidence.ratio = 0.0f;
  result.evidence.abnormalDurationMs = 0;
  result.evidence.dangerDurationMs = 0;
  result.evidence.directDangerBandTriggered = false;
  result.evidence.dangerFromUnrecoveredAbnormal = false;
}

const RhythmStateUpdateResult& RhythmStateJudge::update(const RhythmStateJudgeInput& input) {
  const RhythmStateStatus previousStatus = result.status;

  result.previousStatus = previousStatus;
  result.stateChanged = false;
  result.formalEventCandidate = false;
  result.shouldStopByDanger = false;
  result.stopReason = nullptr;
  result.logShouldEmit = false;
  result.logCause = nullptr;
  result.hasPreviousLoggedStatus = false;
  result.previousLoggedStatus = RhythmStateStatus::BASELINE_PENDING;
  result.previousLoggedReason = "baseline_pending";
  result.advisory = RiskAdvisoryState{};
  result.advisory.reason = "none";

  updateFormalFallCandidate(input);
  updatePrimaryState(input);
  updateRiskAdvisory(input);
  result.stateChanged = previousStatus != result.status;
  updateLogDecision(input.nowMs);
  copyEvidenceToResult();
  return result;
}

const RhythmStateUpdateResult& RhythmStateJudge::lastResult() const {
  return result;
}
