import asyncio
import wave
from datetime import datetime
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

# --- Cấu hình Audio ---
# Các thông số này PHẢI KHỚP với code ESP32
SAMPLE_RATE = 16000
BIT_DEPTH_BYTES = 2  # 16-bit = 2 bytes
CHANNELS = 1

# Thời gian chờ (giây) trước khi coi là kết thúc một câu nói
# Nếu sau 0.5 giây không nhận được dữ liệu mới, server sẽ lưu file.
# Bạn có thể điều chỉnh giá trị này.
AUDIO_TIMEOUT = 0.5

# Khởi tạo ứng dụng FastAPI
app = FastAPI()

def save_audio_to_wav(audio_data: bytes, folder: str = "audio_files") -> str:
    """
    Lưu dữ liệu âm thanh thô (raw PCM) vào một file WAV.
    """
    import os
    # Tạo thư mục nếu chưa có
    os.makedirs(folder, exist_ok=True)
    
    # Tạo tên file duy nhất dựa trên thời gian
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = os.path.join(folder, f"recording_{timestamp}.wav")
    
    try:
        with wave.open(filename, 'wb') as wf:
            wf.setnchannels(CHANNELS)
            wf.setsampwidth(BIT_DEPTH_BYTES)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(audio_data)
        print(f"Successfully saved audio to {filename}")
        return filename
    except Exception as e:
        print(f"Error saving WAV file: {e}")
        return ""

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """
    Endpoint này nhận stream âm thanh, chờ khoảng lặng,
    lưu thành file WAV, và vẫn echo lại cho client.
    """
    await websocket.accept()
    print(f"Client connected from: {websocket.client.host}")
    
    try:
        # Vòng lặp chính, cho phép xử lý nhiều câu nói trong một kết nối
        while True:
            audio_chunks = []
            
            # Vòng lặp nhận dữ liệu cho MỘT câu nói
            while True:
                try:
                    # Chờ nhận dữ liệu với một khoảng timeout
                    data = await asyncio.wait_for(
                        websocket.receive_bytes(), 
                        timeout=AUDIO_TIMEOUT
                    )
                    
                    # Thêm dữ liệu vào danh sách
                    audio_chunks.append(data)
                    
                    # (Phần echo) Gửi trả lại ngay lập tức
                    await websocket.send_bytes(data)

                except asyncio.TimeoutError:
                    # Nếu hết thời gian chờ, nghĩa là người dùng đã ngừng nói
                    print(f"Timeout detected. User stopped speaking.")
                    break # Thoát vòng lặp nhận dữ liệu
            
            # Sau khi vòng lặp trên kết thúc, kiểm tra xem có dữ liệu để lưu không
            if audio_chunks:
                print("Processing received audio chunks...")
                # Ghép tất cả các đoạn audio lại thành một chuỗi bytes duy nhất
                full_audio_data = b"".join(audio_chunks)
                
                # Lưu thành file WAV
                save_audio_to_wav(full_audio_data)
                
                # (Tùy chọn) Gửi tin nhắn báo cho client là server đã xử lý xong
                # await websocket.send_text("TTS_END") # Mở comment dòng này nếu bạn muốn ESP32 biết khi nào nên nghe lại
                
                # Xóa bộ đệm để chuẩn bị cho câu nói tiếp theo
                audio_chunks.clear()

    except WebSocketDisconnect:
        print(f"Client {websocket.client.host} disconnected.")
    except Exception as e:
        print(f"An error occurred: {e}")

@app.get("/")
def read_root():
    return {"status": "Audio processing server is running"}


@app.get("/health")
def health_check():
    return {"status": "ok"} 
# Endpoint kiểm tra sức khỏe server