#pragma once

#include <atomic>
#include <mutex>

using SemaphoreHandle_t = std::mutex*;
constexpr int portMAX_DELAY = -1;
extern std::atomic<int> g_fakeMutexTakeAttempts;

inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
inline bool xSemaphoreTake(SemaphoreHandle_t mutex, int) {
  ++g_fakeMutexTakeAttempts;
  mutex->lock();
  return true;
}
inline bool xSemaphoreGive(SemaphoreHandle_t mutex) {
  mutex->unlock();
  return true;
}
