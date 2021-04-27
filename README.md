# esp32-cam-fpv
esp32 cam digital, low latency FPV

This project uses a modified esp-camera component running on an AI Thinker board to send low-latency mjpeg video to a Raspberry PI base station with 1-2 wifi cards using packet injection and monitor mode.

It uses FEC encoding (4/7 currently configured) with 1400 byte packets and it achieves quite good performance:
* Up to 12Mbps video rate.
* More than 90 FPS at 400x296 or lower with 20-50 ms latency.
* 34-44 FPS at 800x600 or 640x480 with 50-80 ms latency.
* 12 FPS at 1024x768 with >100 ms latency.
* Air unit weighs in at 18g with camera, antenna and a 3D printed case.

It's based on an Ai Thinker board with an OV2640 camera board and the esp-camera component.

The data is received from the camera module as JPEG ar 20MHz I2S clock and passed directly to the wifi module and written to the SD card if the DVR is enabled.
The ESP camera component was modified to send the data as it's received from the DMA instead of frame-by-frame basis. This decreases latency quite significantly (10-20 ms) and reduces the need to allocate full frames in PSRAM.

The wifi data is send using packet injection with configurable rate - from 2 MB CCK to 72MB MCS7 - and power.

The air unit can also record the video straight from the camera to a sd card. The format is a rudimentary MJPEG without any header so when playing back the FPS will be whatever your player will decide.
There is significant buffering when writing to SD (3MB at the moment) to work around the very regular slowdowns of sd cards.



The receiver is a Raspberry PI 4 with 2 wifi adapters in monitor mode (TL-WN722N). The adapters work as diversity receivers and the data is reconstructed from the FEC packets.

The JPEG decoding is done with turbojpeg to lower latency and - based on the resolution - can take between 1 and 7 milliseconds.
It's then uploaded to 3 separate textures as YUV and converted to RGB in a shader.

The link is bi-directional so the ground station can send data to the air unit. At the moment it sends camera and wifi configuration data but I plan to have a full bi-directional serial port for telemetry coming soon.
The back link uses 64byte packets with a 2/6 FEC encoding (so quite solid) at a low wifi rate (I think 1Mb).

This is very WIP at the moment, but it proves that the ESP32 can definitely stream video with very low latency. 
I plan to use this is a long range micro quad.

Here is a video shot at 30 FPS at 800x600 (video converted from the source mjpeg):

https://user-images.githubusercontent.com/10252034/116135308-43c08c00-a6d1-11eb-99b9-9dbb106b6489.mp4




