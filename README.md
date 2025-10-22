arduino-cli compile --fqbn esp32:esp32:esp32s3 .
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 .
arduino-cli monitor -p /dev/ttyACM0 -b esp32:esp32:esp32s3 -c baudrate=115200

ls /dev/tty*
