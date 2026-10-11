# Tachi (Waveshare OBD Pet)

A small animated desk/dash pet that shows live car data from a 2025 Mazda 3.
An ESP32-S3 board reads RPM, speed and coolant temperature from a BLE OBD-II
adapter and shows a face whose mood follows the engine.

## Hardware

- **Board:** Waveshare ESP32-S3 1.47" Display Development Board
  (Micro Center SKU 967034, part ESP32-S3-LCD-1). ESP32-S3, Wi-Fi + BLE 5,
  1.47" 172x320 color LCD, RGB LED, TF card slot, 16 MB flash, 8 MB PSRAM,
  USB-A plug built into the board.
- **OBD adapter:** advertises as `CodeReader31B` (dual-mode Classic + LE).
  This firmware uses its BLE side only (the ESP32-S3 has no Bluetooth Classic).
- **Car power:** 2025 Mazda 3 USB-C port, through a USB-C to USB-A adapter cable.

## What it does

- Scans for the adapter by name, connects over BLE, sets it up
  (`ATZ ATE0 ATL0 ATH0 ATS0 ATSP0`), reconnects automatically if the link drops.
- Polls PIDs round-robin every 250 ms: `010C` RPM, `010D` speed, `0105` coolant.
- Face moods: happy (normal), uneasy (coolant >= 100 C), worried (>= 108 C),
  sleepy (no valid data for 5 s, e.g. engine off). Eyes blink.
- RGB LED color follows the mood.
- Readout under the face; the BOOT button cycles RPM / mph / coolant / hidden.
- Status screens: scanning, connecting, not found, link lost.
- Serial log (115200) of every command and raw reply, and of every BLE device seen
  during scans.

Not included yet: sound, motion sensing, microSD logging, Wi-Fi.

## Files

- `platformio.ini`: board, platform and libraries (NimBLE-Arduino 2.x, GFX Library for Arduino, Adafruit NeoPixel).
- `include/config.h`: adapter name and UUIDs, mood thresholds, timing, and all hardware pins.
- `src/main.cpp`: everything else.

## Build and upload

1. Install VS Code and the PlatformIO extension, open this folder.
2. Plug the board into the computer. If no serial port appears, hold BOOT while plugging in.
3. Click Upload. First build downloads the platform and libraries (a few minutes).
4. Open the serial monitor at 115200.

## Verified vs. unverified

**Verified (from a phone screenshot in LightBlue / nRF Connect):**
- Adapter name `CodeReader31B`; config matches on the substring `CodeReader`.
- BLE service `FFF0` (0000fff0-0000-1000-8000-00805f9b34fb).
- `FFF1`: Notify (adapter -> board). `FFF2`: Write and Write Without Response (board -> adapter).
- The adapter had been bonded to the phone before; it accepts only one connection at a time.

**Unverified, check these first:**
1. **Nothing has been compiled or run.** Expect compile errors and fix them first.
   NimBLE-Arduino is 2.x and Arduino_GFX API names may differ slightly by version.
2. **Display driver and pins.** `main.cpp` assumes an ST7789 172x320 panel with
   column offset 34, and `config.h` pins (MOSI 45, SCLK 40, CS 42, DC 41, RST 39, BL 46)
   are guesses. Take the real driver, pins, offsets and rotation from Waveshare's
   demo code / wiki for this exact board before anything else. A blank screen means this.
3. **RGB LED pin** (`PIN_LED` = 38) and **BOOT button pin** (`PIN_BUTTON` = 0) are guesses.
4. **Mazda 3 PID support.** Standard Mode 01 PIDs are assumed to work with `ATSP0`
   auto-protocol on a 2025 Mazda 3. Not tested. Watch the serial log for `NO DATA` or `UNABLE TO CONNECT`.
5. The adapter may need a pairing/bond. If the BLE connect fails in the car, check this
   (and that no phone app is connected to the adapter).

## Known issues / ideas for next steps

- `rxBuf` (a `String`) is written from the BLE callback task and read from the main task
  with no lock. Fine for a first version; use a mutex or a ring buffer.
- `sendCmd` blocks up to 1.5 s when the adapter doesn't answer, which freezes blinking.
- Face redraw clears a region and redraws; flicker may show. Consider a canvas in PSRAM.
- If the adapter lacks a prompt `>` after some replies, commands will time out.
- Mood thresholds are for coolant in deg C and may need tuning for the Mazda.
- Possible additions: sound (if the board has a buzzer or add an I2S amp), IMU reactions,
  logging to microSD, more PIDs (throttle, fuel trim), simulated-data demo mode for
  testing the face without a car.

## Hardware notes for wiring

- Power only needs 5 V from USB. Operating temperature range of the board is unchecked;
  avoid leaving it on a hot dash.
