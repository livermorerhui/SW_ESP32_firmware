#include "ProtectionCommandService.h"

#include "HubAckBuilder.h"
#include "core/SystemStateMachine.h"

ProtectionCommandService::ProtectionCommandService(
    SystemStateMachine* stateMachine)
    : stateMachine_(stateMachine) {}

bool ProtectionCommandService::handles(CmdType type) const {
  switch (type) {
    case CmdType::FALL_STOP_SET:
    case CmdType::LEAVE_PROTECTION_SET:
    case CmdType::MOTION_SAMPLING_MODE_SET:
      return true;
    default:
      return false;
  }
}

bool ProtectionCommandService::handle(
    const Command& command,
    String& outAck) const {
  if (!stateMachine_) {
    outAck = HubAckBuilder::simpleNack("PROTECTION_SERVICE_UNAVAILABLE");
    return false;
  }
  switch (command.type) {
    case CmdType::FALL_STOP_SET:
      if (!stateMachine_->snapshot().laserAvailable) {
        outAck = HubAckBuilder::simpleNack("LASER_UNAVAILABLE");
        return false;
      }
      stateMachine_->setFallStopEnabled(command.fallStop.enabled);
      outAck = HubAckBuilder::fallStop(
          command.fallStop.enabled, stateMachine_->fallStopModeName());
      return true;

    case CmdType::LEAVE_PROTECTION_SET:
      if (!stateMachine_->snapshot().laserAvailable) {
        outAck = HubAckBuilder::simpleNack("LASER_UNAVAILABLE");
        return false;
      }
      stateMachine_->setLeaveStopEnabled(command.leaveProtection.enabled);
      outAck = HubAckBuilder::leaveProtection(
          stateMachine_->leaveStopEnabled(),
          stateMachine_->leaveStopModeName());
      return true;

    case CmdType::MOTION_SAMPLING_MODE_SET:
      stateMachine_->setMotionSamplingMode(command.motionSamplingMode.enabled);
      outAck = HubAckBuilder::motionSampling(
          command.motionSamplingMode.enabled,
          !stateMachine_->fallStopEnabled());
      return true;

    default:
      outAck = HubAckBuilder::unsupported();
      return false;
  }
}
