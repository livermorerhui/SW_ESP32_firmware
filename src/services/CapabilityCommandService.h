#pragma once

#include "core/CommandBus.h"

class LaserModule;
class SystemStateMachine;

class CapabilityCommandService final {
public:
  CapabilityCommandService(SystemStateMachine* stateMachine,
                           LaserModule* laserModule);

  bool handles(CmdType type) const;
  bool handle(const Command& command, String& outAck) const;

private:
  SystemStateMachine* stateMachine_;
  LaserModule* laserModule_;
};
