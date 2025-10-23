#include <driver/i2s.h>
#include <math.h>

#define I2S_NUM         I2S_NUM_0
#define I2S_SAMPLE_RATE 44100
#define I2S_BCK_IO      37
#define I2S_WS_IO       38
#define I2S_DO_IO       36

#define PI 3.14159265
#define TONE_FREQ 440 // Frequency in Hz (A4 note)
#define BUFFER_SIZE 512

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_IO,
    .ws_io_num = I2S_WS_IO,
    .data_out_num = I2S_DO_IO,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
}

void loopTone() {
  int16_t samples[BUFFER_SIZE];
  static float phase = 0.0;
  float phase_increment = 2.0 * PI * TONE_FREQ / I2S_SAMPLE_RATE;

  for (int i = 0; i < BUFFER_SIZE; i += 2) {
    int16_t sample = (int16_t)(sin(phase) * 32767);
    samples[i] = sample;     // Left channel
    samples[i + 1] = sample; // Right channel
    phase += phase_increment;
    if (phase >= 2.0 * PI) phase -= 2.0 * PI;
  }

  size_t bytes_written;
  i2s_write(I2S_NUM, samples, sizeof(samples), &bytes_written, portMAX_DELAY);
}

void setup() {
  Serial.begin(115200);
  setupI2S();
}

void loop() {
  loopTone();
}