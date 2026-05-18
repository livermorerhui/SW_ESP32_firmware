#include "modules/laser/DeviceConfigStore.h"

DeviceConfigLoadResult DeviceConfigStore::load(Preferences& preferences) const {
  DeviceConfigLoadResult result{};

  uint8_t storedModel = preferences.getUChar(
      kPlatformModelKey,
      static_cast<uint8_t>(PlatformModel::PLUS));
  PlatformModel parsedModel = static_cast<PlatformModel>(storedModel);
  if (!isKnownPlatformModel(parsedModel)) {
    parsedModel = PlatformModel::PLUS;
  }

  result.storedLaserInstalled = preferences.getUChar(kLaserInstalledKey, 1);
  const bool normalizedLaserInstalled = normalizedLaserInstalledForPlatformModel(parsedModel);

  result.config.platformModel = parsedModel;
  result.config.laserInstalled = normalizedLaserInstalled;
  result.laserInstalledNormalized =
      result.storedLaserInstalled != static_cast<uint8_t>(normalizedLaserInstalled);
  return result;
}

void DeviceConfigStore::save(Preferences& preferences, const DeviceConfigSnapshot& config) const {
  preferences.putUChar(kPlatformModelKey, static_cast<uint8_t>(config.platformModel));
  preferences.putUChar(kLaserInstalledKey, config.laserInstalled ? 1 : 0);
}
