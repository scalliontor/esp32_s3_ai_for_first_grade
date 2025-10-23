#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"

// ==== OLED CONFIG ====
// #define SCREEN_WIDTH 128
// #define SCREEN_HEIGHT 64
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==== I2S CONFIG ====
#define I2S_SAMPLE_RATE   16000
#define I2S_SAMPLE_BITS   16
#define I2S_READ_LEN      512

// INMP441 pins
#define INMP441_WS   17   // LRCL
#define INMP441_SD   16    // DOUT
#define INMP441_SCK  18   // BCLK

// MAX98357A pins
#define MAX98357A_BCLK 37
#define MAX98357A_LRC 38
#define MAX98357A_DIN 36

// Buffers
int16_t i2s_read_buffer[I2S_READ_LEN];
int16_t i2s_write_buffer[I2S_READ_LEN];

// ==== I2S SETUP ====
void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE, // Tiết kiệm data hơn 44.1khz và đủ để hiểu tiếng
    .bits_per_sample = (i2s_bits_per_sample_t)I2S_SAMPLE_BITS,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = I2S_READ_LEN,
    .use_apll = false
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
}

void i2s_setpin() {
  const i2s_pin_config_t pin_config = {
  .bck_io_num   = MAX98357A_BCLK,  // use DAC BCLK
  .ws_io_num    = MAX98357A_LRC,   // use DAC LRC
  .data_out_num = MAX98357A_DIN,   // DAC data
  .data_in_num  = INMP441_SD       // mic data
  };
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

// ==== OLED INIT ====
// void oled_init() {
//   if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
//     Serial.println(F("SSD1306 allocation failed"));
//     for (;;);
//   }
//   display.clearDisplay();
//   display.setTextSize(1);
//   display.setTextColor(SSD1306_WHITE);
//   display.setCursor(0,0);
//   display.println("Audio Loopback");
//   display.display();
//   delay(1000);
// }

// ==== DRAW WAVEFORM ====
// void drawWaveform(int16_t *samples, size_t len) {
//   display.clearDisplay();
//   for (int i = 0; i < SCREEN_WIDTH && i < len; i++) {
//     int16_t sample = samples[i];
//     int y = map(sample, -32768, 32767, 0, SCREEN_HEIGHT);
//     display.drawPixel(i, y, SSD1306_WHITE);
//   }
//   display.display();
// }

// ==== MAIN ====
void setup() {
  Serial.begin(115200);
  // oled_init();
  i2s_install();
  i2s_setpin();
  i2s_start(I2S_NUM_0);
}

void loop() {
  size_t bytes_read = 0;
  i2s_read(I2S_NUM_0, (void*)i2s_read_buffer, sizeof(i2s_read_buffer), &bytes_read, portMAX_DELAY);

  // Copy input to output (loopback)
  size_t bytes_written = 0;
  i2s_write(I2S_NUM_0, (const char*)i2s_read_buffer, bytes_read, &bytes_written, portMAX_DELAY);

  // Show waveform on OLED
  // int samples = bytes_read / sizeof(int16_t);
  // drawWaveform(i2s_read_buffer, samples);
}