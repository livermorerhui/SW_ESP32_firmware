#pragma once

#include <Arduino.h>
#include "core/PlatformSnapshot.h"
#include "core/ProtocolCodec.h"
#include "modules/laser/CalibrationModelStore.h"

namespace HubAckBuilder {

inline void appendKeyValue(String& out, const char* key, const char* value) {
  out += key;
  out += value ? value : "";
}

inline void appendKeyIntValue(String& out, const char* key, int value) {
  out += key;
  out += value;
}

inline void appendKeyUnsignedLongValue(String& out, const char* key, unsigned long value) {
  out += key;
  out += value;
}

inline void appendKeyFloatValue(String& out, const char* key, float value, unsigned char decimals) {
  out += key;
  out += String(value, static_cast<unsigned int>(decimals));
}

inline const char* calibrationModelTypeName(CalibrationModelType type) {
  switch (type) {
    case CalibrationModelType::LINEAR:
      return "LINEAR";
    case CalibrationModelType::QUADRATIC:
      return "QUADRATIC";
  }
  return "UNKNOWN";
}

inline String ok() {
  return "ACK:OK";
}

inline String unsupported() {
  return "NACK:UNSUPPORTED";
}

inline String simpleNack(const String& reason) {
  String out = "NACK:";
  out += reason;
  return out;
}

inline String startRejected(FaultCode reason) {
  return (reason == FaultCode::FAULT_LOCKED) ? "NACK:FAULT_LOCKED" : "NACK:NOT_ARMED";
}

inline String cap(
    const char* firmwareVersion,
    const char* buildId,
    const char* boardId,
    int protocolVersion,
    PlatformModel platformModel,
    bool laserInstalled) {
  String out;
  out.reserve(128);
  appendKeyValue(out, "ACK:CAP fw=", firmwareVersion);
  appendKeyValue(out, " build=", buildId);
  appendKeyValue(out, " board=", boardId);
  appendKeyIntValue(out, " proto=", protocolVersion);
  appendKeyValue(out, " platform_model=", platformModelName(platformModel));
  appendKeyIntValue(out, " laser_installed=", laserInstalled ? 1 : 0);
  appendKeyIntValue(out, " leave_stop_supported=", 1);
  ProtocolCodec::logTruthPayloadBudgetWarningIfNeeded(
      "bootstrap_truth",
      out.length() + 1,
      ProtocolCodec::kCapTruthPayloadBudgetBytes,
      out);
  return out;
}

inline String deviceConfig(PlatformModel platformModel, bool laserInstalled) {
  String out;
  out.reserve(80);
  appendKeyValue(out, "ACK:DEVICE_CONFIG platform_model=", platformModelName(platformModel));
  appendKeyIntValue(out, " laser_installed=", laserInstalled ? 1 : 0);
  return out;
}

inline String degradedStart(const PlatformSnapshot& snapshot) {
  String out;
  out.reserve(56);
  appendKeyIntValue(out, "ACK:DEGRADED_START enabled=", snapshot.degradedStartEnabled ? 1 : 0);
  appendKeyIntValue(out, " available=", snapshot.degradedStartAvailable ? 1 : 0);
  return out;
}

inline String calibrationPoint(
    uint32_t index,
    uint32_t timestampMs,
    float distanceMm,
    float referenceWeightKg,
    float predictedWeightKg,
    bool stableFlag,
    bool validFlag) {
  String out;
  out.reserve(128);
  appendKeyUnsignedLongValue(out, "ACK:CAL_POINT idx=", static_cast<unsigned long>(index));
  appendKeyUnsignedLongValue(out, " ts=", static_cast<unsigned long>(timestampMs));
  appendKeyFloatValue(out, " d_mm=", distanceMm, 2);
  appendKeyFloatValue(out, " ref_kg=", referenceWeightKg, 2);
  appendKeyFloatValue(out, " pred_kg=", predictedWeightKg, 2);
  appendKeyIntValue(out, " stable=", stableFlag ? 1 : 0);
  appendKeyIntValue(out, " valid=", validFlag ? 1 : 0);
  return out;
}

inline String calibrationModel(const CalibrationModel& model) {
  String out;
  out.reserve(104);
  appendKeyValue(out, "ACK:CAL_MODEL type=", calibrationModelTypeName(model.type));
  appendKeyFloatValue(out, " ref=", model.referenceDistance, 4);
  appendKeyFloatValue(out, " c0=", model.coefficients[0], 6);
  appendKeyFloatValue(out, " c1=", model.coefficients[1], 6);
  appendKeyFloatValue(out, " c2=", model.coefficients[2], 6);
  return out;
}

inline String calibrationSetModel(const CalibrationModel& model) {
  String out;
  out.reserve(112);
  appendKeyValue(out, "ACK:CAL_SET_MODEL type=", calibrationModelTypeName(model.type));
  appendKeyFloatValue(out, " ref=", model.referenceDistance, 4);
  appendKeyFloatValue(out, " c0=", model.coefficients[0], 6);
  appendKeyFloatValue(out, " c1=", model.coefficients[1], 6);
  appendKeyFloatValue(out, " c2=", model.coefficients[2], 6);
  return out;
}

inline String calibrationSetModelRejected(CalibrationModelType type, const String& reason) {
  String out;
  out.reserve(96);
  appendKeyValue(out, "NACK:CAL_SET_MODEL type=", calibrationModelTypeName(type));
  appendKeyValue(out, " reason=", reason.c_str());
  return out;
}

inline String fallStop(bool enabled, const char* mode) {
  String out;
  out.reserve(56);
  appendKeyIntValue(out, "ACK:FALL_STOP enabled=", enabled ? 1 : 0);
  appendKeyValue(out, " mode=", mode);
  return out;
}

inline String leaveProtection(bool enabled, const char* effect) {
  String out;
  out.reserve(80);
  appendKeyIntValue(out, "ACK:LEAVE_PROTECTION enabled=", enabled ? 1 : 0);
  appendKeyIntValue(out, " supported=", 1);
  appendKeyValue(out, " effect=", effect);
  return out;
}

inline String motionSampling(bool enabled, bool fallActionSuppressed) {
  String out;
  out.reserve(72);
  appendKeyIntValue(out, "ACK:MOTION_SAMPLING enabled=", enabled ? 1 : 0);
  appendKeyIntValue(out, " fall_action_suppressed=", fallActionSuppressed ? 1 : 0);
  return out;
}

}  // namespace HubAckBuilder
