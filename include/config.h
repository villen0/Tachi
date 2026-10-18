#pragma once
// ===== User settings =====
#define FW_TAG "cluster"   // short feature tag shown with the build time on the Debug page (proves which firmware is flashed)
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
#define CLUSTER_ROTATION   1      // 1 or 3: which way the picture is turned (swap if it shows upside down)
#define CLUSTER_FPS        24
#define RPM_REDLINE        6500   // shift lights and the red zone of the sweep

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

// ===== Hardware pins =====
// LCD pins and RGB LED match the espp ws-s3-lcd-1-47 pin map for this board.
// The BOOT button is the standard ESP32-S3 strapping pin. Not yet confirmed on hardware.
#define LCD_W 172
#define LCD_H 320
#define PIN_LCD_MOSI 45
#define PIN_LCD_SCLK 40
#define PIN_LCD_CS   42
#define PIN_LCD_DC   41
#define PIN_LCD_RST  39
#define PIN_LCD_BL   48
#define PIN_LED      38   // onboard RGB LED (WS2812-style)
#define PIN_BUTTON    0   // BOOT button: short press cycles readout, long press toggles demo

// ===== microSD logging (TF slot, SD_MMC 1-bit mode) =====
// Pins from the espp ws-s3-lcd-1-47 pin map (D0 16, D1 18, D2 17, D3 21, CLK 14, CMD 15). Unconfirmed.
// Logging is optional: with no card inserted the pet runs normally.
#define SD_LOG_ENABLED   1
#define SD_LOG_INTERVAL_MS 1000   // one CSV row per second while valid data is coming in
#define PIN_SD_CLK  14
#define PIN_SD_CMD  15
#define PIN_SD_D0   16
