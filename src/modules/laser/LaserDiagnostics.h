#pragma once

#include <Arduino.h>

#include "core/Types.h"
#include "modules/laser/MeasurementAvailabilityProbePolicy.h"

// LaserDiagnostics only formats serial evidence for LaserModule.
// It does not own measurement IO, health state, BLE events, or safety actions.
namespace LaserDiagnostics {

const char* measurementProbeStateName(MeasurementProbeState state);

void logMeasurementProbeDecision(
    const MeasurementProbeDecision& decision,
    TopState topState);

void logMeasurementProbeObservation(
    const MeasurementProbeObservation& observation,
    TopState topState);

void logDistanceValid(uint16_t rawRegister, int16_t signedRaw, float scaledDistance);

void logDistanceInvalid(
    uint16_t rawRegister,
    int16_t signedRaw,
    float scaledDistance,
    bool sentinel,
    const char* reason);

}  // namespace LaserDiagnostics
