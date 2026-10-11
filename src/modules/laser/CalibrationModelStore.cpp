#include "modules/laser/CalibrationModelStore.h"
#include "core/VerboseSerial.h"

#include <math.h>
#include "config/GlobalConfig.h"

const CalibrationModelStore::ModelRecordKeys& CalibrationModelStore::primaryRecordKeys() {
  static const ModelRecordKeys keys{
      kModelTypeKey,
      kModelReferenceKey,
      kModelCoefficient0Key,
      kModelCoefficient1Key,
      kModelCoefficient2Key,
      kModelSchemaKey,
      kModelGenerationKey,
      kModelCommitKey,
      kModelCrcKey,
  };
  return keys;
}

const CalibrationModelStore::ModelRecordKeys& CalibrationModelStore::backupRecordKeys() {
  // Keys are deliberately short because ESP32 Preferences/NVS key names have
  // a small fixed maximum length. The backup is a last-known-good model used
  // if power is lost while the primary record is being replaced.
  static const ModelRecordKeys keys{
      "b_t",
      "b_ref",
      "b_c0",
      "b_c1",
      "b_c2",
      "b_ver",
      "b_gen",
      "b_ok",
      "b_crc",
  };
  return keys;
}

const CalibrationModelStore::EmptyDistanceRecordKeys& CalibrationModelStore::primaryEmptyRecordKeys() {
  static const EmptyDistanceRecordKeys keys{
      kEmptyDistanceKey, kEmptyToleranceKey, kEmptyBackendKey,
      kEmptySchemaKey, kEmptyGenerationKey, kEmptyCommitKey, kEmptyCrcKey};
  return keys;
}

const CalibrationModelStore::EmptyDistanceRecordKeys& CalibrationModelStore::backupEmptyRecordKeys() {
  static const EmptyDistanceRecordKeys keys{
      "e_b_mm", "e_b_tol", "e_b_bk", "e_b_ver", "e_b_gen", "e_b_ok", "e_b_crc"};
  return keys;
}

LegacyScaleParams CalibrationModelStore::loadLegacyParams(Preferences& preferences) const {
  LegacyScaleParams params{};
  params.zeroDistance = preferences.getFloat(kLegacyZeroKey, -22.0f);
  params.scaleFactor = preferences.getFloat(kLegacyFactorKey, 1.0f);
  return params;
}

bool CalibrationModelStore::saveLegacyParams(
    Preferences& preferences,
    float zeroDistance,
    float scaleFactor) const {
  preferences.putFloat(kLegacyZeroKey, zeroDistance);
  preferences.putFloat(kLegacyFactorKey, scaleFactor);
  return isfinite(preferences.getFloat(kLegacyZeroKey, NAN)) &&
      isfinite(preferences.getFloat(kLegacyFactorKey, NAN)) &&
      fabsf(preferences.getFloat(kLegacyZeroKey, NAN) - zeroDistance) < 0.0001f &&
      fabsf(preferences.getFloat(kLegacyFactorKey, NAN) - scaleFactor) < 0.0001f;
}

CalibrationModel CalibrationModelStore::loadModel(
    Preferences& preferences,
    float defaultZeroDistance,
    float defaultScaleFactor) const {
  const ModelRecordKeys& primary = primaryRecordKeys();
  if (hasValidRecord(preferences, primary)) {
    return loadRecord(preferences, primary, defaultZeroDistance, defaultScaleFactor);
  }

  const ModelRecordKeys& backup = backupRecordKeys();
  if (hasValidRecord(preferences, backup)) {
    SW_VERBOSE_SERIAL_PRINTLN("[CAL] MODEL restore_source=backup");
    return loadRecord(preferences, backup, defaultZeroDistance, defaultScaleFactor);
  }

  CalibrationModel fallback{};
  fallback.type = CalibrationModelType::LINEAR;
  fallback.referenceDistance = defaultZeroDistance;
  fallback.coefficients[0] = 0.0f;
  fallback.coefficients[1] = defaultScaleFactor;
  fallback.coefficients[2] = 0.0f;
  return fallback;
}

bool CalibrationModelStore::saveModel(Preferences& preferences, const CalibrationModel& model) const {
  if (!isModelFinite(model)) return false;
  const ModelRecordKeys& primary = primaryRecordKeys();
  const ModelRecordKeys& backup = backupRecordKeys();
  uint32_t previousGeneration = 0;

  if (hasValidRecord(preferences, primary)) {
    CalibrationModel previous = loadRecord(preferences, primary, 0.0f, 1.0f);
    previousGeneration = preferences.getUInt(primary.generation, 0);
    saveRecord(preferences, backup, previous, previousGeneration);
  } else if (hasValidRecord(preferences, backup)) {
    previousGeneration = preferences.getUInt(backup.generation, 0);
  }

  const uint32_t generation = previousGeneration == 0 ? 1 : previousGeneration + 1;
  // The commit marker is written last. The previous committed model is also
  // kept in a backup record, so a torn primary write can be rolled back.
  saveRecord(preferences, primary, model, generation);
  return hasValidRecord(preferences, primary);
}

bool CalibrationModelStore::clearModel(Preferences& preferences) const {
  removeRecord(preferences, primaryRecordKeys());
  removeRecord(preferences, backupRecordKeys());
  preferences.remove(kLegacyZeroKey);
  preferences.remove(kLegacyFactorKey);
  return !hasStoredModel(preferences) && !hasLegacyParams(preferences);
}

bool CalibrationModelStore::hasStoredModel(Preferences& preferences) const {
  return hasValidRecord(preferences, primaryRecordKeys()) ||
      hasValidRecord(preferences, backupRecordKeys());
}

bool CalibrationModelStore::hasLegacyParams(Preferences& preferences) const {
  return preferences.isKey(kLegacyZeroKey) || preferences.isKey(kLegacyFactorKey);
}

bool CalibrationModelStore::loadEmptyDistance(
    Preferences& preferences,
    EmptyDistanceRecord& out) const {
  EmptyDistanceRecord primary{};
  EmptyDistanceRecord backup{};
  const bool primaryValid = hasValidEmptyRecord(
      preferences, primaryEmptyRecordKeys(), primary);
  const bool backupValid = hasValidEmptyRecord(
      preferences, backupEmptyRecordKeys(), backup);
  if (!primaryValid && !backupValid) {
    out = EmptyDistanceRecord{};
    return false;
  }
  // A backup may be newer after a power loss during primary replacement.
  const uint32_t primaryGeneration = preferences.getUInt(
      primaryEmptyRecordKeys().generation, 0);
  const uint32_t backupGeneration = preferences.getUInt(
      backupEmptyRecordKeys().generation, 0);
  out = backupValid && (!primaryValid || backupGeneration > primaryGeneration)
      ? backup : primary;
  return true;
}

bool CalibrationModelStore::saveEmptyDistance(
    Preferences& preferences,
    const EmptyDistanceRecord& record) const {
  if (!isfinite(record.distanceRuntime) || record.distanceRuntime <= 0.0f ||
      !isfinite(record.toleranceRuntime) || record.toleranceRuntime < 0.0f) {
    return false;
  }
  EmptyDistanceRecord primaryRecord{};
  EmptyDistanceRecord backupRecord{};
  const bool havePrimary = hasValidEmptyRecord(
      preferences, primaryEmptyRecordKeys(), primaryRecord);
  const bool haveBackup = hasValidEmptyRecord(
      preferences, backupEmptyRecordKeys(), backupRecord);
  const uint32_t primaryGeneration = preferences.getUInt(
      primaryEmptyRecordKeys().generation, 0);
  const uint32_t backupGeneration = preferences.getUInt(
      backupEmptyRecordKeys().generation, 0);
  const uint32_t previous = (havePrimary || haveBackup)
      ? (primaryGeneration > backupGeneration ? primaryGeneration : backupGeneration)
      : 0;
  const uint32_t generation = previous == 0 ? 1 : previous + 1;
  if (havePrimary) {
    const uint32_t oldGeneration = primaryGeneration;
    if (!saveEmptyRecord(preferences, backupEmptyRecordKeys(), primaryRecord, oldGeneration)) {
      return false;
    }
  }
  if (!saveEmptyRecord(preferences, primaryEmptyRecordKeys(), record, generation)) {
    return false;
  }
  EmptyDistanceRecord verified{};
  if (!loadEmptyDistance(preferences, verified)) return false;
  return fabsf(verified.distanceRuntime - record.distanceRuntime) < 0.0001f &&
      fabsf(verified.toleranceRuntime - record.toleranceRuntime) < 0.0001f &&
      verified.backend == record.backend;
}

bool CalibrationModelStore::clearEmptyDistance(Preferences& preferences) const {
  removeEmptyRecord(preferences, primaryEmptyRecordKeys());
  removeEmptyRecord(preferences, backupEmptyRecordKeys());
  EmptyDistanceRecord verified{};
  return !loadEmptyDistance(preferences, verified);
}

bool CalibrationModelStore::hasValidEmptyRecord(
    Preferences& preferences,
    const EmptyDistanceRecordKeys& keys,
    EmptyDistanceRecord& out) const {
  out = EmptyDistanceRecord{};
  if (preferences.getUChar(keys.schema, 0) != kEmptySchemaVersion ||
      preferences.getUChar(keys.commit, 0) != 1) return false;
  const uint32_t generation = preferences.getUInt(keys.generation, 0);
  if (generation == 0) return false;
  out.distanceRuntime = preferences.getFloat(keys.distance, NAN);
  out.toleranceRuntime = preferences.getFloat(keys.tolerance, NAN);
  out.backend = preferences.getUChar(keys.backend, 0);
  if (!isfinite(out.distanceRuntime) || out.distanceRuntime <= 0.0f ||
      !isfinite(out.toleranceRuntime) || out.toleranceRuntime < 0.0f) return false;
  return emptyDistanceCrc(out, generation) == preferences.getUInt(keys.crc, 0);
}

bool CalibrationModelStore::saveEmptyRecord(
    Preferences& preferences,
    const EmptyDistanceRecordKeys& keys,
    const EmptyDistanceRecord& record,
    uint32_t generation) const {
  preferences.putUChar(keys.commit, 0);
  preferences.putFloat(keys.distance, record.distanceRuntime);
  preferences.putFloat(keys.tolerance, record.toleranceRuntime);
  preferences.putUChar(keys.backend, record.backend);
  preferences.putUChar(keys.schema, kEmptySchemaVersion);
  preferences.putUInt(keys.generation, generation);
  preferences.putUInt(keys.crc, emptyDistanceCrc(record, generation));
  preferences.putUChar(keys.commit, 1);
  EmptyDistanceRecord verify{};
  return hasValidEmptyRecord(preferences, keys, verify) &&
      fabsf(verify.distanceRuntime - record.distanceRuntime) < 0.0001f &&
      fabsf(verify.toleranceRuntime - record.toleranceRuntime) < 0.0001f &&
      verify.backend == record.backend;
}

void CalibrationModelStore::removeEmptyRecord(
    Preferences& preferences,
    const EmptyDistanceRecordKeys& keys) const {
  preferences.putUChar(keys.commit, 0);
  preferences.remove(keys.distance);
  preferences.remove(keys.tolerance);
  preferences.remove(keys.backend);
  preferences.remove(keys.schema);
  preferences.remove(keys.generation);
  preferences.remove(keys.crc);
}

bool CalibrationModelStore::hasValidRecord(
    Preferences& preferences,
    const ModelRecordKeys& keys) const {
  const uint8_t storedType = preferences.getUChar(keys.type, 0);
  if (storedType != static_cast<uint8_t>(CalibrationModelType::LINEAR) &&
      storedType != static_cast<uint8_t>(CalibrationModelType::QUADRATIC)) {
    return false;
  }
  if (preferences.getUChar(keys.schema, 0) != kModelSchemaVersion ||
      preferences.getUChar(keys.commit, 0) != 1) {
    return false;
  }
  const uint32_t generation = preferences.getUInt(keys.generation, 0);
  if (generation == 0) return false;

  CalibrationModel model{};
  model.type = static_cast<CalibrationModelType>(storedType);
  model.referenceDistance = preferences.getFloat(keys.reference, NAN);
  model.coefficients[0] = preferences.getFloat(keys.coefficient0, NAN);
  model.coefficients[1] = preferences.getFloat(keys.coefficient1, NAN);
  model.coefficients[2] = preferences.getFloat(keys.coefficient2, NAN);
  if (!isModelFinite(model)) return false;
  return modelCrc(model, generation) == preferences.getUInt(keys.crc, 0);
}

CalibrationModel CalibrationModelStore::loadRecord(
    Preferences& preferences,
    const ModelRecordKeys& keys,
    float defaultZeroDistance,
    float defaultScaleFactor) const {
  CalibrationModel model{};
  const uint8_t storedType = preferences.getUChar(keys.type, 0);
  if (storedType == static_cast<uint8_t>(CalibrationModelType::LINEAR) ||
      storedType == static_cast<uint8_t>(CalibrationModelType::QUADRATIC)) {
    model.type = static_cast<CalibrationModelType>(storedType);
  } else {
    model.type = CalibrationModelType::LINEAR;
  }
  model.referenceDistance = preferences.getFloat(keys.reference, defaultZeroDistance);
  model.coefficients[0] = preferences.getFloat(keys.coefficient0, 0.0f);
  model.coefficients[1] = preferences.getFloat(keys.coefficient1, defaultScaleFactor);
  model.coefficients[2] = preferences.getFloat(keys.coefficient2, 0.0f);
  return model;
}

void CalibrationModelStore::saveRecord(
    Preferences& preferences,
    const ModelRecordKeys& keys,
    const CalibrationModel& model,
    uint32_t generation) const {
  preferences.putUChar(keys.commit, 0);
  preferences.putUChar(keys.type, static_cast<uint8_t>(model.type));
  preferences.putFloat(keys.reference, model.referenceDistance);
  preferences.putFloat(keys.coefficient0, model.coefficients[0]);
  preferences.putFloat(keys.coefficient1, model.coefficients[1]);
  preferences.putFloat(keys.coefficient2, model.coefficients[2]);
  preferences.putUChar(keys.schema, kModelSchemaVersion);
  preferences.putUInt(keys.generation, generation);
  preferences.putUInt(keys.crc, modelCrc(model, generation));
  preferences.putUChar(keys.commit, 1);
}

void CalibrationModelStore::removeRecord(
    Preferences& preferences,
    const ModelRecordKeys& keys) const {
  preferences.putUChar(keys.commit, 0);
  preferences.remove(keys.type);
  preferences.remove(keys.reference);
  preferences.remove(keys.coefficient0);
  preferences.remove(keys.coefficient1);
  preferences.remove(keys.coefficient2);
  preferences.remove(keys.schema);
  preferences.remove(keys.generation);
  preferences.remove(keys.crc);
}

uint32_t CalibrationModelStore::modelCrc(
    const CalibrationModel& model,
    uint32_t generation) const {
  // Small deterministic FNV-1a checksum; this is integrity detection, not a
  // security primitive.
  uint32_t hash = 2166136261UL;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&model);
  for (size_t i = 0; i < sizeof(model); ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  const uint8_t* generationBytes = reinterpret_cast<const uint8_t*>(&generation);
  for (size_t i = 0; i < sizeof(generation); ++i) {
    hash ^= generationBytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t CalibrationModelStore::emptyDistanceCrc(
    const EmptyDistanceRecord& record,
    uint32_t generation) const {
  uint32_t hash = 2166136261UL;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  for (size_t i = 0; i < sizeof(record); ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  const uint8_t* generationBytes = reinterpret_cast<const uint8_t*>(&generation);
  for (size_t i = 0; i < sizeof(generation); ++i) {
    hash ^= generationBytes[i];
    hash *= 16777619UL;
  }
  return hash;
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
  return isModelMonotonic(
      model,
      LASER_VALID_RUNTIME_DISTANCE_MIN,
      LASER_VALID_RUNTIME_DISTANCE_MAX);
}

bool CalibrationModelStore::isModelMonotonic(
    const CalibrationModel& model,
    float minDistance,
    float maxDistance) const {
  const float xMin = minDistance - model.referenceDistance;
  const float xMax = maxDistance - model.referenceDistance;
  const float kSlopeTolerance = 0.0001f;

  if (model.type == CalibrationModelType::LINEAR) {
    return fabsf(model.coefficients[1]) >= kSlopeTolerance;
  }

  const float dMin = 2.0f * model.coefficients[0] * xMin + model.coefficients[1];
  const float dMax = 2.0f * model.coefficients[0] * xMax + model.coefficients[1];
  return (dMin >= kSlopeTolerance && dMax >= kSlopeTolerance) ||
      (dMin <= -kSlopeTolerance && dMax <= -kSlopeTolerance);
}
