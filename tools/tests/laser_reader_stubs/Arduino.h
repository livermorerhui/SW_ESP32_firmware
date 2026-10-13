#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::int16_t;
using std::isfinite;

#define I2S_NUM_0 0
#define SERIAL_8N1 1

extern std::atomic<uint32_t> g_fakeNowMs;
inline uint32_t millis() { return ++g_fakeNowMs; }

class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  bool operator==(const char* other) const {
    return value_ == (other ? other : "");
  }
  bool equalsIgnoreCase(const char* other) const {
    std::string rhs(other ? other : "");
    if (value_.size() != rhs.size()) return false;
    for (size_t i = 0; i < rhs.size(); ++i) {
      char left = value_[i];
      char right = rhs[i];
      if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
      if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
      if (left != right) return false;
    }
    return true;
  }

 private:
  std::string value_;
};

struct FakeSerial {
  int beginCount = 0;
  int endCount = 0;
  int lastRxPin = -1;
  int lastTxPin = -1;
  void begin(uint32_t, uint32_t, int rx, int tx) {
    ++beginCount;
    lastRxPin = rx;
    lastTxPin = tx;
  }
  void end() { ++endCount; }
  template <typename... Args>
  int printf(const char*, Args...) { return 0; }
  void println(const char*) {}
  void print(const char*) {}
};

extern FakeSerial Serial;
extern FakeSerial Serial1;
