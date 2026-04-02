#pragma once

#include <Arduino.h>

enum class PlatformModel : uint8_t {
  BASE = 0,
  PLUS = 1,
  PRO = 2,
  ULTRA = 3
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

inline bool platformModelImpliesLaserInstalled(PlatformModel model) {
  return model != PlatformModel::BASE;
}

struct DeviceConfigSnapshot {
  PlatformModel platformModel = PlatformModel::PLUS;
  bool laserInstalled = true;
};
