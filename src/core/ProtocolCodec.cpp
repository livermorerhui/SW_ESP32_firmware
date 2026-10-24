#include "ProtocolCodec.h"

#include "core/ProtocolBoundary.h"

bool ProtocolCodec::isSnapshotQuery(const String& in) {
    return ProtocolSupport::isSnapshotQuery(in);
  }

void ProtocolCodec::logTruthPayloadBudgetWarningIfNeeded(const char* frameKind,
                                                   size_t framedLen,
                                                   size_t budgetBytes,
                                                   const String& payload) {
    ProtocolSupport::logTruthPayloadBudgetWarningIfNeeded(
        frameKind, framedLen, budgetBytes, payload);
  }

const char* ProtocolCodec::minimalCompatFaultReason(FaultCode code) {
    return ProtocolSupport::minimalCompatFaultReason(code);
  }

bool ProtocolCodec::parseStrictFloat(const String& raw, float& out) {
    return ProtocolSupport::parseStrictFloat(raw, out);
  }

bool ProtocolCodec::parseStrictInt(const String& raw, int& out) {
    return ProtocolSupport::parseStrictInt(raw, out);
  }

bool ProtocolCodec::containsAsciiIgnoreCase(const String& text, const char* needle) {
    return ProtocolSupport::containsAsciiIgnoreCase(text, needle);
  }

  // 返回 true = 成功解析
String ProtocolCodec::encodeSnapshot(const PlatformSnapshot& snapshot) {
  return EventEncoder::encodeSnapshot(snapshot);
}

bool ProtocolCodec::parseCommand(const String& input,
                                 Command& command,
                                 String& error) {
  return CommandDecoder::decode(input, command, error);
}

String ProtocolCodec::encodeEvent(const Event& event) {
  return EventEncoder::encode(event);
}
