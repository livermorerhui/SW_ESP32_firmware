#pragma once

#include "core/CommandBus.h"

class WaveCommandService;
class CapabilityCommandService;
class CalibrationCommandService;
class DeviceConfigCommandService;
class MeasurementStreamCommandService;
class ProtectionCommandService;

class LegacyCommandAdapter final : public CommandHandler {
public:
  LegacyCommandAdapter(
      WaveCommandService* waveCommands,
      CapabilityCommandService* capabilityCommands,
      CalibrationCommandService* calibrationCommands,
      DeviceConfigCommandService* deviceConfigCommands,
      MeasurementStreamCommandService* streamCommands,
      ProtectionCommandService* protectionCommands);

  bool handle(const Command& command, String& outAck) override;

private:
  WaveCommandService* waveCommands_;
  CapabilityCommandService* capabilityCommands_;
  CalibrationCommandService* calibrationCommands_;
  DeviceConfigCommandService* deviceConfigCommands_;
  MeasurementStreamCommandService* streamCommands_;
  ProtectionCommandService* protectionCommands_;
};
