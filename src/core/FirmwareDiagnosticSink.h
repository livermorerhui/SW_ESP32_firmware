#pragma once
#include <cstddef>
#include <cstdint>
namespace FirmwareDiagnostics {
struct WriteResult {
  size_t accepted;
  bool error;
  bool uncertain;
  WriteResult(size_t acceptedBytes = 0, bool failed = false, bool unconfirmed = false)
      : accepted(acceptedBytes), error(failed), uncertain(unconfirmed) {}
};
// 只允许后台 writer 调用；返回的是驱动接纳，绝不是电脑收到。
void prepareSink();
const char* sinkName();
WriteResult writeSome(const uint8_t* bytes, size_t length);
}
