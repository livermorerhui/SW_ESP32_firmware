#pragma once

#include <Arduino.h>

enum class PlatformModel : uint8_t {
  BASE = 0,
  PLUS = 1,
  PRO = 2,
  ULTRA = 3
};

// Stable on-wire and persisted sensor identity. Keep these values separate
// from LaserSensorProfileId, whose older J100-only enum starts at zero.
enum class SensorProfileId : uint8_t {
  NONE = 0,
  J100_485 = 1,
  CYD3S1612_IIC = 2,
};

inline const char* sensorProfileName(SensorProfileId profile) {
  switch (profile) {
    case SensorProfileId::NONE: return "NONE";
    case SensorProfileId::J100_485: return "J100_485";
    case SensorProfileId::CYD3S1612_IIC: return "CYD3S1612_IIC";
  }
  return "UNKNOWN";
}

inline bool isKnownSensorProfile(SensorProfileId profile) {
  switch (profile) {
    case SensorProfileId::NONE:
    case SensorProfileId::J100_485:
    case SensorProfileId::CYD3S1612_IIC:
      return true;
  }
  return false;
}

inline bool parseSensorProfile(const String& raw, SensorProfileId& out) {
  // Schema 1 IDs are exact protocol tokens, not case-insensitive UI labels.
  if (raw == "NONE") {
    out = SensorProfileId::NONE;
    return true;
  }
  if (raw == "J100_485") {
    out = SensorProfileId::J100_485;
    return true;
  }
  if (raw == "CYD3S1612_IIC") {
    out = SensorProfileId::CYD3S1612_IIC;
    return true;
  }
  return false;
}

enum class LaserInstallConstraint : uint8_t {
  FORBIDDEN = 0,
  REQUIRED = 1,
};

inline const char* platformModelName(PlatformModel model) {
  switch (model) {
    case PlatformModel::BASE: return "BASE";
    case PlatformModel::PLUS: return "PLUS";
    case PlatformModel::PRO: return "PRO";
    case PlatformModel::ULTRA: return "ULTRA";
  }
  return "UNKNOWN";
}

inline bool isKnownPlatformModel(PlatformModel model) {
  switch (model) {
    case PlatformModel::BASE:
    case PlatformModel::PLUS:
    case PlatformModel::PRO:
    case PlatformModel::ULTRA:
      return true;
  }
  return false;
}

inline bool parsePlatformModel(const String& raw, PlatformModel& out) {
  if (raw.equalsIgnoreCase("BASE")) {
    out = PlatformModel::BASE;
    return true;
  }
  if (raw.equalsIgnoreCase("PLUS")) {
    out = PlatformModel::PLUS;
    return true;
  }
  if (raw.equalsIgnoreCase("PRO")) {
    out = PlatformModel::PRO;
    return true;
  }
  if (raw.equalsIgnoreCase("ULTRA")) {
    out = PlatformModel::ULTRA;
    return true;
  }
  return false;
}

inline LaserInstallConstraint platformModelLaserInstallConstraint(PlatformModel model) {
  switch (model) {
    case PlatformModel::BASE:
      return LaserInstallConstraint::FORBIDDEN;
    case PlatformModel::PLUS:
    case PlatformModel::PRO:
    case PlatformModel::ULTRA:
      return LaserInstallConstraint::REQUIRED;
  }
  return LaserInstallConstraint::REQUIRED;
}

inline const char* laserInstallConstraintName(LaserInstallConstraint constraint) {
  switch (constraint) {
    case LaserInstallConstraint::FORBIDDEN:
      return "FORBIDDEN";
    case LaserInstallConstraint::REQUIRED:
      return "REQUIRED";
  }
  return "UNKNOWN";
}

inline bool isLaserInstallAllowedForPlatformModel(PlatformModel model, bool laserInstalled) {
  switch (platformModelLaserInstallConstraint(model)) {
    case LaserInstallConstraint::FORBIDDEN:
      return !laserInstalled;
    case LaserInstallConstraint::REQUIRED:
      return laserInstalled;
  }
  return false;
}

inline bool normalizedLaserInstalledForPlatformModel(PlatformModel model) {
  switch (platformModelLaserInstallConstraint(model)) {
    case LaserInstallConstraint::FORBIDDEN:
      return false;
    case LaserInstallConstraint::REQUIRED:
      return true;
  }
  return true;
}

inline bool platformModelImpliesLaserInstalled(PlatformModel model) {
  return normalizedLaserInstalledForPlatformModel(model);
}

struct DeviceConfigSnapshot {
  PlatformModel platformModel = PlatformModel::BASE;
  bool laserInstalled = false;
  SensorProfileId sensorProfile = SensorProfileId::NONE;
  uint32_t generation = 0;
  bool schema1Managed = false;
};
