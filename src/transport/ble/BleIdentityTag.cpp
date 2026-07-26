#include "BleIdentityTag.h"

#include <cstring>

#include <mbedtls/sha256.h>

namespace {

constexpr char kHashDomain[] = "SW-BLE-IDENTITY-TAG-V1:";
constexpr size_t kDigestBytes = 32;

bool equalsIgnoreCase(const char* value, const char* expected) {
  if (value == nullptr || expected == nullptr) return false;
  while (*value != '\0' && *expected != '\0') {
    char lhs = *value;
    char rhs = *expected;
    if (lhs >= 'a' && lhs <= 'z') lhs = static_cast<char>(lhs - 'a' + 'A');
    if (rhs >= 'a' && rhs <= 'z') rhs = static_cast<char>(rhs - 'a' + 'A');
    if (lhs != rhs) return false;
    ++value;
    ++expected;
  }
  return *value == '\0' && *expected == '\0';
}

}  // namespace

uint8_t BleIdentityTag::modelCodeForName(const char* modelName) {
  if (equalsIgnoreCase(modelName, "BASE")) return 1;
  if (equalsIgnoreCase(modelName, "PLUS")) return 2;
  if (equalsIgnoreCase(modelName, "PRO")) return 3;
  if (equalsIgnoreCase(modelName, "ULTRA")) return 4;
  return 0;
}

bool BleIdentityTag::build(const char* firmwareDeviceId,
                           const char* modelName,
                           uint8_t outPayload[kPayloadBytes]) {
  if (firmwareDeviceId == nullptr || firmwareDeviceId[0] == '\0' || outPayload == nullptr) {
    return false;
  }

  const size_t domainBytes = sizeof(kHashDomain) - 1;
  const size_t identityBytes = std::strlen(firmwareDeviceId);
  if (identityBytes > 128) return false;

  uint8_t input[domainBytes + 128]{};
  std::memcpy(input, kHashDomain, domainBytes);
  std::memcpy(input + domainBytes, firmwareDeviceId, identityBytes);

  uint8_t digest[kDigestBytes]{};
  if (mbedtls_sha256_ret(input, domainBytes + identityBytes, digest, 0) != 0) {
    return false;
  }

  outPayload[0] = kVersion;
  outPayload[1] = modelCodeForName(modelName);
  std::memcpy(outPayload + 2, digest, kTagBytes);
  return true;
}

bool BleIdentityTag::isValidPayload(const uint8_t* payload, size_t length) {
  return payload != nullptr && length == kPayloadBytes && payload[0] == kVersion;
}

std::string BleIdentityTag::serviceData(const uint8_t payload[kPayloadBytes]) {
  if (!isValidPayload(payload, kPayloadBytes)) return {};
  return std::string(reinterpret_cast<const char*>(payload), kPayloadBytes);
}
