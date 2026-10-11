#pragma once
// ===== User settings =====
// Part of the BLE adapter's name (check with the nRF Connect app or the scan log).
#define ADAPTER_NAME_SUBSTR "CodeReader"   // seen in nRF Connect as "CodeReader31B"

// Typical BLE ELM327 UUIDs. VERIFY against your adapter (nRF Connect).
#define SERVICE_UUID_STR "FFF0"
#define RX_UUID_STR      "FFF1"   // adapter -> board (notify)
#define TX_UUID_STR      "FFF2"   // board -> adapter (write)

// Mood thresholds (coolant, deg C)
#define TEMP_UNEASY_C   100
#define TEMP_WORRIED_C  108

// Timing
#define POLL_INTERVAL_MS   250
#define STALE_AFTER_MS     5000   // no valid data this long -> sleepy
#define RECONNECT_WAIT_MS  3000

// ===== Hardware pins: UNVERIFIED, confirm against Waveshare's demo for this board =====
#define LCD_W 172
#define LCD_H 320
#define PIN_LCD_MOSI 45
#define PIN_LCD_SCLK 40
#define PIN_LCD_CS   42
#define PIN_LCD_DC   41
#define PIN_LCD_RST  39
#define PIN_LCD_BL   46
#define PIN_LED      38   // onboard RGB LED (WS2812-style), UNVERIFIED
#define PIN_BUTTON    0   // BOOT button, cycles readout pages
