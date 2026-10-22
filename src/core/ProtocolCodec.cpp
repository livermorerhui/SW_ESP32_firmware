#include "ProtocolCodec.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/ProtocolBoundary.h"
#include "core/VerboseSerial.h"

bool ProtocolCodec::isSnapshotQuery(const String& in) {
    String s = in;
    s.trim();
    return s.equalsIgnoreCase("SNAPSHOT?");
  }

void ProtocolCodec::logTruthPayloadBudgetWarningIfNeeded(const char* frameKind,
                                                   size_t framedLen,
                                                   size_t budgetBytes,
                                                   const String& payload) {
    if (framedLen <= budgetBytes) return;
    SW_VERBOSE_SERIAL_PRINTF(
        "[PROTO] warn=%s_budget_exceeded framed_len=%u budget=%u payload=%s\n",
        frameKind,
        static_cast<unsigned>(framedLen),
        static_cast<unsigned>(budgetBytes),
        payload.c_str());
  }

const char* ProtocolCodec::minimalCompatFaultReason(FaultCode code) {
    switch (code) {
      case FaultCode::USER_LEFT_PLATFORM:
        return "USER_LEFT_PLATFORM";
      case FaultCode::FALL_SUSPECTED:
        return "FALL_SUSPECTED";
      default:
        return nullptr;
    }
  }

bool ProtocolCodec::parseStrictFloat(const String& raw, float& out) {
    if (raw.length() == 0) return false;

    char* end = nullptr;
    errno = 0;
    const float value = strtof(raw.c_str(), &end);
    if (end == raw.c_str() || *end != '\0' || errno == ERANGE || !isfinite(value)) {
      return false;
    }

    out = value;
    return true;
  }

bool ProtocolCodec::parseStrictInt(const String& raw, int& out) {
    if (raw.length() == 0) return false;

    char* end = nullptr;
    errno = 0;
    const long value = strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0' || errno == ERANGE) {
      return false;
    }
    if (value < INT_MIN || value > INT_MAX) {
      return false;
    }

    out = static_cast<int>(value);
    return true;
  }

bool ProtocolCodec::containsAsciiIgnoreCase(const String& text, const char* needle) {
    if (!needle) return false;
    const size_t needleLen = strlen(needle);
    if (needleLen == 0 || needleLen > text.length()) return false;
    const char* haystack = text.c_str();
    for (size_t start = 0; start + needleLen <= text.length(); ++start) {
      size_t matched = 0;
      while (matched < needleLen &&
          tolower(static_cast<unsigned char>(haystack[start + matched])) ==
              tolower(static_cast<unsigned char>(needle[matched]))) {
        ++matched;
      }
      if (matched == needleLen) return true;
    }
    return false;
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
