#pragma once

#include <Arduino.h>

// USB 串口的人话日志。这里只负责解释运行事实，不参与 BLE 协议、状态机或控制逻辑。
namespace HumanReadableLog {

inline void boot(const char* firmware, int protocolVersion, const char* resetReason) {
  Serial.printf("[设备启动] 固件已启动：版本=%s，协议版本=%d，重启原因=%s\n",
                firmware ? firmware : "未知",
                protocolVersion,
                resetReason ? resetReason : "未知");
}

inline void ready() {
  Serial.println("[设备状态] 设备已准备，可以连接 APP");
}

inline void bleWaiting(const char* reason) {
  Serial.printf("[蓝牙] 正在等待 APP 连接：原因=%s\n", reason ? reason : "启动");
}

inline void bleConnected() {
  Serial.println("[蓝牙] APP 已连接");
}

inline void bleDisconnected(const char* reason) {
  Serial.printf("[蓝牙] APP 已断开：原因=%s\n", reason ? reason : "未知");
}

inline void command(const char* name) {
  Serial.printf("[控制指令] 收到 APP 指令：%s\n", name ? name : "未知指令");
}

inline void waveParameters(float frequencyHz, int intensity, const char* phase) {
  Serial.printf("[波形参数] %s：频率=%.2f Hz，最终强度=%d/120\n",
                phase ? phase : "参数已应用",
                frequencyHz,
                intensity);
}

inline void waveStarted(float frequencyHz, int intensity) {
  Serial.printf("[波形输出] 已开始：频率=%.2f Hz，强度=%d/120\n", frequencyHz, intensity);
}

inline void waveStopped(const char* reason) {
  Serial.printf("[波形输出] 已停止：原因=%s\n", reason ? reason : "未知");
}

inline void cueRequested(const char* cue, const char* result) {
  Serial.printf("[提示音] 请求%s：%s\n",
                cue ? cue : "未知音效",
                result ? result : "未知结果");
}

inline void cueStarted(const char* cue) {
  Serial.printf("[提示音] 已开始播放：%s\n", cue ? cue : "未知音效");
}

inline void cueCompleted(const char* cue) {
  Serial.printf("[提示音] 已播放完成：%s\n", cue ? cue : "未知音效");
}

inline void cueFailed(const char* cue) {
  Serial.printf("[提示音] 播放失败，已停止输出：%s\n", cue ? cue : "未知音效");
}

inline void bleDisconnectQueueRecovered() {
  Serial.println("[连接] 控制队列拥堵，已优先处理设备断开");
}

inline void bleLifecycleQueueFailed(bool disconnect) {
  Serial.printf("[连接] %s事件排队失败，请检查设备状态\n",
                disconnect ? "断开" : "连接");
}

inline void stateChanged(const char* previous, const char* current) {
  Serial.printf("[设备状态] 状态变化：%s → %s\n",
                previous ? previous : "未知",
                current ? current : "未知");
}

inline void startDecision(bool allowed, const char* reason) {
  Serial.printf("[开始检查] %s%s\n",
                allowed ? "已满足开始条件，可以输出" : "暂不能开始",
                (!allowed && reason) ? reason : "");
}

inline void safety(const char* reason, const char* action) {
  Serial.printf("[安全保护] 原因=%s，处理=%s\n",
                reason ? reason : "未知",
                action ? action : "记录并继续观察");
}

inline void measurement(const char* message) {
  Serial.printf("[测量] %s\n", message ? message : "测量状态变化");
}

inline void baseline(const char* message) {
  Serial.printf("[基线] %s\n", message ? message : "基线状态变化");
}

inline void baselineEstablished(float weightKg, float distance) {
  Serial.printf("[基线] 已建立：稳定重量=%.2f kg，距离=%.2f\n", weightKg, distance);
}

inline void baselineReady(float weightKg, float distance) {
  Serial.printf("[基线] 已准备完成：重量=%.2f kg，距离=%.2f\n", weightKg, distance);
}

inline void measurementFailure() {
  Serial.println("[测量] 激光传感器暂时无响应，正在等待恢复");
}

inline void error(const char* message) {
  Serial.printf("[错误] %s\n", message ? message : "设备发生错误");
}

}  // namespace HumanReadableLog
