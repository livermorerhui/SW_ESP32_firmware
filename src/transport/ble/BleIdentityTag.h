#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Stable discovery hint. It is deliberately not an authentication credential.
class BleIdentityTag {
public:
  static constexpr uint8_t kVersion = 1;
  static constexpr size_t kTagBytes = 8;
  static constexpr size_t kPayloadBytes = 2 + kTagBytes;
  static constexpr size_t kUuidBytes = 16;
  static constexpr size_t kServiceDataAdBytes = 2 + kUuidBytes + kPayloadBytes;
  static constexpr size_t kFlagsAdBytes = 3;
  static constexpr size_t kLegacyBlePayloadLimit = 31;

  // Wire payload: version(1), model_code(1), tag(8).
  static bool build(const char* firmwareDeviceId,
                    const char* modelName,
                    uint8_t outPayload[kPayloadBytes]);

  static uint8_t modelCodeForName(const char* modelName);

  static bool isValidPayload(const uint8_t* payload, size_t length);

  // Returns only the service-data value. BLEAdvertisementData adds the UUID.
  static std::string serviceData(const uint8_t payload[kPayloadBytes]);

  // A tagged device puts its service data in the primary advertisement so a
  // central can form one scan candidate without waiting for Scan Response.
  static_assert(kFlagsAdBytes + kServiceDataAdBytes <= kLegacyBlePayloadLimit,
                "identity tag must fit in one legacy BLE primary advertisement");
};
