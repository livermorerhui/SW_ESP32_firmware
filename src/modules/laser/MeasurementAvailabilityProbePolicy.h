#pragma once

#include <Arduino.h>

#include "core/Types.h"

enum class MeasurementProbeState : uint8_t {
  CLOSED,
  OPEN_UNAVAILABLE
};

enum class MeasurementProbeEvent : uint8_t {
  NONE,
  OPEN,
  SKIP,
  PROBE,
  STILL_UNAVAILABLE,
  RECOVERED,
  RESET
};

struct MeasurementAvailabilityProbeConfig {
  uint8_t timeoutCode = 0xE2;
  uint8_t openAfterConsecutiveTimeouts = 2;
  uint32_t probeIntervalMs = 5000UL;
  uint32_t skipLogIntervalMs = 5000UL;
};

struct MeasurementProbeDecision {
  bool shouldRead = true;
  MeasurementProbeEvent event = MeasurementProbeEvent::NONE;
  MeasurementProbeState state = MeasurementProbeState::CLOSED;
  uint32_t nextProbeInMs = 0;
  uint32_t probeIntervalMs = 0;
  uint32_t outageMs = 0;
  uint8_t consecutiveTimeouts = 0;
};

struct MeasurementProbeObservation {
  MeasurementProbeEvent event = MeasurementProbeEvent::NONE;
  MeasurementProbeState state = MeasurementProbeState::CLOSED;
  uint32_t probeIntervalMs = 0;
  uint32_t outageMs = 0;
  uint8_t consecutiveTimeouts = 0;
  uint8_t code = 0;
};

// Pure policy for known-unavailable measurement chains.
// It does not own Modbus IO, BLE frames, safety actions, or serial logging.
class MeasurementAvailabilityProbePolicy {
public:
  explicit MeasurementAvailabilityProbePolicy(
      const MeasurementAvailabilityProbeConfig& config = MeasurementAvailabilityProbeConfig{});

  void reset();

  MeasurementProbeDecision beforeRead(
      uint32_t now,
      bool laserInstalled,
      bool measurementFaultConfirmed,
      TopState topState);

  MeasurementProbeObservation afterRead(
      uint32_t now,
      bool laserInstalled,
      bool measurementFaultConfirmed,
      bool transportOk,
      uint8_t resultCode,
      TopState topState);

  MeasurementProbeState state() const { return currentState; }
  uint32_t nextProbeAtMs() const { return nextProbeAt; }
  uint8_t consecutiveTimeouts() const { return timeoutCount; }

private:
  bool policyEligible(bool laserInstalled, bool measurementFaultConfirmed) const;
  uint32_t remainingMs(uint32_t now, uint32_t target) const;

  MeasurementAvailabilityProbeConfig config{};
  MeasurementProbeState currentState = MeasurementProbeState::CLOSED;
  uint8_t timeoutCount = 0;
  uint32_t openedAt = 0;
  uint32_t nextProbeAt = 0;
  uint32_t lastSkipLogAt = 0;
};
