#include "BleDeviceConfigEffects.h"

#include "BleTransport.h"
#include "config/GlobalConfig.h"

BleDeviceConfigEffects::BleDeviceConfigEffects(BleTransport* transport)
    : transport_(transport) {}

void BleDeviceConfigEffects::invalidatePendingStreamFrames() {
  if (transport_) {
    transport_->invalidatePendingStreamFrames();
  }
}

void BleDeviceConfigEffects::applyAdvertisingIdentity(PlatformModel model) {
  deviceName_ = deviceNameForModel(model);
  if (transport_) {
    transport_->updateAdvertisingIdentity(
        deviceName_.c_str(), platformModelName(model));
  }
}

String BleDeviceConfigEffects::deviceNameForModel(PlatformModel model) {
  switch (model) {
    case PlatformModel::BASE:
      return "SonicWave_Base";
    case PlatformModel::PLUS:
      return "SonicWave_Plus";
    case PlatformModel::PRO:
      return "SonicWave_Pro";
    case PlatformModel::ULTRA:
      return "SonicWave_Ultra";
  }
  return BLE_DEVICE_NAME;
}
