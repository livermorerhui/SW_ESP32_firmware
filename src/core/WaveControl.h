#pragma once

#include "WaveObservation.h"

class WaveControl : public WaveObservation {
public:
  ~WaveControl() override = default;
  virtual void start() = 0;
  virtual void stopSoft() = 0;
};
