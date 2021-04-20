# esp32-cam-fpv
esp32 cam digital, low latency FPV

This project uses a modified esp-camera component running on an AI Thinker board to send low-latency mjpeg video to a Raspberry PI base station with 1-2 wifi cards using packet injection and monitor mode.

It uses FEC encoding (K4 N7 currently configured) 1400 byte packets and it achieves quite good performance:
* Up to 16Mbps video rate
* 12.5 FPS @1024x768, 34-42 FPS (temperature dependent) @800x600 or 640x480 and >90 FPS @400x296 or lower
* 25-50ms latency glass-to-glass, meassured very accurately on an oscilloscope at high FPS (90 FPS)
* 50-80ms at medium FPS
* Air unit weighs in at 18g with camera, antenna and a 3D printed case.

The ESP wifi is fully configurable (rate, power).
The GS wifi cards have to be among the ones supported by wifibroadcast projects (EZ-wifibroadcast and the like) but the 2.4 GHz band only.
The system uses channel 11 at the moment (hardcoded in the air unit firmware).

The ESP camera component was modified to send the data as it's received from the DMA instead of frame-by-frame basis. This decreases latency quite significantly (10-20 ms) and reduces the need to allocate full frames in PSRAM.
The CPU usage on the ESP32 is quite low right now - around 25% per core with most of it being used for the FEC encoding and WIFI TX task.
So there is still room to add quite a lot of processing - including saving the video to a SD card.

The link is bi-directional so the RPI can send data to the esp air unit. ATM it can send camera and wifi configuration data but I plan to have a full bi-directional serial port for telemetry coming soon.
The back link uses 64byte packets with a 2/6 FEC encoding (so quite solid) at a low wifi rate (I think 1Mb).

For the base station I'm using a raspberry 4 in a RasPad3 tablet form for ease of use. The base station software supports both X11 and Dispmax.
Raspberry PI 3 should work, but Zero is too slow and it will increase latency quite a lot (but still usable).

This is very WIP at the moment, but it proves that the ESP32 can definitely stream video with very low latency. 
I plan to use this is a long range micro quad.
