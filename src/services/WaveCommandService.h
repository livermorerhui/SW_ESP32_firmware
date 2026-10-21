#pragma once

#include "core/CommandBus.h"

class SystemStateMachine;
class WaveModule;

class WaveCommandService final {
public:
  WaveCommandService(SystemStateMachine* stateMachine, WaveModule* waveModule);

  bool handles(CmdType type) const;
  bool handle(const Command& command, String& outAck) const;

private:
  bool handleAudioCue(const Command& command, String& outAck) const;
  bool handleLegacyFie(const Command& command, String& outAck) const;

  SystemStateMachine* stateMachine_;
  WaveModule* waveModule_;
};
