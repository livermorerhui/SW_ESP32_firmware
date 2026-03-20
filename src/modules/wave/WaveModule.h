#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include "config/GlobalConfig.h"

class WaveModule {
public:
  // Initialize LUT + I2S + worker task.
  void begin();

  // Unified parameter interface for protocol / state machine caller.
  void setParams(float hz, int inten);

  // Runtime control should be driven by SystemStateMachine only.
  void start();
  void stopSoft();
  bool isRunning() const;

  // Compatibility wrappers used by legacy command path.
  void setFreq(float hz);        // 0..50
  void setIntensity(int inten);  // 0..120
  void setEnable(bool en);
  void setWave(float hz, int inten, bool en);

private:
  enum class RampState : uint8_t { IDLE, RAMP_UP, RAMP_DOWN, RAMP_FREQ };

  static void taskThunk(void* arg);
  void audioTask();

  void initLut();
  void initI2S();
  static uint32_t phaseIncrementFromHz(float hz);
  static float phaseIncrementToHz(uint32_t phaseInc);
  static float intensityToAmplitude(int intensity);
  static float clampUnit(float value);

private:
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  volatile uint32_t target_phase_inc = 0;
  volatile int      target_intensity = 0;
  volatile bool     run_requested    = false;
  volatile bool     run_state        = false;
  volatile float    display_freq     = 0.0f;

  static const int16_t MAX_AMPLITUDE = 8826;
  int16_t sine_lut[256]{};
};
