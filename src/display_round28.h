#pragma once
// Display + touch driver for the Waveshare ESP32-S3-Touch-LCD-2.8C (ST7701 over 16-bit RGB, GT911, TCA9554 expander).
// Header-only and static: included by exactly one source file per build (main.cpp or round28_bringup.cpp).
// Pins and timings: include/board_round28.h. Not yet run on hardware.
#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#ifdef BOARD_ROUND28
#define private public    // Arduino_ESP32RGBPanel keeps its panel handle private; the vsync hook below needs it (main.cpp does the same)
#endif
#include <Arduino_GFX_Library.h>
#ifdef BOARD_ROUND28
#undef private
#endif
#include "config.h"
#include "round28_init.h"

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

// ---- ST7701 init: the command list from Waveshare's board config (src/round28_init.h), sent over the 3-wire 9-bit SPI ----
// The chip-select (on the expander) must go low and high around EACH command. Holding it low for the whole sequence
// (as Arduino_GFX's batch init does) leaves the panel blank, found on hardware.
static inline void sckPulse(int v) { digitalWrite(PIN_LCD_SPI_SCL, v); delayMicroseconds(2); }
static void spiBit9(bool dc, uint8_t b) {            // mode 0, MSB first, D/C bit first
  uint16_t w = ((uint16_t)dc << 8) | b;
  for (int i = 8; i >= 0; i--) { digitalWrite(PIN_LCD_SPI_SDA, (w >> i) & 1); delayMicroseconds(2); sckPulse(1); sckPulse(0); }
}
static void st7701Send(const InitCmd &c) {
  exSet(EXIO_LCD_CS, false); delayMicroseconds(5);
  spiBit9(false, c.cmd);
  for (int i = 0; i < c.n; i++) spiBit9(true, c.d[i]);
  exSet(EXIO_LCD_CS, true);
  if (c.delayMs) delay(c.delayMs);
}
static void st7701Init() {
  pinMode(PIN_LCD_SPI_SCL, OUTPUT); pinMode(PIN_LCD_SPI_SDA, OUTPUT);
  digitalWrite(PIN_LCD_SPI_SCL, LOW); digitalWrite(PIN_LCD_SPI_SDA, LOW);
  exSet(EXIO_LCD_RST, false); delay(10);
  exSet(EXIO_LCD_RST, true);  delay(120);
  for (const InitCmd &c : OFFICIAL_INIT) st7701Send(c);
}

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
    nullptr /*no bus: init is sent by st7701Init()*/, GFX_NOT_DEFINED /*reset is on the expander*/, nullptr, 0);

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

// ---- vsync tracking: lets the dial pages update a row only right after the panel's scan has passed it (no tearing) ----
static volatile uint32_t vsyncUs = 0, framePeriodUs = 0;
static volatile bool vsyncHooked = false;
static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  uint32_t now = (uint32_t)esp_timer_get_time(), dt = now - vsyncUs;
  if (vsyncUs && dt > 4000 && dt < 120000) framePeriodUs = framePeriodUs ? (framePeriodUs * 7 + dt) / 8 : dt;   // measured frame time
  vsyncUs = now;
  return false;
}
static void hookVsync() {
  esp_lcd_panel_handle_t h = rgbpanel->_panel_handle;
  if (!h) { Serial.println("vsync hook: no panel handle (updates are not synchronised)"); return; }
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = onVsync;
  vsyncHooked = esp_lcd_rgb_panel_register_event_callbacks(h, &cbs, nullptr) == ESP_OK;
  Serial.printf("vsync hook: %s\n", vsyncHooked ? "ok" : "FAILED (updates are not synchronised)");
}
// Returns once the panel's scan has passed picture row y in the current frame (or immediately between frames / without a hook).
static void waitBeamPast(int y) {
  uint32_t period = framePeriodUs;
  if (!vsyncHooked || !period) return;
  const uint32_t lineUs = period / (LCD_H + LCD_VPW + LCD_VBP + LCD_VFP), passAt = (uint32_t)(LCD_VBP + y + 6) * lineUs;
  for (;;) {
    uint32_t t = (uint32_t)esp_timer_get_time() - vsyncUs;
    if (t >= period || t >= passAt) return;
    uint32_t need = passAt - t;
    if (need > 2000) vTaskDelay(1); else if (need > 80) delayMicroseconds(need - 40); else taskYIELD();
  }
}

// Whole bring-up of the round board: I2C, expander, panel reset, ST7701 init, RGB panel, backlight.
static bool roundDisplayInit() {
  i2cBegin();
  if (!exInit()) Serial.println("ERROR: TCA9554 expander not found, display reset/CS cannot be controlled");
  st7701Init();                                   // reset pulse + init commands, chip-select toggled per command
  bool ok = rgbDisp->begin();
  dispBeginOk = ok;
  if (ok) hookVsync();
  Serial.printf("display begin: %s, PSRAM %u bytes (free %u)\n", ok ? "ok" : "FAILED", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
  if (!ok) Serial.println("ERROR: display begin failed (PSRAM frame buffer / RGB panel)");
  ledcAttach(PIN_LCD_BL, 20000, 10);
  ledcWrite(PIN_LCD_BL, 800);                     // about 80% brightness
  return ok;
}

