### See all connected boards and ports
```shell
arduino-cli board list
```

### Compile ESP32-S3 Code
```shell
arduino-cli compile --fqbn esp32:esp32:esp32s3 . 
```

### Run Python Web Server
```shell
python -m uvicorn main_chat:app --host 0.0.0.0 --port 8000
```

### Upload to ESP32-S3
```shell
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 .
```

### Monitor Serial Output
```shell
arduino-cli monitor -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 -c baudrate=115200
```


