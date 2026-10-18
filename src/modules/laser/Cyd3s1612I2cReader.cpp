#include "modules/laser/Cyd3s1612I2cReader.h"
#include "core/VerboseSerial.h"

void Cyd3s1612I2cReader::begin(TwoWire& wire) {
  wire_ = &wire;
  started_ = wire_->begin(kSdaPin, kSclPin, kBusFrequencyHz);
  wire_->setTimeOut(kBusTimeoutMs);
  readyAtMs_ = millis() + kStartupGraceMs;
  SW_VERBOSE_SERIAL_PRINTF(
      "[LASER_SENSOR_PROFILE] active=CYD3S1612_IIC address=0x%02X sda=%u scl=%u frequency_hz=%lu timeout_ms=%u ready_after_ms=%lu init_ok=%d\n",
      static_cast<unsigned>(kAddress),
      static_cast<unsigned>(kSdaPin),
      static_cast<unsigned>(kSclPin),
      static_cast<unsigned long>(kBusFrequencyHz),
      static_cast<unsigned>(kBusTimeoutMs),
      static_cast<unsigned long>(kStartupGraceMs),
      started_ ? 1 : 0);
}

Cyd3s1612I2cReadResult Cyd3s1612I2cReader::read() {
  Cyd3s1612I2cReadResult out{};
  out.readStartedAtMs = millis();

  const auto finish = [&out]() -> Cyd3s1612I2cReadResult {
    out.readCompletedAtMs = millis();
    out.readDurationMs = out.readCompletedAtMs - out.readStartedAtMs;
    return out;
  };

  if (!started_) {
    out.quality = LaserMeasurementQuality::I2C_NOT_STARTED;
    out.invalidReason = "I2C_NOT_STARTED";
    return finish();
  }

  if (static_cast<int32_t>(millis() - readyAtMs_) < 0) {
    out.quality = LaserMeasurementQuality::I2C_STARTUP_GRACE;
    out.invalidReason = "I2C_STARTUP_GRACE";
    return finish();
  }

  wire_->beginTransmission(kAddress);
  wire_->write(kDistanceRegister);
  const uint8_t writeError = wire_->endTransmission(false);
  out.writeError = writeError;
  out.wireError = writeError;
  if (writeError != 0) {
    out.quality = LaserMeasurementQuality::I2C_WRITE_FAIL;
    out.invalidReason = "I2C_WRITE_FAIL";
    return finish();
  }

  const size_t received = wire_->requestFrom(
      static_cast<uint8_t>(kAddress), static_cast<size_t>(2), true);
  out.receivedBytes = static_cast<uint8_t>(received > 255 ? 255 : received);
  if (received != 2 || wire_->available() < 2) {
    // Keep wireError reserved for the native Wire endTransmission result.
    // A short read is a distinct measurement-quality failure and must not be
    // disguised as an I2C status code equal to the byte count.
    out.quality = LaserMeasurementQuality::I2C_SHORT_READ;
    out.invalidReason = "I2C_READ_LENGTH_FAIL";
    return finish();
  }

  const uint16_t distanceMm =
      (static_cast<uint16_t>(wire_->read()) << 8) |
      static_cast<uint16_t>(wire_->read());
  out.rawDistanceMm = distanceMm;
  out.physicalDistanceMm = static_cast<float>(distanceMm);
  out.distanceMm = static_cast<float>(distanceMm);
  out.sampleSequence = ++sampleSequence_;
  out.sampleFresh = true;
  out.valueRepeated = hasLastDistance_ && lastDistanceMm_ == distanceMm;
  lastDistanceMm_ = distanceMm;
  hasLastDistance_ = true;
  out.transportOk = true;
  if (distanceMm < kMinDistanceMm || distanceMm > kMaxDistanceMm) {
    out.quality = LaserMeasurementQuality::I2C_DISTANCE_OUT_OF_RANGE;
    out.invalidReason = "DISTANCE_OUT_OF_RANGE";
  } else {
    out.validDistance = true;
    out.quality = LaserMeasurementQuality::VALID;
  }

  return finish();
}
