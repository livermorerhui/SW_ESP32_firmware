#pragma once

#include "DeviceConfig.h"
#include "Types.h"

class MeasurementFacts {
public:
  virtual ~MeasurementFacts() = default;
  virtual bool isUserPresent() const = 0;
  virtual bool baselineReady() const = 0;
  virtual float stableWeightKg() const = 0;
  virtual PlatformModel platformModel() const = 0;
  virtual bool laserInstalled() const = 0;
  // A Plus device may be validly configured before a physical laser profile
  // is selected. Keep this separate from laserInstalled(), whose compatibility
  // meaning is "Plus hardware can host a laser".
  virtual bool laserProfileConfigured() const = 0;
  virtual bool laserAvailable() const = 0;
  virtual MeasurementHealthState measurementHealth() const = 0;
  virtual bool measurementFaultConfirmed() const = 0;
  // True once the reader has either resolved startup or is intentionally not
  // participating in measurement (Base / Plus without a selected profile).
  virtual bool measurementStartupResolved() const = 0;
  // Persisted model/profile identity is valid and no configuration transition
  // or storage error is active. This is separate from D_e/baseline readiness.
  virtual bool configurationValid() const = 0;
  virtual bool startConfigurationComplete() const = 0;
  virtual bool protectedStartPolicyAvailable() const = 0;
};
