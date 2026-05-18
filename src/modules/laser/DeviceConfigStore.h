#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "core/DeviceConfig.h"

struct DeviceConfigLoadResult {
  DeviceConfigSnapshot config{};
  uint8_t storedLaserInstalled = 1;
  bool laserInstalledNormalized = false;
};

class DeviceConfigStore {
public:
  DeviceConfigLoadResult load(Preferences& preferences) const;
  void save(Preferences& preferences, const DeviceConfigSnapshot& config) const;

private:
  static constexpr const char* kPlatformModelKey = "cfg_model";
  static constexpr const char* kLaserInstalledKey = "cfg_laser";
};
