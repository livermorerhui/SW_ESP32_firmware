#include "FirmwareDeviceIdentityProvider.h"

#include <esp_err.h>
#include <esp_mac.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace {

constexpr char kIdentityDomain[] = "SW-FIRMWARE-DEVICE-ID-V1:";
constexpr char kIdentityPrefix[] = "sw1_";
constexpr size_t kBaseMacBytes = 6;
constexpr size_t kDigestBytes = 32;
constexpr size_t kDerivedBytes = 16;
constexpr size_t kHexLength = kDerivedBytes * 2;

char hexDigit(uint8_t value) {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

}  // namespace

bool FirmwareDeviceIdentityProvider::deriveFromBaseMac(
    const uint8_t baseMac[kBaseMacBytes],
    String& outDeviceId) {
  if (baseMac == nullptr) {
    outDeviceId = "";
    return false;
  }

  uint8_t payload[(sizeof(kIdentityDomain) - 1) + kBaseMacBytes]{};
  memcpy(payload, kIdentityDomain, sizeof(kIdentityDomain) - 1);
  memcpy(payload + sizeof(kIdentityDomain) - 1, baseMac, kBaseMacBytes);

  uint8_t digest[kDigestBytes]{};
  if (mbedtls_sha256_ret(payload, sizeof(payload), digest, 0) != 0) {
    outDeviceId = "";
    return false;
  }

  String next;
  next.reserve(sizeof(kIdentityPrefix) - 1 + kHexLength);
  next = kIdentityPrefix;
  for (size_t index = 0; index < kDerivedBytes; ++index) {
    next += hexDigit(static_cast<uint8_t>(digest[index] >> 4));
    next += hexDigit(static_cast<uint8_t>(digest[index] & 0x0f));
  }
  outDeviceId = next;
  return true;
}

bool FirmwareDeviceIdentityProvider::readFromEfuse(String& outDeviceId) {
  uint8_t baseMac[kBaseMacBytes]{};
  if (esp_efuse_mac_get_default(baseMac) != ESP_OK) {
    outDeviceId = "";
    return false;
  }
  return deriveFromBaseMac(baseMac, outDeviceId);
}

bool FirmwareDeviceIdentityProvider::isValid(const String& deviceId) {
  if (!deviceId.startsWith(kIdentityPrefix) || deviceId.length() != (sizeof(kIdentityPrefix) - 1 + kHexLength)) {
    return false;
  }
  for (size_t index = sizeof(kIdentityPrefix) - 1; index < deviceId.length(); ++index) {
    const char value = deviceId.c_str()[index];
    if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
      return false;
    }
  }
  return true;
}

String FirmwareDeviceIdentityProvider::logSuffix(const String& deviceId) {
  if (!isValid(deviceId)) return "invalid";
  return deviceId.substring(deviceId.length() - 8);
}
