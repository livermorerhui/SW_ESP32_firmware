#pragma once

#include <Arduino.h>
#include <Preferences.h>

enum class CalibrationModelType : uint8_t {
  LINEAR = 1,
  QUADRATIC = 2
};

struct CalibrationModel {
  CalibrationModelType type = CalibrationModelType::LINEAR;
  float referenceDistance = 0.0f;
  float coefficients[3] = {0.0f, 1.0f, 0.0f};
};

struct LegacyScaleParams {
  float zeroDistance = -22.0f;
  float scaleFactor = 1.0f;
};

class CalibrationModelStore {
public:
  LegacyScaleParams loadLegacyParams(Preferences& preferences) const;
  void saveLegacyParams(Preferences& preferences, float zeroDistance, float scaleFactor) const;
  CalibrationModel loadModel(
      Preferences& preferences,
      float defaultZeroDistance,
      float defaultScaleFactor) const;
  void saveModel(Preferences& preferences, const CalibrationModel& model) const;
  bool isModelFinite(const CalibrationModel& model) const;
  bool isModelMonotonic(const CalibrationModel& model) const;

private:
  static constexpr const char* kLegacyZeroKey = "zero";
  static constexpr const char* kLegacyFactorKey = "factor";
  static constexpr const char* kModelTypeKey = "mdl_t";
  static constexpr const char* kModelReferenceKey = "mdl_ref";
  static constexpr const char* kModelCoefficient0Key = "mdl_c0";
  static constexpr const char* kModelCoefficient1Key = "mdl_c1";
  static constexpr const char* kModelCoefficient2Key = "mdl_c2";
};
