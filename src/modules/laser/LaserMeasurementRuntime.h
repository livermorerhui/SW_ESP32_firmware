#pragma once

#include "LaserMeasurementReader.h"
#include "MeasurementAvailabilityProbePolicy.h"
#include "MeasurementHealthStateMachine.h"
#include "MeasurementPlane.h"

// Owns the runtime objects that turn a configured sensor into a measurement
// stream. LaserModule coordinates this boundary but does not own the reader,
// probe policy, health state machine, or published measurement plane directly.
class LaserMeasurementRuntime {
public:
  LaserMeasurementReader& reader() { return reader_; }
  const LaserMeasurementReader& reader() const { return reader_; }

  MeasurementAvailabilityProbePolicy& probePolicy() { return probePolicy_; }
  const MeasurementAvailabilityProbePolicy& probePolicy() const {
    return probePolicy_;
  }

  MeasurementHealthStateMachine& health() { return health_; }
  const MeasurementHealthStateMachine& health() const { return health_; }

  MeasurementPlane& plane() { return plane_; }
  const MeasurementPlane& plane() const { return plane_; }

private:
  LaserMeasurementReader reader_{};
  MeasurementAvailabilityProbePolicy probePolicy_{};
  MeasurementHealthStateMachine health_{};
  MeasurementPlane plane_{};
};
