#pragma once

#include "core/CommandBus.h"
#include "core/DeviceConfigPorts.h"

class DeviceConfigCommandService final {
public:
  DeviceConfigCommandService(
      IDeviceConfigOwner* configOwner,
      IStreamFrameFence* streamFrameFence,
      IAdvertisingIdentityUpdater* advertisingIdentityUpdater,
      const PlatformSnapshotOwner* snapshotOwner);

  bool handles(CmdType type) const;
  bool handle(const Command& command, String& outAck) const;

private:
  bool handleGetProfileConfig(String& outAck) const;
  bool handleSetProfileConfig(
      const DeviceProfileConfigCommand& command,
      String& outAck) const;
  bool handleSetLegacyConfig(
      const DeviceConfigCommand& command,
      String& outAck) const;

  IDeviceConfigOwner* configOwner_;
  IStreamFrameFence* streamFrameFence_;
  IAdvertisingIdentityUpdater* advertisingIdentityUpdater_;
  const PlatformSnapshotOwner* snapshotOwner_;
};
