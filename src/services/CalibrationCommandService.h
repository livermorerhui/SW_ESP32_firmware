#pragma once

#include "core/CommandBus.h"

class LaserModule;

class CalibrationCommandService final {
public:
  explicit CalibrationCommandService(LaserModule* laserModule);

  bool handles(CmdType type) const;
  bool handle(const Command& command, String& outAck) const;

private:
  bool handleEmptyDistance(const Command& command, String& outAck) const;
  LaserModule* laserModule_;
};
