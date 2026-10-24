#pragma once

#include "core/EventBus.h"
#include "core/PlatformSnapshot.h"

class EventEncoder final {
public:
  static String encode(const Event& event);
  static String encodeSnapshot(const PlatformSnapshot& snapshot);
};
