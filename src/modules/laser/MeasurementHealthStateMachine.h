#pragma once

#include <Arduino.h>

#include "config/GlobalConfig.h"
#include "core/Types.h"

struct MeasurementHealthConfig {
  uint32_t startupGraceMs = LASER_STARTUP_GRACE_MS;
  uint32_t transientGraceMs = LASER_HEALTH_TRANSIENT_GRACE_MS;
  uint32_t runtimeFaultGraceMs = LASER_HEALTH_RUNTIME_FAULT_GRACE_MS;
  uint8_t readySuccessSamples = LASER_HEALTH_READY_SUCCESS_SAMPLES;
  uint8_t startupFaultFailureSamples = LASER_HEALTH_FAULT_FAILURE_SAMPLES;
  uint8_t runtimeFaultFailureSamples = LASER_HEALTH_RUNTIME_FAULT_FAILURE_SAMPLES;
};

struct MeasurementHealthTransition {
  MeasurementHealthState previous = MeasurementHealthState::BOOTING;
  MeasurementHealthState next = MeasurementHealthState::BOOTING;
  bool changed = false;
};

class MeasurementHealthStateMachine {
public:
  explicit MeasurementHealthStateMachine(
      const MeasurementHealthConfig& config = MeasurementHealthConfig{});

  MeasurementHealthTransition reset(uint32_t now, bool laserInstalled);
  MeasurementHealthTransition observe(
      uint32_t now,
      bool laserInstalled,
      bool transportOk,
      bool validDistance);

  MeasurementHealthState state() const { return currentState; }
  bool faultConfirmed() const { return currentState == MeasurementHealthState::FAULT; }
  bool startupResolved() const {
    return currentState == MeasurementHealthState::READY ||
        currentState == MeasurementHealthState::FAULT;
  }
  bool everReady() const { return hasEverReady; }
  uint8_t successSamples() const { return successSampleCount; }
  uint8_t failureSamples() const { return failureSampleCount; }
  uint32_t startedAtMs() const { return startedAt; }
  uint32_t lastReadyAtMs() const { return lastReadyAt; }

private:
  static uint8_t incrementSaturated(uint8_t value);

  MeasurementHealthConfig config{};
  MeasurementHealthState currentState = MeasurementHealthState::BOOTING;
  uint32_t startedAt = 0;
  uint32_t lastReadyAt = 0;
  uint8_t successSampleCount = 0;
  uint8_t failureSampleCount = 0;
  bool hasEverReady = false;
};
