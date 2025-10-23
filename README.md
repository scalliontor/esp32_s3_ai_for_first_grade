### Compile ESP32-S3 Code
```shell
arduino-cli compile --fqbn esp32:esp32:esp32s3 . 
```

### Upload to ESP32-S3
```shell
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 .
```

### Monitor Serial Output
```shell
arduino-cli monitor -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 -c baudrate=115200
```

### List Serial Ports
```shell
ls /dev/tty*
```

### Run Python Web Server
```shell
python -m uvicorn main:app --host 0.0.0.0 --port 8000
```