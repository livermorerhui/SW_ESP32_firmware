#include "modules/laser/CalibrationModelStore.h"

#include <math.h>
#include "config/GlobalConfig.h"

LegacyScaleParams CalibrationModelStore::loadLegacyParams(Preferences& preferences) const {
  LegacyScaleParams params{};
  params.zeroDistance = preferences.getFloat(kLegacyZeroKey, -22.0f);
  params.scaleFactor = preferences.getFloat(kLegacyFactorKey, 1.0f);
  return params;
}

void CalibrationModelStore::saveLegacyParams(
    Preferences& preferences,
    float zeroDistance,
    float scaleFactor) const {
  preferences.putFloat(kLegacyZeroKey, zeroDistance);
  preferences.putFloat(kLegacyFactorKey, scaleFactor);
}

CalibrationModel CalibrationModelStore::loadModel(
    Preferences& preferences,
    float defaultZeroDistance,
    float defaultScaleFactor) const {
  const uint8_t storedType = preferences.getUChar(kModelTypeKey, 0);
  CalibrationModel model{};

  if (storedType == static_cast<uint8_t>(CalibrationModelType::LINEAR) ||
      storedType == static_cast<uint8_t>(CalibrationModelType::QUADRATIC)) {
    model.type = static_cast<CalibrationModelType>(storedType);
    model.referenceDistance = preferences.getFloat(kModelReferenceKey, defaultZeroDistance);
    model.coefficients[0] = preferences.getFloat(kModelCoefficient0Key, 0.0f);
    model.coefficients[1] = preferences.getFloat(kModelCoefficient1Key, defaultScaleFactor);
    model.coefficients[2] = preferences.getFloat(kModelCoefficient2Key, 0.0f);
    return model;
  }

  model.type = CalibrationModelType::LINEAR;
  model.referenceDistance = defaultZeroDistance;
  model.coefficients[0] = 0.0f;
  model.coefficients[1] = defaultScaleFactor;
  model.coefficients[2] = 0.0f;
  return model;
}

void CalibrationModelStore::saveModel(Preferences& preferences, const CalibrationModel& model) const {
  preferences.putUChar(kModelTypeKey, static_cast<uint8_t>(model.type));
  preferences.putFloat(kModelReferenceKey, model.referenceDistance);
  preferences.putFloat(kModelCoefficient0Key, model.coefficients[0]);
  preferences.putFloat(kModelCoefficient1Key, model.coefficients[1]);
  preferences.putFloat(kModelCoefficient2Key, model.coefficients[2]);
}

bool CalibrationModelStore::isModelFinite(const CalibrationModel& model) const {
  const uint8_t type = static_cast<uint8_t>(model.type);
  if (type != static_cast<uint8_t>(CalibrationModelType::LINEAR) &&
      type != static_cast<uint8_t>(CalibrationModelType::QUADRATIC)) {
    return false;
  }

  if (!isfinite(model.referenceDistance)) return false;
  if (!isfinite(model.coefficients[0])) return false;
  if (!isfinite(model.coefficients[1])) return false;
  if (!isfinite(model.coefficients[2])) return false;
  return true;
}

bool CalibrationModelStore::isModelMonotonic(const CalibrationModel& model) const {
  const float minDistance = LASER_VALID_MEASUREMENT_MIN_RAW * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  const float maxDistance = LASER_VALID_MEASUREMENT_MAX_RAW * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  const float xMin = minDistance - model.referenceDistance;
  const float xMax = maxDistance - model.referenceDistance;
  const float kSlopeTolerance = -0.0001f;

  if (model.type == CalibrationModelType::LINEAR) {
    return model.coefficients[1] >= kSlopeTolerance;
  }

  const float dMin = 2.0f * model.coefficients[0] * xMin + model.coefficients[1];
  const float dMax = 2.0f * model.coefficients[0] * xMax + model.coefficients[1];
  return dMin >= kSlopeTolerance && dMax >= kSlopeTolerance;
}
