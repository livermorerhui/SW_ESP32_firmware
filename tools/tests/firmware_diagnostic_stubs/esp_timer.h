#pragma once
#include <cstdint>
extern uint32_t testNowMs;
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(testNowMs) * 1000; }
