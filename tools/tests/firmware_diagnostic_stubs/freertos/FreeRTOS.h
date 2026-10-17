#pragma once
#include <cstdint>
#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mux) (mux)->lock()
#define portEXIT_CRITICAL(mux) (mux)->unlock()
using StackType_t = uint8_t;
struct StaticTask_t { char bytes[256]; };
constexpr int tskNO_AFFINITY = -1;
extern thread_local bool testInIsr;
inline bool xPortInIsrContext() { return testInIsr; }
