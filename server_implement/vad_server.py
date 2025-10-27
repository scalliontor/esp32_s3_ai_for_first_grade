# --- START OF FILE main.py ---

import asyncio
import wave
from datetime import datetime
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
import os
import torch
from collections import deque

# --- IMPORT PIPELINE TỪ THƯ MỤC MODULES ---
from modules.pipeline import VoiceAssistantPipeline

# --- Cấu hình ---
SAMPLE_RATE = 16000
BIT_DEPTH_BYTES = 2
CHANNELS = 1
AUDIO_CHUNK_SIZE = 1024 # Kích thước mỗi đoạn audio gửi về client

# --- <<< NEW: Cấu hình VAD >>> ---
VAD_FRAME_MS = 30  # Silero VAD hoạt động tốt nhất với frame 30ms
VAD_CHUNK_SIZE = (SAMPLE_RATE * VAD_FRAME_MS // 1000) * BIT_DEPTH_BYTES # = 960 bytes cho 30ms @ 16kHz/16bit
VAD_SPEECH_THRESHOLD = 0.5  # Ngưỡng tin cậy để coi là có tiếng nói
VAD_SILENCE_FRAMES_TRIGGER = 1  # Số frame có tiếng nói liên tiếp để bắt đầu thu
VAD_SILENCE_FRAMES_END = 25     # Số frame im lặng liên tiếp để kết thúc thu (khoảng 0.75s)
VAD_BUFFER_FRAMES = 5       # Lưu lại 5 frame âm thanh ngay TRƯỚC khi có tiếng nói

# --- Khởi tạo ứng dụng và các mô hình AI ---
app = FastAPI()

# Khởi tạo pipeline MỘT LẦN DUY NHẤT khi server bắt đầu.
pipeline = VoiceAssistantPipeline()

# <<< NEW: Tải mô hình Silero VAD >>>
try:
    torch.set_num_threads(1)
    vad_model, utils = torch.hub.load(repo_or_dir='snakers4/silero-vad',
                                      model='silero_vad',
                                      force_reload=False,
                                      onnx=True) # Dùng ONNX để nhanh hơn trên CPU
    (get_speech_timestamps, save_audio, read_audio, VADIterator, collect_chunks) = utils
    print("Silero VAD model loaded successfully.")
except Exception as e:
    print(f"Error loading Silero VAD model: {e}")
    vad_model = None

def save_audio_to_wav(audio_data: bytes, folder: str = "audio_files") -> str:
    """Lưu dữ liệu âm thanh thô vào một file WAV."""
    os.makedirs(folder, exist_ok=True)
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = os.path.join(folder, f"recording_{timestamp}.wav")
    
    try:
        with wave.open(filename, 'wb') as wf:
            wf.setnchannels(CHANNELS)
            wf.setsampwidth(BIT_DEPTH_BYTES)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(audio_data)
        print(f"Audio received and saved to: {filename}")
        return filename
    except Exception as e:
        print(f"Error saving WAV file: {e}")
        return ""

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """
    Endpoint nhận stream audio LIÊN TỤC, dùng VAD để quyết định, 
    gọi AI pipeline, và stream audio trả về.
    """
    await websocket.accept()
    print(f"Client connected from: {websocket.client.host}")
    
    # <<< NEW: Logic VAD state machine >>>
    is_speaking = False
    silence_counter = 0
    speech_trigger_counter = 0
    
    # Dùng deque để làm bộ đệm vòng hiệu quả
    pre_buffer = deque(maxlen=VAD_BUFFER_FRAMES) 
    speech_buffer = []
    
    try:
        # Vòng lặp nhận dữ liệu liên tục
        while True:
            # Nhận chính xác kích thước chunk mà VAD cần
            data = await websocket.receive_bytes()
            
            if len(data) != VAD_CHUNK_SIZE:
                # Bỏ qua các gói tin không đúng kích thước nếu có
                # Hoặc có thể xử lý ghép nối các gói tin nhỏ lại
                print(f"Warning: Received chunk of size {len(data)}, expected {VAD_CHUNK_SIZE}. Skipping.")
                continue

            # Chuyển bytes thành tensor cho VAD model
            audio_tensor = torch.from_numpy(
                bytearray(data)
            ).to(torch.int16).float() / 32768.0

            # Dự đoán xác suất có tiếng nói
            speech_prob = vad_model(audio_tensor, SAMPLE_RATE).item()

            if speech_prob > VAD_SPEECH_THRESHOLD:
                # Phát hiện có tiếng nói
                silence_counter = 0
                if not is_speaking:
                    speech_trigger_counter += 1
                    if speech_trigger_counter >= VAD_SILENCE_FRAMES_TRIGGER:
                        print("==> Voice activity detected. Start recording.")
                        is_speaking = True
                        # Đổ toàn bộ pre_buffer vào speech_buffer
                        speech_buffer.extend(list(pre_buffer))
                
                if is_speaking:
                    speech_buffer.append(data)
            else:
                # Phát hiện im lặng
                speech_trigger_counter = 0
                if is_speaking:
                    silence_counter += 1
                    speech_buffer.append(data) # Vẫn thu thêm một chút im lặng
                    if silence_counter >= VAD_SILENCE_FRAMES_END:
                        print("==> Silence detected. End of utterance.")
                        
                        # --- BẮT ĐẦU XỬ LÝ PIPELINE ---
                        full_audio_data = b"".join(speech_buffer)
                        input_audio_path = save_audio_to_wav(full_audio_data)
                        
                        if input_audio_path:
                            try:
                                result = pipeline.process(audio_input_path=input_audio_path)
                                output_audio_path = result.get("output_audio")

                                if output_audio_path and os.path.exists(output_audio_path):
                                    print(f"Streaming response audio from: {output_audio_path}")
                                    with open(output_audio_path, 'rb') as audio_file:
                                        while True:
                                            chunk = audio_file.read(AUDIO_CHUNK_SIZE)
                                            if not chunk: break
                                            await websocket.send_bytes(chunk)
                                    
                                    await websocket.send_text("TTS_END")
                                    print("Finished streaming response.")
                                else:
                                    print("Pipeline did not return a valid audio output path.")
                            except Exception as e:
                                print(f"An error occurred during pipeline processing: {e}")
                        
                        # Reset state để chờ câu nói tiếp theo
                        is_speaking = False
                        silence_counter = 0
                        speech_buffer.clear()
                        pre_buffer.clear()

                else:
                    # Nếu chưa nói, chỉ cần đẩy vào pre_buffer
                    pre_buffer.append(data)


    except WebSocketDisconnect:
        print(f"Client {websocket.client.host} disconnected.")
    except Exception as e:
        print(f"A critical error occurred in websocket connection: {e}")

@app.get("/")
def read_root():
    return {"status": "Voice Assistant Server is running"}