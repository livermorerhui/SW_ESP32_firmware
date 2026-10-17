// 当前日志节流纯策略回归；不依赖设备、RTOS 或 PlatformIO。
#include <cstdio>
#include <cstdint>
#include "core/FirmwareLogPolicy.h"

namespace {
int assertions = 0;
int failures = 0;
void expect(const char* name, bool passed) {
  ++assertions;
  if (!passed) ++failures;
  std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}
}

int main() {
  using FirmwareLogPolicy::shouldLogNow;
  uint32_t last = 0;
  expect("initial_emission", shouldLogNow(100, last, 2000) && last == 100);
  expect("interval_suppressed", !shouldLogNow(101, last, 2000) && last == 100);
  expect("interval_last_ms", !shouldLogNow(2099, last, 2000) && last == 100);
  expect("interval_exact_boundary", shouldLogNow(2100, last, 2000) && last == 2100);
  expect("force_bypasses_interval", shouldLogNow(2101, last, 2000, true) && last == 2101);
  expect("zero_interval", shouldLogNow(2101, last, 0) && last == 2101);
  last = UINT32_MAX - 9;
  expect("wrap_before_interval", !shouldLogNow(9, last, 20) && last == UINT32_MAX - 9);
  expect("wrap_exact_boundary", shouldLogNow(10, last, 20) && last == 10);
  last = 1;
  expect("maximum_interval", shouldLogNow(0, last, UINT32_MAX) && last == 0);
  last = 300;
  expect("force_updates_watermark", shouldLogNow(301, last, 2000, true) && last == 301);
  std::printf("assertions=%d failures=%d\n", assertions, failures);
  return failures ? 1 : 0;
}
