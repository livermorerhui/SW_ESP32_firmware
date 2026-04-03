#pragma once

#include <Arduino.h>

// Rhythm-state parameters are intentionally separated from GlobalConfig so
// future tuning does not scatter detector thresholds across unrelated modules.
struct RhythmStateResearchParams {
  uint8_t movingAverageWindow = 5;
  uint8_t tailWindow = 10;
  float significantWeightDropThresholdKg = 12.0f;
  float significantDistanceDropThreshold = 1.5f;
  float lowWeightRatio = 0.60f;
  float recoveryRatio = 0.85f;
  float tailLowRatio = 0.75f;
  uint16_t lowWeightMinFrames = 10;
  float dangerDeltaWeightKg = 20.0f;
  float dangerRecoveryMax = 0.75f;
  float dangerDeltaDistance = 2.0f;
  // Transition/reason/threshold logs emit immediately; periodic summaries are
  // now sparse keepalives only.
  uint32_t summaryIntervalMs = 15000UL;
  uint32_t leaveSummaryIntervalMs = 10000UL;
  uint32_t stableLeaveSummaryAfterMs = 30000UL;
  // Once leave/off-platform has been unchanged for this long, a value of 0
  // disables further keepalives so only new diagnostic events continue to log.
  uint32_t stableLeaveSummaryIntervalMs = 0UL;
};

// Formal fall-candidate extraction remains separate from FSM action policy.
struct RhythmStateFallParams {
  float suspectRateThresholdKgPerSec = 25.0f;
  // Low-frequency / high-intensity oscillation can still satisfy two directed
  // samples, so the formal path now requires a third consistent sample.
  uint8_t confirmSamples = 3;
  // Require a net drop, not just repeated oscillation spikes in alternating motion.
  float minimumCumulativeWeightDropKg = 10.0f;
  float minimumCumulativeDistanceDrop = 1.0f;
  // Do not emit on the first deep trough. Require a short follow-through sample
  // to stay materially displaced so rebound-heavy rhythmic motion clears first.
  uint8_t holdSamplesAfterThreshold = 1;
  float sustainDropRatioAfterThreshold = 0.70f;
};

struct RhythmStateBaselineParams {
  // safeBandRatio：安全偏离范围。
  // 含义：MA12 相对 stable_weight 的允许正常偏离上限。
  float safeBandRatio = 0.16f;
  // dangerBandRatio：危险偏离范围阈值。
  // 含义：MA12 相对 stable_weight 超过此比例时，视为明显危险。
  float dangerBandRatio = 0.28f;
  // abnormalRecoveryTimeMs：异常恢复时间窗口。
  // 含义：超过安全范围后，允许恢复回安全范围的最大时间。
  uint32_t abnormalRecoveryTimeMs = 1500UL;
  // dangerHoldTimeMs：危险持续确认时间。
  // 含义：危险状态必须持续成立的最短时间，达到后才自动停波。
  uint32_t dangerHoldTimeMs = 700UL;
};

struct RhythmStateAdvisoryParams {
  // advisoryBandRatio：风险提示偏离范围。
  // 含义：ratio 超过该值时，可判定为明显大动作/大幅载荷转移。
  float advisoryBandRatio = 0.20f;
  // advisoryRecoveryTimeMs：风险提示恢复时间阈值。
  // 含义：异常恢复时间超过该值时，输出恢复较慢提示。
  uint32_t advisoryRecoveryTimeMs = 1800UL;
  // advisoryNearDangerBandRatio：接近危险阈值范围。
  // 含义：ratio 接近 danger_band 但未达到自动停波条件时，输出较高风险提示。
  float advisoryNearDangerBandRatio = 0.24f;
  // repeatThrottleMs：同类风险提示节流时间。
  // 含义：同一 advisory_type 连续出现时，至少间隔该时间再重复打印日志。
  uint32_t repeatThrottleMs = 3000UL;
};

struct RhythmStateJudgeParams {
  RhythmStateResearchParams research{};
  RhythmStateFallParams fall{};
  RhythmStateBaselineParams baseline{};
  RhythmStateAdvisoryParams advisory{};
};
