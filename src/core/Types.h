#pragma once
#include <Arduino.h>

enum class TopState : uint8_t { IDLE, ARMED, RUNNING, FAULT_STOP };

enum class SafetySignalKind : uint8_t {
  NONE,
  WARNING_ONLY,
  RECOVERABLE_PAUSE,
  ABNORMAL_STOP
};

enum class FaultSeverity : uint8_t {
  INFO_ONLY,
  WARNING_ONLY,
  BLOCKING_FAULT
};

enum class FaultOrigin : uint8_t {
  SAFETY_RUNTIME,
  MEASUREMENT_CAPABILITY,
  COMMUNICATION_SESSION,
  COMMAND_INPUT
};

enum class FaultCode : uint16_t {
  NONE = 0,
  USER_LEFT_PLATFORM = 100,
  FALL_SUSPECTED = 101,
  BLE_DISCONNECTED = 102,
  MEASUREMENT_UNAVAILABLE = 200,
  INVALID_PARAM = 300,
  NOT_ARMED = 400,
  FAULT_LOCKED = 401
};

inline const char* topStateName(TopState s) {
  switch (s) {
    case TopState::IDLE: return "IDLE";
    case TopState::ARMED: return "ARMED";
    case TopState::RUNNING: return "RUNNING";
    case TopState::FAULT_STOP: return "FAULT_STOP";
  }
  return "UNKNOWN";
}

inline const char* faultCodeName(FaultCode code) {
  switch (code) {
    case FaultCode::NONE: return "NONE";
    case FaultCode::USER_LEFT_PLATFORM: return "USER_LEFT_PLATFORM";
    case FaultCode::FALL_SUSPECTED: return "FALL_SUSPECTED";
    case FaultCode::BLE_DISCONNECTED: return "BLE_DISCONNECTED";
    case FaultCode::MEASUREMENT_UNAVAILABLE: return "MEASUREMENT_UNAVAILABLE";
    case FaultCode::INVALID_PARAM: return "INVALID_PARAM";
    case FaultCode::NOT_ARMED: return "NOT_ARMED";
    case FaultCode::FAULT_LOCKED: return "FAULT_LOCKED";
  }
  return "UNKNOWN";
}

inline const char* safetySignalName(SafetySignalKind signal) {
  switch (signal) {
    case SafetySignalKind::NONE: return "NONE";
    case SafetySignalKind::WARNING_ONLY: return "WARNING_ONLY";
    case SafetySignalKind::RECOVERABLE_PAUSE: return "RECOVERABLE_PAUSE";
    case SafetySignalKind::ABNORMAL_STOP: return "ABNORMAL_STOP";
  }
  return "UNKNOWN";
}

struct WaveParams {
  float freqHz = 0.0f; // 0..50
  int intensity = 0;   // 0..120
  bool enable = false;
  bool hasEnable = false;
};
