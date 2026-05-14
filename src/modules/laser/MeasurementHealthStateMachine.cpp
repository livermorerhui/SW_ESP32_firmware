#include "modules/laser/MeasurementHealthStateMachine.h"

MeasurementHealthStateMachine::MeasurementHealthStateMachine(
    const MeasurementHealthConfig& config)
    : config(config) {}

MeasurementHealthTransition MeasurementHealthStateMachine::reset(
    uint32_t now,
    bool laserInstalled) {
  const MeasurementHealthState previous = currentState;
  currentState = laserInstalled
      ? MeasurementHealthState::BOOTING
      : MeasurementHealthState::FAULT;
  startedAt = now;
  lastReadyAt = 0;
  successSampleCount = 0;
  failureSampleCount = 0;
  hasEverReady = false;

  MeasurementHealthTransition transition{};
  transition.previous = previous;
  transition.next = currentState;
  transition.changed = previous != currentState;
  return transition;
}

MeasurementHealthTransition MeasurementHealthStateMachine::observe(
    uint32_t now,
    bool laserInstalled,
    bool transportOk,
    bool validDistance) {
  const MeasurementHealthState previous = currentState;

  if (!laserInstalled) {
    currentState = MeasurementHealthState::FAULT;
    startedAt = now;
    successSampleCount = 0;
    failureSampleCount = 0;
    hasEverReady = false;

    MeasurementHealthTransition transition{};
    transition.previous = previous;
    transition.next = currentState;
    transition.changed = previous != currentState;
    return transition;
  }

  if (startedAt == 0) {
    startedAt = now;
  }

  const bool sampleReady = transportOk && validDistance;
  if (sampleReady) {
    successSampleCount = incrementSaturated(successSampleCount);
    failureSampleCount = 0;
    if (successSampleCount >= config.readySuccessSamples) {
      currentState = MeasurementHealthState::READY;
      hasEverReady = true;
      lastReadyAt = now;
    } else if (!hasEverReady) {
      currentState = MeasurementHealthState::PROBING;
    }
  } else {
    successSampleCount = 0;
    failureSampleCount = incrementSaturated(failureSampleCount);

    const uint32_t startupAgeMs = now >= startedAt ? (now - startedAt) : 0;
    const uint32_t sinceReadyMs =
        lastReadyAt > 0 && now >= lastReadyAt ? (now - lastReadyAt) : UINT32_MAX;
    const bool runtimeFaultPath = hasEverReady;
    const uint8_t failureThreshold = runtimeFaultPath
        ? config.runtimeFaultFailureSamples
        : config.startupFaultFailureSamples;
    const uint32_t faultGraceMs = runtimeFaultPath
        ? config.runtimeFaultGraceMs
        : config.transientGraceMs;
    const bool failureThresholdReached = failureSampleCount >= failureThreshold;

    if (!hasEverReady) {
      currentState = startupAgeMs < config.startupGraceMs
          ? MeasurementHealthState::PROBING
          : MeasurementHealthState::FAULT;
    } else if (failureThresholdReached && sinceReadyMs >= faultGraceMs) {
      currentState = MeasurementHealthState::FAULT;
    } else {
      currentState = MeasurementHealthState::TRANSIENT_UNAVAILABLE;
    }
  }

  MeasurementHealthTransition transition{};
  transition.previous = previous;
  transition.next = currentState;
  transition.changed = previous != currentState;
  return transition;
}

uint8_t MeasurementHealthStateMachine::incrementSaturated(uint8_t value) {
  return value < 0xFF ? static_cast<uint8_t>(value + 1) : value;
}
