#pragma once

#include "CommandBus.h"
#include "EventBus.h"
#include "PlatformSnapshot.h"
#include "ProtocolSupport.h"
#include "config/GlobalConfig.h"

class ProtocolCodec {
public:
  static constexpr uint16_t kSingleNotifyBudgetMtu = ProtocolSupport::kSingleNotifyBudgetMtu;
  static constexpr uint16_t kSingleNotifyPayloadBudget = ProtocolSupport::kSingleNotifyPayloadBudget;
  static constexpr size_t kCapTruthPayloadBudgetBytes = ProtocolSupport::kCapTruthPayloadBudgetBytes;
  static constexpr size_t kConnectSnapshotPayloadBudgetBytes = ProtocolSupport::kConnectSnapshotPayloadBudgetBytes;
  static constexpr uint8_t kStreamDefaultRateHz = ProtocolSupport::kStreamDefaultRateHz;
  static constexpr uint8_t kStreamMinRateHz = ProtocolSupport::kStreamMinRateHz;
  static constexpr uint8_t kStreamMaxRateHz = ProtocolSupport::kStreamMaxRateHz;

  static bool isSnapshotQuery(const String& in);
  static void logTruthPayloadBudgetWarningIfNeeded(
      const char* frameKind,
      size_t framedLen,
      size_t budgetBytes,
      const String& payload);
  static String encodeSnapshot(const PlatformSnapshot& snapshot);
  static const char* minimalCompatFaultReason(FaultCode code);
  static bool parseStrictFloat(const String& raw, float& out);
  static bool parseStrictInt(const String& raw, int& out);
  static bool containsAsciiIgnoreCase(const String& text, const char* needle);
  static bool parseCommand(const String& in, Command& out, String& err);
  static String encodeEvent(const Event& e);
};
