#pragma once
// Display + touch driver for the Waveshare ESP32-S3-Touch-LCD-2.8C (ST7701 over 16-bit RGB, GT911, TCA9554 expander).
// Header-only and static: included by exactly one source file per build (main.cpp or round28_bringup.cpp).
// Pins and timings: include/board_round28.h. Not yet run on hardware.
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include "config.h"

static bool i2cStarted = false;
static bool exFound = false, dispBeginOk = false;   // status flags, printed by the bring-up sketch
static void i2cBegin() { if (!i2cStarted) { Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000); i2cStarted = true; } }

// ---- TCA9554 expander (EXIOn pins are 1-based in config; register bit = n-1) ----
static uint8_t exAddr = TCA9554_ADDR;
static uint8_t exOut = 0xFF;

static bool exWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(exAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static void exSet(uint8_t pin, bool high) {
  if (high) exOut |= (1u << (pin - 1)); else exOut &= ~(1u << (pin - 1));
  exWrite(0x01, exOut);
}
static bool exInit() {
  // Find the expander: configured address first, then the rest of the TCA9554 / PCA9554 range.
  const uint8_t tryAddr[] = {TCA9554_ADDR, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
  for (uint8_t a : tryAddr) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { exAddr = a; break; }
    if (a == 0x3F) return false;
  }
  Serial.printf("expander found at 0x%02X\n", exAddr);
  // Only the three pins we know are driven (LCD reset, touch reset, LCD chip-select; all start high). The other five
  // (the buzzer is one of them) stay inputs, exactly as at power-up, so nothing else on the board gets switched on.
  const uint8_t mine = (1u << (EXIO_LCD_RST - 1)) | (1u << (EXIO_TOUCH_RST - 1)) | (1u << (EXIO_LCD_CS - 1));
  exOut = mine;
  exWrite(0x01, exOut);            // output register first, so the pins start high
  exWrite(0x03, (uint8_t)~mine);   // configuration: 0 = output, 1 = input
  exFound = true;
  return true;
}

// ---- ST7701 init sequence (from Waveshare Display_ST7701.cpp, 2.8 inch) ----
static const uint8_t st7701_init[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF, WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
    WRITE_C8_D8, 0xEF, 0x08,
    WRITE_COMMAND_8, 0xFF, WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x10, 0x0C,
    WRITE_C8_D16, 0xC2, 0x07, 0x0A,
    WRITE_C8_D8, 0xC7, 0x00,
    WRITE_C8_D8, 0xCC, 0x10,
    WRITE_C8_D8, 0xCD, 0x08,
    WRITE_COMMAND_8, 0xB0, WRITE_BYTES, 16,
    0x05, 0x12, 0x98, 0x0E, 0x0F, 0x07, 0x07, 0x09, 0x09, 0x23, 0x05, 0x52, 0x0F, 0x67, 0x2C, 0x11,
    WRITE_COMMAND_8, 0xB1, WRITE_BYTES, 16,
    0x0B, 0x11, 0x97, 0x0C, 0x12, 0x06, 0x06, 0x08, 0x08, 0x22, 0x03, 0x51, 0x11, 0x66, 0x2B, 0x0F,
    WRITE_COMMAND_8, 0xFF, WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
    WRITE_C8_D8, 0xB0, 0x5D,
    WRITE_C8_D8, 0xB1, 0x3E,
    WRITE_C8_D8, 0xB2, 0x81,
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x4E,
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x20,
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,
    WRITE_C8_D8, 0xD0, 0x88,
    WRITE_COMMAND_8, 0xE0, WRITE_BYTES, 3, 0x00, 0x00, 0x02,
    WRITE_COMMAND_8, 0xE1, WRITE_BYTES, 11, 0x06, 0x30, 0x08, 0x30, 0x05, 0x30, 0x07, 0x30, 0x00, 0x33, 0x33,
    WRITE_COMMAND_8, 0xE2, WRITE_BYTES, 12, 0x11, 0x11, 0x33, 0x33, 0xF4, 0x00, 0x00, 0x00, 0xF4, 0x00, 0x00, 0x00,
    WRITE_COMMAND_8, 0xE3, WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,
    WRITE_C8_D16, 0xE4, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 16,
    0x0D, 0xF5, 0x30, 0xF0, 0x0F, 0xF7, 0x30, 0xF0, 0x09, 0xF1, 0x30, 0xF0, 0x0B, 0xF3, 0x30, 0xF0,
    WRITE_COMMAND_8, 0xE6, WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,
    WRITE_C8_D16, 0xE7, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE8, WRITE_BYTES, 16,
    0x0C, 0xF4, 0x30, 0xF0, 0x0E, 0xF6, 0x30, 0xF0, 0x08, 0xF0, 0x30, 0xF0, 0x0A, 0xF2, 0x30, 0xF0,
    WRITE_C8_D16, 0xE9, 0x36, 0x01,
    WRITE_COMMAND_8, 0xEB, WRITE_BYTES, 7, 0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40,
    WRITE_COMMAND_8, 0xED, WRITE_BYTES, 16,
    0xFF, 0x10, 0xAF, 0x76, 0x54, 0x2B, 0xCF, 0xFF, 0xFF, 0xFC, 0xB2, 0x45, 0x67, 0xFA, 0x01, 0xFF,
    WRITE_COMMAND_8, 0xEF, WRITE_BYTES, 6, 0x08, 0x08, 0x08, 0x45, 0x3F, 0x54,
    WRITE_COMMAND_8, 0xFF, WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_C8_D8, 0x3A, 0x66,   // pixel format used with the RGB interface (as in Waveshare's demo)
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_COMMAND_8, 0x29,     // display on
    END_WRITE};

static Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED /*9-bit mode*/, GFX_NOT_DEFINED /*CS on expander*/,
                                                PIN_LCD_SPI_SCL, PIN_LCD_SPI_SDA, GFX_NOT_DEFINED);
static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    PIN_LCD_DE, PIN_LCD_VSYNC, PIN_LCD_HSYNC, PIN_LCD_PCLK,
    PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
    PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
    PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
    1 /*hsync_polarity*/, LCD_HFP, LCD_HPW, LCD_HBP,
    1 /*vsync_polarity*/, LCD_VFP, LCD_VPW, LCD_VBP,
    0 /*pclk_active_neg*/, LCD_PCLK_HZ, false /*big endian*/, 0, 0, LCD_BOUNCE_PX);
static Arduino_RGB_Display *rgbDisp = new Arduino_RGB_Display(
    LCD_W, LCD_H, rgbpanel, 0 /*rotation*/, true /*auto flush*/,
    bus, GFX_NOT_DEFINED /*reset is on the expander*/, st7701_init, sizeof(st7701_init));

// ---- GT911 touch ----
static bool touchOk = false;

static bool gtRead(uint16_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)GT911_ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static void gtWrite8(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(v);
  Wire.endTransmission();
}
static void touchInit() {
  // INT low while reset is released selects address 0x5D.
  pinMode(PIN_TOUCH_INT, OUTPUT);
  digitalWrite(PIN_TOUCH_INT, LOW);
  exSet(EXIO_TOUCH_RST, false);
  delay(10);
  exSet(EXIO_TOUCH_RST, true);
  delay(10);
  pinMode(PIN_TOUCH_INT, INPUT);
  delay(60);
  uint8_t id[4] = {0};
  touchOk = gtRead(0x8140, id, 4);
  Serial.printf("GT911 %s, product id %c%c%c%c\n", touchOk ? "found" : "NOT found", id[0], id[1], id[2], id[3]);
}
// Returns true when the controller had a fresh report. *down = a finger is on the glass (then *x, *y are valid).
static bool touchPoll(bool *down, int *x, int *y) {
  if (!touchOk) return false;
  uint8_t st;
  if (!gtRead(0x814E, &st, 1)) return false;
  if (!(st & 0x80)) return false;                       // no new report yet
  *down = (st & 0x0F) > 0;
  if (*down) {
    uint8_t p[4];
    if (gtRead(0x8150, p, 4)) { *x = p[0] | (p[1] << 8); *y = p[2] | (p[3] << 8); }
    else *down = false;
  }
  gtWrite8(0x814E, 0);
  return true;
}

// Whole bring-up of the round board: I2C, expander, panel reset, ST7701 init, RGB panel, backlight.
static bool roundDisplayInit() {
  i2cBegin();
  if (!exInit()) Serial.println("ERROR: TCA9554 expander not found, display reset/CS cannot be controlled");
  exSet(EXIO_LCD_RST, false); delay(10);          // panel reset pulse, then chip-select low for the whole init (as in Waveshare's demo)
  exSet(EXIO_LCD_RST, true);  delay(50);
  exSet(EXIO_LCD_CS, false);  delay(10);
  bool ok = rgbDisp->begin();
  exSet(EXIO_LCD_CS, true);
  dispBeginOk = ok;
  Serial.printf("display begin: %s, PSRAM %u bytes (free %u)\n", ok ? "ok" : "FAILED", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
  if (!ok) Serial.println("ERROR: display begin failed (PSRAM frame buffer / RGB panel)");
  ledcAttach(PIN_LCD_BL, 20000, 10);
  ledcWrite(PIN_LCD_BL, 800);                     // about 80% brightness
  return ok;
}

