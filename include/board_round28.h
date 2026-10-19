#pragma once
// Waveshare ESP32-S3-Touch-LCD-2.8C: 2.8" round IPS 480x480, ST7701 on parallel RGB (16-bit), GT911 touch.
// Pins and timings from Waveshare's demo (Display_ST7701.h, Touch_GT911.h, SD_Card.h). Not yet run on hardware.
// EXIOn = pin n (1-based, as in Waveshare's demo) of the on-board TCA9554 I/O expander, not a GPIO.

#define LCD_W 480
#define LCD_H 480

// RGB panel (esp_lcd order: DATA0-4 = B0-B4, DATA5-10 = G0-G5, DATA11-15 = R0-R4)
#define PIN_LCD_HSYNC 38
#define PIN_LCD_VSYNC 39
#define PIN_LCD_DE    40
#define PIN_LCD_PCLK  41
#define PIN_LCD_R0 46
#define PIN_LCD_R1 3
#define PIN_LCD_R2 8
#define PIN_LCD_R3 18
#define PIN_LCD_R4 17
#define PIN_LCD_G0 14
#define PIN_LCD_G1 13
#define PIN_LCD_G2 12
#define PIN_LCD_G3 11
#define PIN_LCD_G4 10
#define PIN_LCD_G5 9
#define PIN_LCD_B0 5
#define PIN_LCD_B1 45
#define PIN_LCD_B2 48
#define PIN_LCD_B3 47
#define PIN_LCD_B4 21
#define LCD_PCLK_HZ  (30 * 1000 * 1000)
#define LCD_HPW 8
#define LCD_HBP 10
#define LCD_HFP 50
#define LCD_VPW 2
#define LCD_VBP 18
#define LCD_VFP 8
#define LCD_BOUNCE_PX (10 * LCD_W)   // bounce buffer, avoids screen drift while PSRAM is busy

// ST7701 init bus (3-wire, 9-bit SPI bit-banged on GPIO), chip-select and reset live on the expander
#define PIN_LCD_SPI_SCL 2
#define PIN_LCD_SPI_SDA 1
#define EXIO_LCD_RST  1
#define EXIO_LCD_CS   3
#define PIN_LCD_BL    6          // backlight PWM, active high

// Shared I2C bus: TCA9554 expander, GT911 touch, QMI8658 IMU, PCF85063 RTC
#define PIN_I2C_SDA   15
#define PIN_I2C_SCL   7
#define TCA9554_ADDR  0x20       // Waveshare's TCA9554PWR.h was not available: 0x20 is the chip default, the bring-up sketch scans to confirm
#define PIN_TOUCH_INT 16
#define EXIO_TOUCH_RST 2
#define GT911_ADDR    0x5D

#define PIN_BUTTON    0          // BOOT button
#define PIN_LED       -1         // no RGB LED on this board; LED code must be skipped

// TF slot (shares GPIO 1/2 with the LCD init bus, which is idle after panel init)
#define SD_LOG_ENABLED   0       // off until the full firmware is ported
#define SD_LOG_INTERVAL_MS 1000
#define PIN_SD_CLK  2
#define PIN_SD_CMD  1
#define PIN_SD_D0   42

// UI layout (the face art is drawn 2x at the centre; dots sit at the top, the demo label at the bottom, both inside the circle)
#define UI_LABEL_X0   100
#define UI_LABEL_Y    424
#define UI_LABEL_W    280
#define UI_LABEL_H    32
#define UI_LABEL_SIZE 3
#define UI_LABEL_TY   428
#define FACE_SCALE    2
