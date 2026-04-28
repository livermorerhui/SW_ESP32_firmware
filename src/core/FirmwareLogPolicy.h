#pragma once

#include <Arduino.h>

namespace FirmwareLogPolicy {

// Keep high-frequency diagnostics as periodic summaries. This preserves serial
// capture evidence without allowing disconnected BLE telemetry to dominate UART.
static constexpr uint32_t kBleTxSendSkipLogIntervalMs = 2000UL;
static constexpr uint32_t kBleTxPressureSnapshotIntervalMs = 2000UL;

inline bool shouldLogNow(
    uint32_t nowMs,
    uint32_t& lastLogMs,
    uint32_t intervalMs,
    bool force = false) {
  if (force) {
    lastLogMs = nowMs;
    return true;
  }
  if (lastLogMs != 0 && nowMs - lastLogMs < intervalMs) {
    return false;
  }
  lastLogMs = nowMs;
  return true;
}

}  // namespace FirmwareLogPolicy
