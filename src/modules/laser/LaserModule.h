#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include <Preferences.h>
#include "config/GlobalConfig.h"
#include "core/EventBus.h"
#include "core/SystemStateMachine.h"

class LaserModule {
public:
  void begin(EventBus* eb, SystemStateMachine* fsm);
  void startTask();

  // Command hooks（由统一命令处理器调用）
  void triggerZero();
  void setParams(float zero, float factor);
  void getParams(float &zero, float &factor) const;

private:
  static void taskThunk(void* arg);
  void taskLoop();

  float getMean() const;
  float getStdDev() const;

private:
  EventBus* bus = nullptr;
  SystemStateMachine* sm = nullptr;

  ModbusMaster node;
  Preferences preferences;

  // 与你原固件保持一致
  float zeroDistance = 0.0f;
  float scaleFactor = 1.0f;

  float weightBuffer[WINDOW_N]{};
  int bufHead = 0, bufCount = 0;
  bool reportedStable = false;

  // 静默日志
  float lastLogDist = -999.0f;

  // flags
  volatile bool needZero = false;
  volatile bool needSendParams = false;

  // for fall detect
  float lastWeight = 0.0f;
  uint32_t lastMs = 0;
};