#include "modules/laser/LaserSensorProfile.h"

#include "config/GlobalConfig.h"

namespace {

const LaserSensorProfile kWangWtxJ100Profile{
    LaserSensorProfileId::WANG_WTX_J100_485,
    "WANG_WTX_J100_485",
    LaserSensorSerialConfig{
        MODBUS_BAUD,
        SERIAL_8N1,
    },
    LaserSensorModbusConfig{
        MODBUS_SLAVE_ID,
        LaserModbusReadFunction::READ_INPUT_REGISTERS,
        REG_DISTANCE,
        1,
    },
    LaserSensorDecodeConfig{
        true,
        -(LASER_DISTANCE_RUNTIME_DIVISOR * LASER_DISTANCE_RUNTIME_DIVISOR),
        LASER_WANG_REFERENCE_DISTANCE_RUNTIME,
        LASER_VALID_MEASUREMENT_MIN_RAW,
        LASER_VALID_MEASUREMENT_MAX_RAW,
        LASER_SENTINEL_OVER_RANGE_RAW,
        0.01f,   // signed displacement raw unit -> millimetres
        100.0f,  // J100 mechanical measurement centre
        -1.0f,   // negative displacement means the target is farther away
        0.01f,   // legacy runtime unit is physical millimetres / 100
        64.3f,   // 100 - (+35.7) mm
        135.7f,  // 100 - (-35.7) mm
    },
};

}  // namespace

const LaserSensorProfile& activeLaserSensorProfile() {
  return kWangWtxJ100Profile;
}

const char* laserSensorProfileIdName(LaserSensorProfileId id) {
  switch (id) {
    case LaserSensorProfileId::WANG_WTX_J100_485:
      return "WANG_WTX_J100_485";
  }
  return "UNKNOWN";
}

LaserDistanceDecodeResult decodeLaserDistanceRaw(
    const LaserSensorProfile& profile,
    uint16_t rawRegister) {
  LaserDistanceDecodeResult result{};
  result.signedRaw =
      profile.decode.signedRaw16 ? static_cast<int16_t>(rawRegister) : static_cast<int16_t>(rawRegister);
  // Keep the legacy runtime expression available for diagnostics and old
  // callers.  The physical value below is the new unit-of-measure contract.
  result.scaledDistance =
      static_cast<float>(result.signedRaw) / profile.decode.scaleDivisor + profile.decode.offset;

  if (rawRegister == profile.decode.sentinelOverRangeRaw) {
    result.sentinel = true;
    result.quality = LaserMeasurementQuality::MODBUS_SENTINEL;
    result.invalidReason = "SENTINEL_OVER_RANGE";
    return result;
  }

  if (result.signedRaw < profile.decode.validMinRaw) {
    result.quality = LaserMeasurementQuality::MODBUS_OUT_OF_RANGE_LOW;
    result.invalidReason = "OUT_OF_RANGE_LOW";
    return result;
  }

  if (result.signedRaw > profile.decode.validMaxRaw) {
    result.quality = LaserMeasurementQuality::MODBUS_OUT_OF_RANGE_HIGH;
    result.invalidReason = "OUT_OF_RANGE_HIGH";
    return result;
  }

  result.physicalDistanceMm =
      profile.decode.physicalOffsetMm +
      profile.decode.physicalDirection *
          static_cast<float>(result.signedRaw) * profile.decode.rawToPhysicalScaleMm;
  result.scaledDistance = result.physicalDistanceMm * profile.decode.runtimeUnitsPerMm;
  if (!isfinite(result.physicalDistanceMm) || !isfinite(result.scaledDistance)) {
    result.quality = LaserMeasurementQuality::DISTANCE_NONFINITE;
    result.invalidReason = "DISTANCE_NONFINITE";
    return result;
  }
  if (profile.decode.validMinPhysicalMm < profile.decode.validMaxPhysicalMm &&
      (result.physicalDistanceMm < profile.decode.validMinPhysicalMm ||
       result.physicalDistanceMm > profile.decode.validMaxPhysicalMm)) {
    result.quality = result.physicalDistanceMm < profile.decode.validMinPhysicalMm
        ? LaserMeasurementQuality::MODBUS_OUT_OF_RANGE_LOW
        : LaserMeasurementQuality::MODBUS_OUT_OF_RANGE_HIGH;
    result.invalidReason = result.physicalDistanceMm < profile.decode.validMinPhysicalMm
        ? "PHYSICAL_DISTANCE_OUT_OF_RANGE_LOW"
        : "PHYSICAL_DISTANCE_OUT_OF_RANGE_HIGH";
    return result;
  }

  result.validDistance = true;
  result.quality = LaserMeasurementQuality::VALID;
  return result;
}
