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

### Second board: round 2.8" touch (in progress)

A Waveshare ESP32-S3-Touch-LCD-2.8C (480x480 round IPS, ST7701 on parallel RGB, GT911 touch) is planned alongside the 1.47" stick.
Board pins live in `include/board_stick147.h` (default) and `include/board_round28.h`; PlatformIO env `waveshare_s3_round28` selects the round one.
Scaffold only: the round build stops with an `#error` until the display and touch drivers exist. Still needed: the 16 RGB data pins,
panel timings and ST7701 init sequence from Waveshare's demo. The stick build is unchanged.

## What it does

- Scans for the adapter by name, connects over BLE, sets it up
  (`ATZ ATE0 ATL0 ATH0 ATS0 ATSP0`), reconnects automatically if the link drops.
- Polls back-to-back with separate requests for speed (`010D`) and RPM (`010C`) every cycle, and coolant (`0105`) / oil temp (`015C`) alternately every 6th cycle (about 5-6 cycles/s on the Mazda 3). Tested on the car and found NOT to help, so they are off: the combined `010C0D` request (`USE_COMBINED_PID`), the engine-only filter `ATCRA7E8` (`USE_ENGINE_FILTER`, not accepted) and the trailing-`1` reply count (`USE_COUNT_SUFFIX`, made requests much slower). Two modules answer every request (replies come back twice, e.g. `410D00410D00`); the first one is used.
- **Animated character (Face page, `src/mochi.h`):** Mochi-style glowing eyes that morph between 27 expressions
  (see `docs/preview/` for pictures of all of them), a welcome at power-on, the "headlights drive-in": two lamps appear far away, a car drives in and brakes (its plate reads TACHI),
  the lamps flash and become the eyes, the eyes turn into happy arches while a glowing hand-drawn "TACHI" writes itself on, then the
  normal face returns (about 5.4 s; the RGB LED follows the headlights). After a quick restart (the car's USB
  power can dip while cranking) a short 0.9 s version plays instead; `INTRO_FORCE` in `config.h` forces full or short. The Debug page
  shows `boot: power-on|restart rN` so you can see which kind the board saw,
  random idle behaviour (looks around, smirks, squints, sneezes, ...; Police/Headlights/Glitch only while parked),
  and moods that follow the car (below). It runs in its own task so Bluetooth polling cannot stall it. The Debug page
  shows `face: NN fps` and the car polling rate while the Face page is shown (`req:`). Long press (demo mode) replays the
  welcome and then shows every expression with its name, one every ~3 s. `MOCHI_FACE 0` in `config.h` brings back the old face.
  Mood -> expression: happy = default, uneasy = sweat drop, worried = angry, sleepy = sleepy, excited = turbo eyes, calm = squint.
- Face moods, checked in this order (thresholds in `config.h`):
  sleepy (no valid data for 5 s, e.g. engine off), worried (coolant >= 108 C), uneasy (>= 100 C),
  excited (RPM >= 3500, or speed rising >= 8 mph/s: big sparkly eyes, open grin, magenta LED),
  calm (cruising >= 55 mph with RPM < 2600: half-closed eyes, small cyan smile, teal LED),
  happy (everything else). Eyes blink.
- RGB LED color follows the mood.
- Separate pages, cycled by a short press of the BOOT button through the *visible* pages: Face, MPH, RPM, Coolant, then (only if the
  car answers them) Battery, Intake air, Outside air, Fuel, Throttle, Engine load, Oil temp (only if it ever answered), and Debug last.
  At connect the board reads which standard values the engine computer supports (bitmaps 0100/0120/0140/0160, shown as `sup:` on the Debug page).
  Battery uses the adapter's own voltage reading (`ATRV`, works with the engine off), with the car's control-module voltage as fallback.
  Hide any page with the `SHOW_PAGE_*` switches in `config.h`. The face page shows only the character; the LED shows the mood on every page.
- Temperatures are shown in deg F (`TEMP_UNIT_F` in `config.h`, set 0 for deg C). Mood thresholds stay in deg C.
- Long press (~1.5 s) toggles demo mode. One summary line per second goes to the serial monitor.
- Status screens: scanning, connecting, not found, link lost.
- Serial log (115200) of every command and raw reply, and of every BLE device seen
  during scans.

- microSD logging: with a card in the TF slot, writes `tachi_000.csv`, `tachi_001.csv`, ... (one file per boot)
  with `ms,rpm,mph,coolant_f,oil_f,batt_v,intake_f,ambient_f,fuel_pct,throttle_pct,load_pct` (deg C if `TEMP_UNIT_F` is 0; blank when unknown) once per second while data is valid. No card = no logging, no error. Use a
  FAT32 card. SD pins (CLK 14, CMD 15, D0 16, 1-bit mode) come from the same unconfirmed pin map.

Not included yet: sound, motion sensing, Wi-Fi.

### Rally cluster page
Page 2 (after Face) is a landscape digital rally cluster modelled on a reference dash: big estimated gear, speed, RPM, shift lights,
a curved RPM sweep, tiles for load / throttle / run time / coolant / intake / ambient / fuel / battery, and a LIVE footer.
The panel is turned 90 degrees while this page is shown (`CLUSTER_ROTATION` 1 or 3 picks the direction), so mount the stick sideways.
Text uses anti-aliased Krona One glyph bitmaps (`src/cluster_font.h`, regenerate with `tools/fontgen/fontgen.py`, SIL OFL font);
the gear is estimated from RPM/speed (ratios in `cluster.h`, tune from a drive log). Preview: `tools/preview/cluster_preview.cpp`, picture in `docs/preview/cluster_mockup.png`.

## Files

- `platformio.ini`: board, platform and libraries (NimBLE-Arduino 2.x, GFX Library for Arduino, Adafruit NeoPixel).
- `include/config.h`: adapter name and UUIDs, mood thresholds, timing, and all hardware pins.
- `src/main.cpp`: everything else.

## Build and upload
After pulling new code run `pio run -t clean` once before `pio run -t upload`. The Debug page shows the build time and a feature tag in its top right corner (also printed on Serial at boot), so a photo proves which firmware is on the board.


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
