#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "config/GlobalConfig.h"
#include "modules/laser/LaserSensorProfile.h"

struct Cyd3s1612I2cReadResult {
  bool transportOk = false;
  bool validDistance = false;
  bool sampleFresh = false;
  bool valueRepeated = false;
  uint16_t rawDistanceMm = 0;
  float physicalDistanceMm = NAN;
  // Kept as an alias for the existing smoke-test and maintenance callers.
  float distanceMm = NAN;
  uint32_t sampleSequence = 0;
  uint32_t readStartedAtMs = 0;
  uint32_t readCompletedAtMs = 0;
  uint32_t readDurationMs = 0;
  uint8_t writeError = 0;
  uint8_t receivedBytes = 0;
  uint8_t wireError = 0;
  LaserMeasurementQuality quality = LaserMeasurementQuality::UNKNOWN;
  const char* invalidReason = nullptr;
};

class Cyd3s1612I2cReader {
public:
  static constexpr uint8_t kAddress = 0x30;
  static constexpr uint8_t kDistanceRegister = 0x01;
  static constexpr uint8_t kSdaPin = I2C_SDA_PIN;
  static constexpr uint8_t kSclPin = I2C_SCL_PIN;
  static constexpr uint32_t kBusFrequencyHz = 100000UL;
  static constexpr uint16_t kBusTimeoutMs = 50;
  static constexpr uint32_t kStartupGraceMs = 1000UL;
  static constexpr uint16_t kMinDistanceMm = 20;
  static constexpr uint16_t kMaxDistanceMm = 5000;

  void begin(TwoWire& wire = Wire);
  Cyd3s1612I2cReadResult read();

private:
  TwoWire* wire_ = &Wire;
  bool started_ = false;
  uint32_t readyAtMs_ = 0;
  uint32_t sampleSequence_ = 0;
  uint16_t lastDistanceMm_ = 0;
  bool hasLastDistance_ = false;
};
