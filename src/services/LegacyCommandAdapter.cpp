#include "LegacyCommandAdapter.h"

#include "HubAckBuilder.h"
#include "services/CalibrationCommandService.h"
#include "services/CapabilityCommandService.h"
#include "services/DeviceConfigCommandService.h"
#include "services/MeasurementStreamCommandService.h"
#include "services/ProtectionCommandService.h"
#include "services/WaveCommandService.h"

LegacyCommandAdapter::LegacyCommandAdapter(
    WaveCommandService* waveCommands,
    CapabilityCommandService* capabilityCommands,
    CalibrationCommandService* calibrationCommands,
    DeviceConfigCommandService* deviceConfigCommands,
    MeasurementStreamCommandService* streamCommands,
    ProtectionCommandService* protectionCommands)
    : waveCommands_(waveCommands),
      capabilityCommands_(capabilityCommands),
      calibrationCommands_(calibrationCommands),
      deviceConfigCommands_(deviceConfigCommands),
      streamCommands_(streamCommands),
      protectionCommands_(protectionCommands) {}

bool LegacyCommandAdapter::handle(
    const Command& command,
    String& outAck) {
  if (waveCommands_ && waveCommands_->handles(command.type)) {
    return waveCommands_->handle(command, outAck);
  }
  if (capabilityCommands_ && capabilityCommands_->handles(command.type)) {
    return capabilityCommands_->handle(command, outAck);
  }
  if (calibrationCommands_ && calibrationCommands_->handles(command.type)) {
    return calibrationCommands_->handle(command, outAck);
  }
  if (deviceConfigCommands_ && deviceConfigCommands_->handles(command.type)) {
    return deviceConfigCommands_->handle(command, outAck);
  }
  if (streamCommands_ && streamCommands_->handles(command.type)) {
    return streamCommands_->handle(command, outAck);
  }
  if (protectionCommands_ && protectionCommands_->handles(command.type)) {
    return protectionCommands_->handle(command, outAck);
  }

  outAck = HubAckBuilder::unsupported();
  return false;
}
