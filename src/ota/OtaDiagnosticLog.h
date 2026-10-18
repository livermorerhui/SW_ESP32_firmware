#pragma once
#include "core/VerboseSerial.h"
// OTA中文/技术前缀保持，所有产品诊断走同一writer，系统panic不在此合同内。
#define SW_OTA_LOGF(fmt, ...) SW_VERBOSE_SERIAL_PRINTF(fmt, ##__VA_ARGS__)
#define SW_OTA_ROLLBACK_LOGF(fmt, ...) SW_VERBOSE_SERIAL_PRINTF(fmt, ##__VA_ARGS__)
