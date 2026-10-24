#pragma once

#include "modules/laser/BaselineContractDiagnostics.h"
#include "modules/laser/MotionSafetyShadowEvaluator.h"
#include "modules/laser/RhythmStateJudge.h"
#include "modules/laser/RunSummaryCollector.h"

// Owns laser-derived judgment and run-session evidence. It does not perform
// hardware I/O and it never controls the waveform output directly.
class LaserDecisionRuntime final {
public:
  RhythmStateJudge rhythmStateJudge{};
  MotionSafetyShadowEvaluator motionSafetyShadow{};
  RunSummaryCollector runSummaryCollector{};
  BaselineContractDiagnostics baselineContractDiagnostics{};
};
