Camera Feed Streaming on esp32s3
Board & camera
Board: Seeed XIAO ESP32S3 Sense + Sense expansion board
Camera: OV3660
ESP-IDF: v5.3.4, target esp32s3


get_idf
cd ~/camera_stream_esp32s3/firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor

http://192.168.18.149:81/stream
http://172.17.77.72:81/stream

http://192.168.18.149:81/stream


idf.py menuconfig
change the settings 
ESP PSRAM
OCTAL MODE
80 MHZ
ESP SYSTEM SETTINGS
CPU FREQ 240 HZ
Make RAM allocatable using malloc() as well

