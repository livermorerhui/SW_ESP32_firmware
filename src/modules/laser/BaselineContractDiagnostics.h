#pragma once

#include <Arduino.h>

#include "core/Types.h"

struct BaselineContractStateView {
  bool userPresent = false;
  bool stableCandidate = false;
  bool stableReadyLive = false;
  bool baselineReadyLatched = false;
  bool startReady = false;
  float baselineReadyWeightKg = 0.0f;
  float startReadyWeightKg = 0.0f;
  const char* startReadyBridge = "not_ready";
};

struct BaselineContractWritebackInput {
  uint32_t now = 0;
  const char* source = "unknown";
  TopState topState = TopState::IDLE;
  bool startReady = false;
  float startReadyWeightKg = 0.0f;
  const char* reason = "unknown";
  BaselineContractStateView state{};
};

class BaselineContractDiagnostics {
public:
  static bool hasState(const BaselineContractStateView& state);

  void logLatch(
      uint32_t now,
      const char* source,
      float distance,
      float weight,
      const BaselineContractStateView& state) const;

  void logClear(
      uint32_t now,
      const char* reason,
      const BaselineContractStateView& before) const;

  void logStartReadyWriteback(const BaselineContractWritebackInput& input);

private:
  bool shouldLogStartReadyWriteback(const BaselineContractWritebackInput& input) const;
  void rememberStartReadyWriteback(const BaselineContractWritebackInput& input);

  bool hasLoggedStartReadyWriteback = false;
  bool lastLoggedStartReadyWritebackReady = false;
  TopState lastLoggedStartReadyWritebackTopState = TopState::IDLE;
  float lastLoggedStartReadyWritebackWeightKg = 0.0f;
  const char* lastLoggedStartReadyWritebackSource = nullptr;
  const char* lastLoggedStartReadyWritebackReason = nullptr;
};
