#include "ProtocolSupport.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/VerboseSerial.h"

bool ProtocolSupport::isSnapshotQuery(const String& input) {
  String normalized = input;
  normalized.trim();
  return normalized.equalsIgnoreCase("SNAPSHOT?");
}

void ProtocolSupport::logTruthPayloadBudgetWarningIfNeeded(
    const char* frameKind,
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

const char* ProtocolSupport::minimalCompatFaultReason(FaultCode code) {
  switch (code) {
    case FaultCode::USER_LEFT_PLATFORM:
      return "USER_LEFT_PLATFORM";
    case FaultCode::FALL_SUSPECTED:
      return "FALL_SUSPECTED";
    default:
      return nullptr;
  }
}

bool ProtocolSupport::parseStrictFloat(const String& raw, float& out) {
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

bool ProtocolSupport::parseStrictInt(const String& raw, int& out) {
  if (raw.length() == 0) return false;

  char* end = nullptr;
  errno = 0;
  const long value = strtol(raw.c_str(), &end, 10);
  if (end == raw.c_str() || *end != '\0' || errno == ERANGE) return false;
  if (value < INT_MIN || value > INT_MAX) return false;

  out = static_cast<int>(value);
  return true;
}

bool ProtocolSupport::containsAsciiIgnoreCase(
    const String& text,
    const char* needle) {
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
