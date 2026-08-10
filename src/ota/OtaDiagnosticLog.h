#pragma once

#include <Arduino.h>

#if defined(SW_ESP32_ROLLBACK_USB_DIAGNOSTICS)
#include <esp_log.h>

#define SW_OTA_LOGF(fmt, ...) \
  ESP_LOGI("OTA", fmt, ##__VA_ARGS__)
#define SW_OTA_ROLLBACK_LOGF(fmt, ...) \
  ESP_LOGI("OTA_ROLLBACK", fmt, ##__VA_ARGS__)

#else

#define SW_OTA_LOGF(fmt, ...) \
  Serial.printf(fmt "\n", ##__VA_ARGS__)
#define SW_OTA_ROLLBACK_LOGF(fmt, ...) \
  Serial.printf(fmt "\n", ##__VA_ARGS__)

#endif
