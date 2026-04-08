#pragma once

#include "DeviceConfig.h"
#include "Types.h"

struct PlatformSnapshot {
  TopState topState = TopState::IDLE;
  bool userPresent = false;
  bool runtimeReady = false;
  bool startReady = false;
  bool baselineReady = false;
  bool waveOutputActive = false;
  FaultCode currentReasonCode = FaultCode::NONE;
  SafetySignalKind currentSafetyEffect = SafetySignalKind::NONE;
  float stableWeightKg = 0.0f;
  float currentFrequencyHz = 0.0f;
  int currentIntensity = 0;
  PlatformModel platformModel = PlatformModel::PLUS;
  bool laserInstalled = true;
  bool laserAvailable = false;
  bool protectionDegraded = true;
  bool degradedStartAvailable = false;
  bool degradedStartEnabled = false;
};
