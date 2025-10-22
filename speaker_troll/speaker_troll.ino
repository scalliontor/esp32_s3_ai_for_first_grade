#include <Arduino.h>
#include <SPIFFS.h>
#include <driver/i2s.h>
#include "esp_heap_caps.h"

#define I2S_NUM         I2S_NUM_0
#define I2S_SAMPLE_RATE 44100
#define I2S_BCK_IO      37
#define I2S_WS_IO       38
#define I2S_DO_IO       36

#define WAV_PATH "/bomb.wav"
#define BUF_SAMPLES 512

void setupI2S(uint32_t sample_rate) {
  i2s_driver_uninstall(I2S_NUM);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = sample_rate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = 6,
    .dma_buf_len = BUF_SAMPLES,
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

struct WAVHeader {
  uint32_t chunkID;
  uint32_t chunkSize;
  uint32_t format;
  uint32_t subchunk1ID;
  uint32_t subchunk1Size;
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
};

bool readWavHeader(File &f, WAVHeader &h, uint32_t &dataStart, uint32_t &dataLen) {
  if (f.size() < 44) return false;
  f.seek(0);
  f.read((uint8_t*)&h, sizeof(WAVHeader));

  if (memcmp(&h.chunkID, "RIFF", 4) != 0 || memcmp(&h.format, "WAVE", 4) != 0) {
    Serial.println("Invalid WAV format");
    return false;
  }

  dataStart = 12 + 8 + h.subchunk1Size;
  f.seek(dataStart);
  char id[4];
  uint32_t size;

  while (f.position() + 8 <= f.size()) {
    f.read((uint8_t*)id, 4);
    f.read((uint8_t*)&size, 4);
    if (memcmp(id, "data", 4) == 0) {
      dataLen = size;
      dataStart = f.position();
      return true;
    } else {
      f.seek(f.position() + size);
    }
  }
  return false;
}

void playWav(const char *path) {
  File f = SPIFFS.open(path, "r");
  if (!f) {
    Serial.println("WAV file not found");
    return;
  }

  WAVHeader h;
  uint32_t dataStart = 0, dataLen = 0;
  if (!readWavHeader(f, h, dataStart, dataLen)) {
    Serial.println("Bad WAV header");
    f.close();
    return;
  }

  Serial.printf("WAV: %u Hz, %u bits, %u channels, %u bytes data\n",
                h.sampleRate, h.bitsPerSample, h.numChannels, dataLen);

  if (h.bitsPerSample != 16) {
    Serial.println("Only 16-bit PCM supported");
    f.close();
    return;
  }

  setupI2S(h.sampleRate);
  f.seek(dataStart);

  const size_t frames = BUF_SAMPLES;
  const size_t bufBytes = frames * 2 * sizeof(int16_t);
  int16_t *outBuf = (int16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_DMA);
  if (!outBuf) {
    Serial.println("DMA memory allocation failed");
    f.close();
    return;
  }

  uint32_t bytesRemaining = dataLen;
  while (bytesRemaining > 0) {
    if (h.numChannels == 1) {
      int16_t monoBuf[frames];
      size_t toRead = min<uint32_t>(bytesRemaining, frames * sizeof(int16_t));
      size_t r = f.read((uint8_t*)monoBuf, toRead);
      if (r == 0) break;
      size_t samplesRead = r / sizeof(int16_t);
      for (size_t i = 0; i < samplesRead; ++i) {
        outBuf[2*i] = monoBuf[i];
        outBuf[2*i + 1] = monoBuf[i];
      }
      for (size_t i = samplesRead; i < frames; ++i) {
        outBuf[2*i] = 0;
        outBuf[2*i + 1] = 0;
      }
      bytesRemaining -= r;
    } else {
      size_t toRead = min<uint32_t>(bytesRemaining, bufBytes);
      size_t r = f.read((uint8_t*)outBuf, toRead);
      if (r == 0) break;
      if (r < bufBytes) {
        memset(((uint8_t*)outBuf) + r, 0, bufBytes - r);
      }
      bytesRemaining -= r;
    }

    size_t bytesWritten = 0;
    i2s_write(I2S_NUM, outBuf, bufBytes, &bytesWritten, portMAX_DELAY);
    if (bytesWritten < bufBytes) {
      Serial.printf("Underrun: wrote %u of %u bytes\n", bytesWritten, bufBytes);
    }
  }

  free(outBuf);
  f.close();
  Serial.println("Playback finished");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    while (1) delay(1000);
  }
  playWav(WAV_PATH);
}

void loop() {
  static unsigned long lastPlay = 0;
  if (millis() - lastPlay > 10000) {
    playWav(WAV_PATH);
    lastPlay = millis();
  }
}