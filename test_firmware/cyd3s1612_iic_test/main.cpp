#include <Arduino.h>

#include "modules/laser/Cyd3s1612I2cReader.h"

namespace {
Cyd3s1612I2cReader sensor;
uint32_t lastReadMs = 0;
uint32_t previousReadCompletedMs = 0;
uint32_t sequence = 0;
constexpr uint32_t kReadIntervalMs = 33UL;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== CYD3S1612-IIC hardware test ===");
  Serial.println("wiring: SDA=GPIO16 SCL=GPIO15 VCC=3.3V GND=GND");
  sensor.begin();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastReadMs < kReadIntervalMs) {
    delay(1);
    return;
  }
  lastReadMs = now;

  const Cyd3s1612I2cReadResult result = sensor.read();
  const uint32_t intervalMs = previousReadCompletedMs == 0
      ? 0
      : result.readCompletedAtMs - previousReadCompletedMs;
  previousReadCompletedMs = result.readCompletedAtMs;
  Serial.printf(
      "[I2C_TEST] seq=%lu timestamp_ms=%lu interval_ms=%lu transport_ok=%d valid=%d "
      "raw_mm=%u distance_mm=%.0f duration_ms=%lu write_error=%u received_bytes=%u "
      "wire_error=%u reason=%s\n",
      static_cast<unsigned long>(sequence++),
      static_cast<unsigned long>(result.readCompletedAtMs),
      static_cast<unsigned long>(intervalMs),
      result.transportOk ? 1 : 0,
      result.validDistance ? 1 : 0,
      static_cast<unsigned>(result.rawDistanceMm),
      result.distanceMm,
      static_cast<unsigned long>(result.readDurationMs),
      static_cast<unsigned>(result.writeError),
      static_cast<unsigned>(result.receivedBytes),
      static_cast<unsigned>(result.wireError),
      result.invalidReason ? result.invalidReason : "OK");
}
