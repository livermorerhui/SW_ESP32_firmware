#include "modules/laser/BaselineContractDiagnostics.h"

#include <math.h>
#include <string.h>

#include "config/GlobalConfig.h"

bool BaselineContractDiagnostics::hasState(const BaselineContractStateView& state) {
  return state.stableCandidate ||
      state.stableReadyLive ||
      state.baselineReadyLatched ||
      state.startReady ||
      state.baselineReadyWeightKg > 0.0f ||
      state.startReadyWeightKg > 0.0f;
}

void BaselineContractDiagnostics::logLatch(
    uint32_t now,
    const char* source,
    float distance,
    float weight,
    const BaselineContractStateView& state) const {
  if (!BASELINE_CONTRACT_DIAG_ENABLED) {
    return;
  }

  Serial.printf(
      "[BASELINE_CONTRACT] event=latch source=%s baseline_latched=1 weight=%.2f distance=%.2f captured_ms=%lu user_present=%d stable_live=%d start_ready=%d bridge=%s\n",
      source ? source : "unknown",
      weight,
      distance,
      static_cast<unsigned long>(now),
      state.userPresent ? 1 : 0,
      state.stableReadyLive ? 1 : 0,
      state.startReady ? 1 : 0,
      state.startReadyBridge ? state.startReadyBridge : "unknown");
}

void BaselineContractDiagnostics::logClear(
    uint32_t now,
    const char* reason,
    const BaselineContractStateView& before) const {
  if (!BASELINE_CONTRACT_DIAG_ENABLED || !hasState(before)) {
    return;
  }

  Serial.printf(
      "[BASELINE_CONTRACT] event=clear reason=%s before_user_present=%d before_stable_candidate=%d before_stable_live=%d before_baseline_latched=%d before_start_ready=%d before_baseline_weight=%.2f before_start_weight=%.2f before_bridge=%s cleared_ms=%lu\n",
      reason ? reason : "unspecified",
      before.userPresent ? 1 : 0,
      before.stableCandidate ? 1 : 0,
      before.stableReadyLive ? 1 : 0,
      before.baselineReadyLatched ? 1 : 0,
      before.startReady ? 1 : 0,
      before.baselineReadyWeightKg,
      before.startReadyWeightKg,
      before.startReadyBridge ? before.startReadyBridge : "unknown",
      static_cast<unsigned long>(now));
}

void BaselineContractDiagnostics::logStartReadyWriteback(
    const BaselineContractWritebackInput& input) {
  if (!BASELINE_CONTRACT_DIAG_ENABLED || !shouldLogStartReadyWriteback(input)) {
    return;
  }

  Serial.printf(
      "[BASELINE_CONTRACT] event=start_ready_writeback source=%s top_state=%s start_ready=%d start_weight=%.2f reason=%s user_present=%d baseline_latched=%d stable_live=%d baseline_weight=%.2f\n",
      input.source ? input.source : "unknown",
      topStateName(input.topState),
      input.startReady ? 1 : 0,
      input.startReadyWeightKg,
      input.reason ? input.reason : "unknown",
      input.state.userPresent ? 1 : 0,
      input.state.baselineReadyLatched ? 1 : 0,
      input.state.stableReadyLive ? 1 : 0,
      input.state.baselineReadyWeightKg);

  rememberStartReadyWriteback(input);
}

bool BaselineContractDiagnostics::shouldLogStartReadyWriteback(
    const BaselineContractWritebackInput& input) const {
  const char* source = input.source ? input.source : "unknown";
  const char* reason = input.reason ? input.reason : "unknown";
  const bool sourceChanged =
      !lastLoggedStartReadyWritebackSource ||
      strcmp(lastLoggedStartReadyWritebackSource, source) != 0;
  const bool reasonChanged =
      !lastLoggedStartReadyWritebackReason ||
      strcmp(lastLoggedStartReadyWritebackReason, reason) != 0;
  const bool weightChanged =
      input.startReady &&
      fabsf(lastLoggedStartReadyWritebackWeightKg - input.startReadyWeightKg) >= 0.01f;
  return !hasLoggedStartReadyWriteback ||
      lastLoggedStartReadyWritebackReady != input.startReady ||
      lastLoggedStartReadyWritebackTopState != input.topState ||
      sourceChanged ||
      reasonChanged ||
      weightChanged;
}

void BaselineContractDiagnostics::rememberStartReadyWriteback(
    const BaselineContractWritebackInput& input) {
  hasLoggedStartReadyWriteback = true;
  lastLoggedStartReadyWritebackReady = input.startReady;
  lastLoggedStartReadyWritebackTopState = input.topState;
  lastLoggedStartReadyWritebackWeightKg = input.startReadyWeightKg;
  lastLoggedStartReadyWritebackSource = input.source ? input.source : "unknown";
  lastLoggedStartReadyWritebackReason = input.reason ? input.reason : "unknown";
}
