#pragma once

#include <Arduino.h>

#include "modules/laser/CalibrationModelStore.h"

struct EffectiveZeroInput {
  float calibrationZeroDistance = 0.0f;
  float calibrationModelReferenceDistance = 0.0f;
  float legacyZeroDistance = 0.0f;
  bool applyRuntimeZero = false;
  bool runtimeZeroValid = false;
  float runtimeZeroDistance = 0.0f;
  float clampMaxOffsetFromCalibration = 0.0f;
  bool effectiveZeroLocked = false;
  float lockedEffectiveZeroDistance = 0.0f;
};

struct EffectiveZeroResult {
  float calibrationZeroDistance = 0.0f;
  float effectiveZeroDistance = 0.0f;
  bool usesRuntimeZero = false;
};

// CalibrationRuntime owns pure calibration/runtime-zero math only.
// It does not read Preferences, mutate LaserModule state, publish BLE events,
// or decide when runtime-zero should refresh.
namespace CalibrationRuntime {

float evaluateWeight(
    const CalibrationModel& model,
    float distance,
    float zeroReferenceDistance);

float evaluateClampedWeight(
    const CalibrationModel& model,
    float distance,
    float zeroReferenceDistance);

EffectiveZeroResult computeUnlockedEffectiveZero(const EffectiveZeroInput& input);

EffectiveZeroResult computeEffectiveZero(const EffectiveZeroInput& input);

}  // namespace CalibrationRuntime
