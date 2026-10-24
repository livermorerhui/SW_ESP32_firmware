#pragma once

#include "core/CommandBus.h"

class LaserModule;

class MeasurementStreamCommandService final {
public:
  explicit MeasurementStreamCommandService(LaserModule* laserModule);

  bool handles(CmdType type) const;
  bool handle(const Command& command, String& outAck) const;

private:
  LaserModule* laserModule_;
};
