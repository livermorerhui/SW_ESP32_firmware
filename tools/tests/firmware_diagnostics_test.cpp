// 编译真实准入/编码/排空实现；只替换RTOS锁/时钟/任务启动及硬件sink。
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "config/GlobalConfig.h"
#include "core/FirmwareDiagnostics.h"
#include "core/FirmwareDiagnosticBuffer.h"
#include "core/FirmwareDiagnosticSink.h"
#include "core/HumanReadableLog.h"
uint32_t testNowMs = 100;
thread_local bool testInIsr = false;
bool testTaskFailure = false;
int testTaskCreations = 0;
namespace {
int count = 0, failed = 0;
size_t sinkLimit = 768;
bool sinkError = false, sinkUncertain = false;
std::string output;
void expect(const char* name, bool value) {
  ++count; if (!value) ++failed;
  std::printf("%s %s\n", value ? "PASS" : "FAIL", name);
}
void reset() {
  FirmwareDiagnostics::resetForTest(); testNowMs = 100; testTaskFailure = false;
  testTaskCreations = 0; sinkLimit = 768; sinkError = sinkUncertain = false; output.clear();
}
void drain() { for (int i = 0; i < 4096; ++i) FirmwareDiagnostics::drainForTest(); }
size_t occurrences(const std::string& value) {
  size_t result = 0, pos = 0;
  while ((pos = output.find(value, pos)) != std::string::npos) { ++result; pos += value.size(); }
  return result;
}
bool validUtf8(const std::string& text) {
  std::vector<char> checked(text.size() + 1); uint8_t flags = 0;
  FirmwareDiagnostics::copyText(checked.data(), checked.size(), text.c_str(), flags);
  return !(flags & 2);
}
bool eventSequencesIncrease() {
  uint64_t previous = 0;
  size_t position = 0;
  while ((position = output.find(" [SW_DIAG ", position)) != std::string::npos) {
    position = output.find(" seq=", position);
    if (position == std::string::npos) return false;
    char* end = nullptr;
    const uint64_t current = std::strtoull(output.c_str() + position + 5, &end, 10);
    if (end == output.c_str() + position + 5 || current <= previous) return false;
    previous = current;
    position = static_cast<size_t>(end - output.c_str());
  }
  return previous != 0;
}
}
namespace FirmwareDiagnostics {
void prepareSink() {}
const char* sinkName() { return "fake"; }
WriteResult writeSome(const uint8_t* bytes, size_t length) {
  const size_t accepted = sinkError ? 0 : std::min(sinkLimit, length);
  output.append(reinterpret_cast<const char*>(bytes), accepted);
  return {accepted, sinkError, sinkUncertain};
}
}
int main() {
  using namespace FirmwareDiagnostics;
  reset();
  expect("before_init_rejected", record(EventKind::Ready) == Admission::Unavailable && health().unavailable == 1);
  expect("begin_and_repeat_single_writer", begin() && begin() && testTaskCreations == 1);
  drain();
  expect("static_profile_capability", output.find("scope_mode=static") != std::string::npos && output.find("collector_observed=unknown") != std::string::npos);
  testInIsr = true;
  expect("isr_rejected_without_seq", record(EventKind::Safety) == Admission::InvalidContext && health().assigned == 0);
  testInIsr = false;
  reset(); testTaskFailure = true;
  expect("worker_failure_no_sync_fallback", !begin() && record(EventKind::Error) == Admission::Unavailable && output.empty());
  reset(); begin();
  bool regular = true, critical = true;
  for (size_t i = 0; i < kQueueCapacity - kCriticalReserve; ++i) regular &= record(EventKind::Command, "参数") == Admission::Accepted;
  expect("ordinary_exact_capacity", regular && health().queued == 24);
  expect("ordinary_cannot_use_reserve", record(EventKind::Command) == Admission::Full && health().normalDropped == 1);
  for (size_t i = 0; i < kCriticalReserve; ++i) critical &= record(EventKind::Safety, "原因", "停止") == Admission::Accepted;
  expect("critical_uses_reserve", critical && health().queued == 32);
  expect("full_critical_is_counted", record(EventKind::Safety) == Admission::Full && health().criticalDropped == 1 && health().assigned == 34);
  sinkLimit = 0; drainForTest();
  expect("blocked_sink_no_unbounded_admission", health().queued == 32 && output.empty());
  sinkLimit = 768; drain();
  expect("fifo_and_drop_seq_preserved", health().queued == 0 && health().transportAccepted == 33 && occurrences("[SW_DIAG") == 32 && output.find(" seq=25 ") == std::string::npos);
  reset(); begin(); drain(); output.clear(); sinkLimit = 5;
  HumanReadableLog::safety("USER_LEFT_PLATFORM", "暂停输出，等待条件恢复");
  drainForTest(); const size_t partial = output.size();
  sinkError = true; drainForTest();
  expect("write_failure_preserves_offset", partial == 5 && output.size() == partial && health().queued == 1 && health().writeErrors == 1);
  sinkError = false; sinkLimit = 1; drain();
  expect("partial_utf8_reassembles_exactly_once", validUtf8(output) && occurrences("[安全保护]") == 1 && output.find("原因=USER_LEFT_PLATFORM，处理=暂停输出，等待条件恢复 [SW_DIAG") != std::string::npos && output.back() == '\n');
  reset(); begin(); drain(); output.clear(); sinkUncertain = true;
  HumanReadableLog::waveParameters(35, 80, "参数已应用"); drain();
  expect("driver_acceptance_is_not_collector_ack", health().uncertainWrites > 0 && output.find("collector_observed=known") == std::string::npos && output.find("频率=35.00 Hz，最终强度=80/120") != std::string::npos);
  reset(); begin(); drain(); output.clear();
  std::string large; for (int i = 0; i < 300; ++i) large += "中文";
  record(EventKind::Measurement, large.c_str()); drain();
  expect("oversize_keeps_identity_and_utf8", validUtf8(output) && output.find("truncated=1") != std::string::npos && output.find(" seq=1 ") != std::string::npos);
  reset(); begin(); drain(); output.clear();
  record(EventKind::Error, "甲\n乙\r丙"); drain();
  expect("newline_cannot_inject_records", std::count(output.begin(), output.end(), '\n') == 1 && output.find("甲 乙 丙") != std::string::npos);
  bool cuts = true;
  for (size_t cap = 1; cap < 20; ++cap) {
    char text[24]{}; uint8_t flags = 0; copyText(text, cap, "中文四字", flags);
    cuts &= validUtf8(text) && std::strlen(text) < cap;
  }
  expect("all_utf8_cut_positions", cuts);
  reset(); begin(); drain(); output.clear();
  const char malformed[] = {'A', static_cast<char>(0xff), '\n', 'B', 0};
  record(EventKind::Error, malformed); drain();
  expect("malformed_utf8_is_explicit", validUtf8(output) && output.find("A? B") != std::string::npos && output.find("malformed=1") != std::string::npos);
  reset(); begin(); drain(); output.clear();
  HumanReadableLog::boot(nullptr, 1, nullptr); drain();
  HumanReadableLog::ready(); drain();
  HumanReadableLog::bleWaiting(nullptr); drain();
  HumanReadableLog::bleConnected(); drain();
  HumanReadableLog::bleDisconnected(nullptr); drain();
  HumanReadableLog::command(nullptr); drain();
  HumanReadableLog::waveParameters(35, 80, nullptr); drain();
  HumanReadableLog::waveStarted(35, 80); drain();
  HumanReadableLog::waveStopped(nullptr); drain();
  HumanReadableLog::cueRequested(nullptr, nullptr); drain();
  HumanReadableLog::cueStarted(nullptr); drain();
  HumanReadableLog::cueCompleted(nullptr); drain();
  HumanReadableLog::cueFailed(nullptr); drain();
  HumanReadableLog::bleDisconnectQueueRecovered(); drain();
  HumanReadableLog::bleLifecycleQueueFailed(true); drain();
  HumanReadableLog::stateChanged(nullptr, nullptr); drain();
  HumanReadableLog::startDecision(false, "：条件未满足"); drain();
  HumanReadableLog::safety(nullptr, nullptr); drain();
  HumanReadableLog::measurement(nullptr); drain();
  HumanReadableLog::baseline(nullptr); drain();
  HumanReadableLog::baselineEstablished(60, 20); drain();
  HumanReadableLog::baselineReady(60, 20); drain();
  HumanReadableLog::measurementFailure(); drain();
  HumanReadableLog::error(nullptr); drain();
  const char* chineseLines[] = {
    "[设备启动] 固件已启动：版本=未知，协议版本=1，重启原因=未知",
    "[设备状态] 设备已准备，可以连接 APP", "[蓝牙] 正在等待 APP 连接：原因=启动",
    "[蓝牙] APP 已连接", "[蓝牙] APP 已断开：原因=未知", "[控制指令] 收到 APP 指令：未知指令",
    "[波形参数] 参数已应用：频率=35.00 Hz，最终强度=80/120", "[波形输出] 已开始：频率=35.00 Hz，强度=80/120",
    "[波形输出] 已停止：原因=未知", "[提示音] 请求未知音效：未知结果", "[提示音] 已开始播放：未知音效",
    "[提示音] 已播放完成：未知音效", "[提示音] 播放失败，已停止输出：未知音效",
    "[连接] 控制队列拥堵，已优先处理设备断开", "[连接] 断开事件排队失败，请检查设备状态",
    "[设备状态] 状态变化：未知 → 未知", "[开始检查] 暂不能开始：条件未满足",
    "[安全保护] 原因=未知，处理=记录并继续观察", "[测量] 测量状态变化", "[基线] 基线状态变化",
    "[基线] 已建立：稳定重量=60.00 kg，距离=20.00", "[基线] 已准备完成：重量=60.00 kg，距离=20.00",
    "[测量] 激光传感器暂时无响应，正在等待恢复", "[错误] 设备发生错误"
  };
  bool chinesePreserved = occurrences("[SW_DIAG") == 24;
  for (const auto* line : chineseLines) chinesePreserved &= output.find(std::string(line) + " [SW_DIAG") != std::string::npos;
  expect("all_chinese_wrappers_preserve_full_line", chinesePreserved);
  reset(); begin(); drain(); output.clear();
  bootDiagnostic("SW-ESP32-1.0.0", 1, "POWERON", 1, 120000, 110000, 8000000, true);
  record(EventKind::RollbackReady); drain();
  expect("rollback_static_boot_evidence_keeps_independent_gate",
         output.find("[BOOT_DIAG] fw=SW-ESP32-1.0.0 proto=1 reset_reason=POWERON(1) heap_free=120000 heap_min_free=110000 psram_free=8000000 board=sonicwave_esp32s3_n16r8 psram_enabled=1 [SW_DIAG") != std::string::npos &&
         output.find("[BOOT_DIAG] Ready [SW_DIAG") != std::string::npos && health().assigned == 2);
  reset(); begin();
  const uint64_t before = health().assigned;
  expect("nonmatching_scope_not_assigned", !detailEnabled("src/modules/laser/Laser.cpp") && detailf("src/modules/laser/Laser.cpp", "skip") == Admission::Filtered && health().assigned == before);
  expect("matching_scope_matches_build", detailEnabled("src/modules/wave/Wave.cpp") == (SERIAL_VERBOSE_DIAGNOSTICS != 0));
  reset(); begin(); drain(); output.clear();
  std::atomic<int> admitted{0}, ordinaryAdmitted{0};
  std::vector<std::thread> producers;
  for (int t = 0; t < 8; ++t) producers.emplace_back([t, &admitted, &ordinaryAdmitted] {
    for (int i = 0; i < 1000; ++i) {
      const bool urgent = t == 7;
      if (record(urgent ? EventKind::Safety : EventKind::Command, "并发") == Admission::Accepted) {
        ++admitted; if (!urgent) ++ordinaryAdmitted;
      }
    }
  });
  for (auto& producer : producers) producer.join();
  const Health saturated = health();
  expect("concurrent_reserve_atomic", admitted == 32 && ordinaryAdmitted <= 24 && saturated.queued == 32 && saturated.peak == 32);
  expect("concurrent_identity_and_loss_accounting", saturated.assigned == 8000 && saturated.normalDropped + saturated.criticalDropped + admitted == 8000);
  drain();
  expect("concurrent_fifo_sequences_increase", health().queued == 0 && occurrences("[SW_DIAG") == 32 && eventSequencesIncrease());
  reset(); begin(); drain(); output.clear();
  std::atomic<bool> producersFinished{false};
  std::atomic<int> streamingAdmitted{0};
  std::thread writer([&producersFinished] {
    // 有界循环，错误实现不能让测试永久挂起；没有 sleep 或硬件时序依赖。
    for (int i = 0; i < 1000000; ++i) {
      drainForTest();
      if (producersFinished && health().queued == 0) break;
      std::this_thread::yield();
    }
  });
  producers.clear();
  for (int t = 0; t < 4; ++t) producers.emplace_back([t, &streamingAdmitted] {
    for (int i = 0; i < 1000; ++i)
      if (record(t == 3 ? EventKind::Safety : EventKind::Command, "边写边收") == Admission::Accepted)
        ++streamingAdmitted;
  });
  for (auto& producer : producers) producer.join();
  producersFinished = true; writer.join();
  const Health streamed = health();
  expect("concurrent_producer_and_writer_ring_wrap", streamed.queued == 0 && streamed.peak <= 32 &&
         occurrences("[SW_DIAG") == static_cast<size_t>(streamingAdmitted) && eventSequencesIncrease() &&
         streamed.assigned == 4000 && streamed.normalDropped + streamed.criticalDropped + streamingAdmitted == 4000);
  reset(); begin(); drain(); output.clear();
  for (int i = 0; i < 24; ++i) record(EventKind::Command);
  sinkLimit = 1; drainForTest();
  expect("inflight_counts_toward_capacity", record(EventKind::Command) == Admission::Full && health().queued == 24);
  const uint64_t assigned = health().assigned; sinkLimit = 768; drain(); testNowMs += 2000; drain();
  expect("health_not_recursive_or_seq_consuming", health().assigned == assigned && occurrences("[诊断状态]") == 1);
  expect("fixed_record_and_buffer_budget", sizeof(Event) <= 256 && sizeof(Buffer) < 9 * 1024);
  std::printf("assertions=%d failures=%d\n", count, failed);
  return failed ? 1 : 0;
}
