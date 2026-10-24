#pragma once

#include "core/CommandBus.h"

class CommandDecoder final {
public:
  static bool decode(const String& input, Command& command, String& error);
};
