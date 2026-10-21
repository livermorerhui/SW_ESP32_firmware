#pragma once

#include "core/CommandBus.h"

class SystemStateMachine;

class ProtectionCommandService final {
public:
  explicit ProtectionCommandService(SystemStateMachine* stateMachine);

  bool handles(CmdType type) const;
  bool handle(const Command& command, String& outAck) const;

private:
  SystemStateMachine* stateMachine_;
};
