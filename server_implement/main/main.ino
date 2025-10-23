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

// ===============================================================
// 2. BIẾN TOÀN CỤC
// ===============================================================

// Khởi tạo client WebSocket
WebsocketsClient client;

// Máy trạng thái để quản lý logic
enum State {
  STATE_MUTED,                // Trạng thái im lặng, đang lắng nghe
  STATE_STREAMING,            // Trạng thái kích hoạt, đang truyền âm thanh đi
  STATE_WAITING_FOR_SERVER,   // Đã gửi xong, đang chờ server xử lý và phản hồi
  STATE_PLAYING_RESPONSE      // Đang nhận và phát âm thanh từ server
};
volatile State currentState = STATE_MUTED;

// Bộ đệm nhỏ để đọc dữ liệu từ I2S
int16_t i2s_read_buffer[VAD_FRAME_SAMPLES];

// ===============================================================
// 3. CÁC HÀM CÀI ĐẶT I2S (Giữ nguyên như cũ)
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

// Hàm xử lý các sự kiện WebSocket
void onWebsocketEvent(WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
        Serial.println("Websocket connection opened.");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
        Serial.println("Websocket connection closed.");
    } else if (event == WebsocketsEvent::GotPing) {
        Serial.println("Got a Ping!");
    }
}

// Hàm xử lý khi nhận được tin nhắn từ WebSocket
void onWebsocketMessage(WebsocketsMessage message) {
    // Server có thể gửi text để điều khiển
    if (message.isText()) {
        Serial.printf("Server sent text: %s\n", message.c_str());
        // Ví dụ: server gửi "TTS_END" khi đã gửi xong hết audio
        if (message.stringValue() == "TTS_END") {
            Serial.println("End of TTS stream from server. Returning to listening mode.");
            currentState = STATE_MUTED;
        }
    }
    // Dữ liệu âm thanh từ server sẽ ở dạng nhị phân (binary)
    else if (message.isBinary()) {
        // Khi nhận được gói âm thanh đầu tiên, chuyển trạng thái
        if (currentState != STATE_PLAYING_RESPONSE) {
            Serial.println("Receiving audio from server, starting playback...");
            currentState = STATE_PLAYING_RESPONSE;
            i2s_zero_dma_buffer(I2S_SPEAKER_PORT); // Xóa bộ đệm loa trước khi phát
        }
        
        // Ghi trực tiếp dữ liệu âm thanh nhận được ra loa
        size_t bytes_written = 0;
        i2s_write(I2S_SPEAKER_PORT, message.c_str(), message.length(), &bytes_written, portMAX_DELAY);
    }
}


void audio_processing_task(void *pvParameters) {
  size_t bytes_read;
  int speech_frames = 0;
  int silence_frames = 0;
  long last_rms_report = 0;

  while (true) {
    // Chỉ đọc mic khi ở trạng thái lắng nghe hoặc đang gửi đi
    // Không đọc mic khi đang chờ server hoặc đang phát loa để tránh vòng lặp âm thanh
    if (currentState == STATE_MUTED || currentState == STATE_STREAMING) {
        // Đọc một đoạn âm thanh từ micro
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

                if (speech_frames >= VAD_SPEECH_FRAME_COUNT) {
                    Serial.printf("==> Voice detected! RMS: %.2f. Starting stream to server.\n", rms);
                    speech_frames = 0;
                    currentState = STATE_STREAMING;
                }
                break;

            case STATE_STREAMING:
                // THAY VÌ GHI RA LOA, GỬI DỮ LIỆU QUA WEBSOCKET
                if (client.isConnected()) {
                    client.sendBinary((const char*)i2s_read_buffer, bytes_read);
                }

                // Kiểm tra sự im lặng để dừng gửi
                if (is_sound_detected) {
                    silence_frames = 0;
                } else {
                    silence_frames++;
                }

                if (silence_frames >= VAD_SILENCE_FRAME_COUNT) {
                    Serial.println("==> Silence detected. Stopping stream and waiting for server response.");
                    silence_frames = 0;
                    // Tùy chọn: Gửi một tin nhắn báo kết thúc stream
                    // client.send("END_OF_STREAM"); 
                    currentState = STATE_WAITING_FOR_SERVER;
                }
                break;
        }
    } else {
        // Khi đang chờ hoặc đang phát, task này chỉ cần nghỉ một chút
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
  
  // --- Kết nối WiFi ---
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // --- Cài đặt I2S ---
  setup_i2s_input();
  setup_i2s_output();

  // --- Cài đặt WebSocket ---
  client.onEvent(onWebsocketEvent);
  client.onMessage(onWebsocketMessage);
  
  // Thử kết nối tới server
  Serial.printf("Connecting to WebSocket server: %s:%d%s\n", websocket_server_host, websocket_server_port, websocket_server_path);
  bool connected = client.connect(websocket_server_host, websocket_server_port, websocket_server_path);
  if (!connected) {
      Serial.println("WebSocket connection failed!");
      // Có thể thêm logic khởi động lại ESP32 ở đây
  } else {
      Serial.println("WebSocket connected successfully!");
  }


  // --- Khởi tạo Task xử lý âm thanh ---
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
  // BẮT BUỘC phải gọi client.loop() trong hàm loop chính
  // để thư viện WebSocket có thể xử lý các sự kiện mạng
  client.loop();
  
  // Có thể thêm logic kiểm tra và kết nối lại WebSocket nếu bị mất kết nối
  if (!client.isConnected()) {
    Serial.println("WebSocket disconnected. Reconnecting...");
    bool connected = client.connect(websocket_server_host, websocket_server_port, websocket_server_path);
    if (!connected) {
      Serial.println("Reconnect failed.");
      delay(2000); // Chờ 2 giây rồi thử lại
    } else {
      Serial.println("Reconnected successfully.");
    }
  }

  // Hàm loop() có thể để trống hoặc chỉ chứa client.loop() và logic reconnect
  // vì tác vụ chính đã được xử lý trong FreeRTOS task.
  // Delay một chút để không chiếm hết CPU core 0.
  delay(10);
}