#pragma once

#include <Arduino.h>

enum class PlatformModel : uint8_t {
  BASE = 0,
  PLUS = 1,
  PRO = 2,
  ULTRA = 3
};

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
  PlatformModel platformModel = PlatformModel::PLUS;
  bool laserInstalled = true;
};
