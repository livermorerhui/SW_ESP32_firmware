#include "modules/laser/LaserDiagnostics.h"
#include "core/VerboseSerial.h"

#include <math.h>

namespace LaserDiagnostics {

const char* measurementProbeStateName(MeasurementProbeState state) {
  switch (state) {
    case MeasurementProbeState::CLOSED:
      return "closed";
    case MeasurementProbeState::OPEN_UNAVAILABLE:
      return "open";
  }
  return "unknown";
}

void logMeasurementProbeDecision(
    const MeasurementProbeDecision& decision,
    TopState topState) {
  switch (decision.event) {
    case MeasurementProbeEvent::NONE:
    case MeasurementProbeEvent::OPEN:
    case MeasurementProbeEvent::STILL_UNAVAILABLE:
    case MeasurementProbeEvent::RECOVERED:
      return;
    case MeasurementProbeEvent::SKIP:
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT_PROBE] event=skip state=%s next_probe_in_ms=%lu outage_ms=%lu top_state=%s\n",
          measurementProbeStateName(decision.state),
          static_cast<unsigned long>(decision.nextProbeInMs),
          static_cast<unsigned long>(decision.outageMs),
          topStateName(topState));
      return;
    case MeasurementProbeEvent::PROBE:
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT_PROBE] event=probe state=%s top_state=%s outage_ms=%lu\n",
          measurementProbeStateName(decision.state),
          topStateName(topState),
          static_cast<unsigned long>(decision.outageMs));
      return;
    case MeasurementProbeEvent::RESET:
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT_PROBE] event=reset state=%s top_state=%s\n",
          measurementProbeStateName(decision.state),
          topStateName(topState));
      return;
  }
}

void logMeasurementProbeObservation(
    const MeasurementProbeObservation& observation,
    TopState topState) {
  switch (observation.event) {
    case MeasurementProbeEvent::NONE:
    case MeasurementProbeEvent::SKIP:
    case MeasurementProbeEvent::PROBE:
      return;
    case MeasurementProbeEvent::OPEN:
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT_PROBE] event=open reason=timeout code=0x%02X consecutive=%u top_state=%s probe_interval_ms=%lu\n",
          static_cast<unsigned>(observation.code),
          static_cast<unsigned>(observation.consecutiveTimeouts),
          topStateName(topState),
          static_cast<unsigned long>(observation.probeIntervalMs));
      return;
    case MeasurementProbeEvent::STILL_UNAVAILABLE:
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT_PROBE] event=still_unavailable code=0x%02X consecutive=%u top_state=%s outage_ms=%lu next_probe_in_ms=%lu\n",
          static_cast<unsigned>(observation.code),
          static_cast<unsigned>(observation.consecutiveTimeouts),
          topStateName(topState),
          static_cast<unsigned long>(observation.outageMs),
          static_cast<unsigned long>(observation.probeIntervalMs));
      return;
    case MeasurementProbeEvent::RECOVERED:
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT_PROBE] event=recovered code=0x%02X top_state=%s outage_ms=%lu\n",
          static_cast<unsigned>(observation.code),
          topStateName(topState),
          static_cast<unsigned long>(observation.outageMs));
      return;
    case MeasurementProbeEvent::RESET:
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT_PROBE] event=reset state=%s top_state=%s\n",
          measurementProbeStateName(observation.state),
          topStateName(topState));
      return;
  }
}

void logDistanceValid(uint16_t rawRegister, int16_t signedRaw, float scaledDistance) {
  SW_VERBOSE_SERIAL_PRINTF("[LASER] VALID raw_u16=%u raw_i16=%d scaled=%.2f\n",
      static_cast<unsigned int>(rawRegister),
      static_cast<int>(signedRaw),
      scaledDistance);
}

void logDistanceInvalid(
    uint16_t rawRegister,
    int16_t signedRaw,
    float scaledDistance,
    bool sentinel,
    const char* reason) {
  if (isfinite(scaledDistance)) {
    SW_VERBOSE_SERIAL_PRINTF("[LASER] INVALID raw_u16=%u raw_i16=%d scaled=%.2f sentinel=%d reason=%s\n",
        static_cast<unsigned int>(rawRegister),
        static_cast<int>(signedRaw),
        scaledDistance,
        sentinel ? 1 : 0,
        reason ? reason : "UNKNOWN");
  } else {
    SW_VERBOSE_SERIAL_PRINTF("[LASER] INVALID sentinel=%d reason=%s\n",
        sentinel ? 1 : 0,
        reason ? reason : "UNKNOWN");
  }
}

}  // namespace LaserDiagnostics
