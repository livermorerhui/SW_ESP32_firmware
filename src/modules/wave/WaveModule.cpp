#include "WaveModule.h"
#include <math.h>
#include "esp_task_wdt.h"

static void wdtInitIfNeeded() {
#if ENABLE_WDT
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
}

void WaveModule::initLut() {
  for (int i = 0; i < 256; i++) {
    sine_lut[i] = (int16_t)(sin(i * 2.0 * PI / 256.0) * MAX_AMPLITUDE);
  }
}

void WaveModule::initI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRCK_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

void WaveModule::begin() {
  initLut();
  initI2S();
  wdtInitIfNeeded();

  // default STOP
  portENTER_CRITICAL(&mux);
  target_phase_inc = 0;
  target_intensity = 0;
  output_enable = false;
  display_freq = 0.0f;
  portEXIT_CRITICAL(&mux);

  xTaskCreatePinnedToCore(taskThunk, "I2S_Audio", 4096, this, 4, NULL, 1);
  Serial.println("[OK] WaveModule started");
}

void WaveModule::setEnable(bool en) {
  portENTER_CRITICAL(&mux);
  output_enable = en;
  portEXIT_CRITICAL(&mux);
}

void WaveModule::setFreq(float new_freq) {
  if (new_freq < 0) new_freq = 0;
  if (new_freq > 50.0f) new_freq = 50.0f;

  uint32_t new_inc = (uint32_t)((new_freq * 4294967296.0) / SAMPLE_RATE);

  portENTER_CRITICAL(&mux);
  target_phase_inc = new_inc;
  display_freq = new_freq;
  portEXIT_CRITICAL(&mux);
}

void WaveModule::setIntensity(int new_intensity) {
  if (new_intensity < 0) new_intensity = 0;
  if (new_intensity > 120) new_intensity = 120;

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

  int32_t audio_buffer[BUFFER_LEN * 2];
  size_t bytes_written = 0;

  uint32_t phase_acc = 0;
  uint32_t inc_smoothed = 0;
  int intensity_smoothed = 0;

  const int INT_STEP_PER_BUF = 2;
  const uint32_t INC_STEP_PER_BUF = 100000;

  while (true) {
#if ENABLE_WDT
    esp_task_wdt_reset();
#endif

    uint32_t inc_target;
    int intensity_target;
    bool en;

    portENTER_CRITICAL(&mux);
    inc_target = target_phase_inc;
    intensity_target = target_intensity;
    en = output_enable;
    portEXIT_CRITICAL(&mux);

    if (!en) {
      intensity_target = 0;
      inc_target = 0;
    }

    if (intensity_smoothed < intensity_target) {
      intensity_smoothed = min(intensity_smoothed + INT_STEP_PER_BUF, intensity_target);
    } else if (intensity_smoothed > intensity_target) {
      intensity_smoothed = max(intensity_smoothed - INT_STEP_PER_BUF, intensity_target);
    }

    int32_t diff = (int32_t)inc_target - (int32_t)inc_smoothed;
    if (diff > (int32_t)INC_STEP_PER_BUF) diff = (int32_t)INC_STEP_PER_BUF;
    if (diff < -(int32_t)INC_STEP_PER_BUF) diff = -(int32_t)INC_STEP_PER_BUF;
    inc_smoothed = (uint32_t)((int32_t)inc_smoothed + diff);

    if (intensity_smoothed == 0 || inc_smoothed == 0) {
      memset(audio_buffer, 0, sizeof(audio_buffer));
    } else {
      for (int i = 0; i < BUFFER_LEN; i++) {
        phase_acc += inc_smoothed;
        uint8_t idx = (uint8_t)(phase_acc >> 24);

        int16_t base_val = sine_lut[idx];
        int32_t scaled = (int32_t)base_val * (int32_t)intensity_smoothed;
        scaled /= 120;
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;

        int32_t out32 = ((int32_t)((int16_t)scaled)) << 16;
        audio_buffer[i * 2]     = out32;
        audio_buffer[i * 2 + 1] = out32;
      }
    }

    i2s_write(I2S_PORT, audio_buffer, sizeof(audio_buffer), &bytes_written, portMAX_DELAY);
  }
}