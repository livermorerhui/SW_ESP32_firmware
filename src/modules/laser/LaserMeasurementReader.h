#pragma once

#include <Arduino.h>
#include <ModbusMaster.h>
#include "core/Types.h"

struct MeasurementReadResult {
  bool transportOk = false;
  bool validDistance = false;
  bool sentinel = false;
  uint8_t modbusResult = 0;
  uint16_t rawRegister = 0;
  int16_t signedRaw = 0;
  float scaledDistance = NAN;
  uint32_t readStartedAtMs = 0;
  uint32_t readCompletedAtMs = 0;
  uint32_t readDurationMs = 0;
  const char* invalidReason = nullptr;
};

class LaserMeasurementReader {
public:
  void begin();
  MeasurementReadResult read(
      TopState topState,
      bool laserInstalled,
      uint32_t nextReadBackoffMs);
  void clearFailureBurst(uint32_t now, bool flushSummary = true);

private:
  bool isDistanceSentinelRaw(uint16_t rawRegister, int16_t signedRaw, const char*& reason) const;
  bool isDistanceValidRaw(int16_t signedRaw, const char*& reason) const;
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

  ModbusMaster node;
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
};
