#include "modules/laser/LaserStableWindow.h"

#include <math.h>

LaserWindowStats LaserStableWindow::computeStats(
    const float* values,
    int head,
    int count,
    int capacity,
    int startOffset,
    int sampleCount) {
  LaserWindowStats stats{};
  if (!values || count <= 0 || capacity <= 0 || sampleCount <= 0 || startOffset < 0 ||
      startOffset + sampleCount > count) {
    return stats;
  }

  const int oldestIndex = (count == capacity) ? head : 0;
  float sum = 0.0f;
  float minValue = INFINITY;
  float maxValue = -INFINITY;
  for (int i = 0; i < sampleCount; ++i) {
    const int index = (oldestIndex + startOffset + i) % capacity;
    const float value = values[index];
    sum += value;
    if (value < minValue) minValue = value;
    if (value > maxValue) maxValue = value;
  }

  stats.mean = sum / sampleCount;
  float sumSqDiff = 0.0f;
  for (int i = 0; i < sampleCount; ++i) {
    const int index = (oldestIndex + startOffset + i) % capacity;
    const float diff = values[index] - stats.mean;
    sumSqDiff += diff * diff;
  }

  stats.stddev = sqrtf(sumSqDiff / sampleCount);
  stats.range = maxValue - minValue;
  return stats;
}

LaserStableWindowMetrics LaserStableWindow::computeMetrics(
    const float* values,
    int head,
    int count,
    int capacity,
    int sampleCount) {
  LaserStableWindowMetrics metrics{};
  if (!values || sampleCount <= 1 || count < sampleCount) {
    return metrics;
  }

  const int startOffset = count - sampleCount;
  const LaserWindowStats full = computeStats(
      values,
      head,
      count,
      capacity,
      startOffset,
      sampleCount);
  if (!isfinite(full.mean) || !isfinite(full.stddev) || !isfinite(full.range)) {
    return metrics;
  }

  const int firstHalfCount = sampleCount / 2;
  const int secondHalfCount = sampleCount - firstHalfCount;
  if (firstHalfCount <= 0 || secondHalfCount <= 0) {
    return metrics;
  }

  const LaserWindowStats firstHalf = computeStats(
      values,
      head,
      count,
      capacity,
      startOffset,
      firstHalfCount);
  const LaserWindowStats secondHalf = computeStats(
      values,
      head,
      count,
      capacity,
      startOffset + firstHalfCount,
      secondHalfCount);
  if (!isfinite(firstHalf.mean) || !isfinite(secondHalf.mean)) {
    return metrics;
  }

  metrics.valid = true;
  metrics.mean = full.mean;
  metrics.stddev = full.stddev;
  metrics.range = full.range;
  metrics.drift = fabsf(secondHalf.mean - firstHalf.mean);
  return metrics;
}

float LaserStableWindow::computeTrimmedMean(
    const float* values,
    int head,
    int count,
    int capacity,
    int sampleCount,
    int trimCount) {
  if (!values || sampleCount <= 0 || capacity <= 0 || count < sampleCount) {
    return NAN;
  }

  const int startOffset = count - sampleCount;
  const int oldestIndex = (count == capacity) ? head : 0;
  float ordered[WINDOW_N]{};
  for (int i = 0; i < sampleCount; ++i) {
    const int index = (oldestIndex + startOffset + i) % capacity;
    ordered[i] = values[index];
  }

  for (int i = 1; i < sampleCount; ++i) {
    const float key = ordered[i];
    int j = i - 1;
    while (j >= 0 && ordered[j] > key) {
      ordered[j + 1] = ordered[j];
      --j;
    }
    ordered[j + 1] = key;
  }

  int keepStart = trimCount;
  int keepEnd = sampleCount - trimCount;
  if (keepStart >= keepEnd) {
    keepStart = 0;
    keepEnd = sampleCount;
  }

  float sum = 0.0f;
  int kept = 0;
  for (int i = keepStart; i < keepEnd; ++i) {
    sum += ordered[i];
    ++kept;
  }

  return kept > 0 ? (sum / kept) : NAN;
}
