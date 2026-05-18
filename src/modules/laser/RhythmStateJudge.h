#pragma once

#include <Arduino.h>
#include "config/RhythmStateConfig.h"
#include "core/Types.h"

// 基线型主判断状态枚举。
// 这些状态只描述“基于 stable_weight + MA12 + ratio 的主判断结果”，
// 不直接拥有波形启停动作，最终动作仍由 SystemStateMachine 统一执行。
enum class RhythmStateStatus : uint8_t {
  BASELINE_PENDING,
  NORMAL,
  ABNORMAL_RECOVERABLE,
  DANGER_CANDIDATE,
  DANGER,
  STOPPED_BY_DANGER
};

const char* rhythmStateName(RhythmStateStatus status);

enum class RiskAdvisoryType : uint8_t {
  NONE,
  LOAD_SHIFT_LARGE,
  WEIGHT_DEVIATION_HIGH,
  RECOVERY_SLOW,
  EVENT_LIKE_BUT_RECOVERED
};

enum class RiskAdvisoryLevel : uint8_t {
  NONE,
  INFO,
  WARN
};

const char* riskAdvisoryTypeName(RiskAdvisoryType type);
const char* riskAdvisoryLevelName(RiskAdvisoryLevel level);

struct RhythmStateJudgeInput {
  uint32_t nowMs = 0;
  float distance = 0.0f;
  float weightKg = 0.0f;
  bool sampleValid = false;
  bool userPresent = false;
  TopState topState = TopState::IDLE;
};

struct RhythmStateEvidence {
  bool baselineReady = false;
  float baselineWeightKg = 0.0f;
  float baselineDistance = 0.0f;
  // ma12WeightKg / ma12Distance：律动期主观察量 MA12。
  float ma12WeightKg = 0.0f;
  float ma12Distance = 0.0f;
  // deviationKg：偏离量，等于 MA12 - stable_weight。
  float deviationKg = 0.0f;
  // ratio：相对偏离比例，等于 |deviation| / stable_weight。
  float ratio = 0.0f;
  // abnormalDurationMs：异常持续时间。
  uint32_t abnormalDurationMs = 0;
  // dangerDurationMs：危险持续时间。
  uint32_t dangerDurationMs = 0;
  uint32_t baselineCapturedAtMs = 0;
  bool directDangerBandTriggered = false;
  bool dangerFromUnrecoveredAbnormal = false;
};

struct RiskAdvisoryState {
  bool active = false;
  bool shouldLog = false;
  RiskAdvisoryType type = RiskAdvisoryType::NONE;
  RiskAdvisoryLevel level = RiskAdvisoryLevel::NONE;
  const char* reason = "none";
  bool eventAuxSeen = false;
  uint32_t triggeredAtMs = 0;
  float peakRatio = 0.0f;
  uint32_t abnormalDurationMs = 0;
  uint32_t dangerDurationMs = 0;
};

struct RhythmStateUpdateResult {
  RhythmStateStatus status = RhythmStateStatus::BASELINE_PENDING;
  const char* reason = "baseline_pending";
  RhythmStateStatus previousStatus = RhythmStateStatus::BASELINE_PENDING;
  bool stateChanged = false;
  bool formalEventCandidate = false;
  // shouldStopByDanger / stopReason：
  // 仅表达“主判断建议进入危险停波”，用于 final action owner 做统一执行。
  bool shouldStopByDanger = false;
  const char* stopReason = nullptr;
  bool logShouldEmit = false;
  const char* logCause = nullptr;
  bool hasPreviousLoggedStatus = false;
  RhythmStateStatus previousLoggedStatus = RhythmStateStatus::BASELINE_PENDING;
  const char* previousLoggedReason = "baseline_pending";
  RhythmStateEvidence evidence{};
  RiskAdvisoryState advisory{};
};

// RhythmStateJudge owns detector-local motion/rhythm evidence only.
// It must not control BLE, waveform output, or formal state-machine actions.
class RhythmStateJudge {
public:
  void configure(const RhythmStateJudgeParams& params);

  // Resets detector-local runtime state after calibration/zero/model changes.
  void reset(const char* reason);

  // Invalid measurements should clear short-lived fall candidates without
  // destroying the last accepted stable baseline.
  void noteInvalidMeasurement();

  // Stable-state generation remains owned by LaserModule; the judge only
  // mirrors the accepted baseline so it can evaluate later motion against it.
  void refreshBaselineFromStable(float stableDistance, float stableWeightKg, uint32_t nowMs);

  const RhythmStateUpdateResult& update(const RhythmStateJudgeInput& input);
  const RhythmStateUpdateResult& lastResult() const;

private:
  static constexpr uint8_t kMaxMovingAverageWindow = 16;

  void clampParams();
  void clearFormalFallCandidate();
  void resetMotionEvaluation();
  void resetAdvisoryTracking();
  void updateFormalFallCandidate(const RhythmStateJudgeInput& input);
  void updatePrimaryState(const RhythmStateJudgeInput& input);
  void updateRiskAdvisory(const RhythmStateJudgeInput& input);
  void updateLogDecision(uint32_t nowMs);
  void copyEvidenceToResult();
  void fillAdvisoryState(
      RiskAdvisoryType type,
      RiskAdvisoryLevel level,
      const char* reason,
      uint32_t nowMs,
      uint32_t abnormalDurationMs,
      uint32_t dangerDurationMs);

private:
  RhythmStateJudgeParams params{};
  RhythmStateUpdateResult result{};

  float maWeightWindow[kMaxMovingAverageWindow]{};
  float maDistanceWindow[kMaxMovingAverageWindow]{};
  uint8_t maHead = 0;
  uint8_t maCount = 0;
  float maWeightSum = 0.0f;
  float maDistanceSum = 0.0f;
  uint32_t abnormalStartedAtMs = 0;
  uint32_t dangerStartedAtMs = 0;
  bool dangerStopLatched = false;
  const char* latchedStopReason = nullptr;

  float lastWeightKg = 0.0f;
  float lastDistance = 0.0f;
  uint32_t lastSampleMs = 0;
  bool fallCandidateActive = false;
  bool fallHoldPending = false;
  float fallCandidateStartWeightKg = 0.0f;
  float fallCandidateStartDistance = 0.0f;
  uint8_t fallConfirmCount = 0;
  uint8_t fallHoldCount = 0;

  bool hasLoggedStatus = false;
  RhythmStateStatus lastLoggedStatus = RhythmStateStatus::BASELINE_PENDING;
  const char* lastLoggedReason = "baseline_pending";
  uint32_t lastLogMs = 0;

  bool advisoryExcursionActive = false;
  bool advisoryEventAuxSeen = false;
  bool advisoryNearDangerSeen = false;
  float advisoryPeakRatio = 0.0f;
  uint32_t advisoryLastAbnormalDurationMs = 0;
  uint32_t advisoryLastDangerDurationMs = 0;
  RiskAdvisoryType lastAdvisoryLoggedType = RiskAdvisoryType::NONE;
  uint32_t lastAdvisoryLogMs = 0;
};
