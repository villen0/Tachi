#pragma once
// Display + touch driver for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (CO5300 AMOLED over QSPI, CST9217 touch).
// Header-only and static, like display_round28.h: included by exactly one source file per build (main.cpp or amoled175_bringup.cpp).
// It offers the same functions as display_round28.h (roundDisplayInit, touchInit, touchPoll, setBacklight, ...) so main.cpp is the same for both boards.
// Pins: include/board_amoled175.h.
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrvCSTXXX.hpp>
#include "config.h"
#include "panel_crop.h"

static bool i2cStarted = false, dispBeginOk = false, touchOk = false;
static void i2cBegin() { if (!i2cStarted) { Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000); i2cStarted = true; } }

// ---- display: CO5300 on the QSPI bus (the panel keeps its own picture memory: only changed rows are sent) ----
static Arduino_DataBus *amBus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
static Arduino_CO5300 *rgbDisp = new Arduino_CO5300(amBus, PIN_LCD_RST, 0 /*rotation*/, PANEL_W, PANEL_H, 6 /*column offset: Waveshare's value for this panel*/, 0, 0, 0);

// Sends one row span of the 480-wide frame buffer to the panel (x, y in frame-buffer pixels, p = the first pixel of the span in the frame buffer).
// The middle 466x466 is shown, so the span is shifted by the crop and clipped; the CO5300 needs even column starts / widths and windows that start
// on an even row with an even height, so the span goes out as two rows (this row and its partner, both read from the frame buffer).
static inline void panelPush(int x, int y, int w, const uint16_t *p) {
  panelcrop::Span sp;
  if (!panelcrop::cropSpan(x, y, w, PANEL_CROP_X, PANEL_CROP_Y, PANEL_W, PANEL_H, &sp)) return;
  static uint16_t pairBuf[PANEL_W * 2];
  int y0 = panelcrop::pairRow(sp.py);                                   // panel row of the pair's first row
  const uint16_t *r0 = p + sp.skip + (y0 - sp.py) * LCD_W;              // that row's pixels in the frame buffer (p is this span's row)
  memcpy(pairBuf, r0, sp.w * 2); memcpy(pairBuf + sp.w, r0 + LCD_W, sp.w * 2);
  rgbDisp->draw16bitRGBBitmap(sp.px, y0, pairBuf, sp.w, 2);
}
static void waitBeamPast(int) {}                   // no scan-line synchronisation yet (the panel's TE pin could do it); partial rows are sent as they are

// ---- brightness: a panel command (0..255); the percent from the brightness page is mapped with a gamma, kept across restarts ----
static int backlightPct = 85;                      // 5..100
static void loadBacklight() { Preferences pr; if (pr.begin("tachi", true)) { backlightPct = pr.getInt("bl", 85); pr.end(); } if (backlightPct < 5 || backlightPct > 100) backlightPct = 85; }
static void saveBacklight() { Preferences pr; if (pr.begin("tachi", false)) { pr.putInt("bl", backlightPct); pr.end(); } }
static void setBacklight(int pct) {
  backlightPct = pct < 5 ? 5 : (pct > 100 ? 100 : pct);
  int v = (int)(255.0f * powf(backlightPct / 100.0f, 1.6f) + 0.5f);
  rgbDisp->setBrightness((uint8_t)(v < 6 ? 6 : v));
}
static void backlightOff() { rgbDisp->setBrightness(0); }       // sleep: an AMOLED shows pure black as "off"; the saved level is untouched
static void backlightRestore() { setBacklight(backlightPct); }

static bool roundDisplayInit() {
  i2cBegin();
  bool ok = rgbDisp->begin();
  dispBeginOk = ok;
  Serial.printf("display begin: %s (CO5300 %dx%d over QSPI), PSRAM %u bytes (free %u)\n", ok ? "ok" : "FAILED", PANEL_W, PANEL_H, (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
  if (!ok) Serial.println("ERROR: display begin failed (check the QSPI pins and the board variant)");
  rgbDisp->fillScreen(RGB565_BLACK);
  loadBacklight();
  setBacklight(backlightPct);
  return ok;
}

// ---- CST9217 touch (SensorLib), mapped to frame-buffer coordinates ----
static TouchDrvCST92xx cst;
static bool touchDown = false;
static void touchInit() {
  cst.setPins(PIN_TOUCH_RST, PIN_TOUCH_INT);
  touchOk = cst.begin(Wire, CST92XX_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  Serial.printf("CST9217 touch %s%s%s\n", touchOk ? "found: " : "NOT found", touchOk ? cst.getModelName() : "", touchOk ? "" : " (check the board variant / I2C)");
}
// Maps the controller's raw point to frame-buffer pixels (the shown panel is the middle of the frame buffer).
static void touchMap(int16_t rx, int16_t ry, int *x, int *y) { panelcrop::mapTouch(rx, ry, TOUCH_SWAP_XY, TOUCH_FLIP_X, TOUCH_FLIP_Y, PANEL_W, PANEL_H, PANEL_CROP_X, PANEL_CROP_Y, x, y); }
// Returns true when there is something to report: a finger on the glass (*down, with *x, *y), or its lift. False when nothing changed.
static bool touchPoll(bool *down, int *x, int *y) {
  if (!touchOk) return false;
  int16_t tx[1] = {0}, ty[1] = {0};
  uint8_t n = cst.getPoint(tx, ty, 1);
  if (n > 0) { touchMap(tx[0], ty[0], x, y); *down = true; touchDown = true; return true; }
  if (touchDown) { touchDown = false; *down = false; return true; }
  return false;
}
