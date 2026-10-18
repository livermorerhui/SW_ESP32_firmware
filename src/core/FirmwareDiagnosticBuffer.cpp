#include "FirmwareDiagnosticBuffer.h"
#include <cstdio>
#include <cstring>

namespace FirmwareDiagnostics {
Admission Buffer::push(const Event& event) {
  ++state.assigned;
  const size_t outstanding = count_ + (inFlight_ ? 1 : 0);
  if (outstanding >= kQueueCapacity || (!event.critical && outstanding >= kQueueCapacity - kCriticalReserve)) {
    if (event.critical) ++state.criticalDropped; else ++state.normalDropped;
    return Admission::Full;
  }
  entries_[(head_ + count_) % kQueueCapacity] = event;
  entries_[(head_ + count_) % kQueueCapacity].seq = state.assigned;
  ++count_;
  state.queued = static_cast<uint32_t>(outstanding + 1);
  if (state.queued > state.peak) state.peak = state.queued;
  return Admission::Accepted;
}
bool Buffer::take(Event& event) {
  if (inFlight_ || !count_) return false;
  event = entries_[head_];
  head_ = (head_ + 1) % kQueueCapacity;
  --count_;
  inFlight_ = true;
  return true;
}
void Buffer::complete() {
  inFlight_ = false;
  state.queued = static_cast<uint32_t>(count_);
}
bool isCritical(EventKind kind) {
  switch (kind) {
    case EventKind::Boot: case EventKind::BleConnected: case EventKind::BleDisconnected:
    case EventKind::WaveStarted: case EventKind::WaveStopped: case EventKind::CueFailed:
    case EventKind::BleQueueRecovered: case EventKind::BleQueueFailed: case EventKind::StateChanged:
    case EventKind::StartDecision: case EventKind::Safety: case EventKind::MeasurementFailure:
    case EventKind::Error: case EventKind::BootDiagnostic: case EventKind::RollbackReady: return true;
    default: return false;
  }
}
Owner ownerFor(EventKind kind) {
  switch (kind) {
    case EventKind::WaveParameters: case EventKind::WaveStarted: case EventKind::WaveStopped:
    case EventKind::CueRequested: case EventKind::CueStarted: case EventKind::CueCompleted:
    case EventKind::CueFailed: return Owner::Wave;
    case EventKind::BleWaiting: case EventKind::BleConnected: case EventKind::BleDisconnected:
    case EventKind::BleQueueRecovered: case EventKind::BleQueueFailed: return Owner::Ble;
    case EventKind::Measurement: case EventKind::Baseline: case EventKind::BaselineEstablished:
    case EventKind::BaselineReady: case EventKind::MeasurementFailure: return Owner::Laser;
    default: return Owner::System;
  }
}
Owner ownerForFile(const char* file) {
  if (!file) return Owner::System;
  if (std::strstr(file, "/wave/")) return Owner::Wave;
  if (std::strstr(file, "/laser/")) return Owner::Laser;
  if (std::strstr(file, "/ble/")) return Owner::Ble;
  if (std::strstr(file, "/ota/")) return Owner::Ota;
  return Owner::System;
}
const char* ownerName(Owner owner) {
  switch (owner) {
    case Owner::Wave: return "wave"; case Owner::Laser: return "laser";
    case Owner::Ble: return "ble"; case Owner::Ota: return "ota";
    default: return "system";
  }
}
const char* eventName(EventKind kind) {
  static const char* const names[] = {"BOOT","READY","BLE_WAITING","BLE_CONNECTED","BLE_DISCONNECTED",
    "COMMAND","WAVE_PARAMETERS","WAVE_STARTED","WAVE_STOPPED","CUE_REQUESTED","CUE_STARTED",
    "CUE_COMPLETED","CUE_FAILED","BLE_QUEUE_RECOVERED","BLE_QUEUE_FAILED","STATE_CHANGED",
    "START_DECISION","SAFETY","MEASUREMENT","BASELINE","BASELINE_ESTABLISHED","BASELINE_READY",
    "MEASUREMENT_FAILURE","ERROR","DETAIL","BOOT_DIAGNOSTIC","ROLLBACK_READY"};
  const size_t index = static_cast<size_t>(kind);
  return index < sizeof(names) / sizeof(names[0]) ? names[index] : "UNKNOWN";
}
size_t copyText(char* target, size_t capacity, const char* source, uint8_t& flags) {
  if (!capacity) { if (source && *source) flags |= 1; return 0; }
  if (!source) source = "";
  size_t in = 0, out = 0;
  while (source[in]) {
    const auto c = static_cast<unsigned char>(source[in]);
    size_t width = c < 0x80 ? 1 : (c >= 0xc2 && c <= 0xdf ? 2 :
        (c >= 0xe0 && c <= 0xef ? 3 : (c >= 0xf0 && c <= 0xf4 ? 4 : 0)));
    bool valid = width != 0;
    for (size_t j = 1; valid && j < width; ++j)
      valid = source[in + j] && (static_cast<unsigned char>(source[in + j]) & 0xc0) == 0x80;
    if (valid && width >= 3) {
      const auto next = static_cast<unsigned char>(source[in + 1]);
      valid = !(c == 0xe0 && next < 0xa0) && !(c == 0xed && next >= 0xa0) &&
              !(c == 0xf0 && next < 0x90) && !(c == 0xf4 && next >= 0x90);
    }
    if (!valid) {
      if (out + 1 >= capacity) { flags |= 1; break; }
      target[out++] = '?'; ++in; flags |= 2; continue;
    }
    if (out + width >= capacity) { flags |= 1; break; }
    if (width == 1 && (c == '\n' || c == '\r')) target[out++] = ' ';
    else { std::memcpy(target + out, source + in, width); out += width; }
    in += width;
  }
  target[out] = '\0';
  return out;
}
size_t encode(const Event& e, uint64_t stream, char* out, size_t cap) {
  const char* a = e.text;
  const char* b = e.split < kTextBytes ? e.text + e.split : "";
  int n = 0;
#define HUMAN(...) n = std::snprintf(out, cap, __VA_ARGS__)
  switch (e.kind) {
    case EventKind::Boot: HUMAN("[设备启动] 固件已启动：版本=%s，协议版本=%d，重启原因=%s", a, static_cast<int>(e.number), b); break;
    case EventKind::Ready: HUMAN("[设备状态] 设备已准备，可以连接 APP"); break;
    case EventKind::BleWaiting: HUMAN("[蓝牙] 正在等待 APP 连接：原因=%s", a); break;
    case EventKind::BleConnected: HUMAN("[蓝牙] APP 已连接"); break;
    case EventKind::BleDisconnected: HUMAN("[蓝牙] APP 已断开：原因=%s", a); break;
    case EventKind::Command: HUMAN("[控制指令] 收到 APP 指令：%s", a); break;
    case EventKind::WaveParameters: HUMAN("[波形参数] %s：频率=%.2f Hz，最终强度=%d/120", a, e.firstValue, static_cast<int>(e.number)); break;
    case EventKind::WaveStarted: HUMAN("[波形输出] 已开始：频率=%.2f Hz，强度=%d/120", e.firstValue, static_cast<int>(e.number)); break;
    case EventKind::WaveStopped: HUMAN("[波形输出] 已停止：原因=%s", a); break;
    case EventKind::CueRequested: HUMAN("[提示音] 请求%s：%s", a, b); break;
    case EventKind::CueStarted: HUMAN("[提示音] 已开始播放：%s", a); break;
    case EventKind::CueCompleted: HUMAN("[提示音] 已播放完成：%s", a); break;
    case EventKind::CueFailed: HUMAN("[提示音] 播放失败，已停止输出：%s", a); break;
    case EventKind::BleQueueRecovered: HUMAN("[连接] 控制队列拥堵，已优先处理设备断开"); break;
    case EventKind::BleQueueFailed: HUMAN("[连接] %s事件排队失败，请检查设备状态", e.number ? "断开" : "连接"); break;
    case EventKind::StateChanged: HUMAN("[设备状态] 状态变化：%s → %s", a, b); break;
    case EventKind::StartDecision: HUMAN("[开始检查] %s%s", e.number ? "已满足开始条件，可以输出" : "暂不能开始", e.number ? "" : a); break;
    case EventKind::Safety: HUMAN("[安全保护] 原因=%s，处理=%s", a, b); break;
    case EventKind::Measurement: HUMAN("[测量] %s", a); break;
    case EventKind::Baseline: HUMAN("[基线] %s", a); break;
    case EventKind::BaselineEstablished: HUMAN("[基线] 已建立：稳定重量=%.2f kg，距离=%.2f", e.firstValue, e.secondValue); break;
    case EventKind::BaselineReady: HUMAN("[基线] 已准备完成：重量=%.2f kg，距离=%.2f", e.firstValue, e.secondValue); break;
    case EventKind::MeasurementFailure: HUMAN("[测量] 激光传感器暂时无响应，正在等待恢复"); break;
    case EventKind::Error: HUMAN("[错误] %s", a); break;
    case EventKind::Detail: HUMAN("%s", a); break;
    case EventKind::BootDiagnostic:
      HUMAN("[BOOT_DIAG] fw=%s proto=%d reset_reason=%s(%u) heap_free=%u heap_min_free=%u psram_free=%u board=sonicwave_esp32s3_n16r8 psram_enabled=%u",
            a, static_cast<int>(e.number), b, static_cast<unsigned>(e.bootFacts[0]),
            static_cast<unsigned>(e.bootFacts[1]), static_cast<unsigned>(e.bootFacts[2]),
            static_cast<unsigned>(e.bootFacts[3]), static_cast<unsigned>(e.bootFacts[4])); break;
    case EventKind::RollbackReady: HUMAN("[BOOT_DIAG] Ready"); break;
  }
#undef HUMAN
  if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
  // 原前缀/中文不变，诊断身份仅附加到行尾；不会进入 BLE 帧。
  const int tail = std::snprintf(out + n, cap - n,
      " [SW_DIAG schema=1 stream=%016llx seq=%llu ts_us=%llu event=%s owner=%s level=%s truncated=%u malformed=%u]\n",
      static_cast<unsigned long long>(stream), static_cast<unsigned long long>(e.seq),
      static_cast<unsigned long long>(e.timestampUs), eventName(e.kind), ownerName(e.owner),
      e.critical ? "critical" : "info", e.flags & 1, (e.flags >> 1) & 1);
  return tail < 0 || static_cast<size_t>(tail) >= cap - n ? 0 : static_cast<size_t>(n + tail);
}
}
