#pragma once

#include <Arduino.h>
#include "config/GlobalConfig.h"

// 默认关闭内部诊断串口，避免正常使用时被技术字段刷屏。
// 排查固件问题时改为 true，即可恢复详细诊断输出。
#define SW_VERBOSE_SERIAL_PRINTF(...) \
  do { if (SERIAL_VERBOSE_DIAGNOSTICS) Serial.printf(__VA_ARGS__); } while (0)
#define SW_VERBOSE_SERIAL_PRINTLN(...) \
  do { if (SERIAL_VERBOSE_DIAGNOSTICS) Serial.println(__VA_ARGS__); } while (0)
#define SW_VERBOSE_SERIAL_PRINT(...) \
  do { if (SERIAL_VERBOSE_DIAGNOSTICS) Serial.print(__VA_ARGS__); } while (0)
