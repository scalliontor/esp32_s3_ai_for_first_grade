#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h> // Thư viện WebSocket

// ===============================================================
// 1. CẤU HÌNH - ĐIỀU CHỈNH CÁC GIÁ TRỊ NÀY CHO PHÙ HỢP
// ===============================================================

// --- Cấu hình Mạng & WebSocket ---
const char* ssid = "TEN_WIFI_CUA_BAN";         // <-- THAY ĐỔI
const char* password = "MAT_KHAU_WIFI_CUA_BAN"; // <-- THAY ĐỔI
const char* websocket_server_host = "192.168.1.10"; // <-- THAY ĐỔI IP của server FastAPI
const uint16_t websocket_server_port = 8000;        // <-- THAY ĐỔI Port của server
const char* websocket_server_path = "/ws";          // <-- THAY ĐỔI Endpoint WebSocket

// Chân cắm phần cứng I2S (Giữ nguyên như của bạn)
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

// --- Cấu hình Âm thanh Loa ---
#define SPEAKER_GAIN            8.0f  // <-- ĐÂY LÀ HỆ SỐ KHUẾCH ĐẠI ÂM LƯỢNG MÀ BẠN MUỐN

// ===============================================================
// 2. BIẾN TOÀN CỤC
// ===============================================================

// Khởi tạo client WebSocket
WebsocketsClient client;

// Máy trạng thái để quản lý logic
enum State {
  STATE_MUTED,
  STATE_STREAMING,
  STATE_WAITING_FOR_SERVER,
  STATE_PLAYING_RESPONSE
};
volatile State currentState = STATE_MUTED;

// Bộ đệm đọc/ghi
int16_t i2s_read_buffer[VAD_FRAME_SAMPLES];
// Bộ đệm riêng để xử lý âm thanh phát ra loa (tránh xung đột dữ liệu)
int16_t i2s_write_buffer[VAD_FRAME_SAMPLES]; 

// ===============================================================
// 3. CÁC HÀM CÀI ĐẶT I2S (Giữ nguyên)
// ===============================================================
float calculate_rms(int16_t* buffer, size_t len) {
    double sum_sq = 0;
    for (int i = 0; i < len; i++) {
        sum_sq += (double)buffer[i] * buffer[i];
    }
    return sqrt(sum_sq / len);
}
void setup_i2s_input() {
    Serial.println("Configuring I2S Input (Microphone)...");
    i2s_config_t i2s_mic_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), .sample_rate = I2S_SAMPLE_RATE, .bits_per_sample = I2S_BITS_PER_SAMPLE, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, .communication_format = I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, .dma_buf_count = 8, .dma_buf_len = 256};
    i2s_pin_config_t i2s_mic_pins = {.bck_io_num = I2S_MIC_SERIAL_CLOCK, .ws_io_num = I2S_MIC_WORD_SELECT, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_MIC_SERIAL_DATA};
    ESP_ERROR_CHECK(i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_MIC_PORT, &i2s_mic_pins));
}
void setup_i2s_output() {
    Serial.println("Configuring I2S Output (Speaker)...");
    i2s_config_t i2s_speaker_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), .sample_rate = I2S_SAMPLE_RATE, .bits_per_sample = I2S_BITS_PER_SAMPLE, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, .communication_format = I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, .dma_buf_count = 8, .dma_buf_len = 256, .tx_desc_auto_clear = true};
    i2s_pin_config_t i2s_speaker_pins = {.bck_io_num = I2S_SPEAKER_SERIAL_CLOCK, .ws_io_num = I2S_SPEAKER_WORD_SELECT, .data_out_num = I2S_SPEAKER_SERIAL_DATA, .data_in_num = I2S_PIN_NO_CHANGE};
    ESP_ERROR_CHECK(i2s_driver_install(I2S_SPEAKER_PORT, &i2s_speaker_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_SPEAKER_PORT, &i2s_speaker_pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_SPEAKER_PORT));
}


// ===============================================================
// 4. TÁC VỤ XỬ LÝ ÂM THANH VÀ WEBSOCKET
// ===============================================================

void onWebsocketEvent(WebsocketsEvent event, String data) { /* ... Giữ nguyên ... */ }

// *** HÀM ĐƯỢC CẬP NHẬT ***
void onWebsocketMessage(WebsocketsMessage message) {
    if (message.isText()) {
        Serial.printf("Server sent text: %s\n", message.c_str());
        if (message.stringValue() == "TTS_END") {
            Serial.println("End of TTS stream from server. Returning to listening mode.");
            currentState = STATE_MUTED;
        }
    }
    else if (message.isBinary()) {
        if (currentState != STATE_PLAYING_RESPONSE) {
            Serial.println("Receiving audio from server, starting playback...");
            currentState = STATE_PLAYING_RESPONSE;
            i2s_zero_dma_buffer(I2S_SPEAKER_PORT);
        }
        
        // --- LOGIC TĂNG ÂM LƯỢNG CỦA BẠN ĐƯỢC TÍCH HỢP VÀO ĐÂY ---
        size_t len = message.length();
        // Sao chép dữ liệu nhận được vào bộ đệm để xử lý
        memcpy(i2s_write_buffer, message.c_str(), len);
        
        // Áp dụng hệ số khuếch đại (gain)
        for (int i = 0; i < len / sizeof(int16_t); i++) {
          float amplified = i2s_write_buffer[i] * SPEAKER_GAIN;
          // Giới hạn giá trị trong khoảng của int16 để tránh vỡ âm thanh
          if (amplified > 32767) amplified = 32767; 
          if (amplified < -32768) amplified = -32768;
          i2s_write_buffer[i] = (int16_t)amplified;
        }
        
        // Ghi dữ liệu đã được khuếch đại ra loa
        size_t bytes_written = 0;
        i2s_write(I2S_SPEAKER_PORT, i2s_write_buffer, len, &bytes_written, portMAX_DELAY);
    }
}


void audio_processing_task(void *pvParameters) {
  size_t bytes_read;
  int speech_frames = 0;
  int silence_frames = 0;
  long last_rms_report = 0;

  while (true) {
    if (currentState == STATE_MUTED || currentState == STATE_STREAMING) {
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
                    speech_frames = 0;
                }

                if (speech_frames >= VAD_SPEECH_FRAME_COUNT) {
                    Serial.printf("==> Voice detected! RMS: %.2f. Starting stream to server.\n", rms);
                    speech_frames = 0;
                    currentState = STATE_STREAMING;
                }
                break;

            case STATE_STREAMING:
                if (client.isConnected()) {
                    client.sendBinary((const char*)i2s_read_buffer, bytes_read);
                }

                if (is_sound_detected) {
                    silence_frames = 0;
                } else {
                    silence_frames++;
                }

                if (silence_frames >= VAD_SILENCE_FRAME_COUNT) {
                    Serial.println("==> Silence detected. Stopping stream and waiting for server response.");
                    silence_frames = 0;
                    currentState = STATE_WAITING_FOR_SERVER;
                }
                break;
        }
    } else {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

// ===============================================================
// 5. ARDUINO SETUP & LOOP
// ===============================================================

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  setup_i2s_input();
  setup_i2s_output();

  client.onEvent(onWebsocketEvent); // onEvent vẫn giữ nguyên, không cần sửa
  client.onMessage(onWebsocketMessage);
  
  Serial.printf("Connecting to WebSocket server: %s:%d%s\n", websocket_server_host, websocket_server_port, websocket_server_path);
  bool connected = client.connect(websocket_server_host, websocket_server_port, websocket_server_path);
  if (!connected) {
      Serial.println("WebSocket connection failed!");
  } else {
      Serial.println("WebSocket connected successfully!");
  }

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
  Serial.println("       Voice Assistant Client Ready");
  Serial.println("==============================================");
  Serial.println("Speak to activate the assistant.");
}

void loop() {
  client.loop();
  
  if (!client.isConnected()) {
    Serial.println("WebSocket disconnected. Reconnecting...");
    bool connected = client.connect(websocket_server_host, websocket_server_port, websocket_server_path);
    if (!connected) {
      Serial.println("Reconnect failed.");
      delay(2000);
    } else {
      Serial.println("Reconnected successfully.");
    }
  }
  delay(10);
}