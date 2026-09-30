#include "modules/laser/LaserMeasurementReader.h"
#include "config/GlobalConfig.h"
#include "core/HumanReadableLog.h"
#include "core/VerboseSerial.h"

namespace {
constexpr uint32_t kReadFailureSummaryIntervalMs = 10000UL;
constexpr uint32_t kMeasurementDiagSummaryIntervalMs = 2000UL;
constexpr uint32_t kMeasurementDiagSlowReadWarnMs = 80UL;
constexpr uint8_t kUnifiedProbeUnavailableCode = 0xE3;
constexpr uint8_t kUnifiedProbeConflictCode = 0xE4;
constexpr uint8_t kUnifiedIicProbeAttempts = 3;
// ModbusMaster's transaction timeout is blocking. Keep the boot probe to one
// bounded attempt so a missing 485 device cannot starve the task watchdog.
constexpr uint8_t kUnified485ProbeAttempts = 1;
}

void LaserMeasurementReader::begin() {
#if defined(SW_DISTANCE_BACKEND_UNIFIED)
  profile = &activeLaserSensorProfile();
  cydReader.begin();
  Serial1.begin(profile->serial.baud, profile->serial.serialConfig, RX_PIN, TX_PIN);
  node.begin(profile->modbus.slaveId, Serial1);
  SW_VERBOSE_SERIAL_PRINTF(
      "[LASER_SENSOR_PROFILE] active=UNIFIED iic_address=0x%02X iic_sda=%u iic_scl=%u "
      "uart_baud=%lu uart_slave_id=%u\n",
      static_cast<unsigned>(Cyd3s1612I2cReader::kAddress),
      static_cast<unsigned>(Cyd3s1612I2cReader::kSdaPin),
      static_cast<unsigned>(Cyd3s1612I2cReader::kSclPin),
      static_cast<unsigned long>(profile->serial.baud),
      static_cast<unsigned>(profile->modbus.slaveId));

  // The module needs its documented startup grace period before a probe can
  // distinguish a real CYD3S1612 response from a boot-time NACK.
  delay(Cyd3s1612I2cReader::kStartupGraceMs);
  uint8_t iicResult = 0;
  uint8_t modbusResult = 0;
  LaserMeasurementQuality iicQuality = LaserMeasurementQuality::UNKNOWN;
  LaserMeasurementQuality modbusQuality = LaserMeasurementQuality::UNKNOWN;
  const char* iicReason = nullptr;
  const char* modbusReason = nullptr;
  uint8_t iicAttempts = 0;
  uint8_t modbusAttempts = 0;
  bool iicDetected = false;
  bool modbusDetected = false;
  const uint32_t iicProbeStartedAtMs = millis();
  for (; iicAttempts < kUnifiedIicProbeAttempts && !iicDetected; ++iicAttempts) {
    iicDetected = probeIic(iicResult, iicQuality, iicReason);
  }
  const uint32_t iicProbeDurationMs = millis() - iicProbeStartedAtMs;
  // Probe the two physical transports independently.  A successful IIC probe
  // must not suppress the 485 probe; otherwise the documented dual-device
  // conflict state can never be observed.
  const uint32_t modbusProbeStartedAtMs = millis();
  for (; modbusAttempts < kUnified485ProbeAttempts && !modbusDetected; ++modbusAttempts) {
    modbusDetected = probe485(modbusResult, modbusQuality, modbusReason);
  }
  const uint32_t modbusProbeDurationMs = millis() - modbusProbeStartedAtMs;
  SW_VERBOSE_SERIAL_PRINTF(
      "[SENSOR_PROBE] backend=CYD3S1612_IIC result=%s quality=%s reason=%s wire=0x%02X attempts=%u duration_ms=%lu\n",
      iicDetected ? "pass" : "fail",
      laserMeasurementQualityName(iicQuality),
      iicReason ? iicReason : (iicDetected ? "OK" : "UNKNOWN"),
      static_cast<unsigned>(iicResult),
      static_cast<unsigned>(iicAttempts),
      static_cast<unsigned long>(iicProbeDurationMs));
  SW_VERBOSE_SERIAL_PRINTF(
      "[SENSOR_PROBE] backend=LASER_485 result=%s quality=%s reason=%s wire=0x%02X attempts=%u duration_ms=%lu\n",
      modbusDetected ? "pass" : "fail",
      laserMeasurementQualityName(modbusQuality),
      modbusReason ? modbusReason : (modbusDetected ? "OK" : "UNKNOWN"),
      static_cast<unsigned>(modbusResult),
      static_cast<unsigned>(modbusAttempts),
      static_cast<unsigned long>(modbusProbeDurationMs));
  if (iicDetected && modbusDetected) {
    backend_ = LaserMeasurementBackend::SENSOR_CONFLICT;
  } else if (iicDetected) {
    backend_ = LaserMeasurementBackend::CYD3S1612_IIC;
  } else if (modbusDetected) {
    backend_ = LaserMeasurementBackend::LASER_485;
  } else {
    backend_ = LaserMeasurementBackend::SENSOR_UNAVAILABLE;
  }
  SW_VERBOSE_SERIAL_PRINTF(
      "[SENSOR_PROBE] selected=%s lock=1\n",
      laserMeasurementBackendName(backend_));
#elif defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC)
  cydReader.begin();
  profile = nullptr;
#else
  profile = &activeLaserSensorProfile();
  Serial1.begin(profile->serial.baud, profile->serial.serialConfig, RX_PIN, TX_PIN);
  node.begin(profile->modbus.slaveId, Serial1);
  SW_VERBOSE_SERIAL_PRINTF(
      "[LASER_SENSOR_PROFILE] active=%s baud=%lu slave_id=%u function=0x%02X register=0x%04X count=%u\n",
      profile->stableName,
      static_cast<unsigned long>(profile->serial.baud),
      static_cast<unsigned>(profile->modbus.slaveId),
      static_cast<unsigned>(profile->modbus.distanceReadFunction),
      static_cast<unsigned>(profile->modbus.distanceRegister),
      static_cast<unsigned>(profile->modbus.distanceRegisterCount));
#endif
}

MeasurementReadResult LaserMeasurementReader::read(
    TopState topState,
    bool laserInstalled,
    uint32_t nextReadBackoffMs) {
  MeasurementReadResult out{};
#if defined(SW_DISTANCE_BACKEND_UNIFIED)
  switch (backend_) {
    case LaserMeasurementBackend::CYD3S1612_IIC:
      return readIic(topState, laserInstalled, nextReadBackoffMs);
    case LaserMeasurementBackend::LASER_485:
      return read485(topState, laserInstalled, nextReadBackoffMs);
    case LaserMeasurementBackend::SENSOR_CONFLICT:
      out.modbusResult = kUnifiedProbeConflictCode;
      out.quality = LaserMeasurementQuality::SENSOR_CONFLICT;
      out.invalidReason = "SENSOR_CONFLICT";
      break;
    case LaserMeasurementBackend::SENSOR_UNAVAILABLE:
      out.modbusResult = kUnifiedProbeUnavailableCode;
      out.quality = LaserMeasurementQuality::SENSOR_UNAVAILABLE;
      out.invalidReason = "SENSOR_UNAVAILABLE";
      break;
  }
  out.readStartedAtMs = millis();
  out.readCompletedAtMs = out.readStartedAtMs;
  return out;
#elif defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC)
  const Cyd3s1612I2cReadResult cydResult = cydReader.read();
  out.transportOk = cydResult.transportOk;
  out.validDistance = cydResult.validDistance;
  out.sampleFresh = cydResult.sampleFresh;
  out.valueRepeated = cydResult.valueRepeated;
  out.transportError = cydResult.wireError;
  out.receivedBytes = cydResult.receivedBytes;
  out.rawDistanceMm = cydResult.rawDistanceMm;
  out.rawRegister = cydResult.rawDistanceMm;
  out.signedRaw = static_cast<int16_t>(cydResult.rawDistanceMm);
  out.physicalDistanceMm = cydResult.physicalDistanceMm;
  out.scaledDistance = cydResult.physicalDistanceMm * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  out.sampleSequence = cydResult.sampleSequence;
  out.quality = cydResult.quality;
  out.readStartedAtMs = cydResult.readStartedAtMs;
  out.readCompletedAtMs = cydResult.readCompletedAtMs;
  out.readDurationMs = cydResult.readDurationMs;
  out.invalidReason = cydResult.invalidReason;
  out.modbusResult = cydResult.wireError;
  logMeasurementDiag(
      out.readCompletedAtMs,
      out.modbusResult,
      out.readDurationMs,
      topState,
      laserInstalled,
      nextReadBackoffMs);
  return out;
#else
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
    out.quality = LaserMeasurementQuality::MODBUS_READ_FAIL;
    return out;
  }

  noteTransientRecovery(out.readCompletedAtMs, out.readDurationMs, topState);
  clearFailureBurst(out.readCompletedAtMs, true);
  out.transportOk = true;

  out.rawRegister = node.getResponseBuffer(0);
  const LaserDistanceDecodeResult decoded = decodeLaserDistanceRaw(*profile, out.rawRegister);
  out.signedRaw = decoded.signedRaw;
  out.physicalDistanceMm = decoded.physicalDistanceMm;
  out.scaledDistance = decoded.scaledDistance;
  out.sentinel = decoded.sentinel;
  out.validDistance = decoded.validDistance;
  out.sampleFresh = true;
  out.sampleSequence = ++sampleSequence_;
  out.quality = decoded.quality;
  if (isfinite(out.physicalDistanceMm)) {
    out.valueRepeated = hasLastPhysicalDistance_ &&
        fabsf(lastPhysicalDistanceMm_ - out.physicalDistanceMm) < 0.0001f;
    lastPhysicalDistanceMm_ = out.physicalDistanceMm;
    hasLastPhysicalDistance_ = true;
  }
  out.invalidReason = decoded.invalidReason;
  return out;
#endif
}

#if !defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
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
#endif

#if defined(SW_DISTANCE_BACKEND_UNIFIED)
bool LaserMeasurementReader::probeIic(
    uint8_t& resultCode,
    LaserMeasurementQuality& quality,
    const char*& reason) {
  const Cyd3s1612I2cReadResult result = cydReader.read();
  resultCode = result.wireError;
  quality = result.quality;
  reason = result.invalidReason;
  return result.transportOk && result.validDistance;
}

bool LaserMeasurementReader::probe485(
    uint8_t& resultCode,
    LaserMeasurementQuality& quality,
    const char*& reason) {
  resultCode = readDistanceRegister(*profile);
  if (resultCode != node.ku8MBSuccess) {
    quality = LaserMeasurementQuality::MODBUS_READ_FAIL;
    reason = "READ_FAIL";
    return false;
  }
  const LaserDistanceDecodeResult decoded =
      decodeLaserDistanceRaw(*profile, node.getResponseBuffer(0));
  quality = decoded.quality;
  reason = decoded.invalidReason;
  return decoded.validDistance;
}

MeasurementReadResult LaserMeasurementReader::readIic(
    TopState topState,
    bool laserInstalled,
    uint32_t nextReadBackoffMs) {
  MeasurementReadResult out{};
  const Cyd3s1612I2cReadResult cydResult = cydReader.read();
  out.transportOk = cydResult.transportOk;
  out.validDistance = cydResult.validDistance;
  out.sampleFresh = cydResult.sampleFresh;
  out.valueRepeated = cydResult.valueRepeated;
  out.transportError = cydResult.wireError;
  out.receivedBytes = cydResult.receivedBytes;
  out.rawDistanceMm = cydResult.rawDistanceMm;
  out.rawRegister = cydResult.rawDistanceMm;
  out.signedRaw = static_cast<int16_t>(cydResult.rawDistanceMm);
  out.physicalDistanceMm = cydResult.physicalDistanceMm;
  out.scaledDistance = cydResult.physicalDistanceMm * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  out.sampleSequence = cydResult.sampleSequence;
  out.quality = cydResult.quality;
  out.readStartedAtMs = cydResult.readStartedAtMs;
  out.readCompletedAtMs = cydResult.readCompletedAtMs;
  out.readDurationMs = cydResult.readDurationMs;
  out.invalidReason = cydResult.invalidReason;
  out.modbusResult = cydResult.wireError;
  logMeasurementDiag(
      out.readCompletedAtMs,
      out.modbusResult,
      out.readDurationMs,
      topState,
      laserInstalled,
      nextReadBackoffMs);
  return out;
}

MeasurementReadResult LaserMeasurementReader::read485(
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
    out.quality = LaserMeasurementQuality::MODBUS_READ_FAIL;
    return out;
  }

  noteTransientRecovery(out.readCompletedAtMs, out.readDurationMs, topState);
  clearFailureBurst(out.readCompletedAtMs, true);
  out.transportOk = true;

  out.rawRegister = node.getResponseBuffer(0);
  const LaserDistanceDecodeResult decoded = decodeLaserDistanceRaw(*profile, out.rawRegister);
  out.signedRaw = decoded.signedRaw;
  out.physicalDistanceMm = decoded.physicalDistanceMm;
  out.scaledDistance = decoded.scaledDistance;
  out.sentinel = decoded.sentinel;
  out.validDistance = decoded.validDistance;
  out.sampleFresh = true;
  out.sampleSequence = ++sampleSequence_;
  out.quality = decoded.quality;
  if (isfinite(out.physicalDistanceMm)) {
    out.valueRepeated = hasLastPhysicalDistance_ &&
        fabsf(lastPhysicalDistanceMm_ - out.physicalDistanceMm) < 0.0001f;
    lastPhysicalDistanceMm_ = out.physicalDistanceMm;
    hasLastPhysicalDistance_ = true;
  }
  out.invalidReason = decoded.invalidReason;
  return out;
}
#endif

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
  if (result == 0) {
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
  SW_VERBOSE_SERIAL_PRINTF(
      "[MEASUREMENT_DIAG] trigger=%s window_ms=%lu ok=%lu fail=%lu last_result=0x%02X last_read_ms=%lu max_read_ms=%lu interval_ms=%lu attempt_rate_hz=%.2f top_state=%s laser_installed=%d next_read_backoff_ms=%ld\n",
      slowRead ? "slow_read" : "periodic",
      static_cast<unsigned long>(diagWindowMs),
      static_cast<unsigned long>(diagReadOkCount),
      static_cast<unsigned long>(diagReadFailCount),
      static_cast<unsigned>(diagLastResult),
      static_cast<unsigned long>(diagLastReadMs),
      static_cast<unsigned long>(diagMaxReadMs),
      static_cast<unsigned long>(
          backend_ == LaserMeasurementBackend::CYD3S1612_IIC
              ? LASER_IIC_MEASUREMENT_READ_INTERVAL_MS
              : LASER_MEASUREMENT_READ_INTERVAL_MS),
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

  SW_VERBOSE_SERIAL_PRINTF(
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

  SW_VERBOSE_SERIAL_PRINTF(
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
    SW_VERBOSE_SERIAL_PRINTF("❌ Modbus read fail (0x%02X)\n", result);
    HumanReadableLog::measurementFailure();
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
    SW_VERBOSE_SERIAL_PRINTF(
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
    SW_VERBOSE_SERIAL_PRINTF(
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
