#pragma once
#include "FirmwareDiagnostics.h"
#include <cstddef>
#include <cstdint>

namespace FirmwareDiagnostics {
constexpr size_t kQueueCapacity = 32;
constexpr size_t kCriticalReserve = 8;
constexpr size_t kTextBytes = 200;
constexpr size_t kFrameBytes = 768;
constexpr uint32_t kHealthIntervalMs = 2000;
struct Event {
  uint64_t seq = 0, timestampUs = 0;
  float firstValue = 0, secondValue = 0;
  int32_t number = 0;
  uint32_t bootFacts[5]{};
  EventKind kind = EventKind::Ready;
  Owner owner = Owner::System;
  bool critical = false;
  uint8_t flags = 0; // bit 0: 截短；bit 1: 非法 UTF-8 已替换
  uint8_t split = 0;
  char text[kTextBytes] = {};
};
static_assert(sizeof(Event) <= 256, "诊断记录超过固定预算");
// 调用者必须持同一短锁。容量包含已取出但尚未写完的一条记录。
class Buffer {
 public:
  Admission push(const Event& event);
  bool take(Event& event);
  void complete();
  Health state;
 private:
  Event entries_[kQueueCapacity]{};
  size_t head_ = 0, count_ = 0;
  bool inFlight_ = false;
};
bool isCritical(EventKind kind);
Owner ownerFor(EventKind kind);
Owner ownerForFile(const char* file);
const char* ownerName(Owner owner);
const char* eventName(EventKind kind);
// 返回复制字节数；限长保留完整 UTF-8，换行变为空格，防止伪造额外日志行。
size_t copyText(char* target, size_t capacity, const char* source, uint8_t& flags);
size_t encode(const Event& event, uint64_t stream, char* output, size_t capacity);
class PartialFrame {
 public:
  char bytes[kFrameBytes]{};
  size_t length = 0, offset = 0;
  bool active() const { return offset < length; }
  void accepted(size_t count) { offset += count > length - offset ? length - offset : count; }
};
}
