#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include "config/GlobalConfig.h"

class WaveModule {
public:
  void begin();
  void setEnable(bool en);
  void setFreq(float hz);      // 0..50
  void setIntensity(int inten);// 0..120
  void setWave(float hz, int inten, bool en);

private:
  static void taskThunk(void* arg);
  void audioTask();

  void initLut();
  void initI2S();

private:
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  volatile uint32_t target_phase_inc = 0;
  volatile int      target_intensity = 0;
  volatile bool     output_enable    = false;
  volatile float    display_freq     = 0.0f;

  static const int16_t MAX_AMPLITUDE = 8826;
  int16_t sine_lut[256]{};
};