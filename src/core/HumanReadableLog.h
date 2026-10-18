#pragma once

#include "FirmwareDiagnostics.h"

// UART/USB 串口的人话日志。只提交固定诊断事件，不参与 BLE 协议、状态机或控制逻辑。
namespace HumanReadableLog {

inline void boot(const char* firmware, int protocolVersion, const char* resetReason) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::Boot, firmware ? firmware : "未知", resetReason ? resetReason : "未知", 0, 0, protocolVersion);
}

inline void ready() {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::Ready);
}

inline void bleWaiting(const char* reason) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::BleWaiting, reason ? reason : "启动");
}

inline void bleConnected() {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::BleConnected);
}

inline void bleDisconnected(const char* reason) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::BleDisconnected, reason ? reason : "未知");
}

inline void command(const char* name) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::Command, name ? name : "未知指令");
}

inline void waveParameters(float frequencyHz, int intensity, const char* phase) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::WaveParameters, phase ? phase : "参数已应用", nullptr, frequencyHz, 0, intensity);
}

inline void waveStarted(float frequencyHz, int intensity) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::WaveStarted, nullptr, nullptr, frequencyHz, 0, intensity);
}

inline void waveStopped(const char* reason) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::WaveStopped, reason ? reason : "未知");
}

inline void cueRequested(const char* cue, const char* result) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::CueRequested, cue ? cue : "未知音效", result ? result : "未知结果");
}

inline void cueStarted(const char* cue) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::CueStarted, cue ? cue : "未知音效");
}

inline void cueCompleted(const char* cue) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::CueCompleted, cue ? cue : "未知音效");
}

inline void cueFailed(const char* cue) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::CueFailed, cue ? cue : "未知音效");
}

inline void bleDisconnectQueueRecovered() {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::BleQueueRecovered);
}

inline void bleLifecycleQueueFailed(bool disconnect) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::BleQueueFailed, nullptr, nullptr, 0, 0, disconnect ? 1 : 0);
}

inline void stateChanged(const char* previous, const char* current) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::StateChanged, previous ? previous : "未知", current ? current : "未知");
}

inline void startDecision(bool allowed, const char* reason) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::StartDecision, reason, nullptr, 0, 0, allowed ? 1 : 0);
}

inline void safety(const char* reason, const char* action) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::Safety, reason ? reason : "未知", action ? action : "记录并继续观察");
}

inline void measurement(const char* message) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::Measurement, message ? message : "测量状态变化");
}

inline void baseline(const char* message) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::Baseline, message ? message : "基线状态变化");
}

inline void baselineEstablished(float weightKg, float distance) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::BaselineEstablished, nullptr, nullptr, weightKg, distance);
}

inline void baselineReady(float weightKg, float distance) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::BaselineReady, nullptr, nullptr, weightKg, distance);
}

inline void measurementFailure() {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::MeasurementFailure);
}

inline void error(const char* message) {
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::Error, message ? message : "设备发生错误");
}

}  // namespace HumanReadableLog
