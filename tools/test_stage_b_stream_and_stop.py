#!/usr/bin/env python3
"""Run selected production BLE TX and stop methods against deterministic host fakes.

The test compiles the method bodies read directly from the firmware sources. It
does not duplicate their branch logic or claim to replace a device BLE test.
"""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def method(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise ValueError(f"Unterminated method: {signature}")


BLE_HARNESS = r'''
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#define strlcpy hostStrlcpy

struct String {
  std::string value;
  String(const char* text) : value(text) {}
  const char* c_str() const { return value.c_str(); }
  bool startsWith(const char* prefix) const { return value.rfind(prefix, 0) == 0; }
  bool equalsIgnoreCase(const char* other) const {
    const std::string expected(other);
    if (value.size() != expected.size()) return false;
    for (size_t index = 0; index < value.size(); ++index) {
      if (std::tolower(static_cast<unsigned char>(value[index])) !=
          std::tolower(static_cast<unsigned char>(expected[index]))) return false;
    }
    return true;
  }
};

using UBaseType_t = unsigned;
using TickType_t = unsigned;
static constexpr int pdTRUE = 1;
static constexpr int pdFALSE = 0;
static constexpr int portMAX_DELAY = -1;
static constexpr uint32_t kControlLatencyWarnMs = 80;
static constexpr uint8_t kMaxControlBurstBeforeStream = 4;
static constexpr TickType_t kDeferredStreamRetryDelayTicks = 1;
#define pdMS_TO_TICKS(n) (n)
#define SW_VERBOSE_SERIAL_PRINTF(...) std::fprintf(stderr, __VA_ARGS__)

static size_t hostStrlcpy(char* dest, const char* source, size_t size) {
  const size_t length = std::strlen(source);
  if (size) {
    const size_t count = length < size - 1 ? length : size - 1;
    std::memcpy(dest, source, count);
    dest[count] = '\0';
  }
  return length;
}

struct TxMsg {
  enum class Priority : uint8_t { CONTROL, STREAM };
  Priority priority = Priority::CONTROL;
  char line[512]{};
  uint32_t enqueuedAtMs = 0;
  uint32_t streamGeneration = 0;
  uint32_t streamSessionId = 0;
};
struct FakeQueue { std::deque<TxMsg> messages; };
using QueueHandle_t = FakeQueue*;
using QueueSetMemberHandle_t = void*;
using SemaphoreHandle_t = std::mutex*;
struct EndOfQueue : std::exception {};
static FakeQueue* activeControl = nullptr;
static FakeQueue* activeStream = nullptr;
static uint32_t fakeClock = 100;
static uint32_t millis() { return fakeClock++; }
static UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
  return queue ? static_cast<UBaseType_t>(queue->messages.size()) : 0;
}
static int xQueueSend(QueueHandle_t queue, const void* message, int) {
  queue->messages.push_back(*static_cast<const TxMsg*>(message));
  return pdTRUE;
}
static int xQueueOverwrite(QueueHandle_t queue, const void* message) {
  queue->messages.clear();
  queue->messages.push_back(*static_cast<const TxMsg*>(message));
  return pdTRUE;
}
static int xQueueReceive(QueueHandle_t queue, void* message, int) {
  if (!queue || queue->messages.empty()) return pdFALSE;
  *static_cast<TxMsg*>(message) = queue->messages.front();
  queue->messages.pop_front();
  return pdTRUE;
}
static void* xQueueSelectFromSet(void*, int) {
  if (uxQueueMessagesWaiting(activeControl)) return activeControl;
  if (uxQueueMessagesWaiting(activeStream)) return activeStream;
  throw EndOfQueue{};
}
static void vTaskDelay(TickType_t) {}
static void xSemaphoreTake(SemaphoreHandle_t mutex, int) { mutex->lock(); }
static void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }

struct FakeOtaManager {
  bool blocking = false;
  bool blocksBusinessCommands() const { return blocking; }
};

class BleTransport {
public:
  using TxMsg = ::TxMsg;
  enum class TxFrameClass : uint8_t { STREAM_EVENT };
  FakeQueue control, stream;
  std::mutex mutex;
  QueueHandle_t txControlQueue = &control;
  QueueHandle_t txStreamQueue = &stream;
  SemaphoreHandle_t streamSendMutex = &mutex;
  std::atomic<uint32_t> streamGeneration{1};
  FakeOtaManager otaManager;
  void* txQueueSet = this;
  uint32_t sessionId = 7;
  uint32_t txStreamReplaceCount = 0;
  UBaseType_t txStreamHighWatermark = 0;
  uint8_t controlBurstSinceStream = 0;
  std::vector<std::string> sent;
  std::vector<std::string> skipped;

  void enqueueStream(const char* line) { assert(enqueueStreamTxLineRaw(line)); }
  void enqueueControl(const char* line) {
    TxMsg message{};
    strlcpy(message.line, line, sizeof(message.line));
    assert(xQueueSend(txControlQueue, &message, 0) == pdTRUE);
  }
  void drain() {
    activeControl = txControlQueue;
    activeStream = txStreamQueue;
    try { txTaskLoop(); } catch (const EndOfQueue&) {}
  }
  void invalidatePendingStreamFrames();
  bool enqueueStreamTxLineRaw(const char* line);
  bool businessCommandBlockedByOta(const String& line) const;
  void txTaskLoop();
  void sendLineNow(const char* line) { sent.emplace_back(line); }
  void noteTxSendSkipped(TxFrameClass, const char*, const char* line) {
    skipped.emplace_back(line);
  }
  bool shouldDeferStreamForControl(uint32_t) const { return false; }
  void noteStreamSuppressedForControl(UBaseType_t, uint32_t) {}
  void flushStreamSuppressionSummaryIfNeeded(uint32_t) {}
  void noteQueueWatermark(const char*, UBaseType_t depth, UBaseType_t& high) {
    if (depth > high) high = depth;
  }
};

__BLE_METHODS__

int main() {
  BleTransport transport;
  transport.otaManager.blocking = true;
  for (const char* command : {
           "DEVICE:SET_CONFIG platform_model=PLUS,sensor_profile=J100_485",
           "EMPTY:SET_CURRENT", "empty:set_current", "EMPTY:SET distance_mm=112.55",
           "EMPTY:CLEAR", "empty:clear", "SCALE:ZERO", "scale:zero",
           "SCALE:CAL 112.55,1.0", "CAL:ZERO"}) {
    assert(transport.businessCommandBlockedByOta(String(command)));
  }
  for (const char* command : {"EMPTY:GET", "DEVICE:GET_CONFIG", "CAP?", "SNAPSHOT?"}) {
    assert(!transport.businessCommandBlockedByOta(String(command)));
  }
  transport.otaManager.blocking = false;
  assert(!transport.businessCommandBlockedByOta(String("EMPTY:SET_CURRENT")));
  std::puts("PASS: production OTA gate blocks NVS mutations and permits reads");

  transport.enqueueStream("EVT:STREAM old_profile");
  transport.invalidatePendingStreamFrames();
  transport.drain();
  assert(transport.sent.empty());
  assert(transport.skipped.size() == 1 &&
         transport.skipped.back() == "EVT:STREAM old_profile");

  transport.enqueueStream("EVT:STREAM new_profile");
  transport.drain();
  assert(transport.sent.size() == 1 &&
         transport.sent.back() == "EVT:STREAM new_profile");

  transport.enqueueControl("ACK:DEVICE_CONFIG new_profile");
  transport.enqueueStream("EVT:STREAM pending_old_profile");
  transport.invalidatePendingStreamFrames();
  transport.drain();
  assert(transport.sent.size() == 2 &&
         transport.sent.back() == "ACK:DEVICE_CONFIG new_profile");
  assert(transport.skipped.size() == 2);

  transport.enqueueStream("EVT:STREAM previous_session");
  ++transport.sessionId;
  transport.drain();
  assert(transport.sent.size() == 2);
  assert(transport.skipped.size() == 3);

  transport.enqueueStream("EVT:STREAM replaced");
  transport.enqueueStream("EVT:STREAM latest");
  transport.drain();
  assert(transport.sent.size() == 3 &&
         transport.sent.back() == "EVT:STREAM latest");
  std::puts("PASS: production BLE stream generation and session fence");
}
'''


STOP_HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define SW_VERBOSE_SERIAL_PRINTF(...) std::fprintf(stderr, __VA_ARGS__)
enum class TopState : uint8_t { IDLE, ARMED, RUNNING, FAULT_STOP };
enum class FaultCode : uint8_t { NONE, FALL_SUSPECTED };
enum class SafetySignalKind : uint8_t { NONE, RECOVERABLE_PAUSE };
enum class VerificationStopSource : uint8_t {
  NONE, FORMAL_SAFETY_OTHER, USER_MANUAL_OTHER
};
static const char* topStateName(TopState state) {
  switch (state) {
    case TopState::IDLE: return "IDLE";
    case TopState::ARMED: return "ARMED";
    case TopState::RUNNING: return "RUNNING";
    case TopState::FAULT_STOP: return "FAULT_STOP";
  }
  return "UNKNOWN";
}
static const char* faultCodeName(FaultCode code) {
  return code == FaultCode::NONE ? "NONE" : "FALL_SUSPECTED";
}
struct LogMarker { static constexpr const char* kFsm = "FSM"; };
struct HumanReadableLog { static void waveStopped(const char*) {} };
struct FakeWave {
  unsigned stopCount = 0;
  void stopSoft() { ++stopCount; }
};
struct StopEvent {
  FaultCode code;
  SafetySignalKind effect;
  TopState target;
  std::string reason;
  VerificationStopSource source;
};
class SystemStateMachine {
public:
  TopState st = TopState::IDLE;
  FaultCode blocking_fault_code = FaultCode::NONE;
  FaultCode pause_reason_code = FaultCode::NONE;
  FaultCode warning_fault_code = FaultCode::NONE;
  bool start_ready = false;
  bool runtime_ready = false;
  float start_ready_stable_weight_kg = 0;
  const char* pending_stop_reason_text = nullptr;
  VerificationStopSource pending_stop_source = VerificationStopSource::NONE;
  std::string lastReason;
  VerificationStopSource lastSource = VerificationStopSource::NONE;
  SafetySignalKind lastEffect = SafetySignalKind::NONE;
  FakeWave* wave = nullptr;
  std::vector<StopEvent> emitted;

  void stopForConfigurationFault();
  void requestStop();
  FaultCode visibleReasonCode() const { return blocking_fault_code; }
  bool canEnterArmedState() const { return runtime_ready && start_ready; }
  void setState(TopState next) {
    st = next;
    if (wave && next != TopState::RUNNING) wave->stopSoft();
  }
  const char* resolvedStopReasonText(FaultCode, const char* fallback) const {
    return pending_stop_reason_text ? pending_stop_reason_text : fallback;
  }
  VerificationStopSource resolvedStopSource(VerificationStopSource fallback) const {
    return pending_stop_source == VerificationStopSource::NONE ? fallback : pending_stop_source;
  }
  void rememberStopContext(const char* reason, VerificationStopSource source,
                           SafetySignalKind effect) {
    lastReason = reason;
    lastSource = source;
    lastEffect = effect;
  }
  void emitStopEvent(FaultCode code, SafetySignalKind effect, TopState target,
                     const char* reason, VerificationStopSource source) {
    emitted.push_back({code, effect, target, reason, source});
  }
  void clearPendingStopContext() {
    pending_stop_reason_text = nullptr;
    pending_stop_source = VerificationStopSource::NONE;
  }
};

__STOP_METHODS__

int main() {
  FakeWave wave;
  SystemStateMachine machine;
  machine.wave = &wave;
  machine.st = TopState::RUNNING;
  machine.stopForConfigurationFault();
  assert(machine.st == TopState::IDLE);
  assert(wave.stopCount > 0);
  assert(machine.emitted.size() == 1);
  assert(machine.emitted[0].reason == "CONFIG_ERROR");
  assert(machine.emitted[0].code == FaultCode::NONE);
  assert(machine.emitted[0].source == VerificationStopSource::NONE);
  assert(machine.emitted[0].effect == SafetySignalKind::NONE);
  assert(machine.pending_stop_reason_text == nullptr);

  machine.st = TopState::RUNNING;
  machine.requestStop();
  assert(machine.emitted.size() == 2);
  assert(machine.emitted.back().reason == "MANUAL_STOP");
  assert(machine.emitted.back().source == VerificationStopSource::USER_MANUAL_OTHER);

  // A leftover pause reason must not turn a storage failure into a fall/leave
  // classification or a recoverable-pause effect.
  machine.st = TopState::RUNNING;
  machine.pause_reason_code = FaultCode::FALL_SUSPECTED;
  machine.stopForConfigurationFault();
  assert(machine.emitted.size() == 3);
  assert(machine.emitted.back().reason == "CONFIG_ERROR");
  assert(machine.emitted.back().code == FaultCode::NONE);
  assert(machine.emitted.back().source == VerificationStopSource::NONE);
  assert(machine.emitted.back().effect == SafetySignalKind::NONE);
  machine.pause_reason_code = FaultCode::NONE;

  // A simultaneous formal blocking fault takes requestStop's early return.
  // Its temporary CONFIG_ERROR attribution must not leak into later actions.
  machine.st = TopState::RUNNING;
  machine.blocking_fault_code = FaultCode::FALL_SUSPECTED;
  machine.stopForConfigurationFault();
  assert(machine.st == TopState::FAULT_STOP);
  assert(machine.pending_stop_reason_text == nullptr);
  std::puts("PASS: production CONFIG_ERROR stop attribution and manual stop isolation");
}
'''


CAL_RESET_HARNESS = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using String = std::string;
using std::isfinite;
#define SW_VERBOSE_SERIAL_PRINTF(...) ((void)0)

struct Preferences {};
static bool bindingWriteOk = true;
static bool clearModelBinding(Preferences&) { return bindingWriteOk; }
struct ScopedSemaphore {
  explicit ScopedSemaphore(bool available) : available(available) {}
  bool held() const { return available; }
  bool available;
};
struct CalibrationModel {
  float referenceDistance = 0.0f;
  float coefficients[3]{};
};
struct EmptyDistanceRecord {
  float distanceRuntime = 0.0f;
};
struct FakeModelStore {
  bool clearOk = true;
  bool clearModel(Preferences&) const { return clearOk; }
};
struct FakeDistanceStateChain {
  void reset(const char*) {}
  void setEmptyReference(float, bool) {}
};
struct FakeRhythmStateJudge {
  void reset(const char*) {}
};
struct FakeStateMachine {
  void setRuntimeReady(bool) {}
  void setStartReadiness(bool, float) {}
};
class LaserModule {
public:
  bool profileTransitionMutex = true;
  bool preferencesOpened = true;
  bool identityValid = true;
  bool mutationBlocked = false;
  bool calibrationModelReady = true;
  bool emptyDistanceReady = true;
  bool needSendParams = false;
  float zeroDistance = 9.0f;
  float scaleFactor = 3.0f;
  struct { bool schema1Managed = true; } deviceConfig;
  struct { float calibrationZeroDistance = 0.0f; } dualZero;
  Preferences preferences;
  CalibrationModel calibrationModel;
  EmptyDistanceRecord emptyDistanceRecord{112.55f};
  FakeModelStore calibrationModelStore;
  FakeDistanceStateChain distanceStateChain;
  FakeRhythmStateJudge rhythmStateJudge;
  FakeStateMachine* sm = nullptr;

  bool configIdentityValid() const { return identityValid; }
  bool configurationMutationBlocked(String& reason) const {
    if (mutationBlocked) reason = "NOT_IDLE";
    return mutationBlocked;
  }
  void refreshEffectiveZero() {}
  void resetRuntimeZero(const char*) {}
  void resetStableTracking(const char*, bool) {}
  void resetMeasurementPlane(const char*, bool) {}
  void clearStableContractBridge(const char*) {}
  bool clearWeightModel(String& reason);
  bool clearCalibrationParameters(String& reason);
};

__CAL_RESET_METHODS__

int main() {
  {
    LaserModule laser;
    String reason;
    bindingWriteOk = false;
    assert(!laser.clearCalibrationParameters(reason));
    assert(reason == "NVS_WRITE_FAILED");
    assert(!laser.needSendParams);
  }
  {
    LaserModule laser;
    String reason;
    bindingWriteOk = true;
    laser.calibrationModelStore.clearOk = false;
    assert(!laser.clearCalibrationParameters(reason));
    assert(reason == "NVS_WRITE_FAILED");
    assert(!laser.needSendParams);
  }
  {
    LaserModule laser;
    String reason;
    bindingWriteOk = true;
    assert(laser.clearCalibrationParameters(reason));
    assert(laser.needSendParams);
    assert(!laser.calibrationModelReady);
    assert(laser.emptyDistanceReady);
    assert(laser.emptyDistanceRecord.distanceRuntime == 112.55f);
  }
  std::puts("PASS: production CAL:ZERO clear publishes PARAMS only after success");
}
'''


def compile_and_run(source: str, label: str, directory: Path) -> None:
    code = directory / f"{label}.cpp"
    binary = directory / label
    code.write_text(source)
    subprocess.run(
        ["g++", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror", str(code), "-o", str(binary)],
        check=True,
        cwd=ROOT,
    )
    subprocess.run([str(binary)], check=True, cwd=ROOT)


def main() -> None:
    ble = (ROOT / "src/transport/ble/BleTransport.cpp").read_text()
    state = (ROOT / "src/core/SystemStateMachine.cpp").read_text()
    laser = (ROOT / "src/modules/laser/LaserModule.cpp").read_text()
    ble_methods = "\n\n".join(
        method(ble, signature)
        for signature in (
            "bool BleTransport::enqueueStreamTxLineRaw(const char* s)",
            "void BleTransport::invalidatePendingStreamFrames()",
            "bool BleTransport::businessCommandBlockedByOta(const String& s) const",
            "void BleTransport::txTaskLoop()",
        )
    )
    stop_methods = "\n\n".join(
        method(state, signature)
        for signature in (
            "void SystemStateMachine::stopForConfigurationFault()",
            "void SystemStateMachine::requestStop()",
        )
    )
    cal_reset_methods = "\n\n".join(
        method(laser, signature)
        for signature in (
            "bool LaserModule::clearWeightModel(String& reason)",
            "bool LaserModule::clearCalibrationParameters(String& reason)",
        )
    )
    with tempfile.TemporaryDirectory(prefix="stage-b-host-replay-") as path:
        directory = Path(path)
        compile_and_run(BLE_HARNESS.replace("__BLE_METHODS__", ble_methods), "stream_fence", directory)
        compile_and_run(STOP_HARNESS.replace("__STOP_METHODS__", stop_methods), "config_stop", directory)
        compile_and_run(
            CAL_RESET_HARNESS.replace("__CAL_RESET_METHODS__", cal_reset_methods),
            "calibration_reset",
            directory,
        )


if __name__ == "__main__":
    main()
