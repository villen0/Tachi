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
- Polls back-to-back with separate requests for speed (`010D`) and RPM (`010C`) every cycle, and coolant (`0105`) / oil temp (`015C`) alternately every 6th cycle. On the Mazda two modules answer each request (the reply comes back twice) and the adapter waits a fixed time for more replies, so at connect time the firmware tries `ATCRA7E8` (only the engine computer) and a trailing `1` on requests (expect one reply) and keeps whichever the adapter accepts (shown as `f:` and `c:` on the Debug page). The combined `010C0D` request did not work on this car and is off (`USE_COMBINED_PID`).
- Face moods, checked in this order (thresholds in `config.h`):
  sleepy (no valid data for 5 s, e.g. engine off), worried (coolant >= 108 C), uneasy (>= 100 C),
  excited (RPM >= 3500, or speed rising >= 8 mph/s: big sparkly eyes, open grin, magenta LED),
  calm (cruising >= 55 mph with RPM < 2600: half-closed eyes, small cyan smile, teal LED),
  happy (everything else). Eyes blink.
- RGB LED color follows the mood.
- Separate pages, cycled by a short press of the BOOT button: Face > MPH > RPM > Coolant > Oil temp > Debug > Face.
  The Debug page shows link state, request rate, value ages and the last raw adapter replies, so problems can be diagnosed in the car without a computer.
  The face page shows only the face; each data page is one big number. The LED shows the mood on every page.
  Oil temp uses PID 015C, which many cars (possibly the Mazda 3) do not answer; the page then shows `--`,
  and after 5 failures the board only retries occasionally so other readings are not slowed.
- Long press (~1.5 s) toggles demo mode. One summary line per second goes to the serial monitor.
- Status screens: scanning, connecting, not found, link lost.
- Serial log (115200) of every command and raw reply, and of every BLE device seen
  during scans.

- microSD logging: with a card in the TF slot, writes `tachi_000.csv`, `tachi_001.csv`, ... (one file per boot)
  with `ms,rpm,mph,coolant_c,oil_c` once per second while data is valid. No card = no logging, no error. Use a
  FAT32 card. SD pins (CLK 14, CMD 15, D0 16, 1-bit mode) come from the same unconfirmed pin map.

Not included yet: sound, motion sensing, Wi-Fi.

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

**Build status:** compiles and links with PlatformIO (NimBLE-Arduino 2.3.7, GFX Library for Arduino 1.6.9,
Arduino-ESP32 3.3.12). It has not been run on hardware yet. GFX Library 1.6.0 does NOT build against this
core, hence the `^1.6.5` pin.

**Unverified, check these first:**
1. **Never run on the board.** First boot may show display problems (see 2).
2. **Display driver and pins.** `main.cpp` assumes an ST7789 172x320 panel with
   column offset 34, and `config.h` pins (MOSI 45, SCLK 40, CS 42, DC 41, RST 39, BL 48)
   come from the espp ws-s3-lcd-1-47 pin map, not Waveshare's own page. Take the real driver, pins, offsets and rotation from Waveshare's
   demo code / wiki for this exact board before anything else. A blank screen means this.
3. **RGB LED pin** (`PIN_LED` = 38) and **BOOT button pin** (`PIN_BUTTON` = 0) are guesses.
4. **Mazda 3 PID support.** Standard Mode 01 PIDs are assumed to work with `ATSP0`
   auto-protocol on a 2025 Mazda 3. Not tested. Watch the serial log for `NO DATA` or `UNABLE TO CONNECT`.
5. The adapter may need a pairing/bond. If the BLE connect fails in the car, check this
   (and that no phone app is connected to the adapter).

## Known issues / ideas for next steps

- (Fixed) receive buffer is now lock-protected; the UI keeps blinking and reading the button while waiting
  for the adapter; blinks redraw only the eyes. Scanning (5 s) still blocks the UI.
- If the adapter lacks a prompt `>` after some replies, commands will time out.
- Mood thresholds are for coolant in deg C and may need tuning for the Mazda.
- Demo mode: set `DEMO_MODE_DEFAULT` to 1 in `config.h`, or hold BOOT ~1.5 s to toggle at runtime.
  Fake data sweeps through every mood, no Bluetooth needed. Use it first to check the screen, LED and button.
- Possible additions: sound (if the board has a buzzer or add an I2S amp), IMU reactions,
  logging to microSD, more PIDs (throttle, fuel trim), simulated-data demo mode for
  testing the face without a car.

## Hardware notes for wiring

- Power only needs 5 V from USB. Operating temperature range of the board is unchecked;
  avoid leaving it on a hot dash.
