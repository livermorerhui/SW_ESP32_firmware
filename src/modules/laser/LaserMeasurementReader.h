#pragma once

#include <Arduino.h>
#include <freertos/semphr.h>
#if !defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
#include <ModbusMaster.h>
#endif
#include "config/GlobalConfig.h"
#include "core/DeviceConfig.h"
#include "core/Types.h"
#include "modules/laser/LaserSensorProfile.h"
#if defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
#include "modules/laser/Cyd3s1612I2cReader.h"
#endif

enum class LaserMeasurementBackend : uint8_t {
  SENSOR_UNAVAILABLE,
  SENSOR_CONFLICT,
  CYD3S1612_IIC,
  LASER_485,
};

inline const char* laserMeasurementBackendName(LaserMeasurementBackend backend) {
  switch (backend) {
    case LaserMeasurementBackend::SENSOR_UNAVAILABLE: return "SENSOR_UNAVAILABLE";
    case LaserMeasurementBackend::SENSOR_CONFLICT: return "SENSOR_CONFLICT";
    case LaserMeasurementBackend::CYD3S1612_IIC: return "CYD3S1612_IIC";
    case LaserMeasurementBackend::LASER_485: return "LASER_485";
  }
  return "UNKNOWN";
}

struct MeasurementReadResult {
  bool transportOk = false;
  bool validDistance = false;
  bool sentinel = false;
  bool sampleFresh = false;
  bool valueRepeated = false;
  uint8_t modbusResult = 0;
  uint8_t transportError = 0;
  uint8_t receivedBytes = 0;
  uint16_t rawRegister = 0;
  uint16_t rawDistanceMm = 0;
  int16_t signedRaw = 0;
  // Formal physical unit contract.  This is millimetres for both backends.
  float physicalDistanceMm = NAN;
  // Compatibility unit consumed by the existing LaserModule path during the
  // migration.  It is physicalDistanceMm / 100 for the current profiles.
  float scaledDistance = NAN;
  uint32_t sampleSequence = 0;
  // Runtime reader generation, independent from persisted config generation.
  // A result from an earlier reader must not enter the new profile's state.
  uint32_t readerEpoch = 0;
  SensorProfileId sensorProfile = SensorProfileId::NONE;
  LaserMeasurementQuality quality = LaserMeasurementQuality::UNKNOWN;
  uint32_t readStartedAtMs = 0;
  uint32_t readCompletedAtMs = 0;
  uint32_t readDurationMs = 0;
  const char* invalidReason = nullptr;
};

class LaserMeasurementReader {
public:
  static bool supportsProfile(SensorProfileId profile);
  // Configure exactly one selected sensor. NONE and unknown identities leave
  // both physical transports stopped; no startup auto-detection is performed.
  bool configure(SensorProfileId profile);
  void stop();
  MeasurementReadResult read(
      TopState topState,
      bool laserInstalled,
      uint32_t nextReadBackoffMs);
  void clearFailureBurst(uint32_t now, bool flushSummary = true);
  LaserMeasurementBackend backend() const { return backend_; }
  SensorProfileId selectedProfile() const { return selectedProfile_; }
  uint32_t readerEpoch() const { return readerEpoch_; }

private:
  MeasurementReadResult readUnlocked(
      TopState topState,
      bool laserInstalled,
      uint32_t nextReadBackoffMs);
  bool ensureMutex();
  void stopUnlocked();
  void resetReaderState();
#if !defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
  uint8_t readDistanceRegister(const LaserSensorProfile& activeProfile);
#endif
#if defined(SW_DISTANCE_BACKEND_UNIFIED)
  MeasurementReadResult readIic(
      TopState topState,
      bool laserInstalled,
      uint32_t nextReadBackoffMs);
  MeasurementReadResult read485(
      TopState topState,
      bool laserInstalled,
      uint32_t nextReadBackoffMs);
#endif
  void noteReadFailure(
      uint8_t result,
      uint32_t now,
      uint32_t readDurationMs,
      TopState topState,
      bool laserInstalled);
  void noteTransientFailure(
      uint8_t result,
      uint32_t now,
      uint32_t readDurationMs,
      TopState topState,
      bool laserInstalled);
  void noteTransientRecovery(uint32_t now, uint32_t readDurationMs, TopState topState);
  void logMeasurementDiag(
      uint32_t now,
      uint8_t result,
      uint32_t readDurationMs,
      TopState topState,
      bool laserInstalled,
      uint32_t nextReadBackoffMs);

#if !defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
  ModbusMaster node;
#endif
  volatile LaserMeasurementBackend backend_ = LaserMeasurementBackend::SENSOR_UNAVAILABLE;
  volatile SensorProfileId selectedProfile_ = SensorProfileId::NONE;
  volatile uint32_t readerEpoch_ = 0;
  SemaphoreHandle_t mutex_ = nullptr;
  const LaserSensorProfile* profile = &activeLaserSensorProfile();
  bool hasLoggedReadFailure = false;
  uint8_t lastReadFailureCode = 0;
  uint32_t lastReadFailureLogMs = 0;
  uint32_t suppressedReadFailureCount = 0;

  bool transientActive = false;
  uint32_t transientStartedAtMs = 0;
  uint32_t transientLastFailureAtMs = 0;
  uint32_t transientLastLogAtMs = 0;
  uint32_t transientConsecutiveFailures = 0;
  uint32_t transientTotalFailures = 0;
  uint32_t transientTotalBursts = 0;
  uint32_t transientMaxBurstMs = 0;
  uint8_t transientLastCode = 0;
  TopState transientLastTopState = TopState::IDLE;

  uint32_t diagWindowStartedAtMs = 0;
  uint32_t diagLastLogAtMs = 0;
  uint32_t diagReadOkCount = 0;
  uint32_t diagReadFailCount = 0;
  uint32_t diagMaxReadMs = 0;
  uint32_t diagLastReadMs = 0;
  uint8_t diagLastResult = 0;
  uint32_t sampleSequence_ = 0;
  float lastPhysicalDistanceMm_ = NAN;
  bool hasLastPhysicalDistance_ = false;
#if defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
  Cyd3s1612I2cReader cydReader;
#endif
};
