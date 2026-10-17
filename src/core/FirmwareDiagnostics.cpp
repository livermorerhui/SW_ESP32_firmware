#include "FirmwareDiagnostics.h"
#include "FirmwareDiagnosticBuffer.h"
#include "FirmwareDiagnosticSink.h"
#include "config/GlobalConfig.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace FirmwareDiagnostics {
namespace {
portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
struct Lock {
  Lock() { portENTER_CRITICAL(&guard); }
  ~Lock() { portEXIT_CRITICAL(&guard); }
};
Buffer queue;
PartialFrame frame;
Event current;
uint64_t stream = 0;
uint32_t lastHealthMs = 0;
bool healthSent = false, healthFrame = false, initialized = false;
StaticTask_t taskStorage;
StackType_t stackStorage[4096 / sizeof(StackType_t)];
static_assert(sizeof(queue) + sizeof(frame) + sizeof(current) + sizeof(taskStorage) +
              sizeof(stackStorage) + 256 <= 16 * 1024, "诊断静态内存超过16KiB预算");

Admission submit(const Event& event) {
  if (xPortInIsrContext()) return Admission::InvalidContext;
  Lock lock;
  if (!queue.state.ready) { ++queue.state.unavailable; return Admission::Unavailable; }
  return queue.push(event);
}
void drainOnce() {
  if (!frame.active()) {
    const uint32_t now = millis();
    if (!healthSent || now - lastHealthMs >= kHealthIntervalMs) {
      const Health h = health();
      const int length = std::snprintf(frame.bytes, sizeof(frame.bytes),
          "[诊断状态] schema=1 stream=%016llx scope_mode=static profile=%d backend=%s event_bytes=%u queue_capacity=%u critical_reserve=%u assigned=%llu queued=%u peak=%u normal_dropped=%llu critical_dropped=%llu filtered=%llu unavailable=%llu write_errors=%llu transport_uncertain=%llu transport_accepted=%llu collector_observed=unknown ready=%u\n",
          static_cast<unsigned long long>(stream), SW_DIAGNOSTIC_PROFILE, sinkName(),
          static_cast<unsigned>(sizeof(Event)), static_cast<unsigned>(kQueueCapacity), static_cast<unsigned>(kCriticalReserve),
          static_cast<unsigned long long>(h.assigned), h.queued, h.peak,
          static_cast<unsigned long long>(h.normalDropped), static_cast<unsigned long long>(h.criticalDropped),
          static_cast<unsigned long long>(h.filtered), static_cast<unsigned long long>(h.unavailable),
          static_cast<unsigned long long>(h.writeErrors), static_cast<unsigned long long>(h.uncertainWrites),
          static_cast<unsigned long long>(h.transportAccepted), h.ready ? 1u : 0u);
      frame.length = length > 0 && static_cast<size_t>(length) < sizeof(frame.bytes) ? static_cast<size_t>(length) : 0;
      frame.offset = 0;
      healthFrame = true; healthSent = true; lastHealthMs = now;
    } else {
      { Lock lock; if (!queue.take(current)) return; }
      frame.length = encode(current, stream, frame.bytes, sizeof(frame.bytes));
      frame.offset = 0; healthFrame = false;
      if (!frame.length) {
        Lock lock; ++queue.state.writeErrors; queue.complete(); return;
      }
    }
  }
  if (!frame.active()) return;
  const size_t remaining = frame.length - frame.offset;
  const WriteResult result = writeSome(reinterpret_cast<const uint8_t*>(frame.bytes + frame.offset), remaining);
  frame.accepted(result.accepted);
  {
    Lock lock;
    if (result.error || result.accepted > remaining) ++queue.state.writeErrors;
    if (result.uncertain) ++queue.state.uncertainWrites;
    if (!frame.active() && !healthFrame) {
      queue.state.transportAccepted = current.seq;
      queue.complete();
    }
  }
  // 背压/失败时保持同一frame和offset，不重新发送前缀、不丢半行后拼接新事件。
}
void writerTask(void*) {
  for (;;) {
    for (unsigned i = 0; i < 8; ++i) drainOnce();
    vTaskDelay(1); // 有界批次，避免日志worker占满CPU；不改变业务任务调度参数。
  }
}
}
bool begin() {
  if (xPortInIsrContext()) return false;
  { Lock lock; if (initialized) return queue.state.ready; initialized = true; }
  prepareSink();
  stream = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
  { Lock lock; queue.state.ready = true; }
  const auto task = xTaskCreateStaticPinnedToCore(writerTask, "DiagnosticLog", sizeof(stackStorage), nullptr,
                                                 1, stackStorage, &taskStorage, tskNO_AFFINITY);
  if (!task) { Lock lock; queue.state.ready = false; }
  return task != nullptr;
}
Health health() { Lock lock; return queue.state; }
bool detailEnabled(const char* sourceFile) {
  const Owner owner = ownerForFile(sourceFile);
  const bool enabled = SERIAL_VERBOSE_DIAGNOSTICS &&
      (SW_DIAGNOSTIC_PROFILE == 1 ||
       (SW_DIAGNOSTIC_PROFILE == 2 && (owner == Owner::Wave || owner == Owner::System)) ||
       (SW_DIAGNOSTIC_PROFILE == 3 && (owner == Owner::Laser || owner == Owner::System)) ||
       (SW_DIAGNOSTIC_PROFILE == 4 && (owner == Owner::Ble || owner == Owner::System)) ||
       (SW_DIAGNOSTIC_PROFILE == 5 && (owner == Owner::Ota || owner == Owner::System)));
  if (!enabled && !xPortInIsrContext()) { Lock lock; ++queue.state.filtered; }
  return enabled;
}
Admission record(EventKind kind, const char* first, const char* second, float firstValue, float secondValue, int32_t number) {
  if (xPortInIsrContext()) return Admission::InvalidContext;
  Event event;
  event.timestampUs = static_cast<uint64_t>(esp_timer_get_time());
  event.kind = kind; event.owner = ownerFor(kind); event.critical = isCritical(kind);
  event.firstValue = firstValue; event.secondValue = secondValue; event.number = number;
  const size_t firstBytes = copyText(event.text, sizeof(event.text), first, event.flags);
  event.split = static_cast<uint8_t>(firstBytes + 1);
  copyText(event.text + event.split, sizeof(event.text) - event.split, second, event.flags);
  return submit(event);
}
Admission detailf(const char* sourceFile, const char* format, ...) {
  if (xPortInIsrContext()) return Admission::InvalidContext;
  if (!detailEnabled(sourceFile)) return Admission::Filtered;
  Event event;
  event.timestampUs = static_cast<uint64_t>(esp_timer_get_time());
  event.kind = EventKind::Detail; event.owner = ownerForFile(sourceFile);
  event.critical = event.owner == Owner::Ota;
  char text[kTextBytes]{};
  va_list args; va_start(args, format);
  const int length = format ? std::vsnprintf(text, sizeof(text), format, args) : 0;
  va_end(args);
  if (length < 0) { text[0] = '\0'; event.flags |= 2; }
  if (length >= static_cast<int>(sizeof(text))) event.flags |= 1;
  copyText(event.text, sizeof(event.text), text, event.flags);
  return submit(event);
}
Admission bootDiagnostic(const char* firmware, int protocolVersion, const char* resetReason,
                         uint32_t resetCode, uint32_t freeHeap, uint32_t minimumHeap,
                         uint32_t freePsram, bool psramEnabled) {
  if (xPortInIsrContext()) return Admission::InvalidContext;
  Event event;
  event.timestampUs = static_cast<uint64_t>(esp_timer_get_time());
  event.kind = EventKind::BootDiagnostic; event.critical = true; event.number = protocolVersion;
  event.bootFacts[0] = resetCode; event.bootFacts[1] = freeHeap;
  event.bootFacts[2] = minimumHeap; event.bootFacts[3] = freePsram; event.bootFacts[4] = psramEnabled;
  event.split = static_cast<uint8_t>(copyText(event.text, sizeof(event.text), firmware, event.flags) + 1);
  copyText(event.text + event.split, sizeof(event.text) - event.split, resetReason, event.flags);
  return submit(event);
}
#ifdef SW_DIAGNOSTIC_HOST_TEST
void drainForTest() { drainOnce(); }
void resetForTest() {
  queue = Buffer{}; frame = PartialFrame{}; current = Event{};
  initialized = healthSent = healthFrame = false; stream = 0; lastHealthMs = 0;
}
#endif
}
