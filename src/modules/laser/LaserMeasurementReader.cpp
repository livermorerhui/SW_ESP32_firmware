#include "modules/laser/LaserMeasurementReader.h"
#include "config/GlobalConfig.h"

namespace {
constexpr uint32_t kReadFailureSummaryIntervalMs = 10000UL;
constexpr uint32_t kMeasurementDiagSummaryIntervalMs = 2000UL;
constexpr uint32_t kMeasurementDiagSlowReadWarnMs = 80UL;
}

void LaserMeasurementReader::begin() {
  profile = &activeLaserSensorProfile();
  Serial1.begin(profile->serial.baud, profile->serial.serialConfig, RX_PIN, TX_PIN);
  node.begin(profile->modbus.slaveId, Serial1);
  Serial.printf(
      "[LASER_SENSOR_PROFILE] active=%s baud=%lu slave_id=%u function=0x%02X register=0x%04X count=%u\n",
      profile->stableName,
      static_cast<unsigned long>(profile->serial.baud),
      static_cast<unsigned>(profile->modbus.slaveId),
      static_cast<unsigned>(profile->modbus.distanceReadFunction),
      static_cast<unsigned>(profile->modbus.distanceRegister),
      static_cast<unsigned>(profile->modbus.distanceRegisterCount));
}

MeasurementReadResult LaserMeasurementReader::read(
    TopState topState,
    bool laserInstalled,
    uint32_t nextReadBackoffMs) {
  MeasurementReadResult out{};
  out.readStartedAtMs = millis();
  out.modbusResult = readDistanceRegister(*profile);
  out.readCompletedAtMs = millis();
  out.readDurationMs = out.readCompletedAtMs - out.readStartedAtMs;

  logMeasurementDiag(
      out.readCompletedAtMs,
      out.modbusResult,
      out.readDurationMs,
      topState,
      laserInstalled,
      nextReadBackoffMs);

  if (out.modbusResult != node.ku8MBSuccess) {
    noteReadFailure(
        out.modbusResult,
        out.readCompletedAtMs,
        out.readDurationMs,
        topState,
        laserInstalled);
    out.invalidReason = "READ_FAIL";
    return out;
  }

  noteTransientRecovery(out.readCompletedAtMs, out.readDurationMs, topState);
  clearFailureBurst(out.readCompletedAtMs, true);
  out.transportOk = true;

  out.rawRegister = node.getResponseBuffer(0);
  const LaserDistanceDecodeResult decoded = decodeLaserDistanceRaw(*profile, out.rawRegister);
  out.signedRaw = decoded.signedRaw;
  out.scaledDistance = decoded.scaledDistance;
  out.sentinel = decoded.sentinel;
  out.validDistance = decoded.validDistance;
  out.invalidReason = decoded.invalidReason;
  return out;
}

uint8_t LaserMeasurementReader::readDistanceRegister(const LaserSensorProfile& activeProfile) {
  switch (activeProfile.modbus.distanceReadFunction) {
    case LaserModbusReadFunction::READ_HOLDING_REGISTERS:
      return node.readHoldingRegisters(
          activeProfile.modbus.distanceRegister,
          activeProfile.modbus.distanceRegisterCount);
    case LaserModbusReadFunction::READ_INPUT_REGISTERS:
      return node.readInputRegisters(
          activeProfile.modbus.distanceRegister,
          activeProfile.modbus.distanceRegisterCount);
  }
  return node.readInputRegisters(
      activeProfile.modbus.distanceRegister,
      activeProfile.modbus.distanceRegisterCount);
}

void LaserMeasurementReader::logMeasurementDiag(
    uint32_t now,
    uint8_t result,
    uint32_t readDurationMs,
    TopState topState,
    bool laserInstalled,
    uint32_t nextReadBackoffMs) {
  if (diagWindowStartedAtMs == 0) {
    diagWindowStartedAtMs = now;
    diagLastLogAtMs = now;
  }
  diagLastReadMs = readDurationMs;
  diagLastResult = result;
  if (readDurationMs > diagMaxReadMs) {
    diagMaxReadMs = readDurationMs;
  }
  if (result == node.ku8MBSuccess) {
    diagReadOkCount += 1;
  } else {
    diagReadFailCount += 1;
  }

  const uint32_t diagWindowMs = now - diagWindowStartedAtMs;
  const bool slowRead = readDurationMs >= kMeasurementDiagSlowReadWarnMs;
  const bool shouldLog = slowRead || now - diagLastLogAtMs >= kMeasurementDiagSummaryIntervalMs;
  if (!shouldLog) {
    return;
  }

  const uint32_t attempts = diagReadOkCount + diagReadFailCount;
  const float attemptRateHz =
      diagWindowMs > 0
          ? (static_cast<float>(attempts) * 1000.0f / static_cast<float>(diagWindowMs))
          : 0.0f;
  Serial.printf(
      "[MEASUREMENT_DIAG] trigger=%s window_ms=%lu ok=%lu fail=%lu last_result=0x%02X last_read_ms=%lu max_read_ms=%lu interval_ms=%lu attempt_rate_hz=%.2f top_state=%s laser_installed=%d next_read_backoff_ms=%ld\n",
      slowRead ? "slow_read" : "periodic",
      static_cast<unsigned long>(diagWindowMs),
      static_cast<unsigned long>(diagReadOkCount),
      static_cast<unsigned long>(diagReadFailCount),
      static_cast<unsigned>(diagLastResult),
      static_cast<unsigned long>(diagLastReadMs),
      static_cast<unsigned long>(diagMaxReadMs),
      static_cast<unsigned long>(LASER_MEASUREMENT_READ_INTERVAL_MS),
      attemptRateHz,
      topStateName(topState),
      laserInstalled ? 1 : 0,
      static_cast<long>(nextReadBackoffMs));
  diagLastLogAtMs = now;
  if (!slowRead) {
    diagWindowStartedAtMs = now;
    diagReadOkCount = 0;
    diagReadFailCount = 0;
    diagMaxReadMs = 0;
  }
}

void LaserMeasurementReader::noteTransientFailure(
    uint8_t result,
    uint32_t now,
    uint32_t readDurationMs,
    TopState topState,
    bool laserInstalled) {
  transientTotalFailures += 1;

  const bool newBurst = !transientActive;
  const bool codeChanged = transientActive && transientLastCode != 0 && transientLastCode != result;
  if (newBurst) {
    transientActive = true;
    transientStartedAtMs = now;
    transientConsecutiveFailures = 0;
    transientTotalBursts += 1;
  }

  transientConsecutiveFailures += 1;
  transientLastFailureAtMs = now;
  transientLastCode = result;
  transientLastTopState = topState;

  const uint32_t elapsedMs = transientStartedAtMs != 0 ? (now - transientStartedAtMs) : 0;
  const bool shouldLog =
      newBurst ||
      codeChanged ||
      transientLastLogAtMs == 0 ||
      (now - transientLastLogAtMs) >= kReadFailureSummaryIntervalMs;
  if (!shouldLog) {
    return;
  }

  Serial.printf(
      "[MEASUREMENT_TRANSIENT] event=%s code=0x%02X consecutive=%lu elapsed_ms=%lu read_ms=%lu top_state=%s laser_installed=%d total_failures=%lu total_bursts=%lu\n",
      newBurst ? "fail_start" : "fail_continue",
      static_cast<unsigned>(result),
      static_cast<unsigned long>(transientConsecutiveFailures),
      static_cast<unsigned long>(elapsedMs),
      static_cast<unsigned long>(readDurationMs),
      topStateName(topState),
      laserInstalled ? 1 : 0,
      static_cast<unsigned long>(transientTotalFailures),
      static_cast<unsigned long>(transientTotalBursts));
  transientLastLogAtMs = now;
}

void LaserMeasurementReader::noteTransientRecovery(
    uint32_t now,
    uint32_t readDurationMs,
    TopState topState) {
  if (!transientActive) {
    return;
  }

  const uint32_t durationMs = transientStartedAtMs != 0 ? (now - transientStartedAtMs) : 0;
  if (durationMs > transientMaxBurstMs) {
    transientMaxBurstMs = durationMs;
  }

  Serial.printf(
      "[MEASUREMENT_TRANSIENT] event=recovered code=0x%02X consecutive=%lu duration_ms=%lu recovery_read_ms=%lu top_state=%s last_failure_top_state=%s total_failures=%lu total_bursts=%lu max_burst_ms=%lu\n",
      static_cast<unsigned>(transientLastCode),
      static_cast<unsigned long>(transientConsecutiveFailures),
      static_cast<unsigned long>(durationMs),
      static_cast<unsigned long>(readDurationMs),
      topStateName(topState),
      topStateName(transientLastTopState),
      static_cast<unsigned long>(transientTotalFailures),
      static_cast<unsigned long>(transientTotalBursts),
      static_cast<unsigned long>(transientMaxBurstMs));

  transientActive = false;
  transientStartedAtMs = 0;
  transientLastFailureAtMs = 0;
  transientLastLogAtMs = 0;
  transientConsecutiveFailures = 0;
  transientLastCode = 0;
  transientLastTopState = TopState::IDLE;
}

void LaserMeasurementReader::noteReadFailure(
    uint8_t result,
    uint32_t now,
    uint32_t readDurationMs,
    TopState topState,
    bool laserInstalled) {
  noteTransientFailure(result, now, readDurationMs, topState, laserInstalled);

  if (!hasLoggedReadFailure || lastReadFailureCode != result) {
    clearFailureBurst(now, true);
    Serial.printf("❌ Modbus read fail (0x%02X)\n", result);
    hasLoggedReadFailure = true;
    lastReadFailureCode = result;
    lastReadFailureLogMs = now;
    suppressedReadFailureCount = 0;
    return;
  }

  suppressedReadFailureCount += 1;
  const uint32_t windowMs = now - lastReadFailureLogMs;
  if (windowMs < kReadFailureSummaryIntervalMs) {
    return;
  }

  if (suppressedReadFailureCount > 0) {
    Serial.printf(
        "❌ Modbus read fail (0x%02X) suppressed=%lu window_ms=%lu\n",
        result,
        static_cast<unsigned long>(suppressedReadFailureCount),
        static_cast<unsigned long>(windowMs));
  }

  hasLoggedReadFailure = true;
  lastReadFailureCode = result;
  lastReadFailureLogMs = now;
  suppressedReadFailureCount = 0;
}

void LaserMeasurementReader::clearFailureBurst(uint32_t now, bool flushSummary) {
  if (flushSummary && hasLoggedReadFailure && suppressedReadFailureCount > 0) {
    Serial.printf(
        "❌ Modbus read fail (0x%02X) suppressed=%lu window_ms=%lu\n",
        lastReadFailureCode,
        static_cast<unsigned long>(suppressedReadFailureCount),
        static_cast<unsigned long>(now - lastReadFailureLogMs));
  }
  hasLoggedReadFailure = false;
  lastReadFailureCode = 0;
  lastReadFailureLogMs = 0;
  suppressedReadFailureCount = 0;
}
