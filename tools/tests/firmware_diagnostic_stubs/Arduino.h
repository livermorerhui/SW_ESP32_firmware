#pragma once
#include <cstdint>
#include <cstddef>
using std::uint8_t; using std::uint16_t; using std::uint32_t;
extern uint32_t testNowMs;
inline uint32_t millis() { return testNowMs; }
