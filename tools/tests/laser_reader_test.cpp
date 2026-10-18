#include <cassert>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <thread>

#include "modules/laser/LaserMeasurementReader.h"
#include "core/FirmwareDiagnostics.h"

// Reader 测试隔离诊断外部副作用；writer 准入/背压由固件日志专项测试覆盖。
namespace FirmwareDiagnostics {
Admission record(EventKind, const char*, const char*, float, float, int32_t) {
  return Admission::Accepted;
}
}

std::atomic<uint32_t> g_fakeNowMs{1000};
std::atomic<int> g_fakeMutexTakeAttempts{0};
FakeSerial Serial;
FakeSerial Serial1;
TwoWire Wire;
std::function<uint8_t()> g_modbusRead;
uint16_t g_modbusRaw = 0;
int g_modbusBeginCount = 0;
int g_modbusReadCount = 0;

namespace {
Cyd3s1612I2cReadResult g_cydRead{};
int g_cydBeginCount = 0;
int g_cydReadCount = 0;

bool near(float lhs, float rhs) {
  return std::fabs(lhs - rhs) < 0.001f;
}

void testProfileSwitchAndFreshSampleIdentity() {
  LaserMeasurementReader reader;
  const auto absent = reader.read(TopState::IDLE, false, 0);
  assert(!absent.transportOk);
  assert(absent.sensorProfile == SensorProfileId::NONE);
  assert(Serial1.beginCount == 0 && g_cydBeginCount == 0);

  assert(reader.configure(SensorProfileId::NONE));
  assert(Serial1.beginCount == 0 && g_cydBeginCount == 0);

  assert(reader.configure(SensorProfileId::J100_485));
  assert(reader.backend() == LaserMeasurementBackend::LASER_485);
  assert(Serial1.lastRxPin == 17 && Serial1.lastTxPin == 18);
  assert(Serial1.beginCount == 1 && g_modbusBeginCount == 1);
  const uint32_t j100Epoch = reader.readerEpoch();
  assert(j100Epoch != 0);

  // J100 reports signed displacement; -12.25 mm maps to 112.25 mm.
  g_modbusRaw = static_cast<uint16_t>(static_cast<int16_t>(-1225));
  const auto firstJ100 = reader.read(TopState::IDLE, true, 0);
  assert(firstJ100.transportOk && firstJ100.validDistance);
  assert(firstJ100.sensorProfile == SensorProfileId::J100_485);
  assert(firstJ100.readerEpoch == j100Epoch && firstJ100.sampleSequence == 1);
  assert(near(firstJ100.physicalDistanceMm, 112.25f));
  assert(!firstJ100.valueRepeated);
  const auto secondJ100 = reader.read(TopState::IDLE, true, 0);
  assert(secondJ100.sampleSequence == 2 && secondJ100.valueRepeated);

  // A repeated SET of the active profile must not reopen the transport or
  // make existing samples look stale by changing their reader epoch.
  assert(reader.configure(SensorProfileId::J100_485));
  assert(reader.readerEpoch() == j100Epoch);
  assert(Serial1.beginCount == 1 && Serial1.endCount == 0);

  g_cydRead.transportOk = true;
  g_cydRead.validDistance = true;
  g_cydRead.sampleFresh = true;
  g_cydRead.physicalDistanceMm = 123.0f;
  g_cydRead.quality = LaserMeasurementQuality::VALID;
  g_cydRead.sampleSequence = 999;  // Must not leak into unified sequence.
  g_cydRead.readStartedAtMs = 1100;
  g_cydRead.readCompletedAtMs = 1101;

  assert(reader.configure(SensorProfileId::CYD3S1612_IIC));
  const uint32_t cydEpoch = reader.readerEpoch();
  assert(cydEpoch != j100Epoch);
  assert(reader.backend() == LaserMeasurementBackend::CYD3S1612_IIC);
  assert(Serial1.endCount == 1 && g_cydBeginCount == 1);
  const auto firstCyd = reader.read(TopState::IDLE, true, 0);
  assert(firstCyd.transportOk && firstCyd.validDistance);
  assert(firstCyd.sensorProfile == SensorProfileId::CYD3S1612_IIC);
  assert(firstCyd.readerEpoch == cydEpoch && firstCyd.sampleSequence == 1);
  assert(near(firstCyd.physicalDistanceMm, 123.0f));
  assert(g_cydReadCount == 1);
  assert(firstJ100.readerEpoch != reader.readerEpoch());

  assert(reader.configure(SensorProfileId::J100_485));
  const uint32_t newJ100Epoch = reader.readerEpoch();
  assert(newJ100Epoch != cydEpoch);
  assert(Wire.endCount == 1);
  assert(Serial1.beginCount == 2);
  const auto newJ100 = reader.read(TopState::IDLE, true, 0);
  assert(newJ100.sensorProfile == SensorProfileId::J100_485);
  assert(newJ100.readerEpoch == newJ100Epoch && newJ100.sampleSequence == 1);
  assert(!newJ100.valueRepeated);

  reader.stop();
  assert(reader.backend() == LaserMeasurementBackend::SENSOR_UNAVAILABLE);
  assert(Serial1.endCount == 2);
  assert(!reader.read(TopState::IDLE, true, 0).transportOk);
  assert(!reader.configure(static_cast<SensorProfileId>(99)));
  assert(reader.backend() == LaserMeasurementBackend::SENSOR_UNAVAILABLE);
}

void testSwitchWaitsForInFlightRead() {
  LaserMeasurementReader reader;
  assert(reader.configure(SensorProfileId::J100_485));
  const uint32_t oldEpoch = reader.readerEpoch();
  g_modbusRaw = 0;

  std::mutex readGate;
  std::condition_variable readEvent;
  bool enteredRead = false;
  bool releaseRead = false;
  g_modbusRead = [&] {
    std::unique_lock<std::mutex> lock(readGate);
    enteredRead = true;
    readEvent.notify_all();
    readEvent.wait(lock, [&] { return releaseRead; });
    return uint8_t{0};
  };

  MeasurementReadResult oldResult{};
  std::thread reading([&] { oldResult = reader.read(TopState::IDLE, true, 0); });
  {
    std::unique_lock<std::mutex> lock(readGate);
    readEvent.wait(lock, [&] { return enteredRead; });
  }
  std::atomic<bool> beganSwitch{false};
  std::atomic<bool> finishedSwitch{false};
  const int takeAttemptsBeforeSwitch = g_fakeMutexTakeAttempts;
  std::thread switching([&] {
    beganSwitch = true;
    assert(reader.configure(SensorProfileId::CYD3S1612_IIC));
    finishedSwitch = true;
  });
  while (!beganSwitch) std::this_thread::yield();
  while (g_fakeMutexTakeAttempts == takeAttemptsBeforeSwitch) {
    std::this_thread::yield();
  }
  assert(!finishedSwitch);
  assert(reader.readerEpoch() == oldEpoch);
  assert(Serial1.endCount == 2);  // Earlier test stopped once; this reader stays open.

  {
    std::lock_guard<std::mutex> lock(readGate);
    releaseRead = true;
  }
  readEvent.notify_all();
  reading.join();
  switching.join();
  g_modbusRead = {};

  assert(finishedSwitch);
  assert(oldResult.transportOk && oldResult.validDistance);
  assert(oldResult.sensorProfile == SensorProfileId::J100_485);
  assert(oldResult.readerEpoch == oldEpoch);
  assert(reader.selectedProfile() == SensorProfileId::CYD3S1612_IIC);
  assert(reader.readerEpoch() != oldResult.readerEpoch);
  assert(Serial1.endCount == 3);
  assert(g_cydBeginCount == 2);
}
}  // namespace

void Cyd3s1612I2cReader::begin(TwoWire&) { ++g_cydBeginCount; }
Cyd3s1612I2cReadResult Cyd3s1612I2cReader::read() {
  ++g_cydReadCount;
  return g_cydRead;
}

int main() {
  testProfileSwitchAndFreshSampleIdentity();
  testSwitchWaitsForInFlightRead();
  std::puts("laser reader profile switch: passed");
}
