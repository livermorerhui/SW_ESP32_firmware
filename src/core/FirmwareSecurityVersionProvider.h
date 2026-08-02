#pragma once

#include <Arduino.h>

// Read-only security facts. This owner never changes eFuse or OTA partitions.
struct FirmwareSecurityVersionFacts {
  uint32_t appSecurityVersion = 0;
  uint32_t efuseSecurityVersion = 0;
  uint32_t currentSecurityVersion = 0;
  bool appAntiRollbackGate = false;
};

class FirmwareSecurityVersionProvider {
public:
  static constexpr int kProtocolVersion = 1;

  static bool read(FirmwareSecurityVersionFacts& outFacts);
};
