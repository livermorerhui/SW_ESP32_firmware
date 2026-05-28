#pragma once

#include <Arduino.h>

enum class LaserSensorProfileId : uint8_t {
  WANG_WTX_J100_485 = 0,
};

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
      uint16_t sentinelOverRangeRawValue)
    : signedRaw16(signedRaw16Value),
      scaleDivisor(scaleDivisorValue),
      offset(offsetValue),
      validMinRaw(validMinRawValue),
      validMaxRaw(validMaxRawValue),
      sentinelOverRangeRaw(sentinelOverRangeRawValue) {}

  bool signedRaw16 = true;
  float scaleDivisor = 100.0f;
  float offset = 0.0f;
  int16_t validMinRaw = -3570;
  int16_t validMaxRaw = 3570;
  uint16_t sentinelOverRangeRaw = 32767;
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
  float scaledDistance = NAN;
  bool sentinel = false;
  bool validDistance = false;
  const char* invalidReason = nullptr;
};

const LaserSensorProfile& activeLaserSensorProfile();
const char* laserSensorProfileIdName(LaserSensorProfileId id);
LaserDistanceDecodeResult decodeLaserDistanceRaw(
    const LaserSensorProfile& profile,
    uint16_t rawRegister);
