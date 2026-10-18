#pragma once
#include <cstddef>
#include <cstdint>

// 仅任务上下文；不改变 BLE、状态机或输出语义。所有文本均复制，不借用调用者指针。
namespace FirmwareDiagnostics {
enum class EventKind : uint16_t {
  Boot, Ready, BleWaiting, BleConnected, BleDisconnected, Command,
  WaveParameters, WaveStarted, WaveStopped, CueRequested, CueStarted,
  CueCompleted, CueFailed, BleQueueRecovered, BleQueueFailed, StateChanged,
  StartDecision, Safety, Measurement, Baseline, BaselineEstablished,
  BaselineReady, MeasurementFailure, Error, Detail, BootDiagnostic, RollbackReady
};
enum class Owner : uint8_t { System, Wave, Laser, Ble, Ota };
enum class Admission : uint8_t { Accepted, Filtered, Full, Unavailable, InvalidContext };
struct Health {
  uint64_t assigned = 0, transportAccepted = 0;
  uint64_t normalDropped = 0, criticalDropped = 0;
  uint64_t filtered = 0, unavailable = 0, writeErrors = 0, uncertainWrites = 0;
  uint32_t queued = 0, peak = 0;
  bool ready = false;
};
// Serial.begin 完成之后、首个产品日志之前调用；重复调用不创建第二个 writer。
bool begin();
Health health();
bool detailEnabled(const char* sourceFile);
Admission record(EventKind kind, const char* first = nullptr, const char* second = nullptr,
                 float firstValue = 0, float secondValue = 0, int32_t number = 0);
Admission detailf(const char* sourceFile, const char* format, ...) __attribute__((format(printf, 2, 3)));
// 仅既有 rollback USB 测试构建使用；保留它原本独立于 verbose 的启动证据。
Admission bootDiagnostic(const char* firmware, int protocolVersion, const char* resetReason,
                         uint32_t resetCode, uint32_t freeHeap, uint32_t minimumHeap,
                         uint32_t freePsram, bool psramEnabled);
#ifdef SW_DIAGNOSTIC_HOST_TEST
void resetForTest();
void drainForTest();
#endif
}
