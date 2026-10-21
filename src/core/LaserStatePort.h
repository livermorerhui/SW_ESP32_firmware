#pragma once

#include "SafetyActionContractEvaluator.h"
#include "Types.h"

class LaserStatePort {
public:
  virtual ~LaserStatePort() = default;

  virtual TopState state() const = 0;
  virtual void onUserOff() = 0;
  virtual void setRuntimeReady(bool ready) = 0;
  virtual void setStartReadiness(bool ready, float stableWeightKg) = 0;
  virtual void setSensorHealthy(bool healthy) = 0;
  virtual void onWeightSample(float weightKg) = 0;
  virtual void stopForConfigurationFault() = 0;

  virtual bool fallStopEnabled() const = 0;
  virtual FallStopActionDecision decideFallSuspectedAction() const = 0;
  virtual void applyFallSuspectedAction(
      const FallStopActionDecision& decision) = 0;

  virtual FaultCode activeFault() const = 0;
  virtual SafetySignalKind lastStopSafetyEffect() const = 0;
  virtual const char* lastStopReasonText() const = 0;
  virtual const char* lastStopSourceText() const = 0;
};
