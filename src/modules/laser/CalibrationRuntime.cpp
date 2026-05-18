#include "modules/laser/CalibrationRuntime.h"

#include <math.h>

namespace {

float firstFinite(float first, float second, float fallback) {
  if (isfinite(first)) return first;
  if (isfinite(second)) return second;
  if (isfinite(fallback)) return fallback;
  return 0.0f;
}

}  // namespace

namespace CalibrationRuntime {

float evaluateWeight(
    const CalibrationModel& model,
    float distance,
    float zeroReferenceDistance) {
  const float x = distance - zeroReferenceDistance;
  return model.coefficients[0] * x * x +
      model.coefficients[1] * x +
      model.coefficients[2];
}

float evaluateClampedWeight(
    const CalibrationModel& model,
    float distance,
    float zeroReferenceDistance) {
  const float weight = evaluateWeight(model, distance, zeroReferenceDistance);
  if (!isfinite(weight)) return NAN;
  return weight < 0.0f ? 0.0f : weight;
}

EffectiveZeroResult computeUnlockedEffectiveZero(const EffectiveZeroInput& input) {
  EffectiveZeroResult result{};
  const float calibrationZero = firstFinite(
      input.calibrationZeroDistance,
      input.calibrationModelReferenceDistance,
      input.legacyZeroDistance);

  result.calibrationZeroDistance = calibrationZero;
  result.effectiveZeroDistance = calibrationZero;
  result.usesRuntimeZero = false;

  if (input.applyRuntimeZero &&
      input.runtimeZeroValid &&
      isfinite(input.runtimeZeroDistance)) {
    float runtimeDelta = input.runtimeZeroDistance - calibrationZero;
    const float clamp = input.clampMaxOffsetFromCalibration;
    if (runtimeDelta > clamp) runtimeDelta = clamp;
    if (runtimeDelta < -clamp) runtimeDelta = -clamp;
    result.effectiveZeroDistance = calibrationZero + runtimeDelta;
    result.usesRuntimeZero = fabsf(result.effectiveZeroDistance - calibrationZero) > 0.0001f;
  }

  if (!isfinite(result.effectiveZeroDistance)) {
    result.effectiveZeroDistance = 0.0f;
    result.usesRuntimeZero = false;
  }
  return result;
}

EffectiveZeroResult computeEffectiveZero(const EffectiveZeroInput& input) {
  if (input.effectiveZeroLocked) {
    EffectiveZeroResult result{};
    result.calibrationZeroDistance = firstFinite(
        input.calibrationZeroDistance,
        input.calibrationModelReferenceDistance,
        input.legacyZeroDistance);
    result.effectiveZeroDistance =
        isfinite(input.lockedEffectiveZeroDistance) ? input.lockedEffectiveZeroDistance : 0.0f;
    result.usesRuntimeZero =
        fabsf(result.effectiveZeroDistance - result.calibrationZeroDistance) > 0.0001f;
    return result;
  }
  return computeUnlockedEffectiveZero(input);
}

}  // namespace CalibrationRuntime
