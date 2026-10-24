#include "DeviceConfigCommandService.h"

#include "HubAckBuilder.h"
#include "core/HumanReadableLog.h"
#include "core/SystemStateMachine.h"
#include "core/VerboseSerial.h"

DeviceConfigCommandService::DeviceConfigCommandService(
    IDeviceConfigOwner* configOwner,
    IStreamFrameFence* streamFrameFence,
    IAdvertisingIdentityUpdater* advertisingIdentityUpdater,
    const PlatformSnapshotOwner* snapshotOwner)
    : configOwner_(configOwner),
      streamFrameFence_(streamFrameFence),
      advertisingIdentityUpdater_(advertisingIdentityUpdater),
      snapshotOwner_(snapshotOwner) {}

bool DeviceConfigCommandService::handles(CmdType type) const {
  switch (type) {
    case CmdType::DEVICE_GET_CONFIG:
    case CmdType::DEVICE_SET_PROFILE_CONFIG:
    case CmdType::DEVICE_SET_CONFIG:
      return true;
    default:
      return false;
  }
}

bool DeviceConfigCommandService::handle(
    const Command& command,
    String& outAck) const {
  if (!configOwner_ || !streamFrameFence_ ||
      !advertisingIdentityUpdater_ || !snapshotOwner_) {
    outAck = HubAckBuilder::simpleNack("DEVICE_CONFIG_SERVICE_UNAVAILABLE");
    return false;
  }

  switch (command.type) {
    case CmdType::DEVICE_GET_CONFIG:
      return handleGetProfileConfig(outAck);
    case CmdType::DEVICE_SET_PROFILE_CONFIG:
      return handleSetProfileConfig(command.deviceProfileConfig, outAck);
    case CmdType::DEVICE_SET_CONFIG:
      return handleSetLegacyConfig(command.deviceConfig, outAck);
    default:
      outAck = HubAckBuilder::unsupported();
      return false;
  }
}

bool DeviceConfigCommandService::handleGetProfileConfig(String& outAck) const {
  DeviceConfigSnapshot config{};
  String reason;
  if (!configOwner_->getProfileConfig(config, reason)) {
    streamFrameFence_->invalidatePendingStreamFrames();
    SW_VERBOSE_SERIAL_PRINTF(
        "[DEVICE_CONFIG] op=GET result=failure reason=%s\n",
        reason.c_str());
    outAck = HubAckBuilder::deviceProfileConfigNack(
        "GET", reason == "UNSUPPORTED_MODEL" ? "UNSUPPORTED_MODEL" : "CONFIG_ERROR");
    return false;
  }
  outAck = HubAckBuilder::deviceProfileConfig(
      "GET", config.platformModel, config.sensorProfile, config.generation);
  SW_VERBOSE_SERIAL_PRINTF(
      "[DEVICE_CONFIG] op=GET result=ok model=%s profile=%s generation=%lu\n",
      platformModelName(config.platformModel),
      sensorProfileName(config.sensorProfile),
      static_cast<unsigned long>(config.generation));
  return true;
}

bool DeviceConfigCommandService::handleSetProfileConfig(
    const DeviceProfileConfigCommand& command,
    String& outAck) const {
  SW_VERBOSE_SERIAL_PRINTF(
      "[DEVICE_CONFIG] op=SET requested_model=%s requested_profile=%s\n",
      platformModelName(command.platformModel),
      sensorProfileName(command.sensorProfile));
  String reason;
  if (!configOwner_->setProfileConfig(
          command.platformModel, command.sensorProfile, reason)) {
    streamFrameFence_->invalidatePendingStreamFrames();
    outAck = HubAckBuilder::deviceProfileConfigNack("SET", reason.c_str());
    SW_VERBOSE_SERIAL_PRINTF(
        "[DEVICE_CONFIG] op=SET result=failure reason=%s\n",
        reason.c_str());
    return false;
  }
  streamFrameFence_->invalidatePendingStreamFrames();
  DeviceConfigSnapshot applied{};
  if (!configOwner_->getProfileConfig(applied, reason)) {
    streamFrameFence_->invalidatePendingStreamFrames();
    outAck = HubAckBuilder::deviceProfileConfigNack("SET", "VERIFY_FAILED");
    SW_VERBOSE_SERIAL_PRINTF(
        "[DEVICE_CONFIG] op=SET result=verify_failed reason=%s\n",
        reason.c_str());
    return false;
  }
  outAck = HubAckBuilder::deviceProfileConfig(
      "SET", applied.platformModel, applied.sensorProfile, applied.generation);
  advertisingIdentityUpdater_->applyAdvertisingIdentity(applied.platformModel);
  SW_VERBOSE_SERIAL_PRINTF(
      "[DEVICE_CONFIG] op=SET result=ok model=%s profile=%s generation=%lu\n",
      platformModelName(applied.platformModel),
      sensorProfileName(applied.sensorProfile),
      static_cast<unsigned long>(applied.generation));
  return true;
}

bool DeviceConfigCommandService::handleSetLegacyConfig(
    const DeviceConfigCommand& command,
    String& outAck) const {
  SW_VERBOSE_SERIAL_PRINTF(
      "[LAYER:COMMAND_DISPATCH] cmd=DEVICE_SET_CONFIG platform_model=%s laser_installed=%d owner=LaserModule::setDeviceConfig\n",
      platformModelName(command.platformModel),
      command.laserInstalled ? 1 : 0);
  String reason;
  if (!configOwner_->setDeviceConfig(
          command.platformModel, command.laserInstalled, reason)) {
    streamFrameFence_->invalidatePendingStreamFrames();
    outAck = HubAckBuilder::simpleNack(reason);
    return false;
  }

  streamFrameFence_->invalidatePendingStreamFrames();
  outAck = HubAckBuilder::deviceConfig(
      configOwner_->platformModel(), configOwner_->laserInstalled());
  advertisingIdentityUpdater_->applyAdvertisingIdentity(configOwner_->platformModel());
  const PlatformSnapshot snapshot = snapshotOwner_->snapshot();
  SW_VERBOSE_SERIAL_PRINTF(
      "[LAYER:CONFIG_TRUTH] source=DEVICE_SET_CONFIG platform_model=%s laser_installed=%d laser_available=%d protection_degraded=%d degraded_start_available=%d degraded_start_enabled=%d runtime_ready=%d start_ready=%d baseline_ready=%d top_state=%s\n",
      platformModelName(snapshot.platformModel),
      snapshot.laserInstalled ? 1 : 0,
      snapshot.laserAvailable ? 1 : 0,
      snapshot.protectionDegraded ? 1 : 0,
      snapshot.degradedStartAvailable ? 1 : 0,
      snapshot.degradedStartEnabled ? 1 : 0,
      snapshot.runtimeReady ? 1 : 0,
      snapshot.startReady ? 1 : 0,
      snapshot.baselineReady ? 1 : 0,
      topStateName(snapshot.topState));
  return true;
}
