#pragma once

#include "ProtocolCodec.h"

// Stable protocol boundary for transport code. The legacy ProtocolCodec stays
// behind this adapter until the wire-format implementation is split into
// decoder, encoder, and legacy compatibility components.
class CommandDecoder final {
public:
  static bool decode(const String& input, Command& command, String& error);
};

class EventEncoder final {
public:
  static String encode(const Event& event);
  static String encodeSnapshot(const PlatformSnapshot& snapshot);
};
