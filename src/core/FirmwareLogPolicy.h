#pragma once

#include <Arduino.h>

namespace FirmwareLogPolicy {

// Keep high-frequency diagnostics as periodic summaries. This preserves serial
// capture evidence without allowing disconnected BLE telemetry to dominate UART.
static constexpr uint32_t kBleTxSendSkipLogIntervalMs = 2000UL;
static constexpr uint32_t kBleTxPressureSnapshotIntervalMs = 2000UL;

struct RateLimitState {
  uint32_t lastMs = 0;
  bool initialized = false;
};

inline bool shouldLogNow(
    uint32_t nowMs,
    RateLimitState& state,
    uint32_t intervalMs,
    bool force = false) {
  if (force) {
    state.lastMs = nowMs;
    state.initialized = true;
    return true;
  }
  if (state.initialized && nowMs - state.lastMs < intervalMs) {
    return false;
  }
  state.lastMs = nowMs;
  state.initialized = true;
  return true;
}

}  // namespace FirmwareLogPolicy
