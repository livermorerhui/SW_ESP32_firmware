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
        LASER_DISTANCE_RUNTIME_DIVISOR,
        0.0f,
        LASER_VALID_MEASUREMENT_MIN_RAW,
        LASER_VALID_MEASUREMENT_MAX_RAW,
        LASER_SENTINEL_OVER_RANGE_RAW,
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
  result.scaledDistance =
      static_cast<float>(result.signedRaw) / profile.decode.scaleDivisor + profile.decode.offset;

  if (rawRegister == profile.decode.sentinelOverRangeRaw) {
    result.sentinel = true;
    result.invalidReason = "SENTINEL_OVER_RANGE";
    return result;
  }

  if (result.signedRaw < profile.decode.validMinRaw) {
    result.invalidReason = "OUT_OF_RANGE_LOW";
    return result;
  }

  if (result.signedRaw > profile.decode.validMaxRaw) {
    result.invalidReason = "OUT_OF_RANGE_HIGH";
    return result;
  }

  result.validDistance = true;
  return result;
}

