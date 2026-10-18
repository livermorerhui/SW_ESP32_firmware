#pragma once
#include "FreeRTOS.h"
extern bool testTaskFailure;
extern int testTaskCreations;
inline void* xTaskCreateStaticPinnedToCore(void (*)(void*), const char*, uint32_t, void*, unsigned,
                                          StackType_t*, StaticTask_t* task, int) {
  ++testTaskCreations;
  return testTaskFailure ? nullptr : task;
}
inline void vTaskDelay(unsigned) {}
