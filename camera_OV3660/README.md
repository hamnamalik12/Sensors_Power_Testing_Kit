# XIAO ESP32-S3 Camera Test Firmware

This repository contains a set of test builds exercising the onboard camera
(OV3660/OV2640-class sensor) on a **Seeed XIAO ESP32-S3 Sense**, comparing
serial-only capture against Wi-Fi MJPEG streaming, different frame sizes,
and different init/deinit strategies between captures. Current draw for
each variant was measured on a **Nordic Semiconductor Power Profiler Kit II
(PPK2)**, powering the board directly from the PPK2 supply rail.

## Hardware / Pin Map

| Signal      | GPIO | Signal      | GPIO |
|-------------|------|-------------|------|
| PWDN        | -1 (unused) | RESET       | -1 (unused) |
| XCLK        | 10   | PCLK        | 13   |
| SIOD (SDA)  | 40   | SIOC (SCL)  | 39   |
| Y9 (D7)     | 48   | Y8 (D6)     | 11   |
| Y7 (D5)     | 12   | Y6 (D4)     | 14   |
| Y5 (D3)     | 16   | Y4 (D2)     | 18   |
| Y3 (D1)     | 17   | Y2 (D0)     | 15   |
| VSYNC       | 38   | HREF        | 47   |

Common camera config across all variants unless noted otherwise: XCLK at
20 MHz, hardware JPEG encode, 2 frame buffers in PSRAM,
`CAMERA_GRAB_WHEN_EMPTY` grab mode.

## Power Measurement Method

- Instrument: **Nordic PPK2**, source/ammeter mode, board powered directly
  from the PPK2 supply rail.
- Values below are taken from the in-code comments left after each bench
  run — steady-state figures for that specific test, not datasheet numbers.
- "Serial only" variants have no Wi-Fi radio active; the streaming variants
  do, so their current includes both the camera and the Wi-Fi TX/RX load.

## Test File Summary

| File       | Frame Size | Transport            | Capture Behavior                                                                 | Measured Current (PPK2) |
|------------|------------|------------------------|-----------------------------------------------------------------------------------|---------------------------|
| `TEST1.c` | 640×480 (VGA) | Serial monitor only, no Wi-Fi | Full **init → capture one frame → deinit** cycle every loop, with a 5 s delay between cycles. Camera driver and buffers are torn down and freed between captures | **115 mA** (during the active init/capture window) |
| `TEST2.c` | 640×480 (VGA) | Serial monitor only, no Wi-Fi | Camera initialized **once** at boot and left running; captures a new frame every 5 s without deinitializing in between | Not recorded in comments |
| `TEST3.c` | 640×480 (VGA) | Wi-Fi MJPEG stream (`/stream`) + single-shot `/capture` endpoint on port 81 | Camera initialized once, connects to Wi-Fi, then serves an HTTP multipart stream that pushes one new frame every 5 s (plus an on-demand single-JPEG endpoint) | Not recorded in comments |
| `TEST4.c` | 240×240 | Wi-Fi MJPEG stream (`/stream`) on port 81 | Camera initialized once, connects to Wi-Fi, then streams frames continuously as fast as they're captured (no 5 s throttling) | **220 mA** |

## Key Takeaways from the PPK2 Measurements

- **Wi-Fi streaming costs far more than serial-only capture**: `TEST4.c`'s
  continuous Wi-Fi stream drew **220 mA**, roughly double `TEST1.c`'s
  **115 mA** serial-only init/capture/deinit cycle — even though `TEST4.c`
  uses a *smaller* frame size (240×240 vs. 640×480). The Wi-Fi radio, not
  the sensor or JPEG resolution, is the dominant load once streaming is
  continuous and unthrottled.
- **Deinitializing between captures (`TEST1.c`) adds re-init overhead but
  isn't a low-power idle state**: unlike the VL53L8CX's `LPn`-controlled
  hard shutdown, `esp_camera_deinit()` frees driver state and buffers but
  does not fully power down the sensor rail, so this pattern trades a
  small amount of average current for slower wake-up (driver
  re-initialization) on every cycle rather than a true low-power sleep.
- **Throttling the capture rate (5 s interval in `TEST2.c`/`TEST3.c`) matters
  most for streaming, not for the sensor itself**: capturing a frame is a
  short, bursty operation, so the main effect of the 5 s interval in the
  streaming case (`TEST3.c`) is to reduce how often the Wi-Fi radio has to
  transmit — `TEST4.c` removes that throttle entirely and pushes frames as
  fast as possible, which lines up with it being the highest-current
  variant measured.

## Suggested Use

- Use `TEST1.c` when only occasional serial-logged snapshots are needed and
  power matters more than capture latency — it pays a re-init cost each
  cycle but avoids keeping Wi-Fi or the camera driver active continuously.
- Use `TEST2.c` for periodic serial-only captures where the extra ~5 s
  re-init overhead of `TEST1.c` isn't worth it and the camera driver can
  stay resident.
- Use `TEST3.c` when a live preview or periodic remote snapshot over Wi-Fi
  is needed but frame rate can be throttled to save power.
- Use `TEST4.c` only when a smooth, continuous live video feed is required
  and the **220 mA** budget is acceptable (e.g., mains or large battery).