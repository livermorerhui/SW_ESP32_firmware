#pragma once

#include "config/GlobalConfig.h"
#include "modules/laser/RuntimeZeroObserver.h"

// Owns the runtime-zero observation window and its decision observer.
// Calibration and effective-zero policy remain orchestrated by LaserModule.
class LaserRuntimeZeroRuntime final {
public:
  void resetWindow() {
    head = 0;
    count = 0;
  }

  void push(float distance) {
    buffer[head] = distance;
    head = (head + 1) % WINDOW_N;
    if (count < WINDOW_N) ++count;
  }

  bool empty() const { return count == 0; }

  RuntimeZeroWindowInput window(float calibrationZero) const {
    RuntimeZeroWindowInput input{};
    input.values = buffer;
    input.head = head;
    input.count = count;
    input.capacity = WINDOW_N;
    input.calibrationZero = calibrationZero;
    return input;
  }

  RuntimeZeroObserver observer{};

private:
  float buffer[WINDOW_N]{};
  uint8_t head = 0;
  uint8_t count = 0;
};
