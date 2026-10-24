#pragma once

#include <Arduino.h>
#include <math.h>
#include "core/PlatformSnapshot.h"
#include "core/ProtocolSupport.h"
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

inline String calibrationZero() {
  return "ACK:CAL_ZERO";
}

inline String unsupported() {
  return "NACK:UNSUPPORTED";
}

inline String simpleNack(const String& reason) {
  String out = "NACK:";
  out += reason;
  return out;
}

inline String audioCue(AudioCueId id, const char* result) {
  String out = "ACK:AUDIO_CUE id=";
  out += audioCueIdName(id);
  out += " result=";
  out += result ? result : "UNKNOWN";
  return out;
}

inline String audioCueNack(AudioCueId id, const char* reason) {
  String out = "NACK:AUDIO_CUE id=";
  out += audioCueIdName(id);
  out += " reason=";
  out += reason ? reason : "UNKNOWN";
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
    bool laserInstalled,
    bool cueSupported = true) {
  String out;
  out.reserve(128);
  appendKeyValue(out, "ACK:CAP fw=", firmwareVersion);
  appendKeyValue(out, " build=", buildId);
  appendKeyValue(out, " board=", boardId);
  appendKeyIntValue(out, " proto=", protocolVersion);
  appendKeyValue(out, " platform_model=", platformModelName(platformModel));
  appendKeyIntValue(out, " laser_installed=", laserInstalled ? 1 : 0);
  appendKeyIntValue(out, " leave_stop_supported=", 1);
  appendKeyIntValue(out, " stream_control_supported=", 1);
  appendKeyIntValue(out, " cue_supported=", cueSupported ? 1 : 0);
  ProtocolSupport::logTruthPayloadBudgetWarningIfNeeded(
      "bootstrap_truth",
      out.length() + 1,
      ProtocolSupport::kCapTruthPayloadBudgetBytes,
      out);
  return out;
}

inline String deviceIdentity(const String& deviceId, int version) {
  String out;
  out.reserve(72);
  appendKeyValue(out, "ACK:DEVICE_ID id=", deviceId.c_str());
  appendKeyIntValue(out, " version=", version);
  return out;
}

inline String deviceSecurity(
    uint32_t appSecurityVersion,
    uint32_t efuseSecurityVersion,
    uint32_t currentSecurityVersion,
    bool appAntiRollbackGate) {
  String out;
  out.reserve(96);
  appendKeyValue(out, "ACK:DEVICE_SECURITY v=", "1");
  appendKeyUnsignedLongValue(out, " app_sv=", static_cast<unsigned long>(appSecurityVersion));
  appendKeyUnsignedLongValue(out, " efuse_sv=", static_cast<unsigned long>(efuseSecurityVersion));
  appendKeyUnsignedLongValue(out, " floor_sv=", static_cast<unsigned long>(currentSecurityVersion));
  appendKeyIntValue(out, " app_gate=", appAntiRollbackGate ? 1 : 0);
  return out;
}

inline String deviceConfig(PlatformModel platformModel, bool laserInstalled) {
  String out;
  out.reserve(80);
  appendKeyValue(out, "ACK:DEVICE_CONFIG platform_model=", platformModelName(platformModel));
  appendKeyIntValue(out, " laser_installed=", laserInstalled ? 1 : 0);
  return out;
}

// The profile-aware reply is a separate wire contract. Keep deviceConfig()
// above byte-for-byte compatible with the older Demo APP.
inline String deviceProfileConfig(
    const char* op,
    PlatformModel platformModel,
    SensorProfileId sensorProfile,
    uint32_t generation) {
  String out;
  out.reserve(128);
  appendKeyValue(out, "ACK:DEVICE_CONFIG op=", op);
  appendKeyIntValue(out, " schema=", 1);
  appendKeyValue(out, " platform_model=", platformModelName(platformModel));
  appendKeyValue(out, " sensor_profile=", sensorProfileName(sensorProfile));
  appendKeyIntValue(
      out, " laser_installed=", normalizedLaserInstalledForPlatformModel(platformModel) ? 1 : 0);
  appendKeyUnsignedLongValue(out, " generation=", static_cast<unsigned long>(generation));
  return out;
}

inline String deviceProfileConfigNack(const char* op, const char* reason) {
  String out;
  out.reserve(88);
  appendKeyValue(out, "NACK:DEVICE_CONFIG op=", op);
  appendKeyValue(out, " reason=", reason);
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

inline String calibrationModel(const CalibrationModel& model, bool ready = true) {
  String out;
  out.reserve(116);
  appendKeyValue(out, "ACK:CAL_MODEL type=", calibrationModelTypeName(model.type));
  appendKeyFloatValue(out, " ref=", model.referenceDistance, 4);
  appendKeyFloatValue(out, " c0=", model.coefficients[0], 6);
  appendKeyFloatValue(out, " c1=", model.coefficients[1], 6);
  appendKeyFloatValue(out, " c2=", model.coefficients[2], 6);
  appendKeyIntValue(out, " ready=", ready ? 1 : 0);
  return out;
}

inline String calibrationSetModel(const CalibrationModel& model, bool ready = true) {
  String out;
  out.reserve(124);
  appendKeyValue(out, "ACK:CAL_SET_MODEL type=", calibrationModelTypeName(model.type));
  appendKeyFloatValue(out, " ref=", model.referenceDistance, 4);
  appendKeyFloatValue(out, " c0=", model.coefficients[0], 6);
  appendKeyFloatValue(out, " c1=", model.coefficients[1], 6);
  appendKeyFloatValue(out, " c2=", model.coefficients[2], 6);
  appendKeyIntValue(out, " ready=", ready ? 1 : 0);
  return out;
}

inline String calibrationSetModelRejected(CalibrationModelType type, const String& reason) {
  String out;
  out.reserve(96);
  appendKeyValue(out, "NACK:CAL_SET_MODEL type=", calibrationModelTypeName(type));
  appendKeyValue(out, " reason=", reason.c_str());
  return out;
}

inline String emptyDistance(
    const char* operation,
    bool supported,
    const char* status,
    float distanceMm,
    float toleranceMm,
    const char* backend) {
  String out;
  out.reserve(128);
  appendKeyValue(out, "ACK:EMPTY op=", operation ? operation : "UNKNOWN");
  appendKeyIntValue(out, " supported=", supported ? 1 : 0);
  appendKeyValue(out, " status=", status ? status : "UNKNOWN");
  if (isfinite(distanceMm)) appendKeyFloatValue(out, " distance_mm=", distanceMm, 2);
  if (isfinite(toleranceMm)) appendKeyFloatValue(out, " tolerance_mm=", toleranceMm, 2);
  appendKeyValue(out, " backend=", backend ? backend : "UNKNOWN");
  return out;
}

// The profile-aware empty-distance reply is a separate contract; the older
// emptyDistance() payload above stays unchanged for legacy clients.
inline String profileEmptyDistance(
    const char* operation,
    bool isSet,
    float distanceMm,
    float toleranceMm,
    SensorProfileId sensorProfile,
    uint32_t configGeneration,
    uint32_t emptyRevision) {
  String out;
  out.reserve(160);
  appendKeyValue(out, "ACK:EMPTY op=", operation);
  appendKeyValue(out, " status=", isSet ? "SET" : "UNSET");
  if (isSet) {
    appendKeyFloatValue(out, " distance_mm=", distanceMm, 2);
    appendKeyFloatValue(out, " tolerance_mm=", toleranceMm, 2);
  }
  appendKeyValue(out, " sensor_profile=", sensorProfileName(sensorProfile));
  appendKeyUnsignedLongValue(
      out, " generation=", static_cast<unsigned long>(configGeneration));
  appendKeyUnsignedLongValue(
      out, " empty_revision=", static_cast<unsigned long>(emptyRevision));
  return out;
}

inline String emptyDistanceRejected(
    const char* operation,
    const String& reason,
    bool supported = true) {
  String out;
  out.reserve(80);
  appendKeyValue(out, "NACK:EMPTY op=", operation ? operation : "UNKNOWN");
  appendKeyIntValue(out, " supported=", supported ? 1 : 0);
  appendKeyValue(out, " reason=", "");
  out += reason;
  return out;
}

inline String weightModelClear(bool ready) {
  String out = "ACK:CAL_CLEAR_WEIGHT_MODEL ready=";
  out += ready ? "1" : "0";
  return out;
}

inline String weightModelClearRejected(const String& reason, bool supported = true) {
  String out = "NACK:CAL_CLEAR_WEIGHT_MODEL supported=";
  out += supported ? "1" : "0";
  out += " reason=";
  out += reason;
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

inline String streamSubscription(bool enabled, uint8_t rateHz) {
  String out;
  out.reserve(56);
  appendKeyIntValue(out, "ACK:STREAM enabled=", enabled ? 1 : 0);
  appendKeyIntValue(out, " supported=", 1);
  appendKeyIntValue(out, " rate_hz=", rateHz);
  return out;
}

}  // namespace HubAckBuilder
