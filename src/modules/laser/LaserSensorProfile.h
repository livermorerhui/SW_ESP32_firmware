#pragma once

#include <Arduino.h>

enum class LaserSensorProfileId : uint8_t {
  WANG_WTX_J100_485 = 0,
};

// The transport-specific wire result is kept separate from the measurement
// quality.  A Modbus/I2C transaction can succeed while the reported distance
// is outside the sensor's physical range.
enum class LaserMeasurementQuality : uint8_t {
  UNKNOWN = 0,
  VALID = 1,
  SENSOR_UNAVAILABLE = 2,
  SENSOR_CONFLICT = 3,
  I2C_NOT_STARTED = 10,
  I2C_STARTUP_GRACE = 11,
  I2C_WRITE_FAIL = 12,
  I2C_SHORT_READ = 13,
  I2C_DISTANCE_OUT_OF_RANGE = 14,
  MODBUS_READ_FAIL = 20,
  MODBUS_SENTINEL = 21,
  MODBUS_OUT_OF_RANGE_LOW = 22,
  MODBUS_OUT_OF_RANGE_HIGH = 23,
  DISTANCE_NONFINITE = 30,
};

inline const char* laserMeasurementQualityName(LaserMeasurementQuality quality) {
  switch (quality) {
    case LaserMeasurementQuality::UNKNOWN: return "UNKNOWN";
    case LaserMeasurementQuality::VALID: return "VALID";
    case LaserMeasurementQuality::SENSOR_UNAVAILABLE: return "SENSOR_UNAVAILABLE";
    case LaserMeasurementQuality::SENSOR_CONFLICT: return "SENSOR_CONFLICT";
    case LaserMeasurementQuality::I2C_NOT_STARTED: return "I2C_NOT_STARTED";
    case LaserMeasurementQuality::I2C_STARTUP_GRACE: return "I2C_STARTUP_GRACE";
    case LaserMeasurementQuality::I2C_WRITE_FAIL: return "I2C_WRITE_FAIL";
    case LaserMeasurementQuality::I2C_SHORT_READ: return "I2C_SHORT_READ";
    case LaserMeasurementQuality::I2C_DISTANCE_OUT_OF_RANGE:
      return "I2C_DISTANCE_OUT_OF_RANGE";
    case LaserMeasurementQuality::MODBUS_READ_FAIL: return "MODBUS_READ_FAIL";
    case LaserMeasurementQuality::MODBUS_SENTINEL: return "MODBUS_SENTINEL";
    case LaserMeasurementQuality::MODBUS_OUT_OF_RANGE_LOW:
      return "MODBUS_OUT_OF_RANGE_LOW";
    case LaserMeasurementQuality::MODBUS_OUT_OF_RANGE_HIGH:
      return "MODBUS_OUT_OF_RANGE_HIGH";
    case LaserMeasurementQuality::DISTANCE_NONFINITE: return "DISTANCE_NONFINITE";
  }
  return "UNKNOWN";
}

enum class LaserModbusReadFunction : uint8_t {
  READ_HOLDING_REGISTERS = 0x03,
  READ_INPUT_REGISTERS = 0x04,
};

struct LaserSensorSerialConfig {
  LaserSensorSerialConfig() = default;
  LaserSensorSerialConfig(uint32_t baudValue, uint32_t serialConfigValue)
    : baud(baudValue), serialConfig(serialConfigValue) {}

  uint32_t baud = 9600;
  uint32_t serialConfig = 0;
};

struct LaserSensorModbusConfig {
  LaserSensorModbusConfig() = default;
  LaserSensorModbusConfig(
      uint8_t slaveIdValue,
      LaserModbusReadFunction distanceReadFunctionValue,
      uint16_t distanceRegisterValue,
      uint8_t distanceRegisterCountValue)
    : slaveId(slaveIdValue),
      distanceReadFunction(distanceReadFunctionValue),
      distanceRegister(distanceRegisterValue),
      distanceRegisterCount(distanceRegisterCountValue) {}

  uint8_t slaveId = 1;
  LaserModbusReadFunction distanceReadFunction = LaserModbusReadFunction::READ_INPUT_REGISTERS;
  uint16_t distanceRegister = 0x0064;
  uint8_t distanceRegisterCount = 1;
};

struct LaserSensorDecodeConfig {
  LaserSensorDecodeConfig() = default;
  LaserSensorDecodeConfig(
      bool signedRaw16Value,
      float scaleDivisorValue,
      float offsetValue,
      int16_t validMinRawValue,
      int16_t validMaxRawValue,
      uint16_t sentinelOverRangeRawValue,
      float rawToPhysicalScaleMmValue = 0.01f,
      float physicalOffsetMmValue = 0.0f,
      float physicalDirectionValue = 1.0f,
      float runtimeUnitsPerMmValue = 0.01f,
      float validMinPhysicalMmValue = 0.0f,
      float validMaxPhysicalMmValue = 0.0f)
    : signedRaw16(signedRaw16Value),
      scaleDivisor(scaleDivisorValue),
      offset(offsetValue),
      validMinRaw(validMinRawValue),
      validMaxRaw(validMaxRawValue),
      sentinelOverRangeRaw(sentinelOverRangeRawValue),
      rawToPhysicalScaleMm(rawToPhysicalScaleMmValue),
      physicalOffsetMm(physicalOffsetMmValue),
      physicalDirection(physicalDirectionValue),
      runtimeUnitsPerMm(runtimeUnitsPerMmValue),
      validMinPhysicalMm(validMinPhysicalMmValue),
      validMaxPhysicalMm(validMaxPhysicalMmValue) {}

  bool signedRaw16 = true;
  float scaleDivisor = 100.0f;
  float offset = 0.0f;
  int16_t validMinRaw = -3570;
  int16_t validMaxRaw = 3570;
  uint16_t sentinelOverRangeRaw = 32767;

  // Explicit physical conversion contract.  The older scaleDivisor/offset
  // fields remain for compatibility with existing diagnostics and tests; new
  // code should consume physicalDistanceMm from LaserDistanceDecodeResult.
  float rawToPhysicalScaleMm = 0.01f;
  float physicalOffsetMm = 0.0f;
  float physicalDirection = 1.0f;
  float runtimeUnitsPerMm = 0.01f;
  float validMinPhysicalMm = 0.0f;
  float validMaxPhysicalMm = 0.0f;
};

struct LaserSensorProfile {
  LaserSensorProfile() = default;
  LaserSensorProfile(
      LaserSensorProfileId idValue,
      const char* stableNameValue,
      const LaserSensorSerialConfig& serialValue,
      const LaserSensorModbusConfig& modbusValue,
      const LaserSensorDecodeConfig& decodeValue)
    : id(idValue),
      stableName(stableNameValue),
      serial(serialValue),
      modbus(modbusValue),
      decode(decodeValue) {}

  LaserSensorProfileId id = LaserSensorProfileId::WANG_WTX_J100_485;
  const char* stableName = "WANG_WTX_J100_485";
  LaserSensorSerialConfig serial{};
  LaserSensorModbusConfig modbus{};
  LaserSensorDecodeConfig decode{};
};

struct LaserDistanceDecodeResult {
  int16_t signedRaw = 0;
  float physicalDistanceMm = NAN;
  float scaledDistance = NAN;
  bool sentinel = false;
  bool validDistance = false;
  LaserMeasurementQuality quality = LaserMeasurementQuality::UNKNOWN;
  const char* invalidReason = nullptr;
};

const LaserSensorProfile& activeLaserSensorProfile();
const char* laserSensorProfileIdName(LaserSensorProfileId id);
LaserDistanceDecodeResult decodeLaserDistanceRaw(
    const LaserSensorProfile& profile,
    uint16_t rawRegister);
