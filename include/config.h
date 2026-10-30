#pragma once
// ===== User settings =====
#define FW_TAG "cluster2"   // short feature tag shown with the build time on the Debug page (proves which firmware is flashed)
// Part of the BLE adapter's name (check with the nRF Connect app or the scan log).
#define ADAPTER_NAME_SUBSTR "CodeReader"   // seen in nRF Connect as "CodeReader31B"

// BLE ELM327 UUIDs, verified from a phone screenshot of the adapter.
#define SERVICE_UUID_STR "FFF0"
#define RX_UUID_STR      "FFF1"   // adapter -> board (notify)
#define TX_UUID_STR      "FFF2"   // board -> adapter (write)

// Temperature display unit: 1 = deg F, 0 = deg C. (Mood thresholds below stay in deg C.)
#define TEMP_UNIT_F 1

// Mood thresholds (coolant, deg C)
#define TEMP_UNEASY_C   100
#define TEMP_WORRIED_C  108

// Excited: revs at/above RPM_EXCITED_ON (until they drop below _OFF), or speed rising this fast (mph per second).
#define RPM_EXCITED_ON         3500
#define RPM_EXCITED_OFF        3000
#define ACCEL_EXCITED_MPH_PER_S   8
// Calm: relaxed cruising at highway speed with low revs.
#define CRUISE_MPH_ON   55
#define CRUISE_MPH_OFF  45
#define CRUISE_RPM_MAX  2600

// Demo mode: fake RPM / speed / coolant, no Bluetooth. Lets you test the face at a desk.
// DEMO_MODE_DEFAULT is the state at boot. Hold the BOOT button ~1.5 s to toggle at runtime.
#define DEMO_MODE_DEFAULT  0
#define LONG_PRESS_MS      1500

// Character (Mochi-style animated eyes on the Face page, see docs/mochi-plan.md)
#define MOCHI_FACE   1        // 1 = animated character, 0 = the old simple face
#define MOCHI_INTRO  1        // play the welcome animation at power-on
#define INTRO_FORCE  0        // welcome type: 0 = auto (full on fresh power-up, short after a restart), 1 = always full, 2 = always short
#define UI_FPS       30       // target frame rate of the Face page
#define LCD_SPI_HZ   40000000 // panel SPI clock (ST7789 handles up to ~60 MHz; lower it if the picture shows noise)

// Rally cluster page (landscape: the panel is turned sideways while this page is shown, see docs/preview/cluster_mockup.png)
#define SHOW_PAGE_CLUSTER  1
#define SHOW_PAGE_DIALS    1   // round board only: analog tachometer, analog speedometer and the vitals rings
#define SHOW_PAGE_BRIGHTNESS 1 // round board only: the brightness slider page (the level is saved across restarts)
#define DIAL_FPS          30   // target frame rate of the round board's dial pages
#define LED_BRIGHTNESS     0   // mood LED on the 1.47" stick: 0 = always off, 60 = the old default, 255 = full (the round board has no LED)
#define SWIPE_NEXT_ON_RIGHT 1  // round board: 1 = swipe right goes to the next page, 0 = swipe left goes to the next page
#define CLUSTER_ROTATION   1      // 1 or 3: which way the picture is turned (swap if it shows upside down)
#define CLUSTER_FPS        24
#define RPM_REDLINE        6500   // shift lights and the red zone of the sweep

// One-off reactions of the face on the Face page: engine start (sparkle), hard braking (startle), low battery / low fuel (crying).
#define CAR_EVENTS            1
#define EVENT_ENGINE_OFF_MS   3000   // engine must have been off (RPM under 400, data fresh) this long before a start counts
#define EVENT_BRAKE_MIN_MPH   15     // hard braking: speed before the stop ...
#define EVENT_BRAKE_DROP_MPH  10     // ... and the drop within one second
#define EVENT_FUEL_LOW_PCT    7      // low fuel reaction below this percent

// Extra car-data pages: shown only if the car answers them (discovered at connect). Set 0 to hide a page.
#define SHOW_PAGE_BATTERY   1
#define SHOW_PAGE_INTAKE    1   // intake air temperature
#define SHOW_PAGE_OUTSIDE   1   // outside air temperature
#define SHOW_PAGE_FUEL      1
#define SHOW_PAGE_THROTTLE  1
#define SHOW_PAGE_LOAD      1   // calculated engine load
// Battery colours: engine off (RPM ~0) yellow below BATT_LOW_OFF_V; engine running yellow below BATT_LOW_RUN_V; red below BATT_CRIT_V
#define BATT_LOW_OFF_V   12.0f
#define BATT_LOW_RUN_V   13.2f
#define BATT_CRIT_V      11.5f
#define FUEL_LOW_PCT     15
#define GAUGE_FRESH_MS   40000   // an extra gauge older than this is shown as --

// Timing
#define SLOW_EVERY         4      // one background poll (coolant / oil / extra gauges) every N speed+RPM polls
#define STALE_AFTER_MS     5000   // no valid data this long -> sleepy
#define FACE_SETTLE_MS     10000  // after the welcome the face stays default this long (no mood change, no idle reaction)
#define BOOT_SLEEP_GRACE_MS 25000 // after power-up the face stays awake this long while waiting for the first data
#define RECONNECT_WAIT_MS  3000
#define CMD_TIMEOUT_MS     1000   // wait for one OBD reply (the UI keeps animating meanwhile)
#define INIT_TIMEOUT_MS    3000   // adapter setup commands (ATZ is slow)
#define SCAN_TIME_MS       5000

// ===== Board selection =====
// Default is the 1.47" stick. platformio.ini env waveshare_s3_round28 defines BOARD_ROUND28.
#if defined(BOARD_ROUND28)
  #include "board_round28.h"
#else
  #include "board_stick147.h"
#endif
