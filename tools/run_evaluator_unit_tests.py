#!/usr/bin/env python3
"""Host-side unit tests for pure firmware evaluators.

The firmware project is Arduino/ESP32 based, but these evaluator classes are
pure C++ and should stay testable without hardware. This script compiles only
the evaluator sources with a tiny Arduino.h stub and runs focused assertions.
"""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


ARDUINO_STUB = r"""
#pragma once
#include <cstdint>
#include <cmath>
#include <cstddef>

#ifndef NAN
#define NAN __builtin_nanf("")
#endif

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;

#define I2S_NUM_0 0
"""


TEST_MAIN = r"""
#include <cassert>
#include <cstring>
#include <iostream>

#include "modules/laser/BaselineEvidenceEvaluator.h"
#include "modules/laser/PresenceContractEvaluator.h"

namespace {

void expect_reason(const char* actual, const char* expected) {
  assert(actual != nullptr);
  assert(std::strcmp(actual, expected) == 0);
}

void test_presence_enter_exit() {
  LaserPresenceThresholdConfig config{};
  config.enterThresholdKg = 5.0f;
  config.exitThresholdKg = 3.0f;
  config.confirmSamples = 2;

  PresenceContractInput input{};
  input.weightKg = 6.0f;
  input.currentUserPresent = false;
  input.enterConfirmCount = 1;
  PresenceContractResult result = PresenceContractEvaluator::evaluate(config, input);
  assert(!result.nextUserPresent);
  assert(!result.changed);
  expect_reason(result.reason, "enter_pending");

  input.enterConfirmCount = 2;
  result = PresenceContractEvaluator::evaluate(config, input);
  assert(result.nextUserPresent);
  assert(result.changed);
  expect_reason(result.reason, "enter_confirmed");

  input.currentUserPresent = true;
  input.enterConfirmCount = 3;
  result = PresenceContractEvaluator::evaluate(config, input);
  assert(result.nextUserPresent);
  assert(!result.changed);
  expect_reason(result.reason, "present_hold");

  input.weightKg = 2.0f;
  input.exitConfirmCount = 1;
  result = PresenceContractEvaluator::evaluate(config, input);
  assert(result.nextUserPresent);
  assert(!result.changed);
  expect_reason(result.reason, "exit_pending");

  input.exitConfirmCount = 2;
  result = PresenceContractEvaluator::evaluate(config, input);
  assert(!result.nextUserPresent);
  assert(result.changed);
  expect_reason(result.reason, "exit_confirmed");
}

void test_presence_deadband_and_zero_confirm() {
  LaserPresenceThresholdConfig config{};
  config.enterThresholdKg = 5.0f;
  config.exitThresholdKg = 3.0f;
  config.confirmSamples = 0;

  PresenceContractInput input{};
  input.weightKg = 4.0f;
  input.currentUserPresent = true;
  PresenceContractResult result = PresenceContractEvaluator::evaluate(config, input);
  assert(result.nextUserPresent);
  assert(!result.changed);
  expect_reason(result.reason, "threshold_deadband");

  input.weightKg = 5.0f;
  input.currentUserPresent = false;
  input.enterConfirmCount = 1;
  result = PresenceContractEvaluator::evaluate(config, input);
  assert(result.nextUserPresent);
  assert(result.changed);
  expect_reason(result.reason, "enter_confirmed");
}

void test_baseline_window_hold() {
  LaserStableThresholdConfig config{};
  config.enterStdDevKg = 0.20f;
  config.enterRangeKg = 0.40f;
  config.enterDriftKg = 0.16f;
  config.enterConfirmWindows = 2;

  BaselineEvidenceInput input{};
  input.currentStableConfirmCount = 2;
  input.metrics.valid = true;
  input.metrics.stddev = 0.21f;
  input.metrics.range = 0.30f;
  input.metrics.drift = 0.10f;

  BaselineEvidenceResult result = BaselineEvidenceEvaluator::evaluate(config, input);
  assert(result.windowReady);
  assert(!result.stableEligible);
  assert(!result.baselineEligible);
  assert(!result.stddevOk);
  assert(result.rangeOk);
  assert(result.driftOk);
  assert(result.nextStableConfirmCount == 0);
  expect_reason(result.reason, "stable_window_hold");
}

void test_baseline_confirm_and_latch() {
  LaserStableThresholdConfig config{};
  config.enterStdDevKg = 0.20f;
  config.enterRangeKg = 0.40f;
  config.enterDriftKg = 0.16f;
  config.enterConfirmWindows = 2;

  BaselineEvidenceInput input{};
  input.metrics.valid = true;
  input.metrics.stddev = 0.08f;
  input.metrics.range = 0.20f;
  input.metrics.drift = 0.05f;
  input.currentStableConfirmCount = 0;

  BaselineEvidenceResult result = BaselineEvidenceEvaluator::evaluate(config, input);
  assert(result.windowReady);
  assert(result.stableEligible);
  assert(!result.baselineEligible);
  assert(result.nextStableConfirmCount == 1);
  expect_reason(result.reason, "stable_confirm_pending");

  input.currentStableConfirmCount = 1;
  result = BaselineEvidenceEvaluator::evaluate(config, input);
  assert(result.stableEligible);
  assert(result.baselineEligible);
  assert(result.nextStableConfirmCount == 2);
  expect_reason(result.reason, "baseline_eligible");
}

void test_baseline_invalid_and_saturation() {
  LaserStableThresholdConfig config{};
  config.enterStdDevKg = 0.20f;
  config.enterRangeKg = 0.40f;
  config.enterDriftKg = 0.16f;
  config.enterConfirmWindows = 2;

  BaselineEvidenceInput input{};
  input.metrics.valid = false;
  input.currentStableConfirmCount = 7;
  BaselineEvidenceResult result = BaselineEvidenceEvaluator::evaluate(config, input);
  assert(!result.windowReady);
  assert(!result.stableEligible);
  assert(!result.baselineEligible);
  assert(result.nextStableConfirmCount == 7);
  expect_reason(result.reason, "window_not_ready");

  input.metrics.valid = true;
  input.metrics.stddev = 0.08f;
  input.metrics.range = 0.20f;
  input.metrics.drift = 0.05f;
  input.currentStableConfirmCount = 0xFF;
  result = BaselineEvidenceEvaluator::evaluate(config, input);
  assert(result.stableEligible);
  assert(result.baselineEligible);
  assert(result.nextStableConfirmCount == 0xFF);
  expect_reason(result.reason, "baseline_eligible");
}

}  // namespace

int main() {
  test_presence_enter_exit();
  test_presence_deadband_and_zero_confirm();
  test_baseline_window_hold();
  test_baseline_confirm_and_latch();
  test_baseline_invalid_and_saturation();
  std::cout << "evaluator unit tests passed\n";
  return 0;
}
"""


def run() -> None:
  with tempfile.TemporaryDirectory(prefix="sw_eval_tests_") as temp_dir:
    temp = Path(temp_dir)
    (temp / "Arduino.h").write_text(ARDUINO_STUB, encoding="utf-8")
    main_cpp = temp / "evaluator_tests.cpp"
    main_cpp.write_text(TEST_MAIN, encoding="utf-8")
    binary = temp / "evaluator_tests"

    cmd = [
      "g++",
      "-std=c++17",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-I",
      str(temp),
      "-I",
      str(ROOT / "src"),
      str(main_cpp),
      str(ROOT / "src/modules/laser/PresenceContractEvaluator.cpp"),
      str(ROOT / "src/modules/laser/BaselineEvidenceEvaluator.cpp"),
      "-o",
      str(binary),
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)
    subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
  run()
