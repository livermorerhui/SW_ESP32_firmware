#include "CalibrationCommandService.h"

#include <cstring>

#include "HubAckBuilder.h"
#include "config/GlobalConfig.h"
#include "core/VerboseSerial.h"
#include "modules/laser/LaserModule.h"

CalibrationCommandService::CalibrationCommandService(LaserModule* laserModule)
    : laserModule_(laserModule) {}

bool CalibrationCommandService::handles(CmdType type) const {
  switch (type) {
    case CmdType::SCALE_ZERO:
    case CmdType::CAL_ZERO:
    case CmdType::SCALE_CAL:
    case CmdType::CAL_CAPTURE:
    case CmdType::CAL_GET_MODEL:
    case CmdType::CAL_SET_MODEL:
    case CmdType::EMPTY_DISTANCE_GET:
    case CmdType::EMPTY_DISTANCE_SET_CURRENT:
    case CmdType::EMPTY_DISTANCE_SET:
    case CmdType::EMPTY_DISTANCE_CLEAR:
    case CmdType::CAL_CLEAR_WEIGHT_MODEL:
      return true;
    default:
      return false;
  }
}

bool CalibrationCommandService::handle(const Command& command,
                                       String& outAck) const {
  if (!laserModule_) {
    outAck = HubAckBuilder::simpleNack("CALIBRATION_SERVICE_UNAVAILABLE");
    return false;
  }

  switch (command.type) {
    case CmdType::SCALE_ZERO:
    case CmdType::CAL_ZERO: {
      String reason;
      if (!laserModule_->clearCalibrationParameters(reason)) {
        outAck = HubAckBuilder::simpleNack(reason);
        return false;
      }
      outAck = command.type == CmdType::CAL_ZERO
          ? HubAckBuilder::calibrationZero()
          : HubAckBuilder::ok();
      return true;
    }

    case CmdType::SCALE_CAL: {
      String reason;
      if (!laserModule_->setParams(command.p1, command.p2, reason)) {
        outAck = HubAckBuilder::simpleNack(reason);
        return false;
      }
      outAck = HubAckBuilder::ok();
      return true;
    }

    case CmdType::CAL_CAPTURE: {
      CalibrationCapture capture{};
      String reason;
      if (!laserModule_->captureCalibrationPoint(
              command.capture.referenceWeightKg, capture, reason)) {
        outAck = HubAckBuilder::simpleNack(reason);
        return false;
      }
      outAck = HubAckBuilder::calibrationPoint(
          capture.index,
          capture.ts_ms,
          capture.distanceMm,
          capture.referenceWeightKg,
          capture.predictedWeightKg,
          capture.stableFlag,
          capture.validFlag);
      return true;
    }

    case CmdType::CAL_GET_MODEL: {
      CalibrationModel model{};
      outAck = HubAckBuilder::calibrationModel(
          model, laserModule_->getCalibrationModel(model));
      return true;
    }

    case CmdType::CAL_SET_MODEL: {
      CalibrationModel model{};
      model.type = static_cast<CalibrationModelType>(command.model.type);
      model.referenceDistance = command.model.referenceDistance;
      model.coefficients[0] = command.model.coefficients[0];
      model.coefficients[1] = command.model.coefficients[1];
      model.coefficients[2] = command.model.coefficients[2];

      String reason;
      if (!laserModule_->setCalibrationModel(model, reason)) {
        SW_VERBOSE_SERIAL_PRINTF(
            "[CAL] SET_MODEL result=failure type=%s reason=%s\n",
            HubAckBuilder::calibrationModelTypeName(model.type),
            reason.c_str());
        outAck = HubAckBuilder::calibrationSetModelRejected(model.type, reason);
        return false;
      }
      SW_VERBOSE_SERIAL_PRINTF(
          "[CAL] SET_MODEL result=success type=%s ref=%.4f c0=%.6f c1=%.6f c2=%.6f\n",
          HubAckBuilder::calibrationModelTypeName(model.type),
          model.referenceDistance,
          model.coefficients[0],
          model.coefficients[1],
          model.coefficients[2]);
      outAck = HubAckBuilder::calibrationSetModel(model, true);
      return true;
    }

    case CmdType::EMPTY_DISTANCE_GET:
    case CmdType::EMPTY_DISTANCE_SET_CURRENT:
    case CmdType::EMPTY_DISTANCE_SET:
    case CmdType::EMPTY_DISTANCE_CLEAR:
      return handleEmptyDistance(command, outAck);

    case CmdType::CAL_CLEAR_WEIGHT_MODEL: {
      String reason;
      if (!laserModule_->clearWeightModel(reason)) {
        outAck = HubAckBuilder::weightModelClearRejected(
            reason, reason != "UNSUPPORTED");
        return false;
      }
      outAck = HubAckBuilder::weightModelClear(false);
      return true;
    }

    default:
      outAck = HubAckBuilder::unsupported();
      return false;
  }
}

bool CalibrationCommandService::handleEmptyDistance(
    const Command& command,
    String& outAck) const {
  if (command.type == CmdType::EMPTY_DISTANCE_GET) {
    DeviceConfigSnapshot config{};
    String configReason;
    if (!laserModule_->getProfileConfig(config, configReason)) {
      outAck = HubAckBuilder::emptyDistanceRejected("GET", "CONFIG_ERROR");
      return false;
    }
    if (config.schema1Managed) {
      ProfileEmptyDistanceRecord profileRecord{};
      if (config.platformModel == PlatformModel::PLUS &&
          config.sensorProfile != SensorProfileId::NONE) {
        String reason;
        if (!laserModule_->getProfileEmptyDistance(profileRecord, reason)) {
          outAck = HubAckBuilder::emptyDistanceRejected("GET", reason);
          return false;
        }
      } else {
        profileRecord.sensorProfile = config.sensorProfile;
        profileRecord.configGeneration = config.generation;
      }
      outAck = HubAckBuilder::profileEmptyDistance(
          "GET", profileRecord.isSet,
          profileRecord.distanceMm, profileRecord.toleranceMm,
          profileRecord.sensorProfile, profileRecord.configGeneration,
          profileRecord.emptyRevision);
      return true;
    }
    EmptyDistanceRecord record{};
    const bool supported = laserModule_->laserInstalled() &&
        (strcmp(laserModule_->sensorBackendName(), "LASER_485") == 0 ||
         strcmp(laserModule_->sensorBackendName(), "CYD3S1612_IIC") == 0);
    const bool ready = supported && laserModule_->getEmptyDistance(record);
    outAck = HubAckBuilder::emptyDistance(
        "GET", supported,
        ready ? "SET" : (supported ? "UNSET" : "UNSUPPORTED"),
        ready ? record.distanceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR : NAN,
        ready ? record.toleranceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR : NAN,
        laserModule_->sensorBackendName());
    return true;
  }

  if (command.type == CmdType::EMPTY_DISTANCE_SET_CURRENT) {
    ProfileEmptyDistanceRecord saved{};
    String reason;
    if (!laserModule_->setEmptyDistanceFromCurrent(saved, reason)) {
      outAck = "NACK:EMPTY op=SET_CURRENT reason=";
      outAck += reason;
      return false;
    }
    outAck = HubAckBuilder::profileEmptyDistance(
        "SET_CURRENT", true, saved.distanceMm, saved.toleranceMm,
        saved.sensorProfile, saved.configGeneration, saved.emptyRevision);
    return true;
  }

  if (command.type == CmdType::EMPTY_DISTANCE_SET) {
    String reason;
    if (!laserModule_->setEmptyDistance(command.emptyDistance.distanceMm, reason)) {
      const bool supported = laserModule_->laserInstalled() &&
          (strcmp(laserModule_->sensorBackendName(), "LASER_485") == 0 ||
           strcmp(laserModule_->sensorBackendName(), "CYD3S1612_IIC") == 0);
      outAck = HubAckBuilder::emptyDistanceRejected("SET", reason, supported);
      return false;
    }
    EmptyDistanceRecord record{};
    laserModule_->getEmptyDistance(record);
    outAck = HubAckBuilder::emptyDistance(
        "SET", true, "SET",
        record.distanceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR,
        record.toleranceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR,
        laserModule_->sensorBackendName());
    return true;
  }

  String reason;
  if (!laserModule_->clearEmptyDistance(reason)) {
    const bool supported = laserModule_->laserInstalled() &&
        (strcmp(laserModule_->sensorBackendName(), "LASER_485") == 0 ||
         strcmp(laserModule_->sensorBackendName(), "CYD3S1612_IIC") == 0);
    outAck = HubAckBuilder::emptyDistanceRejected("CLEAR", reason, supported);
    return false;
  }
  DeviceConfigSnapshot config{};
  String configReason;
  if (!laserModule_->getProfileConfig(config, configReason)) {
    outAck = HubAckBuilder::emptyDistanceRejected("CLEAR", "CONFIG_ERROR");
    return false;
  }
  if (config.schema1Managed) {
    ProfileEmptyDistanceRecord cleared{};
    if (!laserModule_->getProfileEmptyDistance(cleared, configReason)) {
      outAck = HubAckBuilder::emptyDistanceRejected("CLEAR", configReason);
      return false;
    }
    outAck = HubAckBuilder::profileEmptyDistance(
        "CLEAR", false, NAN, NAN, cleared.sensorProfile,
        cleared.configGeneration, cleared.emptyRevision);
    return true;
  }
  outAck = HubAckBuilder::emptyDistance(
      "CLEAR", true, "UNSET", NAN, NAN,
      laserModule_->sensorBackendName());
  return true;
}
