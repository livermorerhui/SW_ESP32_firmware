#pragma once
#include "config/GlobalConfig.h"
#include "FirmwareDiagnostics.h"

// 先判断静态profile再求值；详细文本仅做有界格式化，不在调用线程执行串口I/O。
#define SW_VERBOSE_SERIAL_PRINTF(...) \
  do { if (SERIAL_VERBOSE_DIAGNOSTICS && FirmwareDiagnostics::detailEnabled(__FILE__)) FirmwareDiagnostics::detailf(__FILE__, __VA_ARGS__); } while (0)
#define SW_VERBOSE_SERIAL_PRINTLN(text) SW_VERBOSE_SERIAL_PRINTF("%s", text)
#define SW_VERBOSE_SERIAL_PRINT(text) SW_VERBOSE_SERIAL_PRINTF("%s", text)
