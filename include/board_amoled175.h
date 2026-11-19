#pragma once
// Waveshare ESP32-S3-Touch-AMOLED-1.75: 1.75" round AMOLED 466x466 (CO5300 on QSPI), CST9217 touch, QMI8658 IMU, AXP2101 power chip,
// TCA9554 expander, ES8311 / ES7210 audio, TF slot, 16 MB flash, 8 MB PSRAM (ESP32-S3R8).
// Pins and addresses are from Waveshare's own hardware reference and Arduino demo (repo waveshareteam/ESP32-S3-Touch-AMOLED-1.75:
// HARDWARE_REFERENCE.md, examples/arduino/libraries/Mylibrary/pin_config.h). NOT yet tried on a real board: see README "AMOLED 1.75 board".
#define BOARD_NAME "AMOLED 1.75"

// The UI is drawn into a 480x480 frame buffer (every page is laid out for 480). The panel is 466x466, so the middle 466x466 of that
// buffer is shown: 7 px are cropped on every side. Nothing important sits that close to the edge (the outer bezel ring is the only thing lost).
#define LCD_W 480
#define LCD_H 480
#define PANEL_W 466
#define PANEL_H 466
#define PANEL_CROP_X ((LCD_W - PANEL_W) / 2)
#define PANEL_CROP_Y ((LCD_H - PANEL_H) / 2)

// QSPI display bus (CO5300). The panel has no backlight pin: brightness is a panel command.
#define PIN_LCD_CS    12
#define PIN_LCD_SCLK  38
#define PIN_LCD_D0    4
#define PIN_LCD_D1    5
#define PIN_LCD_D2    6
#define PIN_LCD_D3    7
#define PIN_LCD_RST   39
#define PIN_LCD_TE    13         // tearing-effect output of the panel (not used yet)

// Shared I2C bus: CST9217 touch (0x5A), QMI8658 IMU (0x6B), AXP2101 (0x34), TCA9554 (0x20), PCF85063 RTC (0x51), ES8311 (0x18), ES7210 (0x40)
#define PIN_I2C_SDA   15
#define PIN_I2C_SCL   14
#define PIN_TOUCH_INT 11
#define PIN_TOUCH_RST 40

// The touch controller's own axes versus the picture (measured: X matches, Y is upside down); the bring-up sketch (env waveshare_s3_amoled175_test)
// prints the raw and mapped coordinates so a wrong axis shows up at once. Set these to 1 if a swipe or a tap lands mirrored or rotated.
#define TOUCH_SWAP_XY 0
#define TOUCH_FLIP_X  0
#define TOUCH_FLIP_Y  1          // measured on the board: the CST9217's Y axis runs bottom to top (a touch on the top cross read about 416, on the bottom cross about 6)

#define PIN_BUTTON    0          // BOOT button (the PWR button is wired to the power chip, not to a GPIO)

// TF slot (native SDMMC, 1-bit)
#define SD_LOG_ENABLED   0
#define SD_LOG_INTERVAL_MS 1000
#define PIN_SD_CLK  2
#define PIN_SD_CMD  1
#define PIN_SD_D0   3

// UI layout of the old Tachi face page (hidden in this firmware, kept so the code compiles)
#define UI_LABEL_X0   100
#define UI_LABEL_Y    424
#define UI_LABEL_W    280
#define UI_LABEL_H    32
#define UI_LABEL_SIZE 3
#define UI_LABEL_TY   428
#define FACE_SCALE    1.6f

// How the main firmware draws (see display_*.h): the canvas has no output panel here, frames are pushed with panelPush() / displayFlush().
#define CANVAS_OUTPUT nullptr
