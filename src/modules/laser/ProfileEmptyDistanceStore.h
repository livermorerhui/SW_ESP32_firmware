#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>
#include "core/DeviceConfig.h"

// Stable storage IDs for the physical transport. They are deliberately
// separate from LaserMeasurementBackend's runtime enum values.
enum class ProfileEmptyBackendId : uint8_t {
  NONE = 0,
  UART485 = 1,
  IIC = 2,
};

struct ProfileEmptyDistanceRecord {
  SensorProfileId sensorProfile = SensorProfileId::NONE;
  uint32_t configGeneration = 0;
  uint32_t emptyRevision = 0;
  bool isSet = false;
  float distanceMm = 0.0f;
  float toleranceMm = 0.0f;
  ProfileEmptyBackendId backend = ProfileEmptyBackendId::NONE;
};

enum class ProfileEmptyStoreStatus : uint8_t {
  UNSET,
  SET,
  STALE,
  STORAGE_UNAVAILABLE,
  CONFIG_ERROR,
  UNSUPPORTED_SCHEMA,
  UNSUPPORTED_PROFILE,
};

enum class ProfileEmptyStoreError : uint8_t {
  NONE,
  INVALID_RECORD,
  REVISION_OVERFLOW,
  CONFIG_ERROR,
  STORAGE_FAILED,
  VERIFY_FAILED,
};

struct ProfileEmptyLoadResult {
  ProfileEmptyStoreStatus status = ProfileEmptyStoreStatus::CONFIG_ERROR;
  // STALE preserves the previous record for diagnostics only. It is not an
  // empty-distance value for the active profile/generation.
  ProfileEmptyDistanceRecord record{};
};

struct ProfileEmptySaveResult {
  ProfileEmptyStoreError error = ProfileEmptyStoreError::CONFIG_ERROR;
  ProfileEmptyDistanceRecord record{};
  bool ok() const { return error == ProfileEmptyStoreError::NONE; }
};

class ProfileEmptyDistanceStore {
public:
  // The caller passes the actual Preferences.begin("scale_cal", false)
  // outcome; a failed NVS open is never interpreted as an empty record.
  ProfileEmptyLoadResult load(
      Preferences& preferences,
      bool storageOpened,
      SensorProfileId activeProfile,
      uint32_t activeGeneration) const;

  // The caller validates a fresh, in-range sensor sample before set().
  // This store validates finite representation and profile/backend binding,
  // then writes the other slot and reads it back before reporting success.
  ProfileEmptySaveResult set(
      Preferences& preferences,
      bool storageOpened,
      SensorProfileId activeProfile,
      uint32_t activeGeneration,
      float distanceMm,
      float toleranceMm,
      ProfileEmptyBackendId backend) const;

  // Clearing commits the same revisioned UNSET tombstone to both slots and
  // reads both back before success. It does not change config generation.
  ProfileEmptySaveResult clear(
      Preferences& preferences,
      bool storageOpened,
      SensorProfileId activeProfile,
      uint32_t activeGeneration) const;

  static const char* statusName(ProfileEmptyStoreStatus status);
  static const char* errorName(ProfileEmptyStoreError error);

private:
  ProfileEmptySaveResult save(
      Preferences& preferences,
      bool storageOpened,
      const ProfileEmptyDistanceRecord& nextWithoutRevision) const;
};
