#pragma once
// Waveshare ESP32-S3-Touch-LCD-2.8C: 2.8" round IPS 480x480, ST7701 over parallel RGB, GT911 touch.
// Pins from Waveshare's hardware-connection table. EXIOn = pin n of the on-board TCA9554 I/O expander (I2C), not a GPIO.
// Display driver code for this board is NOT written yet; the RGB data pins below are still missing.

#define LCD_W 480
#define LCD_H 480

// Panel control
#define PIN_LCD_BL    6
#define PIN_LCD_PCLK  41
#define PIN_LCD_DE    40
#define PIN_LCD_VSYNC 39
#define PIN_LCD_HSYNC 38
#define PIN_LCD_SPI_SDA 1    // ST7701 3-wire init bus
#define PIN_LCD_SPI_SCL 2
#define EXIO_LCD_RST  1
#define EXIO_LCD_CS   3
// TODO: 16 RGB data pins (R0-R4, G0-G5, B0-B4), HSYNC/VSYNC/DE/PCLK porch timings, ST7701 init sequence
//       (from Waveshare demo, not yet available).

// Shared I2C bus: TCA9554 expander, GT911 touch, QMI8658 IMU, PCF85063 RTC (header pins SCL 7 / SDA 15)
#define PIN_I2C_SDA   15
#define PIN_I2C_SCL   7
#define PIN_TOUCH_INT 16
#define EXIO_TOUCH_RST 2

#define PIN_BUTTON    0     // BOOT button
#define PIN_LED       -1    // no RGB LED on this board; LED code must be skipped

// TF slot pins not confirmed yet.
#define SD_LOG_ENABLED   0
#define SD_LOG_INTERVAL_MS 1000
