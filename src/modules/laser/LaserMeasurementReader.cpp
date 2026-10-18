#include "modules/laser/LaserMeasurementReader.h"
#include "config/GlobalConfig.h"
#include "core/HumanReadableLog.h"
#include "core/VerboseSerial.h"

namespace {
constexpr uint32_t kReadFailureSummaryIntervalMs = 10000UL;
constexpr uint32_t kMeasurementDiagSummaryIntervalMs = 2000UL;
constexpr uint32_t kMeasurementDiagSlowReadWarnMs = 80UL;
constexpr uint8_t kUnifiedProbeUnavailableCode = 0xE3;
}

bool LaserMeasurementReader::supportsProfile(SensorProfileId requested) {
  switch (requested) {
    case SensorProfileId::NONE:
      return true;
    case SensorProfileId::J100_485:
#if defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) && !defined(SW_DISTANCE_BACKEND_UNIFIED)
      return false;
#else
      return true;
#endif
    case SensorProfileId::CYD3S1612_IIC:
#if defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
      return true;
#else
      return false;
#endif
  }
  return false;
}

bool LaserMeasurementReader::ensureMutex() {
  if (!mutex_) {
    mutex_ = xSemaphoreCreateMutex();
  }
  return mutex_ != nullptr;
}

void LaserMeasurementReader::stopUnlocked() {
  if (backend_ == LaserMeasurementBackend::LASER_485) {
#if !defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
    Serial1.end();
#endif
  } else if (backend_ == LaserMeasurementBackend::CYD3S1612_IIC) {
#if defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
    Wire.end();
    cydReader = Cyd3s1612I2cReader{};
#endif
  }
  backend_ = LaserMeasurementBackend::SENSOR_UNAVAILABLE;
  selectedProfile_ = SensorProfileId::NONE;
  profile = nullptr;
}

void LaserMeasurementReader::resetReaderState() {
  clearFailureBurst(millis(), true);
  hasLoggedReadFailure = false;
  lastReadFailureCode = 0;
  lastReadFailureLogMs = 0;
  suppressedReadFailureCount = 0;
  transientActive = false;
  transientStartedAtMs = 0;
  transientLastFailureAtMs = 0;
  transientLastLogAtMs = 0;
  transientConsecutiveFailures = 0;
  transientLastCode = 0;
  transientLastTopState = TopState::IDLE;
  diagWindowStartedAtMs = 0;
  diagLastLogAtMs = 0;
  diagReadOkCount = 0;
  diagReadFailCount = 0;
  diagMaxReadMs = 0;
  diagLastReadMs = 0;
  diagLastResult = 0;
  sampleSequence_ = 0;
  lastPhysicalDistanceMm_ = NAN;
  hasLastPhysicalDistance_ = false;
}

bool LaserMeasurementReader::configure(SensorProfileId nextProfile) {
  if (!ensureMutex()) {
    SW_VERBOSE_SERIAL_PRINTLN("[LASER_SENSOR_PROFILE] configure_failed reason=NO_MUTEX");
    return false;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool supported = supportsProfile(nextProfile);
  if (!supported) {
    stopUnlocked();
    resetReaderState();
    readerEpoch_ += 1;
    if (readerEpoch_ == 0) readerEpoch_ = 1;
    selectedProfile_ = nextProfile;
    SW_VERBOSE_SERIAL_PRINTF(
        "[LASER_SENSOR_PROFILE] selected=%s backend=SENSOR_UNAVAILABLE reader_epoch=%lu supported=0\n",
        sensorProfileName(nextProfile),
        static_cast<unsigned long>(readerEpoch_));
    xSemaphoreGive(mutex_);
    return false;
  }
  if (selectedProfile_ == nextProfile &&
      (nextProfile == SensorProfileId::NONE ||
       backend_ == LaserMeasurementBackend::LASER_485 ||
       backend_ == LaserMeasurementBackend::CYD3S1612_IIC)) {
    xSemaphoreGive(mutex_);
    return true;
  }

  stopUnlocked();
  resetReaderState();
  readerEpoch_ += 1;
  if (readerEpoch_ == 0) readerEpoch_ = 1;
  selectedProfile_ = nextProfile;
  switch (nextProfile) {
    case SensorProfileId::NONE:
      break;
    case SensorProfileId::J100_485:
#if !defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
      profile = &activeLaserSensorProfile();
      Serial1.begin(profile->serial.baud, profile->serial.serialConfig, RX_PIN, TX_PIN);
      node.begin(profile->modbus.slaveId, Serial1);
      backend_ = LaserMeasurementBackend::LASER_485;
#endif
      break;
    case SensorProfileId::CYD3S1612_IIC:
#if defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
      cydReader = Cyd3s1612I2cReader{};
      cydReader.begin();
      backend_ = LaserMeasurementBackend::CYD3S1612_IIC;
#endif
      break;
    default:
      break;
  }
  SW_VERBOSE_SERIAL_PRINTF(
      "[LASER_SENSOR_PROFILE] selected=%s backend=%s reader_epoch=%lu supported=%d source=explicit_config\n",
      sensorProfileName(nextProfile),
      laserMeasurementBackendName(backend_),
      static_cast<unsigned long>(readerEpoch_),
      supported ? 1 : 0);
  xSemaphoreGive(mutex_);
  return supported;
}

void LaserMeasurementReader::stop() {
  if (!ensureMutex()) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  stopUnlocked();
  resetReaderState();
  readerEpoch_ += 1;
  if (readerEpoch_ == 0) readerEpoch_ = 1;
  xSemaphoreGive(mutex_);
}

MeasurementReadResult LaserMeasurementReader::read(
    TopState topState,
    bool laserInstalled,
    uint32_t nextReadBackoffMs) {
  MeasurementReadResult result{};
  if (!ensureMutex()) {
    result.quality = LaserMeasurementQuality::SENSOR_UNAVAILABLE;
    result.invalidReason = "READER_MUTEX_UNAVAILABLE";
    result.readStartedAtMs = millis();
    result.readCompletedAtMs = result.readStartedAtMs;
    return result;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  result = readUnlocked(topState, laserInstalled, nextReadBackoffMs);
  result.readerEpoch = readerEpoch_;
  result.sensorProfile = selectedProfile_;
#if defined(SW_DISTANCE_BACKEND_CYD3S1612_IIC) || defined(SW_DISTANCE_BACKEND_UNIFIED)
  if (backend_ == LaserMeasurementBackend::CYD3S1612_IIC && result.sampleFresh) {
    result.sampleSequence = ++sampleSequence_;
  }
#endif
  xSemaphoreGive(mutex_);
  return result;
}

MeasurementReadResult LaserMeasurementReader::readUnlocked(
    TopState topState,
    bool laserInstalled,
    uint32_t nextReadBackoffMs) {
  MeasurementReadResult out{};
  if (backend_ == LaserMeasurementBackend::SENSOR_UNAVAILABLE) {
    out.modbusResult = kUnifiedProbeUnavailableCode;
    out.quality = LaserMeasurementQuality::SENSOR_UNAVAILABLE;
    out.invalidReason = "SENSOR_UNAVAILABLE";
    out.readStartedAtMs = millis();
    out.readCompletedAtMs = out.readStartedAtMs;
    return out;
  }
#if defined(SW_DISTANCE_BACKEND_UNIFIED)
  switch (backend_) {
    case LaserMeasurementBackend::CYD3S1612_IIC:
      return readIic(topState, laserInstalled, nextReadBackoffMs);
    case LaserMeasurementBackend::LASER_485:
      return read485(topState, laserInstalled, nextReadBackoffMs);
    case LaserMeasurementBackend::SENSOR_CONFLICT:
      out.modbusResult = kUnifiedProbeUnavailableCode;
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
