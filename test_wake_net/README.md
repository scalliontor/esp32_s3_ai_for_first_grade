arduino-cli compile --verbose \
  --fqbn esp32:esp32:esp32s3 \
  --board-options PartitionScheme=default,PSRAM=opi \
  ./esp32_wakenet_assistant


arduino-cli core uninstall esp32:esp32