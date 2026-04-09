#include "WaveModule.h"
#include "core/LogMarkers.h"
#include <math.h>
#include "esp_task_wdt.h"

static void wdtInitIfNeeded() {
#if ENABLE_WDT
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
}

namespace {

constexpr double kPhaseAccumulatorScale = 4294967296.0;
constexpr float kAmplitudeEpsilon = 0.0005f;

const char* waveCommFormatName() {
  return "I2S";
}

float amplitudeStepPerTick(bool rampUp) {
#if DIAG_DISABLE_WAVE_RAMP
  return 1.0f;
#else
  const uint32_t rampTimeMs = rampUp ? RAMP_START_TIME_MS : RAMP_STOP_TIME_MS;
  if (rampTimeMs == 0 || RAMP_UPDATE_INTERVAL_MS == 0) {
    return 1.0f;
  }

  const float step =
      static_cast<float>(RAMP_UPDATE_INTERVAL_MS) / static_cast<float>(rampTimeMs);
  return (step > 1.0f) ? 1.0f : step;
#endif
}

uint32_t frequencyStepPerTick() {
#if DIAG_DISABLE_WAVE_RAMP
  return UINT32_MAX;
#else
  const uint32_t step =
      static_cast<uint32_t>((RAMP_FREQ_STEP_HZ * kPhaseAccumulatorScale) / SAMPLE_RATE);
  return (step == 0) ? 1U : step;
#endif
}

}  // namespace

void WaveModule::initLut() {
  for (int i = 0; i < 256; i++) {
    sine_lut[i] = (int16_t)(sin(i * 2.0 * PI / 256.0) * MAX_AMPLITUDE);
  }
}

void WaveModule::initI2S() {
  i2s_config_t i2s_config = {};
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_config.sample_rate = SAMPLE_RATE;
  i2s_config.bits_per_sample = (i2s_bits_per_sample_t)WAVE_I2S_SAMPLE_BITS;
  i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 4;
  i2s_config.dma_buf_len = BUFFER_LEN;
  i2s_config.use_apll = false;
  i2s_config.tx_desc_auto_clear = true;

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num = I2S_BCLK_PIN;
  pin_config.ws_io_num = I2S_LRCK_PIN;
  pin_config.data_out_num = I2S_DOUT_PIN;
  pin_config.data_in_num = I2S_PIN_NO_CHANGE;
#ifdef ESP_IDF_VERSION_MAJOR
  pin_config.mck_io_num = I2S_PIN_NO_CHANGE;
#endif

  const esp_err_t installErr = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  const esp_err_t pinErr = i2s_set_pin(I2S_PORT, &pin_config);
  const esp_err_t clkErr = i2s_set_clk(I2S_PORT,
                                       SAMPLE_RATE,
                                       (i2s_bits_per_sample_t)WAVE_I2S_SAMPLE_BITS,
                                       I2S_CHANNEL_STEREO);
  const esp_err_t zeroErr = i2s_zero_dma_buffer(I2S_PORT);

  Serial.printf("[I2S] sample_rate=%d bits=%d stereo=LR format=%s\n",
                SAMPLE_RATE,
                WAVE_I2S_SAMPLE_BITS,
                waveCommFormatName());
  Serial.printf(
      "[LAYER:OUTPUT_DRIVER] init driver_install=%d set_pin=%d set_clk=%d zero_dma=%d port=%d bclk=%d lrck=%d dout=%d\n",
      static_cast<int>(installErr),
      static_cast<int>(pinErr),
      static_cast<int>(clkErr),
      static_cast<int>(zeroErr),
      static_cast<int>(I2S_PORT),
      I2S_BCLK_PIN,
      I2S_LRCK_PIN,
      I2S_DOUT_PIN);
  Serial.printf(
      "[LAYER:MEASUREMENT_POINT] output_interface=I2S digital_point=GPIO%d expected_signal=serial_digital_stream analog_sine_requires_downstream_dac_or_amp=1\n",
      I2S_DOUT_PIN);
}

uint32_t WaveModule::phaseIncrementFromHz(float hz) {
  return static_cast<uint32_t>((hz * kPhaseAccumulatorScale) / SAMPLE_RATE);
}

float WaveModule::phaseIncrementToHz(uint32_t phaseInc) {
  return static_cast<float>((static_cast<double>(phaseInc) * SAMPLE_RATE) /
                            kPhaseAccumulatorScale);
}

float WaveModule::intensityToAmplitude(int intensity) {
  if (intensity < 0) intensity = 0;
  if (intensity > WAVE_INTENSITY_MAX) intensity = WAVE_INTENSITY_MAX;
  return static_cast<float>(intensity) / static_cast<float>(WAVE_INTENSITY_MAX);
}

float WaveModule::clampUnit(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

void WaveModule::begin(EventBus* eventBus) {
  event_bus = eventBus;
  initLut();
  initI2S();
  wdtInitIfNeeded();

#if DIAG_DISABLE_WAVE_RAMP
  Serial.println("[DIAG] Wave startup ramp disabled");
#endif

  // Default to STOP state. Phase accumulator lives in task context and is
  // intentionally not reset on start/stop to keep waveform phase continuous.
  portENTER_CRITICAL(&mux);
  target_phase_inc = 0;
  target_intensity = 0;
  run_requested = false;
  run_state = false;
  display_freq = 0.0f;
  portEXIT_CRITICAL(&mux);

  i2s_stop(I2S_PORT);
  xTaskCreatePinnedToCore(taskThunk, "I2S_Audio", 4096, this, 4, NULL, 1);
  Serial.println("[OK] WaveModule started");
}

void WaveModule::setParams(float hz, int inten) {
  setFreq(hz);
  setIntensity(inten);

  DebugState debug{};
  getDebugState(debug);
  Serial.printf(
      "[LAYER:ACTUATOR] owner=WaveModule::setParams freq_hz=%.2f intensity=%d display_freq_hz=%.2f target_phase_inc=%lu target_intensity=%d run_requested=%d run_state=%d\n",
      hz,
      inten,
      debug.displayFreqHz,
      static_cast<unsigned long>(debug.targetPhaseInc),
      debug.targetIntensity,
      debug.runRequested ? 1 : 0,
      debug.runState ? 1 : 0);
}

void WaveModule::start() {
  float hz;
  int intensity;

  portENTER_CRITICAL(&mux);
  hz = display_freq;
  intensity = target_intensity;
  portEXIT_CRITICAL(&mux);

  Serial.printf("%s [WAVE] start freq=%.2f intensity=%d\n", LogMarker::kWave, hz, intensity);
  Serial.printf(
      "[LAYER:ACTUATOR] owner=WaveModule::start action=setEnable(true) freq_hz=%.2f intensity=%d\n",
      hz,
      intensity);
  setEnable(true);
}

void WaveModule::stopSoft() {
  bool wasRequested = false;

  portENTER_CRITICAL(&mux);
  wasRequested = run_requested;
  run_requested = false;
  portEXIT_CRITICAL(&mux);

  if (wasRequested) {
    Serial.printf("%s [WAVE] stop requested\n", LogMarker::kWave);
    Serial.println("[LAYER:ACTUATOR] owner=WaveModule::stopSoft action=setEnable(false)");
  }
}

bool WaveModule::isRunning() const {
  return run_state;
}

bool WaveModule::isOutputActive() const {
  return run_state;
}

void WaveModule::getDebugState(DebugState& out) {
  portENTER_CRITICAL(&mux);
  out.displayFreqHz = display_freq;
  out.targetPhaseInc = target_phase_inc;
  out.targetIntensity = target_intensity;
  out.runRequested = run_requested;
  out.runState = run_state;
  portEXIT_CRITICAL(&mux);
}

void WaveModule::getSummaryParams(float& hz, int& intensity, float& intensityNormalized) {
  portENTER_CRITICAL(&mux);
  hz = display_freq;
  intensity = target_intensity;
  portEXIT_CRITICAL(&mux);

  intensityNormalized = intensityToAmplitude(intensity);
}

void WaveModule::publishWaveOutputEvent(bool active) {
  if (!event_bus) return;

  Event event{};
  event.type = EventType::WAVE_OUTPUT;
  event.waveOutputActive = active;
  event.ts_ms = millis();
  event_bus->publish(event);
}

void WaveModule::setEnable(bool en) {
  bool changed = false;
  portENTER_CRITICAL(&mux);
  changed = (run_requested != en);
  run_requested = en;
  portEXIT_CRITICAL(&mux);

  if (changed) {
    DebugState debug{};
    getDebugState(debug);
    Serial.printf(
        "[LAYER:ACTUATOR] owner=WaveModule::setEnable run_requested=%d display_freq_hz=%.2f target_phase_inc=%lu target_intensity=%d run_state=%d\n",
        debug.runRequested ? 1 : 0,
        debug.displayFreqHz,
        static_cast<unsigned long>(debug.targetPhaseInc),
        debug.targetIntensity,
        debug.runState ? 1 : 0);
  }
}

void WaveModule::setFreq(float new_freq) {
  if (new_freq < 0) new_freq = 0;
  if (new_freq > 50.0f) new_freq = 50.0f;

  uint32_t new_inc = phaseIncrementFromHz(new_freq);

  portENTER_CRITICAL(&mux);
  target_phase_inc = new_inc;
  display_freq = new_freq;
  portEXIT_CRITICAL(&mux);
}

void WaveModule::setIntensity(int new_intensity) {
  if (new_intensity < 0) new_intensity = 0;
  if (new_intensity > WAVE_INTENSITY_MAX) new_intensity = WAVE_INTENSITY_MAX;

  portENTER_CRITICAL(&mux);
  target_intensity = new_intensity;
  portEXIT_CRITICAL(&mux);
}

void WaveModule::setWave(float hz, int inten, bool en) {
  setFreq(hz);
  setIntensity(inten);
  setEnable(en);
}

void WaveModule::taskThunk(void* arg) {
  static_cast<WaveModule*>(arg)->audioTask();
}

void WaveModule::audioTask() {
#if ENABLE_WDT
  esp_task_wdt_add(NULL);
#endif

  int16_t audio_buffer[BUFFER_LEN * 2];
  uint32_t phase_acc = 0;
  uint32_t current_phase_inc = 0;
  float current_amplitude = 0.0f;
  bool i2s_active = false;
  bool last_run_state = false;
  RampState ramp_state = RampState::IDLE;
  RampState last_logged_amp_state = RampState::IDLE;
  bool freq_ramp_logged = false;
  uint32_t last_ramp_update_ms = millis();

  auto setRunState = [&](bool running) {
    if (running == last_run_state) return;
    portENTER_CRITICAL(&mux);
    run_state = running;
    portEXIT_CRITICAL(&mux);
    last_run_state = running;
    publishWaveOutputEvent(running);
  };

  while (true) {
#if ENABLE_WDT
    esp_task_wdt_reset();
#endif

    uint32_t target_phase_inc;
    int target_intensity;
    bool req_run;

    portENTER_CRITICAL(&mux);
    target_phase_inc = this->target_phase_inc;
    target_intensity = this->target_intensity;
    req_run = run_requested;
    portEXIT_CRITICAL(&mux);

    const float target_amplitude =
        req_run ? intensityToAmplitude(target_intensity) : 0.0f;

    uint32_t now = millis();
    uint32_t ramp_ticks = 0;
    if (RAMP_UPDATE_INTERVAL_MS == 0) {
      ramp_ticks = 1;
      last_ramp_update_ms = now;
    } else {
      const uint32_t elapsed_ms = now - last_ramp_update_ms;
      ramp_ticks = elapsed_ms / RAMP_UPDATE_INTERVAL_MS;
      if (ramp_ticks > 0) {
        last_ramp_update_ms += ramp_ticks * RAMP_UPDATE_INTERVAL_MS;
      }
    }

    const bool start_pending = req_run &&
        target_amplitude > kAmplitudeEpsilon &&
        target_phase_inc > 0 &&
        !i2s_active &&
        current_amplitude <= kAmplitudeEpsilon;
    if (start_pending && ramp_ticks == 0) {
      ramp_ticks = 1;
      last_ramp_update_ms = now;
    }

    if (!req_run && current_amplitude <= kAmplitudeEpsilon && !i2s_active) {
      current_phase_inc = target_phase_inc;
      if (freq_ramp_logged) {
        Serial.printf("%s [WAVE] freq ramp complete freq=%.2f\n",
                      LogMarker::kWave,
                      phaseIncrementToHz(current_phase_inc));
        freq_ramp_logged = false;
      }
    }

    const bool amp_ramp_up =
        target_amplitude > (current_amplitude + kAmplitudeEpsilon);
    const bool amp_ramp_down =
        current_amplitude > (target_amplitude + kAmplitudeEpsilon);

    if (amp_ramp_up) {
      if (last_logged_amp_state != RampState::RAMP_UP) {
        Serial.printf("%s [WAVE] ramp start target_amp=%.3f state=RAMP_UP\n",
                      LogMarker::kWave,
                      target_amplitude);
        last_logged_amp_state = RampState::RAMP_UP;
      }
    } else if (amp_ramp_down) {
      if (last_logged_amp_state != RampState::RAMP_DOWN) {
        if (!req_run && target_amplitude <= kAmplitudeEpsilon) {
          Serial.printf("%s [WAVE] ramp stop\n", LogMarker::kWave);
        } else {
          Serial.printf("%s [WAVE] ramp start target_amp=%.3f state=RAMP_DOWN\n",
                        LogMarker::kWave,
                        target_amplitude);
        }
        last_logged_amp_state = RampState::RAMP_DOWN;
      }
    } else if (last_logged_amp_state != RampState::IDLE) {
      Serial.printf("%s [WAVE] ramp complete amp=%.3f\n", LogMarker::kWave, clampUnit(current_amplitude));
      last_logged_amp_state = RampState::IDLE;
    }

    const bool freq_ramp_needed =
        (target_phase_inc != current_phase_inc) && (req_run || i2s_active);
    if (freq_ramp_needed && !freq_ramp_logged) {
      Serial.printf("%s [WAVE] freq ramp start current=%.2f target=%.2f\n",
                    LogMarker::kWave,
                    phaseIncrementToHz(current_phase_inc),
                    phaseIncrementToHz(target_phase_inc));
      freq_ramp_logged = true;
    } else if (!freq_ramp_needed && freq_ramp_logged) {
      Serial.printf("%s [WAVE] freq ramp complete freq=%.2f\n",
                    LogMarker::kWave,
                    phaseIncrementToHz(current_phase_inc));
      freq_ramp_logged = false;
    }

    const float amp_step_up = amplitudeStepPerTick(true);
    const float amp_step_down = amplitudeStepPerTick(false);
    const uint32_t freq_step = frequencyStepPerTick();

    for (uint32_t tick = 0; tick < ramp_ticks; ++tick) {
      if (current_amplitude < target_amplitude) {
        current_amplitude += amp_step_up;
        if (current_amplitude > target_amplitude) {
          current_amplitude = target_amplitude;
        }
      } else if (current_amplitude > target_amplitude) {
        current_amplitude -= amp_step_down;
        if (current_amplitude < target_amplitude) {
          current_amplitude = target_amplitude;
        }
      }

      if (current_phase_inc < target_phase_inc) {
        const uint32_t delta = target_phase_inc - current_phase_inc;
        current_phase_inc += (delta > freq_step) ? freq_step : delta;
      } else if (current_phase_inc > target_phase_inc) {
        const uint32_t delta = current_phase_inc - target_phase_inc;
        current_phase_inc -= (delta > freq_step) ? freq_step : delta;
      }
    }

    const bool amplitude_active = current_amplitude > kAmplitudeEpsilon;
    const bool frequency_active = current_phase_inc > 0;

    if (amp_ramp_down) {
      ramp_state = RampState::RAMP_DOWN;
    } else if (amp_ramp_up) {
      ramp_state = RampState::RAMP_UP;
    } else if (freq_ramp_needed) {
      ramp_state = RampState::RAMP_FREQ;
    } else {
      ramp_state = RampState::IDLE;
    }

    const bool should_emit = amplitude_active && frequency_active;
    const bool should_start_i2s =
        !i2s_active &&
        ((req_run && target_amplitude > kAmplitudeEpsilon && target_phase_inc > 0) ||
         should_emit);

    if (should_start_i2s) {
      const esp_err_t zeroErr = i2s_zero_dma_buffer(I2S_PORT);
      const esp_err_t startErr = i2s_start(I2S_PORT);
      Serial.printf(
          "[LAYER:OUTPUT_DRIVER] action=i2s_start zero_dma=%d start=%d req_run=%d target_phase_inc=%lu target_intensity=%d target_amplitude=%.4f\n",
          static_cast<int>(zeroErr),
          static_cast<int>(startErr),
          req_run ? 1 : 0,
          static_cast<unsigned long>(target_phase_inc),
          target_intensity,
          target_amplitude);
      i2s_active = true;
    }

    if (!i2s_active) {
      setRunState(false);
      vTaskDelay(pdMS_TO_TICKS(RAMP_UPDATE_INTERVAL_MS == 0 ? 1 : RAMP_UPDATE_INTERVAL_MS));
      continue;
    }

    if (!should_emit) {
      memset(audio_buffer, 0, sizeof(audio_buffer));
      size_t bytes_written = 0;
      const esp_err_t writeErr = i2s_write(I2S_PORT,
                                           audio_buffer,
                                           sizeof(audio_buffer),
                                           &bytes_written,
                                           portMAX_DELAY);
      (void)writeErr;
      (void)bytes_written;

      if (!req_run && current_amplitude <= kAmplitudeEpsilon) {
        const esp_err_t zeroErr = i2s_zero_dma_buffer(I2S_PORT);
        const esp_err_t stopErr = i2s_stop(I2S_PORT);
        Serial.printf(
            "[LAYER:OUTPUT_DRIVER] action=i2s_stop zero_dma=%d stop=%d reason=req_run_cleared\n",
            static_cast<int>(zeroErr),
            static_cast<int>(stopErr));
        i2s_active = false;
        current_amplitude = 0.0f;
        setRunState(false);
        ramp_state = RampState::IDLE;
        vTaskDelay(pdMS_TO_TICKS(RAMP_UPDATE_INTERVAL_MS == 0 ? 1 : RAMP_UPDATE_INTERVAL_MS));
        continue;
      }

      setRunState(false);
      continue;
    }

    const int32_t envelope_q15 =
        static_cast<int32_t>(lroundf(clampUnit(current_amplitude) * 32767.0f));
    for (int i = 0; i < BUFFER_LEN; i++) {
      phase_acc += current_phase_inc;
      uint8_t idx = static_cast<uint8_t>(phase_acc >> 24);

      const int16_t base_val = sine_lut[idx];
      int32_t scaled = static_cast<int32_t>(base_val) * envelope_q15;
      scaled /= 32767;
      if (scaled > 32767) scaled = 32767;
      if (scaled < -32768) scaled = -32768;

      const int16_t sample = static_cast<int16_t>(scaled);
      audio_buffer[i * 2]     = sample;
      audio_buffer[i * 2 + 1] = sample;
    }

    size_t bytes_written = 0;
    const esp_err_t writeErr =
        i2s_write(I2S_PORT, audio_buffer, sizeof(audio_buffer), &bytes_written, portMAX_DELAY);
    (void)writeErr;
    (void)bytes_written;
    setRunState(true);

    (void)ramp_state;
  }
}
