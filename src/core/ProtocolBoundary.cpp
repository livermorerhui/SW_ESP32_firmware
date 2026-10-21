#include "ProtocolBoundary.h"

bool CommandDecoder::decode(const String& input,
                            Command& command,
                            String& error) {
  return ProtocolCodec::parseCommand(input, command, error);
}

String EventEncoder::encode(const Event& event) {
  return ProtocolCodec::encodeEvent(event);
}

String EventEncoder::encodeSnapshot(const PlatformSnapshot& snapshot) {
  return ProtocolCodec::encodeSnapshot(snapshot);
}
