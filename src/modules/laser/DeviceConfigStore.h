#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "core/DeviceConfig.h"

// BLANK is only safe to interpret as a new Base board after the production
// inventory has proved that this device is not an unbridged legacy device.
// The firmware cannot distinguish those two physical histories from NVS.
enum class DeviceConfigStoreStatus : uint8_t {
  BLANK,
  LEGACY,
  LEGACY_RESERVED,
  READY,
  STORAGE_UNAVAILABLE,
  CONFIG_ERROR,
  UNSUPPORTED_SCHEMA,
  UNSUPPORTED_PROFILE,
};

enum class DeviceConfigStoreError : uint8_t {
  NONE,
  INVALID_CONFIG,
  UNSUPPORTED_MODEL,
  GENERATION_OVERFLOW,
  CONFIG_ERROR,
  STORAGE_FAILED,
  VERIFY_FAILED,
};

struct DeviceConfigLoadResult {
  DeviceConfigSnapshot config{};
  DeviceConfigStoreStatus status = DeviceConfigStoreStatus::CONFIG_ERROR;
  uint8_t storedLaserInstalled = 0xFF;
  bool laserInstalledNormalized = false;
};

struct DeviceConfigSaveResult {
  DeviceConfigSnapshot config{};
  DeviceConfigStoreError error = DeviceConfigStoreError::CONFIG_ERROR;
  bool changed = false;

  bool ok() const { return error == DeviceConfigStoreError::NONE; }
};

class DeviceConfigStore {
public:
  // Caller must pass the actual Preferences.begin("scale_cal", false) result.
  // load never repairs or writes NVS; all migration is an explicit save.
  DeviceConfigLoadResult load(Preferences& preferences, bool storageOpened) const;

  // Only BASE/PLUS are writable by the new schema-1 profile command.  The
  // returned config is authoritative only after all writes and readbacks pass.
  DeviceConfigSaveResult save(
      Preferences& preferences,
      bool storageOpened,
      const DeviceConfigSnapshot& current,
      PlatformModel nextModel,
      SensorProfileId nextProfile) const;

  // Old DEVICE:SET_CONFIG retains its reserved PRO/ULTRA write behavior, but
  // it cannot choose a sensor profile.  The new profile command rejects them.
  DeviceConfigSaveResult saveLegacyReserved(
      Preferences& preferences,
      bool storageOpened,
      const DeviceConfigSnapshot& current,
      PlatformModel nextModel) const;

  static const char* statusName(DeviceConfigStoreStatus status);
  static const char* errorName(DeviceConfigStoreError error);

private:
  static constexpr const char* kPlatformModelKey = "cfg_model";
  static constexpr const char* kLaserInstalledKey = "cfg_laser";
  static constexpr const char* kSlot0Key = "cfg_slot0";
  static constexpr const char* kSlot1Key = "cfg_slot1";

  DeviceConfigSaveResult saveInternal(
      Preferences& preferences,
      bool storageOpened,
      const DeviceConfigSnapshot& current,
      PlatformModel nextModel,
      SensorProfileId nextProfile,
      bool allowReserved) const;
};
