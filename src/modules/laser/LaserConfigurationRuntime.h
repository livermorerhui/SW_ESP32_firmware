#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/semphr.h>

#include "core/DeviceConfig.h"
#include "modules/laser/CalibrationModelStore.h"
#include "modules/laser/DeviceConfigStore.h"
#include "modules/laser/ProfileEmptyDistanceStore.h"

struct DualZeroState {
  float calibrationZeroDistance = 0.0f;
  float runtimeZeroDistance = 0.0f;
  bool runtimeZeroValid = false;
  bool occupiedCycleActive = false;
  bool effectiveZeroLocked = false;
  bool effectiveZeroUsesRuntime = false;
  float effectiveZeroDistance = 0.0f;
  uint32_t effectiveZeroLockedAtMs = 0;
  uint32_t runtimeZeroCapturedAtMs = 0;
  float lastRuntimeZeroCandidateDistance = 0.0f;
  uint32_t runtimeZeroWindowStartedAtMs = 0;
};

// Owns persisted device/calibration context and the runtime zero context.
// Policy and hardware I/O remain in their dedicated services/readers.
class LaserConfigurationRuntime {
public:
  DeviceConfigStore deviceConfigStore{};
  CalibrationModelStore calibrationModelStore{};
  ProfileEmptyDistanceStore profileEmptyDistanceStore{};
  Preferences preferences{};
  DeviceConfigSnapshot deviceConfig{};
  DeviceConfigStoreStatus configStatus = DeviceConfigStoreStatus::CONFIG_ERROR;
  bool preferencesOpened = false;
  volatile bool configTransitionActive = false;
  SemaphoreHandle_t profileTransitionMutex = nullptr;

  float zeroDistance = 0.0f;
  float scaleFactor = 1.0f;
  CalibrationModel calibrationModel{};
  EmptyDistanceRecord emptyDistanceRecord{};
  bool calibrationModelReady = false;
  bool emptyDistanceReady = false;
  DualZeroState dualZero{};
};
