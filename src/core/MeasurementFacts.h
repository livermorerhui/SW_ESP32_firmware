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
  virtual bool laserAvailable() const = 0;
  virtual MeasurementHealthState measurementHealth() const = 0;
  virtual bool measurementFaultConfirmed() const = 0;
  virtual bool startConfigurationComplete() const = 0;
  virtual bool protectedStartPolicyAvailable() const = 0;
};
