#pragma once

#include "core/DeviceConfigPorts.h"

class BleTransport;

class BleDeviceConfigEffects final
    : public IStreamFrameFence,
      public IAdvertisingIdentityUpdater {
public:
  explicit BleDeviceConfigEffects(BleTransport* transport);

  void invalidatePendingStreamFrames() override;
  void applyAdvertisingIdentity(PlatformModel model) override;

  static String deviceNameForModel(PlatformModel model);
  void setInitialDeviceName(const String& deviceName) { deviceName_ = deviceName; }
  const String& currentDeviceName() const { return deviceName_; }

private:
  BleTransport* transport_;
  String deviceName_;
};
