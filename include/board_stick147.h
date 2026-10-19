#pragma once
// Waveshare ESP32-S3 1.47" Display Board (ST7789 172x320, SPI). Selected unless BOARD_ROUND28 is defined.

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
