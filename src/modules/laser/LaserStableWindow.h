#pragma once

#include <Arduino.h>

#include "config/GlobalConfig.h"

struct LaserWindowStats {
  float mean = NAN;
  float stddev = NAN;
  float range = NAN;
};

struct LaserStableWindowMetrics {
  bool valid = false;
  float mean = NAN;
  float stddev = NAN;
  float range = NAN;
  float drift = NAN;
};

class LaserStableWindow {
public:
  static LaserWindowStats computeStats(
      const float* values,
      int head,
      int count,
      int capacity,
      int startOffset,
      int sampleCount);

  static LaserStableWindowMetrics computeMetrics(
      const float* values,
      int head,
      int count,
      int capacity,
      int sampleCount);

  static float computeTrimmedMean(
      const float* values,
      int head,
      int count,
      int capacity,
      int sampleCount,
      int trimCount);
};
