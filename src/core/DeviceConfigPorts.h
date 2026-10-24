#pragma once

#include "DeviceConfig.h"
#include "PlatformSnapshotOwner.h"

class IDeviceConfigOwner {
public:
  virtual ~IDeviceConfigOwner() = default;
  virtual bool getProfileConfig(DeviceConfigSnapshot& out, String& reason) = 0;
  virtual bool setProfileConfig(
      PlatformModel platformModel,
      SensorProfileId sensorProfile,
      String& reason) = 0;
  virtual bool setDeviceConfig(
      PlatformModel platformModel,
      bool laserInstalled,
      String& reason) = 0;
  virtual PlatformModel platformModel() const = 0;
  virtual bool laserInstalled() const = 0;
};

class IStreamFrameFence {
public:
  virtual ~IStreamFrameFence() = default;
  virtual void invalidatePendingStreamFrames() = 0;
};

class IAdvertisingIdentityUpdater {
public:
  virtual ~IAdvertisingIdentityUpdater() = default;
  virtual void applyAdvertisingIdentity(PlatformModel model) = 0;
};
