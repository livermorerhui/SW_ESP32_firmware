#pragma once

class WaveObservation {
public:
  virtual ~WaveObservation() = default;
  virtual bool isOutputActive() const = 0;
  virtual void getSummaryParams(float& hz, int& intensity, float& intensityNormalized) = 0;
};
