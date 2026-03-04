#pragma once
#include <Arduino.h>

enum class TopState : uint8_t { IDLE, ARMED, RUNNING, FAULT_STOP };

enum class FaultCode : uint16_t {
  NONE = 0,
  USER_OFF = 100,
  FALL_SUSPECTED = 101,
  SENSOR_ERR = 200,
  INVALID_PARAM = 300,
  NOT_ARMED = 400,
  FAULT_LOCKED = 401
};

struct WaveParams {
  float freqHz = 0.0f; // 0..50
  int intensity = 0;   // 0..120
  bool enable = false;
};