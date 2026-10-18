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

// Independent physical empty-distance record. The value is kept in the
// firmware's runtime distance unit (millimetres / 100) so state-chain and
// calibration validation use one representation; the BLE contract exposes
// physical millimetres.
struct EmptyDistanceRecord {
  float distanceRuntime = NAN;
  float toleranceRuntime = NAN;
  uint8_t backend = 0;
};

class CalibrationModelStore {
public:
  LegacyScaleParams loadLegacyParams(Preferences& preferences) const;
  bool saveLegacyParams(Preferences& preferences, float zeroDistance, float scaleFactor) const;
  CalibrationModel loadModel(
      Preferences& preferences,
      float defaultZeroDistance,
      float defaultScaleFactor) const;
  bool saveModel(Preferences& preferences, const CalibrationModel& model) const;
  bool clearModel(Preferences& preferences) const;
  bool hasStoredModel(Preferences& preferences) const;
  bool hasLegacyParams(Preferences& preferences) const;
  bool loadEmptyDistance(Preferences& preferences, EmptyDistanceRecord& out) const;
  bool saveEmptyDistance(
      Preferences& preferences,
      const EmptyDistanceRecord& record) const;
  bool clearEmptyDistance(Preferences& preferences) const;
  bool isModelFinite(const CalibrationModel& model) const;
  bool isModelMonotonic(const CalibrationModel& model) const;
  bool isModelMonotonic(
      const CalibrationModel& model,
      float minDistance,
      float maxDistance) const;

private:
  struct ModelRecordKeys {
    const char* type;
    const char* reference;
    const char* coefficient0;
    const char* coefficient1;
    const char* coefficient2;
    const char* schema;
    const char* generation;
    const char* commit;
    const char* crc;
  };

  static constexpr const char* kLegacyZeroKey = "zero";
  static constexpr const char* kLegacyFactorKey = "factor";
  static constexpr const char* kModelTypeKey = "mdl_t";
  static constexpr const char* kModelReferenceKey = "mdl_ref";
  static constexpr const char* kModelCoefficient0Key = "mdl_c0";
  static constexpr const char* kModelCoefficient1Key = "mdl_c1";
  static constexpr const char* kModelCoefficient2Key = "mdl_c2";
  static constexpr const char* kModelSchemaKey = "mdl_ver";
  static constexpr const char* kModelGenerationKey = "mdl_gen";
  static constexpr const char* kModelCommitKey = "mdl_ok";
  static constexpr const char* kModelCrcKey = "mdl_crc";
  static constexpr uint8_t kModelSchemaVersion = 1;

  static const ModelRecordKeys& primaryRecordKeys();
  static const ModelRecordKeys& backupRecordKeys();
  bool hasValidRecord(Preferences& preferences, const ModelRecordKeys& keys) const;
  CalibrationModel loadRecord(
      Preferences& preferences,
      const ModelRecordKeys& keys,
      float defaultZeroDistance,
      float defaultScaleFactor) const;
  void saveRecord(
      Preferences& preferences,
      const ModelRecordKeys& keys,
      const CalibrationModel& model,
      uint32_t generation) const;
  void removeRecord(Preferences& preferences, const ModelRecordKeys& keys) const;

  uint32_t modelCrc(const CalibrationModel& model, uint32_t generation) const;
  uint32_t emptyDistanceCrc(const EmptyDistanceRecord& record, uint32_t generation) const;

  struct EmptyDistanceRecordKeys {
    const char* distance;
    const char* tolerance;
    const char* backend;
    const char* schema;
    const char* generation;
    const char* commit;
    const char* crc;
  };
  static const EmptyDistanceRecordKeys& primaryEmptyRecordKeys();
  static const EmptyDistanceRecordKeys& backupEmptyRecordKeys();
  bool hasValidEmptyRecord(
      Preferences& preferences,
      const EmptyDistanceRecordKeys& keys,
      EmptyDistanceRecord& out) const;
  bool saveEmptyRecord(
      Preferences& preferences,
      const EmptyDistanceRecordKeys& keys,
      const EmptyDistanceRecord& record,
      uint32_t generation) const;
  void removeEmptyRecord(
      Preferences& preferences,
      const EmptyDistanceRecordKeys& keys) const;

  static constexpr const char* kEmptyDistanceKey = "emp_mm";
  static constexpr const char* kEmptyToleranceKey = "emp_tol";
  static constexpr const char* kEmptyBackendKey = "emp_bk";
  static constexpr const char* kEmptySchemaKey = "emp_ver";
  static constexpr const char* kEmptyGenerationKey = "emp_gen";
  static constexpr const char* kEmptyCommitKey = "emp_ok";
  static constexpr const char* kEmptyCrcKey = "emp_crc";
  static constexpr uint8_t kEmptySchemaVersion = 1;
};
