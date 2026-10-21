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

  switch (command.type) {
    case CmdType::WAVE_SET:
    case CmdType::WAVE_START:
    case CmdType::WAVE_STOP:
    case CmdType::AUDIO_CUE:
    case CmdType::LEGACY_FIE:
      return waveCommands_ && waveCommands_->handle(command, outAck);

    case CmdType::CAP_QUERY:
    case CmdType::DEVICE_IDENTITY_QUERY:
    case CmdType::DEVICE_SECURITY_QUERY:
    case CmdType::DEGRADED_START_SET:
      return capabilityCommands_ && capabilityCommands_->handle(command, outAck);

    case CmdType::SCALE_ZERO:
    case CmdType::CAL_ZERO:
    case CmdType::SCALE_CAL:
    case CmdType::CAL_CAPTURE:
    case CmdType::CAL_GET_MODEL:
    case CmdType::CAL_SET_MODEL:
    case CmdType::EMPTY_DISTANCE_GET:
    case CmdType::EMPTY_DISTANCE_SET_CURRENT:
    case CmdType::EMPTY_DISTANCE_SET:
    case CmdType::EMPTY_DISTANCE_CLEAR:
    case CmdType::CAL_CLEAR_WEIGHT_MODEL:
      return calibrationCommands_ && calibrationCommands_->handle(command, outAck);

    case CmdType::DEVICE_GET_CONFIG:
    case CmdType::DEVICE_SET_PROFILE_CONFIG:
    case CmdType::DEVICE_SET_CONFIG:
      return deviceConfigCommands_ && deviceConfigCommands_->handle(command, outAck);

    case CmdType::STREAM_SET:
      return streamCommands_ && streamCommands_->handle(command, outAck);

    case CmdType::FALL_STOP_SET:
    case CmdType::LEAVE_PROTECTION_SET:
    case CmdType::MOTION_SAMPLING_MODE_SET:
      return protectionCommands_ && protectionCommands_->handle(command, outAck);
  }
  outAck = HubAckBuilder::unsupported();
  return false;
}
