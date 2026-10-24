#pragma once

#include <Arduino.h>

#include "core/Types.h"

class ProtocolSupport final {
public:
  static constexpr uint16_t kSingleNotifyBudgetMtu = 185;
  static constexpr uint16_t kSingleNotifyPayloadBudget = kSingleNotifyBudgetMtu - 3;
  static constexpr size_t kCapTruthPayloadBudgetBytes = kSingleNotifyPayloadBudget;
  static constexpr size_t kConnectSnapshotPayloadBudgetBytes = kSingleNotifyPayloadBudget;
  static constexpr uint8_t kStreamDefaultRateHz = 10;
  static constexpr uint8_t kStreamMinRateHz = 1;
  static constexpr uint8_t kStreamMaxRateHz = 20;

  static bool isSnapshotQuery(const String& input);
  static void logTruthPayloadBudgetWarningIfNeeded(
      const char* frameKind,
      size_t framedLen,
      size_t budgetBytes,
      const String& payload);
  static const char* minimalCompatFaultReason(FaultCode code);
  static bool parseStrictFloat(const String& raw, float& out);
  static bool parseStrictInt(const String& raw, int& out);
  static bool containsAsciiIgnoreCase(const String& text, const char* needle);
};
