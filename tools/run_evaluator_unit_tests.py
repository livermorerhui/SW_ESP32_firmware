#!/usr/bin/env python3
"""Host-side unit tests for pure firmware evaluators.

The firmware project is Arduino/ESP32 based, but these evaluator classes are
pure C++ and should stay testable without hardware. This script compiles only
the evaluator sources with a tiny Arduino.h stub and runs focused assertions.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GOLDEN_FRAME_FIXTURE = ROOT / "docs/protocol/golden_frames/sonicwave_ble_frames_v1.jsonl"


ARDUINO_STUB = r"""
#pragma once
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <climits>
#include <string>
#include <algorithm>
#include <cctype>

#ifndef NAN
#define NAN __builtin_nanf("")
#endif

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;

#define I2S_NUM_0 0

class String {
public:
  String() = default;
  String(const char* value) : data(value ? value : "") {}
  String(const std::string& value) : data(value) {}
  String(char value) : data(1, value) {}
  String(int value) : data(std::to_string(value)) {}
  String(unsigned value) : data(std::to_string(value)) {}
  String(uint16_t value) : data(std::to_string(value)) {}
  String(unsigned long value) : data(std::to_string(value)) {}
  String(float value, unsigned int decimals) {
    char buffer[48]{};
    std::snprintf(buffer, sizeof(buffer), "%.*f", static_cast<int>(decimals), value);
    data = buffer;
  }

  size_t length() const { return data.length(); }
  const char* c_str() const { return data.c_str(); }
  void reserve(size_t size) { data.reserve(size); }

  void trim() {
    size_t first = 0;
    while (first < data.size() && std::isspace(static_cast<unsigned char>(data[first]))) {
      ++first;
    }
    size_t last = data.size();
    while (last > first && std::isspace(static_cast<unsigned char>(data[last - 1]))) {
      --last;
    }
    data = data.substr(first, last - first);
  }

  bool equalsIgnoreCase(const char* other) const {
    return equalsIgnoreCase(String(other));
  }

  bool equalsIgnoreCase(const String& other) const {
    if (data.size() != other.data.size()) return false;
    for (size_t i = 0; i < data.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(data[i])) !=
          std::tolower(static_cast<unsigned char>(other.data[i]))) {
        return false;
      }
    }
    return true;
  }

  bool startsWith(const char* prefix) const {
    const std::string target(prefix ? prefix : "");
    return data.rfind(target, 0) == 0;
  }

  int indexOf(char needle, int from = 0) const {
    if (from < 0) from = 0;
    const size_t found = data.find(needle, static_cast<size_t>(from));
    return found == std::string::npos ? -1 : static_cast<int>(found);
  }

  int indexOf(const char* needle, int from = 0) const {
    if (from < 0) from = 0;
    const size_t found = data.find(needle ? needle : "", static_cast<size_t>(from));
    return found == std::string::npos ? -1 : static_cast<int>(found);
  }

  int indexOf(const String& needle, int from = 0) const {
    return indexOf(needle.c_str(), from);
  }

  String substring(int begin) const {
    if (begin < 0) begin = 0;
    if (static_cast<size_t>(begin) >= data.size()) return String("");
    return String(data.substr(static_cast<size_t>(begin)));
  }

  String substring(int begin, int end) const {
    if (begin < 0) begin = 0;
    if (end < begin) end = begin;
    const size_t start = std::min(static_cast<size_t>(begin), data.size());
    const size_t stop = std::min(static_cast<size_t>(end), data.size());
    return String(data.substr(start, stop - start));
  }

  bool operator==(const char* other) const { return data == (other ? other : ""); }
  bool operator!=(const char* other) const { return !(*this == other); }

  String& operator=(const char* value) {
    data = value ? value : "";
    return *this;
  }

  String& operator+=(const String& other) {
    data += other.data;
    return *this;
  }

  String& operator+=(const char* value) {
    data += value ? value : "";
    return *this;
  }

  String& operator+=(char value) {
    data += value;
    return *this;
  }

  String& operator+=(int value) {
    data += std::to_string(value);
    return *this;
  }

  String& operator+=(unsigned long value) {
    data += std::to_string(value);
    return *this;
  }

private:
  std::string data;
};

inline String operator+(const char* lhs, const String& rhs) {
  String result(lhs);
  result += rhs;
  return result;
}

struct SerialStub {
  void printf(const char*, ...) {}
};

static SerialStub Serial;
"""


PREFERENCES_STUB = r"""
#pragma once

class Preferences {};
"""


TEST_MAIN = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "core/ProtocolCodec.h"
#include "GoldenFrames.h"
#include "HubAckBuilder.h"
#include "modules/laser/BaselineEvidenceEvaluator.h"
#include "modules/laser/MeasurementAvailabilityProbePolicy.h"
#include "modules/laser/MeasurementHealthStateMachine.h"
#include "modules/laser/PresenceContractEvaluator.h"
#include "modules/laser/StopOutcomeSummaryEvaluator.h"
#include "core/RuntimeProtectionPolicy.h"
#include "core/SafetyActionContractEvaluator.h"

namespace {

void expect_reason(const char* actual, const char* expected) {
  assert(actual != nullptr);
  assert(std::strcmp(actual, expected) == 0);
}

void expect_contains(const String& actual, const char* expected) {
  assert(actual.indexOf(expected) >= 0);
}

void expect_not_contains(const String& actual, const char* unexpected) {
  assert(actual.indexOf(unexpected) < 0);
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

void test_fall_stop_action_decision() {
  FallStopActionDecision result = SafetyActionContractEvaluator::decideFallSuspected(true, true);
  assert(result.stopCandidateDetected);
  assert(result.shouldExecuteStop);
  assert(!result.stopSuppressedBySwitch);
  assert(result.fallStopEnabled);
  assert(result.stopReason == FaultCode::FALL_SUSPECTED);
  assert(result.safetySignal == SafetySignalKind::ABNORMAL_STOP);
  expect_reason(result.detail, "fall_stop_active");

  result = SafetyActionContractEvaluator::decideFallSuspected(false, true);
  assert(result.stopCandidateDetected);
  assert(!result.shouldExecuteStop);
  assert(result.stopSuppressedBySwitch);
  assert(!result.fallStopEnabled);
  assert(result.safetySignal == SafetySignalKind::WARNING_ONLY);
  expect_reason(result.detail, "fall_stop_disabled");

  result = SafetyActionContractEvaluator::decideFallSuspected(true, false);
  assert(result.shouldExecuteStop);
  assert(!result.stopSuppressedBySwitch);
  assert(result.safetySignal == SafetySignalKind::RECOVERABLE_PAUSE);
  expect_reason(result.detail, "fall_pause_override");
}

void test_stop_reason_and_source_fallbacks() {
  expect_reason(
      SafetyActionContractEvaluator::resolveStopReasonText(
          "UPSTREAM_REASON",
          FaultCode::FALL_SUSPECTED,
          "FALLBACK_REASON"),
      "UPSTREAM_REASON");
  expect_reason(
      SafetyActionContractEvaluator::resolveStopReasonText(
          nullptr,
          FaultCode::FALL_SUSPECTED,
          "FALLBACK_REASON"),
      "FALLBACK_REASON");
  expect_reason(
      SafetyActionContractEvaluator::resolveStopReasonText(
          "",
          FaultCode::FALL_SUSPECTED,
          nullptr),
      "FALL_SUSPECTED");
  expect_reason(
      SafetyActionContractEvaluator::resolveStopReasonText(
          nullptr,
          FaultCode::NONE,
          nullptr),
      "MANUAL_STOP");

  assert(SafetyActionContractEvaluator::resolveStopSource(
      VerificationStopSource::BASELINE_MAIN_LOGIC,
      VerificationStopSource::FORMAL_SAFETY_OTHER) ==
      VerificationStopSource::BASELINE_MAIN_LOGIC);
  assert(SafetyActionContractEvaluator::resolveStopSource(
      VerificationStopSource::NONE,
      VerificationStopSource::FORMAL_SAFETY_OTHER) ==
      VerificationStopSource::FORMAL_SAFETY_OTHER);
  assert(SafetyActionContractEvaluator::resolveStopSource(
      VerificationStopSource::NONE,
      VerificationStopSource::NONE) ==
      VerificationStopSource::USER_MANUAL_OTHER);
}

void test_stop_outcome_summary_evaluator() {
  StopOutcomeSummaryDecision result = StopOutcomeSummaryEvaluator::evaluate(
      FaultCode::NONE,
      SafetySignalKind::NONE,
      "MANUAL_STOP",
      "USER_MANUAL_OTHER");
  assert(result.kind == StopOutcomeSummaryKind::STOP_SUMMARY);
  expect_reason(result.result, "NORMAL");
  expect_reason(result.stopReasonText, "MANUAL_STOP");
  expect_reason(result.stopSourceText, "USER_MANUAL_OTHER");

  result = StopOutcomeSummaryEvaluator::evaluate(
      FaultCode::USER_LEFT_PLATFORM,
      SafetySignalKind::RECOVERABLE_PAUSE,
      "USER_LEFT_PLATFORM",
      "FORMAL_SAFETY_OTHER");
  assert(result.kind == StopOutcomeSummaryKind::STOP_SUMMARY);
  expect_reason(result.result, "RECOVERABLE_PAUSE");
  expect_reason(result.stopReasonText, "USER_LEFT_PLATFORM");
  expect_reason(result.stopSourceText, "FORMAL_SAFETY_OTHER");

  result = StopOutcomeSummaryEvaluator::evaluate(
      FaultCode::FALL_SUSPECTED,
      SafetySignalKind::ABNORMAL_STOP,
      "FALL_SUSPECTED",
      "BASELINE_MAIN_LOGIC");
  assert(result.kind == StopOutcomeSummaryKind::ABORT_SUMMARY);
  expect_reason(result.result, "ABNORMAL_STOP");
  expect_reason(result.stopReasonText, "FALL_SUSPECTED");
  expect_reason(result.stopSourceText, "BASELINE_MAIN_LOGIC");

  result = StopOutcomeSummaryEvaluator::evaluate(
      FaultCode::FALL_SUSPECTED,
      SafetySignalKind::WARNING_ONLY,
      nullptr,
      nullptr);
  assert(result.kind == StopOutcomeSummaryKind::STOP_SUMMARY);
  expect_reason(result.result, "WARNING_ONLY");
  expect_reason(result.stopReasonText, "FALL_SUSPECTED");
  expect_reason(result.stopSourceText, "NONE");
}

void test_measurement_health_startup_and_ready() {
  MeasurementHealthConfig config{};
  config.startupGraceMs = 1000;
  config.transientGraceMs = 300;
  config.runtimeFaultGraceMs = 100;
  config.readySuccessSamples = 2;
  config.startupFaultFailureSamples = 3;
  config.runtimeFaultFailureSamples = 2;

  MeasurementHealthStateMachine machine(config);
  MeasurementHealthTransition transition = machine.reset(10, true);
  assert(machine.state() == MeasurementHealthState::BOOTING);
  assert(!transition.changed);
  assert(!machine.startupResolved());

  transition = machine.observe(100, true, false, false);
  assert(machine.state() == MeasurementHealthState::PROBING);
  assert(transition.changed);
  assert(machine.failureSamples() == 1);
  assert(!machine.startupResolved());

  transition = machine.observe(200, true, true, true);
  assert(machine.state() == MeasurementHealthState::PROBING);
  assert(!transition.changed);
  assert(machine.successSamples() == 1);

  transition = machine.observe(220, true, true, true);
  assert(machine.state() == MeasurementHealthState::READY);
  assert(transition.changed);
  assert(machine.everReady());
  assert(machine.startupResolved());
}

void test_measurement_health_startup_fault_after_grace() {
  MeasurementHealthConfig config{};
  config.startupGraceMs = 1000;
  config.startupFaultFailureSamples = 3;

  MeasurementHealthStateMachine machine(config);
  machine.reset(0, true);

  machine.observe(100, true, false, false);
  assert(machine.state() == MeasurementHealthState::PROBING);
  MeasurementHealthTransition transition = machine.observe(1200, true, false, false);
  assert(machine.state() == MeasurementHealthState::FAULT);
  assert(transition.changed);
  assert(machine.faultConfirmed());
  assert(machine.startupResolved());
}

void test_measurement_health_runtime_fault_and_recovery() {
  MeasurementHealthConfig config{};
  config.startupGraceMs = 1000;
  config.runtimeFaultGraceMs = 100;
  config.readySuccessSamples = 2;
  config.runtimeFaultFailureSamples = 2;

  MeasurementHealthStateMachine machine(config);
  machine.reset(0, true);
  machine.observe(10, true, true, true);
  machine.observe(20, true, true, true);
  assert(machine.state() == MeasurementHealthState::READY);

  MeasurementHealthTransition transition = machine.observe(40, true, false, false);
  assert(machine.state() == MeasurementHealthState::TRANSIENT_UNAVAILABLE);
  assert(transition.changed);

  transition = machine.observe(150, true, false, false);
  assert(machine.state() == MeasurementHealthState::FAULT);
  assert(transition.changed);

  transition = machine.observe(170, true, true, true);
  assert(machine.state() == MeasurementHealthState::FAULT);
  assert(!transition.changed);
  transition = machine.observe(190, true, true, true);
  assert(machine.state() == MeasurementHealthState::READY);
  assert(transition.changed);
}

void test_measurement_health_no_laser_is_fault() {
  MeasurementHealthStateMachine machine;
  MeasurementHealthTransition transition = machine.reset(5, false);
  assert(machine.state() == MeasurementHealthState::FAULT);
  assert(transition.changed);
  assert(machine.faultConfirmed());
  assert(machine.startupResolved());

  transition = machine.observe(20, false, true, true);
  assert(machine.state() == MeasurementHealthState::FAULT);
  assert(!transition.changed);
}

void test_measurement_probe_policy_waits_for_confirmed_fault() {
  MeasurementAvailabilityProbeConfig config{};
  config.timeoutCode = 0xE2;
  config.openAfterConsecutiveTimeouts = 2;
  config.probeIntervalMs = 5000;
  MeasurementAvailabilityProbePolicy policy(config);

  MeasurementProbeObservation observation =
      policy.afterRead(100, true, false, false, 0xE2, TopState::RUNNING);
  assert(observation.event == MeasurementProbeEvent::NONE);
  assert(policy.state() == MeasurementProbeState::CLOSED);
  assert(policy.consecutiveTimeouts() == 0);

  observation = policy.afterRead(200, false, true, false, 0xE2, TopState::RUNNING);
  assert(observation.event == MeasurementProbeEvent::NONE);
  assert(policy.state() == MeasurementProbeState::CLOSED);
  assert(policy.consecutiveTimeouts() == 0);
}

void test_measurement_probe_policy_opens_after_timeout_threshold() {
  MeasurementAvailabilityProbeConfig config{};
  config.timeoutCode = 0xE2;
  config.openAfterConsecutiveTimeouts = 2;
  config.probeIntervalMs = 5000;
  MeasurementAvailabilityProbePolicy policy(config);

  MeasurementProbeObservation observation =
      policy.afterRead(100, true, true, false, 0xE2, TopState::RUNNING);
  assert(observation.event == MeasurementProbeEvent::NONE);
  assert(policy.state() == MeasurementProbeState::CLOSED);
  assert(policy.consecutiveTimeouts() == 1);

  observation = policy.afterRead(200, true, true, false, 0xE2, TopState::RUNNING);
  assert(observation.event == MeasurementProbeEvent::OPEN);
  assert(policy.state() == MeasurementProbeState::OPEN_UNAVAILABLE);
  assert(policy.nextProbeAtMs() == 5200);
  assert(observation.consecutiveTimeouts == 2);
}

void test_measurement_probe_policy_skips_until_probe_due() {
  MeasurementAvailabilityProbeConfig config{};
  config.timeoutCode = 0xE2;
  config.openAfterConsecutiveTimeouts = 2;
  config.probeIntervalMs = 5000;
  config.skipLogIntervalMs = 1000;
  MeasurementAvailabilityProbePolicy policy(config);

  policy.afterRead(100, true, true, false, 0xE2, TopState::RUNNING);
  policy.afterRead(200, true, true, false, 0xE2, TopState::RUNNING);

  MeasurementProbeDecision decision = policy.beforeRead(300, true, true, TopState::RUNNING);
  assert(!decision.shouldRead);
  assert(decision.event == MeasurementProbeEvent::SKIP);
  assert(decision.nextProbeInMs == 4900);

  decision = policy.beforeRead(800, true, true, TopState::RUNNING);
  assert(!decision.shouldRead);
  assert(decision.event == MeasurementProbeEvent::NONE);

  decision = policy.beforeRead(5300, true, true, TopState::RUNNING);
  assert(decision.shouldRead);
  assert(decision.event == MeasurementProbeEvent::PROBE);
}

void test_measurement_probe_policy_probe_failure_and_recovery() {
  MeasurementAvailabilityProbeConfig config{};
  config.timeoutCode = 0xE2;
  config.openAfterConsecutiveTimeouts = 2;
  config.probeIntervalMs = 5000;
  MeasurementAvailabilityProbePolicy policy(config);

  policy.afterRead(100, true, true, false, 0xE2, TopState::RUNNING);
  policy.afterRead(200, true, true, false, 0xE2, TopState::RUNNING);

  MeasurementProbeObservation observation =
      policy.afterRead(5300, true, true, false, 0xE2, TopState::RUNNING);
  assert(observation.event == MeasurementProbeEvent::STILL_UNAVAILABLE);
  assert(policy.state() == MeasurementProbeState::OPEN_UNAVAILABLE);
  assert(policy.nextProbeAtMs() == 10300);

  observation = policy.afterRead(10400, true, true, true, 0x00, TopState::RUNNING);
  assert(observation.event == MeasurementProbeEvent::RECOVERED);
  assert(policy.state() == MeasurementProbeState::CLOSED);
  assert(policy.consecutiveTimeouts() == 0);
}

void test_measurement_probe_policy_open_non_timeout_failure_stays_low_frequency() {
  MeasurementAvailabilityProbeConfig config{};
  config.timeoutCode = 0xE2;
  config.openAfterConsecutiveTimeouts = 2;
  config.probeIntervalMs = 5000;
  MeasurementAvailabilityProbePolicy policy(config);

  policy.afterRead(100, true, true, false, 0xE2, TopState::RUNNING);
  policy.afterRead(200, true, true, false, 0xE2, TopState::RUNNING);

  MeasurementProbeObservation observation =
      policy.afterRead(5300, true, true, false, 0xE1, TopState::RUNNING);
  assert(observation.event == MeasurementProbeEvent::STILL_UNAVAILABLE);
  assert(policy.state() == MeasurementProbeState::OPEN_UNAVAILABLE);
  assert(policy.nextProbeAtMs() == 10300);

  MeasurementProbeDecision decision = policy.beforeRead(5400, true, true, TopState::RUNNING);
  assert(!decision.shouldRead);
  assert(decision.nextProbeInMs == 4900);
}

void test_measurement_probe_policy_ignores_non_timeout_and_resets_when_ineligible() {
  MeasurementAvailabilityProbeConfig config{};
  config.timeoutCode = 0xE2;
  config.openAfterConsecutiveTimeouts = 2;
  config.probeIntervalMs = 5000;
  MeasurementAvailabilityProbePolicy policy(config);

  MeasurementProbeObservation observation =
      policy.afterRead(100, true, true, false, 0xE1, TopState::RUNNING);
  assert(observation.event == MeasurementProbeEvent::NONE);
  assert(policy.state() == MeasurementProbeState::CLOSED);
  assert(policy.consecutiveTimeouts() == 0);

  policy.afterRead(200, true, true, false, 0xE2, TopState::RUNNING);
  policy.afterRead(300, true, true, false, 0xE2, TopState::RUNNING);
  assert(policy.state() == MeasurementProbeState::OPEN_UNAVAILABLE);

  MeasurementProbeDecision decision = policy.beforeRead(400, false, true, TopState::RUNNING);
  assert(decision.event == MeasurementProbeEvent::RESET);
  assert(policy.state() == MeasurementProbeState::CLOSED);
  assert(policy.consecutiveTimeouts() == 0);
}

void test_degraded_start_policy_profiles() {
  DegradedStartPolicyInput input{};
  input.laserConfiguredInstalled = false;
  input.measurementFaultConfirmed = false;
  input.degradedStartAuthorized = false;
  input.runtimeReady = false;
  input.startReady = false;

  DegradedStartPolicyDecision decision =
      RuntimeProtectionPolicy::evaluateDegradedStart(input);
  assert(decision.noLaserStartBypassActive);
  assert(!decision.degradedStartAvailable);
  assert(!decision.degradedStartEnabled);
  assert(decision.effectiveRuntimeReady);
  assert(decision.effectiveStartReady);
  assert(decision.protectionDegraded);

  input.laserConfiguredInstalled = true;
  input.measurementFaultConfirmed = false;
  input.runtimeReady = true;
  input.startReady = false;
  decision = RuntimeProtectionPolicy::evaluateDegradedStart(input);
  assert(!decision.noLaserStartBypassActive);
  assert(!decision.degradedStartAvailable);
  assert(!decision.degradedStartEnabled);
  assert(decision.effectiveRuntimeReady);
  assert(!decision.effectiveStartReady);
  assert(!decision.protectionDegraded);

  input.measurementFaultConfirmed = true;
  input.degradedStartAuthorized = false;
  input.runtimeReady = false;
  input.startReady = false;
  decision = RuntimeProtectionPolicy::evaluateDegradedStart(input);
  assert(decision.degradedStartAvailable);
  assert(!decision.degradedStartEnabled);
  assert(!decision.effectiveRuntimeReady);
  assert(!decision.effectiveStartReady);
  assert(decision.protectionDegraded);

  input.degradedStartAuthorized = true;
  decision = RuntimeProtectionPolicy::evaluateDegradedStart(input);
  assert(decision.degradedStartAvailable);
  assert(decision.degradedStartEnabled);
  assert(decision.effectiveRuntimeReady);
  assert(decision.effectiveStartReady);
  assert(decision.protectionDegraded);
}

void test_user_left_policy_actions() {
  UserLeftProtectionInput input{};
  input.topState = TopState::IDLE;
  input.laserConfiguredInstalled = true;
  input.startReady = true;
  input.leaveStopEnabled = true;
  input.recoverablePausePolicy = true;

  UserLeftProtectionDecision decision = RuntimeProtectionPolicy::decideUserLeft(input);
  assert(decision.action == UserLeftProtectionAction::NOT_ELIGIBLE);
  assert(!decision.eligible);
  assert(decision.safetySignal == SafetySignalKind::NONE);
  expect_reason(decision.suppressReason, "not_running");

  input.topState = TopState::RUNNING;
  input.startReady = false;
  decision = RuntimeProtectionPolicy::decideUserLeft(input);
  assert(decision.action == UserLeftProtectionAction::NOT_ELIGIBLE);
  assert(!decision.eligible);
  expect_reason(decision.suppressReason, "baseline_not_ready");

  input.startReady = true;
  input.leaveStopEnabled = false;
  decision = RuntimeProtectionPolicy::decideUserLeft(input);
  assert(decision.action == UserLeftProtectionAction::WARNING_ONLY);
  assert(decision.eligible);
  assert(decision.safetySignal == SafetySignalKind::WARNING_ONLY);

  input.leaveStopEnabled = true;
  input.recoverablePausePolicy = true;
  decision = RuntimeProtectionPolicy::decideUserLeft(input);
  assert(decision.action == UserLeftProtectionAction::RECOVERABLE_PAUSE);
  assert(decision.eligible);
  assert(decision.safetySignal == SafetySignalKind::RECOVERABLE_PAUSE);

  input.recoverablePausePolicy = false;
  decision = RuntimeProtectionPolicy::decideUserLeft(input);
  assert(decision.action == UserLeftProtectionAction::BLOCKING_FAULT);
  assert(decision.eligible);
  assert(decision.safetySignal == SafetySignalKind::ABNORMAL_STOP);
}

void test_protocol_parse_core_commands() {
  Command command{};
  String error;

  assert(ProtocolCodec::parseCommand(" CAP? ", command, error));
  assert(command.type == CmdType::CAP_QUERY);

  assert(ProtocolCodec::isSnapshotQuery(" SNAPSHOT?\r\n"));

  assert(ProtocolCodec::parseCommand("WAVE:SET f=40.5,i=80", command, error));
  assert(command.type == CmdType::WAVE_SET);
  assert(command.wave.freqHz > 40.49f && command.wave.freqHz < 40.51f);
  assert(command.wave.intensity == 80);

  assert(ProtocolCodec::parseCommand("WAVE:SET freq=12.25 amp=30", command, error));
  assert(command.type == CmdType::WAVE_SET);
  assert(command.wave.freqHz > 12.24f && command.wave.freqHz < 12.26f);
  assert(command.wave.intensity == 30);

  assert(ProtocolCodec::parseCommand("WAVE:START", command, error));
  assert(command.type == CmdType::WAVE_START);

  assert(ProtocolCodec::parseCommand("WAVE:STOP", command, error));
  assert(command.type == CmdType::WAVE_STOP);

  assert(!ProtocolCodec::parseCommand("WAVE:SET f=60,i=80", command, error));
  expect_reason(error.c_str(), "INVALID_PARAM");
}

void test_protocol_parse_config_and_safety_commands() {
  Command command{};
  String error;

  assert(ProtocolCodec::parseCommand(
      "DEVICE:SET_CONFIG platform_model=PLUS,laser_installed=1",
      command,
      error));
  assert(command.type == CmdType::DEVICE_SET_CONFIG);
  assert(command.deviceConfig.platformModel == PlatformModel::PLUS);
  assert(command.deviceConfig.laserInstalled);

  assert(ProtocolCodec::parseCommand("DEBUG:DEGRADED_START enabled=true", command, error));
  assert(command.type == CmdType::DEGRADED_START_SET);
  assert(command.degradedStart.enabled);

  assert(ProtocolCodec::parseCommand("SAFETY:LEAVE_PROTECTION enabled=off", command, error));
  assert(command.type == CmdType::LEAVE_PROTECTION_SET);
  assert(!command.leaveProtection.enabled);

  assert(ProtocolCodec::parseCommand("DEBUG:FALL_STOP mode=disabled", command, error));
  assert(command.type == CmdType::FALL_STOP_SET);
  assert(!command.fallStop.enabled);

  assert(!ProtocolCodec::parseCommand("DEBUG:DEGRADED_START enabled=maybe", command, error));
  expect_reason(error.c_str(), "INVALID_PARAM");
}

void test_protocol_parse_legacy_commands() {
  Command command{};
  String error;

  assert(ProtocolCodec::parseCommand("F:40,I:90,E:1", command, error));
  assert(command.type == CmdType::LEGACY_FIE);
  assert(command.wave.freqHz > 39.99f && command.wave.freqHz < 40.01f);
  assert(command.wave.intensity == 90);
  assert(command.wave.hasEnable);
  assert(command.wave.enable);

  assert(ProtocolCodec::parseCommand("I:50", command, error));
  assert(command.type == CmdType::LEGACY_FIE);
  assert(command.wave.freqHz == -1);
  assert(command.wave.intensity == 50);
  assert(!command.wave.hasEnable);

  assert(ProtocolCodec::parseCommand("E:0", command, error));
  assert(command.type == CmdType::LEGACY_FIE);
  assert(command.wave.freqHz == -1);
  assert(command.wave.intensity == -1);
  assert(command.wave.hasEnable);
  assert(!command.wave.enable);
}

void test_protocol_encode_snapshot_contract() {
  PlatformSnapshot snapshot{};
  snapshot.topState = TopState::RUNNING;
  snapshot.startReady = true;
  snapshot.laserAvailable = false;
  snapshot.measurementHealth = MeasurementHealthState::FAULT;
  snapshot.degradedStartAvailable = true;
  snapshot.degradedStartEnabled = true;
  snapshot.leaveStopEnabled = false;

  const String encoded = ProtocolCodec::encodeSnapshot(snapshot);
  expect_contains(encoded, "SNAPSHOT:");
  expect_contains(encoded, "top_state=RUNNING");
  expect_contains(encoded, "start_ready=1");
  expect_contains(encoded, "laser_available=0");
  expect_contains(encoded, "measurement_health=FAULT");
  expect_contains(encoded, "degraded_start_available=1");
  expect_contains(encoded, "degraded_start_enabled=1");
  expect_contains(encoded, "leave_stop_enabled=0");
  assert(encoded.length() + 1 <= ProtocolCodec::kConnectSnapshotPayloadBudgetBytes);
  expect_not_contains(encoded, "platform_model=");
  expect_not_contains(encoded, "laser_installed=");
  expect_not_contains(encoded, "runtime_ready=");
  expect_not_contains(encoded, "baseline_ready=");
}

void test_protocol_snapshot_contract_spec_required_slim_fields() {
  PlatformSnapshot snapshot{};
  snapshot.topState = TopState::FAULT_STOP;
  snapshot.startReady = false;
  snapshot.laserAvailable = false;
  snapshot.measurementHealth = MeasurementHealthState::TRANSIENT_UNAVAILABLE;
  snapshot.degradedStartAvailable = true;
  snapshot.degradedStartEnabled = false;
  snapshot.leaveStopEnabled = true;

  const String encoded = ProtocolCodec::encodeSnapshot(snapshot);
  expect_contains(encoded, "SNAPSHOT:");
  expect_contains(encoded, "top_state=FAULT_STOP");
  expect_contains(encoded, "start_ready=0");
  expect_contains(encoded, "laser_available=0");
  expect_contains(encoded, "measurement_health=TRANSIENT_UNAVAILABLE");
  expect_contains(encoded, "degraded_start_available=1");
  expect_contains(encoded, "degraded_start_enabled=0");
  expect_contains(encoded, "leave_stop_enabled=1");
  assert(encoded.length() + 1 <= ProtocolCodec::kConnectSnapshotPayloadBudgetBytes);
}

void test_protocol_ack_cap_contract_stays_bootstrap_truth() {
  const String encoded = HubAckBuilder::cap("SW-HUB-1.0.0", 1, PlatformModel::PLUS, true);

  expect_reason(encoded.c_str(), GoldenFrames::ack_cap_plus_v1);
  expect_contains(encoded, "ACK:CAP ");
  expect_contains(encoded, "fw=SW-HUB-1.0.0");
  expect_contains(encoded, "proto=1");
  expect_contains(encoded, "platform_model=PLUS");
  expect_contains(encoded, "laser_installed=1");
  expect_contains(encoded, "leave_stop_supported=1");
  expect_not_contains(encoded, "measurement_health=");
  expect_not_contains(encoded, "degraded_start_available=");
  expect_not_contains(encoded, "degraded_start_enabled=");
  assert(encoded.length() + 1 <= ProtocolCodec::kCapTruthPayloadBudgetBytes);
}

void test_protocol_golden_frame_fixture_core_cases() {
  {
    const String encoded = HubAckBuilder::cap("SW-HUB-1.0.0", 1, PlatformModel::PLUS, true);
    expect_reason(encoded.c_str(), GoldenFrames::ack_cap_plus_v1);
  }

  {
    PlatformSnapshot snapshot{};
    snapshot.topState = TopState::ARMED;
    snapshot.startReady = true;
    snapshot.laserAvailable = true;
    snapshot.measurementHealth = MeasurementHealthState::READY;
    snapshot.degradedStartAvailable = false;
    snapshot.degradedStartEnabled = false;
    snapshot.leaveStopEnabled = true;
    const String encoded = ProtocolCodec::encodeSnapshot(snapshot);
    expect_reason(encoded.c_str(), GoldenFrames::snapshot_slim_ready_v1);
  }

  {
    PlatformSnapshot snapshot{};
    snapshot.topState = TopState::FAULT_STOP;
    snapshot.startReady = false;
    snapshot.laserAvailable = false;
    snapshot.measurementHealth = MeasurementHealthState::FAULT;
    snapshot.degradedStartAvailable = true;
    snapshot.degradedStartEnabled = false;
    snapshot.leaveStopEnabled = true;
    const String encoded = ProtocolCodec::encodeSnapshot(snapshot);
    expect_reason(encoded.c_str(), GoldenFrames::snapshot_slim_fault_v1);
  }
}

void test_protocol_encode_stream_contract() {
  Event event{};
  event.type = EventType::STREAM;
  event.sampleSeq = 7;
  event.ts_ms = 1234;
  event.measurementValid = false;
  event.ma12Ready = false;
  std::snprintf(event.measurementReason, sizeof(event.measurementReason), "%s", "READ_FAIL");

  String encoded = ProtocolCodec::encodeEvent(event);
  expect_contains(encoded, "EVT:STREAM ");
  expect_contains(encoded, "seq=7");
  expect_contains(encoded, "ts_ms=1234");
  expect_contains(encoded, "valid=0");
  expect_contains(encoded, "ma12_ready=0");
  expect_contains(encoded, "reason=READ_FAIL");
  expect_not_contains(encoded, "distance=");

  event.measurementValid = true;
  event.ma12Ready = true;
  event.distance = 12.345f;
  event.weightKg = 67.891f;
  event.ma12WeightKg = 66.543f;
  encoded = ProtocolCodec::encodeEvent(event);
  expect_contains(encoded, "valid=1");
  expect_contains(encoded, "distance=12.35");
  expect_contains(encoded, "weight=67.89");
  expect_contains(encoded, "ma12=66.54");
  expect_not_contains(encoded, "reason=");
}

void test_protocol_encode_stop_and_safety_contract() {
  Event safety{};
  safety.type = EventType::SAFETY;
  safety.fault = FaultCode::USER_LEFT_PLATFORM;
  safety.safety = SafetySignalKind::RECOVERABLE_PAUSE;
  safety.state = TopState::RUNNING;
  safety.waveStopped = true;

  String encoded = ProtocolCodec::encodeEvent(safety);
  expect_contains(encoded, "EVT:SAFETY ");
  expect_contains(encoded, "reason=USER_LEFT_PLATFORM");
  expect_contains(encoded, "code=100");
  expect_contains(encoded, "effect=RECOVERABLE_PAUSE");
  expect_contains(encoded, "state=RUNNING");
  expect_contains(encoded, "wave=STOPPED");

  Event stop{};
  stop.type = EventType::STOP;
  stop.fault = FaultCode::FALL_SUSPECTED;
  stop.safety = SafetySignalKind::ABNORMAL_STOP;
  stop.state = TopState::FAULT_STOP;
  std::snprintf(stop.stopReasonText, sizeof(stop.stopReasonText), "%s", "FALL_SUSPECTED");
  std::snprintf(stop.stopSourceText, sizeof(stop.stopSourceText), "%s", "BASELINE_MAIN_LOGIC");

  encoded = ProtocolCodec::encodeEvent(stop);
  expect_contains(encoded, "EVT:STOP ");
  expect_contains(encoded, "stop_reason=FALL_SUSPECTED");
  expect_contains(encoded, "stop_source=BASELINE_MAIN_LOGIC");
  expect_contains(encoded, "code=101");
  expect_contains(encoded, "effect=ABNORMAL_STOP");
  expect_contains(encoded, "state=FAULT_STOP");
}

void test_hub_ack_builder_core_contracts() {
  String encoded = HubAckBuilder::cap("1.2.3", 1, PlatformModel::PLUS, true);
  expect_reason(encoded.c_str(), "ACK:CAP fw=1.2.3 proto=1 platform_model=PLUS laser_installed=1 leave_stop_supported=1");

  encoded = HubAckBuilder::deviceConfig(PlatformModel::BASE, false);
  expect_reason(encoded.c_str(), "ACK:DEVICE_CONFIG platform_model=BASE laser_installed=0");

  PlatformSnapshot snapshot{};
  snapshot.degradedStartEnabled = true;
  snapshot.degradedStartAvailable = true;
  encoded = HubAckBuilder::degradedStart(snapshot);
  expect_reason(encoded.c_str(), "ACK:DEGRADED_START enabled=1 available=1");

  expect_reason(HubAckBuilder::ok().c_str(), "ACK:OK");
  expect_reason(HubAckBuilder::unsupported().c_str(), "NACK:UNSUPPORTED");
  expect_reason(HubAckBuilder::simpleNack("INVALID_PARAM").c_str(), "NACK:INVALID_PARAM");
  expect_reason(HubAckBuilder::startRejected(FaultCode::FAULT_LOCKED).c_str(), "NACK:FAULT_LOCKED");
  expect_reason(HubAckBuilder::startRejected(FaultCode::NOT_ARMED).c_str(), "NACK:NOT_ARMED");
}

void test_hub_ack_builder_calibration_contracts() {
  String encoded = HubAckBuilder::calibrationPoint(
      3,
      1234,
      12.345f,
      67.891f,
      66.543f,
      true,
      false);
  expect_reason(
      encoded.c_str(),
      "ACK:CAL_POINT idx=3 ts=1234 d_mm=12.35 ref_kg=67.89 pred_kg=66.54 stable=1 valid=0");

  CalibrationModel model{};
  model.type = CalibrationModelType::QUADRATIC;
  model.referenceDistance = 10.12345f;
  model.coefficients[0] = 1.234567f;
  model.coefficients[1] = 2.345678f;
  model.coefficients[2] = 3.456789f;

  encoded = HubAckBuilder::calibrationModel(model);
  expect_reason(
      encoded.c_str(),
      "ACK:CAL_MODEL type=QUADRATIC ref=10.1235 c0=1.234567 c1=2.345678 c2=3.456789");

  encoded = HubAckBuilder::calibrationSetModel(model);
  expect_reason(
      encoded.c_str(),
      "ACK:CAL_SET_MODEL type=QUADRATIC ref=10.1235 c0=1.234567 c1=2.345678 c2=3.456789");

  encoded = HubAckBuilder::calibrationSetModelRejected(CalibrationModelType::LINEAR, "NON_MONOTONIC");
  expect_reason(encoded.c_str(), "NACK:CAL_SET_MODEL type=LINEAR reason=NON_MONOTONIC");
}

void test_hub_ack_builder_safety_contracts() {
  String encoded = HubAckBuilder::fallStop(false, "WARNING_ONLY");
  expect_reason(encoded.c_str(), "ACK:FALL_STOP enabled=0 mode=WARNING_ONLY");

  encoded = HubAckBuilder::leaveProtection(true, "RECOVERABLE_PAUSE");
  expect_reason(
      encoded.c_str(),
      "ACK:LEAVE_PROTECTION enabled=1 supported=1 effect=RECOVERABLE_PAUSE");

  encoded = HubAckBuilder::motionSampling(true, true);
  expect_reason(encoded.c_str(), "ACK:MOTION_SAMPLING enabled=1 fall_action_suppressed=1");
}

}  // namespace

int main() {
  test_presence_enter_exit();
  test_presence_deadband_and_zero_confirm();
  test_baseline_window_hold();
  test_baseline_confirm_and_latch();
  test_baseline_invalid_and_saturation();
  test_fall_stop_action_decision();
  test_stop_reason_and_source_fallbacks();
  test_stop_outcome_summary_evaluator();
  test_measurement_health_startup_and_ready();
  test_measurement_health_startup_fault_after_grace();
  test_measurement_health_runtime_fault_and_recovery();
  test_measurement_health_no_laser_is_fault();
  test_measurement_probe_policy_waits_for_confirmed_fault();
  test_measurement_probe_policy_opens_after_timeout_threshold();
  test_measurement_probe_policy_skips_until_probe_due();
  test_measurement_probe_policy_probe_failure_and_recovery();
  test_measurement_probe_policy_open_non_timeout_failure_stays_low_frequency();
  test_measurement_probe_policy_ignores_non_timeout_and_resets_when_ineligible();
  test_degraded_start_policy_profiles();
  test_user_left_policy_actions();
  test_protocol_parse_core_commands();
  test_protocol_parse_config_and_safety_commands();
  test_protocol_parse_legacy_commands();
  test_protocol_encode_snapshot_contract();
  test_protocol_snapshot_contract_spec_required_slim_fields();
  test_protocol_ack_cap_contract_stays_bootstrap_truth();
  test_protocol_golden_frame_fixture_core_cases();
  test_protocol_encode_stream_contract();
  test_protocol_encode_stop_and_safety_contract();
  test_hub_ack_builder_core_contracts();
  test_hub_ack_builder_calibration_contracts();
  test_hub_ack_builder_safety_contracts();
  std::cout << "evaluator unit tests passed\n";
  return 0;
}
"""


def load_golden_frames() -> list[dict[str, object]]:
  if not GOLDEN_FRAME_FIXTURE.exists():
    raise FileNotFoundError(f"golden frame fixture not found: {GOLDEN_FRAME_FIXTURE}")

  cases: list[dict[str, object]] = []
  seen_ids: set[str] = set()
  for line_number, raw_line in enumerate(GOLDEN_FRAME_FIXTURE.read_text(encoding="utf-8").splitlines(), start=1):
    line = raw_line.strip()
    if not line or line.startswith("#"):
      continue
    case = json.loads(line)
    case_id = required_string(case, "id", line_number)
    if case_id in seen_ids:
      raise AssertionError(f"duplicate golden frame id: {case_id}")
    seen_ids.add(case_id)

    frame = required_string(case, "frame", line_number)
    direction = required_string(case, "direction", line_number)
    required_string(case, "kind", line_number)
    required_int(case, "budget_bytes", line_number)
    required_string_list(case, "required", line_number)
    required_string_list(case, "forbidden", line_number)

    if direction != "device_to_app":
      raise AssertionError(f"{case_id}: Phase 2 only validates device_to_app frames")
    if len(frame) + 1 > required_int(case, "budget_bytes", line_number):
      raise AssertionError(f"{case_id}: frame exceeds payload budget")
    for field in required_string_list(case, "required", line_number):
      if f"{field}=" not in frame:
        raise AssertionError(f"{case_id}: missing required field {field}")
    for field in required_string_list(case, "forbidden", line_number):
      if f"{field}=" in frame:
        raise AssertionError(f"{case_id}: forbidden field present {field}")
    cases.append(case)

  if not cases:
    raise AssertionError("golden frame fixture is empty")
  return cases


def required_string(case: dict[str, object], field: str, line_number: int) -> str:
  value = case.get(field)
  if not isinstance(value, str) or not value:
    raise AssertionError(f"line {line_number}: missing string field {field}")
  return value


def required_int(case: dict[str, object], field: str, line_number: int) -> int:
  value = case.get(field)
  if not isinstance(value, int):
    raise AssertionError(f"line {line_number}: missing int field {field}")
  return value


def required_string_list(case: dict[str, object], field: str, line_number: int) -> list[str]:
  value = case.get(field)
  if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
    raise AssertionError(f"line {line_number}: missing string list field {field}")
  return value


def golden_frame_constants(cases: list[dict[str, object]]) -> str:
  by_id = {required_string(case, "id", 0): required_string(case, "frame", 0) for case in cases}
  required_ids = [
    "ack_cap_plus_v1",
    "snapshot_slim_ready_v1",
    "snapshot_slim_fault_v1",
  ]
  missing = [case_id for case_id in required_ids if case_id not in by_id]
  if missing:
    raise AssertionError(f"missing golden frame ids: {', '.join(missing)}")

  lines = [
    "#pragma once",
    "",
    "namespace GoldenFrames {",
  ]
  for case_id in required_ids:
    lines.append(f'constexpr const char* {case_id} = "{escape_cpp_string(by_id[case_id])}";')
  lines.append("}  // namespace GoldenFrames")
  lines.append("")
  return "\n".join(lines)


def escape_cpp_string(value: str) -> str:
  return value.replace("\\", "\\\\").replace('"', '\\"')


def run() -> None:
  golden_frames = load_golden_frames()
  with tempfile.TemporaryDirectory(prefix="sw_eval_tests_") as temp_dir:
    temp = Path(temp_dir)
    (temp / "Arduino.h").write_text(ARDUINO_STUB, encoding="utf-8")
    (temp / "Preferences.h").write_text(PREFERENCES_STUB, encoding="utf-8")
    (temp / "GoldenFrames.h").write_text(golden_frame_constants(golden_frames), encoding="utf-8")
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
      str(ROOT / "src/modules/laser/MeasurementAvailabilityProbePolicy.cpp"),
      str(ROOT / "src/modules/laser/MeasurementHealthStateMachine.cpp"),
      str(ROOT / "src/modules/laser/StopOutcomeSummaryEvaluator.cpp"),
      str(ROOT / "src/core/RuntimeProtectionPolicy.cpp"),
      str(ROOT / "src/core/SafetyActionContractEvaluator.cpp"),
      "-o",
      str(binary),
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)
    subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
  run()
