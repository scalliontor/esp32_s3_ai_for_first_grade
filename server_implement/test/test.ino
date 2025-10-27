#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>

// Sử dụng namespace của thư viện websockets
using namespace websockets;

// ===============================================================
// 1. CẤU HÌNH
// ===============================================================

// --- Cấu hình Mạng & WebSocket ---
const char* ssid = "PTIT_CIE";         // <-- THAY ĐỔI
const char* password = "cie@2025"; // <-- THAY ĐỔI
const char* websocket_server_host = "10.170.77.48"; // <-- THAY ĐỔI IP của server
const uint16_t websocket_server_port = 8000;
const char* websocket_server_path = "/ws";

// --- Chân cắm I2S ---
#define I2S_MIC_SERIAL_CLOCK    18
#define I2S_MIC_WORD_SELECT     17
#define I2S_MIC_SERIAL_DATA     16
#define I2S_SPEAKER_SERIAL_CLOCK 37
#define I2S_SPEAKER_WORD_SELECT  38
#define I2S_SPEAKER_SERIAL_DATA  36

// --- Cài đặt I2S ---
#define I2S_SAMPLE_RATE         16000
#define I2S_BITS_PER_SAMPLE     I2S_BITS_PER_SAMPLE_16BIT
#define I2S_MIC_PORT            I2S_NUM_0
#define I2S_SPEAKER_PORT        I2S_NUM_1

// --- Cài đặt VAD ---
#define VAD_FRAME_SAMPLES       512
#define VAD_RMS_THRESHOLD       800  // Tinh chỉnh giá trị này cho mic của bạn
#define VAD_SPEECH_FRAME_COUNT  3    // Số frame liên tiếp phải có tiếng nói để kích hoạt
#define VAD_SILENCE_FRAME_COUNT 50   // Số frame liên tiếp phải im lặng để ngừng stream

// --- Cấu hình Bộ đệm vòng (Circular Buffer) --- // <<< NEW >>>
#define PRE_SPEECH_BUFFER_FRAMES 10  // Lưu lại 10 frame trước khi nói, có thể tăng/giảm

// --- Cấu hình Âm thanh Loa ---
#define SPEAKER_GAIN            8.0f

// ===============================================================
// 2. BIẾN TOÀN CỤC
// ===============================================================

WebsocketsClient client;

enum State {
  STATE_MUTED,
  STATE_STREAMING,
  STATE_WAITING_FOR_SERVER,
  STATE_PLAYING_RESPONSE
};
volatile State currentState = STATE_MUTED;

// Buffer để đọc/ghi I2S
int16_t i2s_buffer[VAD_FRAME_SAMPLES];

// <<< NEW: Circular buffer để lưu trữ âm thanh trước khi nói >>>
int16_t pre_speech_buffer[PRE_SPEECH_BUFFER_FRAMES][VAD_FRAME_SAMPLES];
int pre_speech_buffer_index = 0;


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
// 4. WEBSOCKET & ÂM THANH
// ===============================================================

void onWebsocketEvent(WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
        Serial.println("Websocket connection opened.");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
        Serial.println("Websocket connection closed.");
    }
}

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
        
        size_t len = message.length();
        // Tạo một buffer tạm để khuếch đại âm thanh
        int16_t temp_write_buffer[len / sizeof(int16_t)];
        memcpy(temp_write_buffer, message.c_str(), len);
        
        for (int i = 0; i < len / sizeof(int16_t); i++) {
          float amplified = temp_write_buffer[i] * SPEAKER_GAIN;
          if (amplified > 32767) amplified = 32767; 
          if (amplified < -32768) amplified = -32768;
          temp_write_buffer[i] = (int16_t)amplified;
        }
        
        size_t bytes_written = 0;
        i2s_write(I2S_SPEAKER_PORT, temp_write_buffer, len, &bytes_written, portMAX_DELAY);
    }
}


void audio_processing_task(void *pvParameters) {
  size_t bytes_read;
  int speech_frames = 0;
  int silence_frames = 0;
  long last_rms_report = 0;

  while (true) {
    // Chỉ đọc từ mic khi không đang phát âm thanh trả về
    if (currentState == STATE_MUTED || currentState == STATE_STREAMING) {
        // Luôn đọc dữ liệu từ I2S
        i2s_read(I2S_MIC_PORT, i2s_buffer, sizeof(i2s_buffer), &bytes_read, portMAX_DELAY);
        
        // <<< CHANGED: Logic xử lý trạng thái được viết lại cho rõ ràng hơn >>>
        switch (currentState) {
            case STATE_MUTED: {
                // Thêm frame hiện tại vào bộ đệm vòng
                memcpy(pre_speech_buffer[pre_speech_buffer_index], i2s_buffer, bytes_read);
                pre_speech_buffer_index = (pre_speech_buffer_index + 1) % PRE_SPEECH_BUFFER_FRAMES;

                // Tính RMS để phát hiện giọng nói
                float rms = calculate_rms(i2s_buffer, bytes_read / sizeof(int16_t));
                
                if (millis() - last_rms_report > 1000) {
                    Serial.printf("Muted. Listening... RMS: %.2f\n", rms);
                    last_rms_report = millis();
                }

                if (rms > VAD_RMS_THRESHOLD) {
                    speech_frames++;
                } else {
                    speech_frames = 0; // Reset nếu có frame im lặng xen kẽ
                }

                if (speech_frames >= VAD_SPEECH_FRAME_COUNT) {
                    Serial.printf("==> Voice detected! RMS: %.2f. Starting stream.\n", rms);
                    
                    currentState = STATE_STREAMING;
                    speech_frames = 0; // Reset bộ đếm

                    // <<< KEY CHANGE: Gửi toàn bộ bộ đệm vòng (pre-speech buffer) trước >>>
                    if (client.available()) {
                        Serial.println("Sending pre-speech buffer...");
                        // Vòng lặp này đảm bảo gửi các frame theo đúng thứ tự thời gian
                        for (int i = 0; i < PRE_SPEECH_BUFFER_FRAMES; i++) {
                            int buffer_to_send_index = (pre_speech_buffer_index + i) % PRE_SPEECH_BUFFER_FRAMES;
                            client.sendBinary((const char*)pre_speech_buffer[buffer_to_send_index], sizeof(i2s_buffer));
                        }
                    }
                }
                break;
            }

            case STATE_STREAMING: {
                // Gửi frame hiện tại vừa đọc được
                if (client.available()) {
                    client.sendBinary((const char*)i2s_buffer, bytes_read);
                }

                // Kiểm tra sự im lặng để kết thúc stream
                float rms = calculate_rms(i2s_buffer, bytes_read / sizeof(int16_t));
                if (rms <= VAD_RMS_THRESHOLD) {
                    silence_frames++;
                } else {
                    silence_frames = 0; // Reset nếu lại có tiếng nói
                }

                if (silence_frames >= VAD_SILENCE_FRAME_COUNT) {
                    Serial.println("==> Silence detected. Stopping stream and waiting for server response.");
                    currentState = STATE_WAITING_FOR_SERVER;
                    silence_frames = 0; // Reset bộ đếm
                }
                break;
            }
        }
    } else {
        // Nếu đang ở trạng thái khác (chờ server, phát loa), nghỉ một chút
        vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

// ===============================================================
// 5. SETUP & LOOP
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

  client.onEvent(onWebsocketEvent);
  client.onMessage(onWebsocketMessage);
  
  Serial.printf("Connecting to WebSocket server: %s:%d%s\n", websocket_server_host, websocket_server_port, websocket_server_path);
  bool connected = client.connect(websocket_server_host, websocket_server_port, websocket_server_path);
  if (!connected) {
      Serial.println("WebSocket connection attempt failed!");
  } else {
      Serial.println("WebSocket connected successfully!");
  }

  // Tăng stack size cho task audio nếu cần
  xTaskCreatePinnedToCore(
      audio_processing_task,
      "Audio Processing Task",
      10240, // Tăng stack size lên 10KB
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
  client.poll();
  
  if (!client.available()) {
    Serial.println("WebSocket disconnected. Reconnecting...");
    bool connected = client.connect(websocket_server_host, websocket_server_port, websocket_server_path);
    if (!connected) {
      Serial.println("Reconnect attempt failed.");
      delay(2000);
    } else {
      Serial.println("Reconnected successfully.");
    }
  }
  delay(10);
}
