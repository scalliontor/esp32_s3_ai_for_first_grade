#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h> // Bắt buộc cho hàm sqrt()

// ===============================================================
// 1. CẤU HÌNH - ĐIỀU CHỈNH CÁC GIÁ TRỊ NÀY CHO PHÙ HỢP
// ===============================================================

// Chân cắm phần cứng I2S
#define I2S_MIC_SERIAL_CLOCK    18
#define I2S_MIC_WORD_SELECT     17
#define I2S_MIC_SERIAL_DATA     16
#define I2S_SPEAKER_SERIAL_CLOCK 37
#define I2S_SPEAKER_WORD_SELECT  38
#define I2S_SPEAKER_SERIAL_DATA  36

// Cài đặt I2S
#define I2S_SAMPLE_RATE         16000
#define I2S_BITS_PER_SAMPLE     I2S_BITS_PER_SAMPLE_16BIT
#define I2S_MIC_PORT            I2S_NUM_0
#define I2S_SPEAKER_PORT        I2S_NUM_1

// --- Cài đặt Voice Activity Detection (VAD) ---
#define VAD_FRAME_SAMPLES       512   
#define VAD_RMS_THRESHOLD       800   // Ngưỡng âm lượng để kích hoạt. HÃY ĐIỀU CHỈNH GIÁ TRỊ NÀY!
#define VAD_SPEECH_FRAME_COUNT  3     // Số khung âm thanh liên tiếp cần có để BẮT ĐẦU truyền
#define VAD_SILENCE_FRAME_COUNT 50    // Số khung im lặng liên tiếp cần có để DỪNG truyền

// ===============================================================
// 2. BIẾN TOÀN CỤC
// ===============================================================

// Máy trạng thái để quản lý logic
enum State { 
  STATE_MUTED,      // Trạng thái im lặng, đang lắng nghe
  STATE_STREAMING   // Trạng thái kích hoạt, đang truyền âm thanh
};
volatile State currentState = STATE_MUTED;

// Bộ đệm nhỏ để đọc dữ liệu từ I2S
int16_t i2s_read_buffer[VAD_FRAME_SAMPLES];


// ===============================================================
// 3. CÁC HÀM CÀI ĐẶT I2S (Không thay đổi)
// ===============================================================
float calculate_rms(int16_t* buffer, size_t len) { /* ... Giữ nguyên như cũ ... */ 
    double sum_sq = 0;
    for (int i = 0; i < len; i++) {
        sum_sq += (double)buffer[i] * buffer[i];
    }
    return sqrt(sum_sq / len);
}
void setup_i2s_input() { /* ... Giữ nguyên như cũ ... */ 
    Serial.println("Configuring I2S Input (Microphone)...");
    i2s_config_t i2s_mic_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), .sample_rate = I2S_SAMPLE_RATE, .bits_per_sample = I2S_BITS_PER_SAMPLE, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, .communication_format = I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, .dma_buf_count = 8, .dma_buf_len = 256};
    i2s_pin_config_t i2s_mic_pins = {.bck_io_num = I2S_MIC_SERIAL_CLOCK, .ws_io_num = I2S_MIC_WORD_SELECT, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_MIC_SERIAL_DATA};
    ESP_ERROR_CHECK(i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_MIC_PORT, &i2s_mic_pins));
}
void setup_i2s_output() { /* ... Giữ nguyên như cũ ... */ 
    Serial.println("Configuring I2S Output (Speaker)...");
    i2s_config_t i2s_speaker_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), .sample_rate = I2S_SAMPLE_RATE, .bits_per_sample = I2S_BITS_PER_SAMPLE, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, .communication_format = I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, .dma_buf_count = 8, .dma_buf_len = 256, .tx_desc_auto_clear = true};
    i2s_pin_config_t i2s_speaker_pins = {.bck_io_num = I2S_SPEAKER_SERIAL_CLOCK, .ws_io_num = I2S_SPEAKER_WORD_SELECT, .data_out_num = I2S_SPEAKER_SERIAL_DATA, .data_in_num = I2S_PIN_NO_CHANGE};
    ESP_ERROR_CHECK(i2s_driver_install(I2S_SPEAKER_PORT, &i2s_speaker_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_SPEAKER_PORT, &i2s_speaker_pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_SPEAKER_PORT));
}


// ===============================================================
// 4. TÁC VỤ XỬ LÝ ÂM THANH CHÍNH
// ===============================================================

void audio_processing_task(void *pvParameters) {
  size_t bytes_read;
  int speech_frames = 0;
  int silence_frames = 0;
  long last_rms_report = 0;

  while (true) {
    // Luôn luôn đọc một đoạn âm thanh từ micro
    i2s_read(I2S_MIC_PORT, i2s_read_buffer, sizeof(i2s_read_buffer), &bytes_read, portMAX_DELAY);
    float rms = calculate_rms(i2s_read_buffer, bytes_read / sizeof(int16_t));
    bool is_sound_detected = rms > VAD_RMS_THRESHOLD;

    switch (currentState) {
      
      case STATE_MUTED:
        if (millis() - last_rms_report > 1000) {
            Serial.printf("Muted. Listening... RMS: %.2f\n", rms);
            last_rms_report = millis();
        }

        if (is_sound_detected) {
          speech_frames++;
        } else {
          speech_frames = 0; // Reset nếu có sự gián đoạn
        }

        // Nếu phát hiện đủ số khung âm thanh, chuyển trạng thái
        if (speech_frames >= VAD_SPEECH_FRAME_COUNT) {
          Serial.printf("==> Voice detected! RMS: %.2f. Starting stream.\n", rms);
          speech_frames = 0;
          currentState = STATE_STREAMING;
        }
        break;

      case STATE_STREAMING:
        // GHI TRỰC TIẾP RA LOA
        size_t bytes_written;
        i2s_write(I2S_SPEAKER_PORT, i2s_read_buffer, bytes_read, &bytes_written, portMAX_DELAY);

        // Bây giờ, kiểm tra sự im lặng để dừng lại
        if (is_sound_detected) {
          silence_frames = 0; // Reset nếu vẫn còn âm thanh
        } else {
          silence_frames++;
        }

        // Nếu đủ số khung im lặng, quay về trạng thái tắt tiếng
        if (silence_frames >= VAD_SILENCE_FRAME_COUNT) {
          Serial.println("==> Silence detected. Muting stream.");
          silence_frames = 0;
          // Dọn dẹp bộ đệm của loa để tránh tiếng ồn còn sót lại
          i2s_zero_dma_buffer(I2S_SPEAKER_PORT); 
          currentState = STATE_MUTED;
        }
        break;
    }
  }
}

// ===============================================================
// 5. ARDUINO SETUP & LOOP (Không thay đổi)
// ===============================================================

void setup() {
  Serial.begin(115200);
  while (!Serial);

  setup_i2s_input();
  setup_i2s_output();

  xTaskCreatePinnedToCore(
      audio_processing_task,
      "Audio Processing Task",
      8192,
      NULL,
      10,
      NULL,
      1
  );
  
  Serial.println("==============================================");
  Serial.println("      VAD-Activated Audio Passthrough");
  Serial.println("==============================================");
  Serial.println("Speak to open the audio stream.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}