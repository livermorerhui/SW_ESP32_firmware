#include "modules/laser/MeasurementAvailabilityProbePolicy.h"

MeasurementAvailabilityProbePolicy::MeasurementAvailabilityProbePolicy(
    const MeasurementAvailabilityProbeConfig& config)
    : config(config) {}

void MeasurementAvailabilityProbePolicy::reset() {
  currentState = MeasurementProbeState::CLOSED;
  timeoutCount = 0;
  openedAt = 0;
  nextProbeAt = 0;
  lastSkipLogAt = 0;
}

MeasurementProbeDecision MeasurementAvailabilityProbePolicy::beforeRead(
    uint32_t now,
    bool laserInstalled,
    bool measurementFaultConfirmed,
    TopState topState) {
  (void)topState;

  MeasurementProbeDecision decision{};
  decision.state = currentState;
  decision.probeIntervalMs = config.probeIntervalMs;
  decision.consecutiveTimeouts = timeoutCount;

  if (!policyEligible(laserInstalled, measurementFaultConfirmed)) {
    if (currentState != MeasurementProbeState::CLOSED || timeoutCount != 0) {
      reset();
      decision.event = MeasurementProbeEvent::RESET;
      decision.state = currentState;
      decision.consecutiveTimeouts = timeoutCount;
    }
    return decision;
  }

  if (currentState == MeasurementProbeState::CLOSED) {
    return decision;
  }

  if (now >= nextProbeAt) {
    decision.shouldRead = true;
    decision.event = MeasurementProbeEvent::PROBE;
    decision.state = currentState;
    decision.outageMs = openedAt > 0 && now >= openedAt ? (now - openedAt) : 0;
    return decision;
  }

  decision.shouldRead = false;
  decision.nextProbeInMs = remainingMs(now, nextProbeAt);
  decision.outageMs = openedAt > 0 && now >= openedAt ? (now - openedAt) : 0;

  if (lastSkipLogAt == 0 || now - lastSkipLogAt >= config.skipLogIntervalMs) {
    lastSkipLogAt = now;
    decision.event = MeasurementProbeEvent::SKIP;
  }

  return decision;
}

MeasurementProbeObservation MeasurementAvailabilityProbePolicy::afterRead(
    uint32_t now,
    bool laserInstalled,
    bool measurementFaultConfirmed,
    bool transportOk,
    uint8_t resultCode,
    TopState topState) {
  (void)topState;

  MeasurementProbeObservation observation{};
  observation.state = currentState;
  observation.probeIntervalMs = config.probeIntervalMs;
  observation.consecutiveTimeouts = timeoutCount;
  observation.code = resultCode;

  if (!policyEligible(laserInstalled, measurementFaultConfirmed)) {
    if (currentState != MeasurementProbeState::CLOSED || timeoutCount != 0) {
      reset();
      observation.event = MeasurementProbeEvent::RESET;
      observation.state = currentState;
      observation.consecutiveTimeouts = timeoutCount;
    }
    return observation;
  }

  if (transportOk) {
    if (currentState == MeasurementProbeState::OPEN_UNAVAILABLE) {
      observation.event = MeasurementProbeEvent::RECOVERED;
      observation.outageMs = openedAt > 0 && now >= openedAt ? (now - openedAt) : 0;
    }
    reset();
    observation.state = currentState;
    observation.consecutiveTimeouts = timeoutCount;
    return observation;
  }

  if (resultCode != config.timeoutCode) {
    if (currentState == MeasurementProbeState::OPEN_UNAVAILABLE) {
      nextProbeAt = now + config.probeIntervalMs;
      lastSkipLogAt = 0;
      observation.event = MeasurementProbeEvent::STILL_UNAVAILABLE;
      observation.state = currentState;
      observation.outageMs = openedAt > 0 && now >= openedAt ? (now - openedAt) : 0;
      observation.consecutiveTimeouts = timeoutCount;
      return observation;
    } else {
      timeoutCount = 0;
    }
    observation.state = currentState;
    observation.consecutiveTimeouts = timeoutCount;
    return observation;
  }

  if (timeoutCount < 0xFF) {
    timeoutCount++;
  }

  if (currentState == MeasurementProbeState::OPEN_UNAVAILABLE) {
    nextProbeAt = now + config.probeIntervalMs;
    lastSkipLogAt = 0;
    observation.event = MeasurementProbeEvent::STILL_UNAVAILABLE;
    observation.state = currentState;
    observation.outageMs = openedAt > 0 && now >= openedAt ? (now - openedAt) : 0;
    observation.consecutiveTimeouts = timeoutCount;
    return observation;
  }

  if (timeoutCount >= config.openAfterConsecutiveTimeouts) {
    currentState = MeasurementProbeState::OPEN_UNAVAILABLE;
    openedAt = now;
    nextProbeAt = now + config.probeIntervalMs;
    lastSkipLogAt = 0;
    observation.event = MeasurementProbeEvent::OPEN;
    observation.state = currentState;
    observation.outageMs = 0;
    observation.consecutiveTimeouts = timeoutCount;
    return observation;
  }

  observation.state = currentState;
  observation.consecutiveTimeouts = timeoutCount;
  return observation;
}

bool MeasurementAvailabilityProbePolicy::policyEligible(
    bool laserInstalled,
    bool measurementFaultConfirmed) const {
  return laserInstalled && measurementFaultConfirmed;
}

uint32_t MeasurementAvailabilityProbePolicy::remainingMs(uint32_t now, uint32_t target) const {
  return now < target ? (target - now) : 0;
}
