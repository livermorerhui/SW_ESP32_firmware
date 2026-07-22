#pragma once

#include <Arduino.h>

// 设备身份 owner：只负责 eFuse 原始 MAC 到稳定伪标识的派生。
// 真相源是 ESP32 eFuse；不负责 BLE 命令、OTA、日志或发布策略。
class FirmwareDeviceIdentityProvider {
public:
  static constexpr int kIdentityVersion = 1;

  static bool deriveFromBaseMac(const uint8_t baseMac[6], String& outDeviceId);
  static bool readFromEfuse(String& outDeviceId);
  static bool isValid(const String& deviceId);
  static String logSuffix(const String& deviceId);
};
