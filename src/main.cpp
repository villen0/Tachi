// Tachi: ESP32-S3 OBD-II desk/dash pet for the Waveshare 1.47" display board.
// STATUS: compiles (PlatformIO); not yet run on hardware. See README.md.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_NeoPixel.h>
#include <SD_MMC.h>
#include "config.h"

// Arduino_GFX 1.6.x names its colors RGB565_*; keep short names for readability.
#define BLACK  RGB565_BLACK
#define WHITE  RGB565_WHITE
#define GREEN  RGB565_GREEN
#define YELLOW RGB565_YELLOW
#define RED    RGB565_RED

enum Mood { HAPPY, UNEASY, WORRIED, SLEEPY, EXCITED, CALM };

// ---------- Display (ST7789 172x320, column offset 34) ----------
Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_LCD_RST, 0 /*rotation*/, true /*IPS*/, LCD_W, LCD_H,
                                      34, 0, 34, 0 /*col/row offsets for 172x320 panel*/);
Adafruit_NeoPixel led(1, PIN_LED, NEO_GRB + NEO_KHZ800);

// ---------- BLE / OBD state ----------
NimBLEClient *client = nullptr;
NimBLERemoteCharacteristic *rxChar = nullptr;  // notify: adapter -> board
NimBLERemoteCharacteristic *txChar = nullptr;  // write:  board -> adapter

// Receive buffer: written by the BLE callback task, read by the main task. Guarded by rxMux.
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
static char rxBuf[128];
static size_t rxLen = 0;
static volatile bool gotPrompt = false;

int rpm = 0, mph = 0, coolantC = 0, oilC = 0;
uint32_t tsRpm = 0, tsMph = 0, tsCool = 0, tsOil = 0;   // when each value was last updated by the car
#define FAST_FRESH_MS 3000   // speed / RPM older than this are treated as unknown (shown as --, counted as 0)
#define SLOW_FRESH_MS 10000  // coolant / oil are polled less often
bool oilValid = false;       // oil temp PID (015C) answered at least once; many cars do not support it
static uint8_t oilFails = 0; // consecutive failed oil polls; after OIL_GIVE_UP we only retry occasionally
uint32_t lastValidMs = 0;   // last time any PID returned valid data
bool linked = false;
bool demo = DEMO_MODE_DEFAULT;
enum Page { PAGE_FACE, PAGE_MPH, PAGE_RPM, PAGE_COOLANT, PAGE_OIL, PAGE_DEBUG, PAGE_COUNT };
int page = PAGE_FACE;       // BOOT short press cycles: Face > MPH > RPM > Coolant > Oil temp > Debug

// ---------- UI state (shared by loop() and the wait loops inside sendCmd) ----------
static int lastMood = -1;
static bool lastOpen = true;
static bool forceDraw = true;
static int drawnPage = -1, shownVal = -99999;
static bool shownStale = false, shownValid = false;
static bool blinking = false;
static uint32_t nextBlink = 0, blinkEnd = 0;

Mood computeMood();
void uiTick();

// Last few adapter exchanges, shown on the Debug page so the raw replies can be read in the car.
struct DbgEntry { char cmd[10]; char reply[40]; bool timeout; };
static DbgEntry dbg[5];
static uint8_t dbgHead = 0;
static uint32_t reqPerSec = 0;
static uint32_t lastCmdMs = 0, lastWriteMs = 0, maxTickGapMs = 0, sdWriteMs = 0;   // timing shown on the Debug page

// ---------- BLE receive ----------
void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  portENTER_CRITICAL(&rxMux);
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '>') gotPrompt = true;                                // ELM327 "ready" prompt
    else if (c != '\r' && c != '\n' && c != ' ' && rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = c;
  }
  rxBuf[rxLen] = 0;
  portEXIT_CRITICAL(&rxMux);
}

String sendCmd(const char *cmd, uint32_t timeoutMs = CMD_TIMEOUT_MS) {
  portENTER_CRITICAL(&rxMux);
  rxLen = 0; rxBuf[0] = 0; gotPrompt = false;
  portEXIT_CRITICAL(&rxMux);

  String out = String(cmd) + "\r";
  uint32_t tw = millis();
  txChar->writeValue((const uint8_t *)out.c_str(), out.length(), false);
  lastWriteMs = millis() - tw;

  uint32_t t0 = millis();
  while (!gotPrompt && millis() - t0 < timeoutMs) {
    uiTick();            // keep blinking / button alive while waiting for the adapter
    delay(5);
  }
  lastCmdMs = millis() - t0;
  char tmp[sizeof(rxBuf)];
  portENTER_CRITICAL(&rxMux);
  memcpy(tmp, rxBuf, sizeof(tmp));   // copy under the lock, allocate the String outside it
  portEXIT_CRITICAL(&rxMux);
  String r(tmp);
  DbgEntry &d = dbg[dbgHead];
  dbgHead = (dbgHead + 1) % 5;
  strncpy(d.cmd, cmd, sizeof(d.cmd) - 1); d.cmd[sizeof(d.cmd) - 1] = 0;
  strncpy(d.reply, tmp, sizeof(d.reply) - 1); d.reply[sizeof(d.reply) - 1] = 0;
  d.timeout = !gotPrompt;
  Serial.printf("> %s   < %s%s\n", cmd, r.c_str(), gotPrompt ? "" : "   (timeout)");
  return r;
}

int hexAt(const String &s, int pos) {
  return (int)strtol(s.substring(pos, pos + 2).c_str(), nullptr, 16);
}

bool parsePid(const String &r, const char *tag, int nBytes, int *out) {
  int i = r.indexOf(tag);
  if (i < 0) return false;
  int p = i + strlen(tag);
  if ((int)r.length() < p + 2 * nBytes) return false;
  for (int k = 0; k < nBytes; k++) out[k] = hexAt(r, p + 2 * k);
  return true;
}

// A two-PID reply is normally ONE message: "41 0C aa bb 0D cc" (the 41 appears once), but some
// adapters/ECUs answer with two messages "410C aabb" and "410D cc". Accept both, in either order.
bool parseRpmSpeed(const String &r, bool *gotRpm, bool *gotSpeed) {
  int b[2];
  *gotRpm = *gotSpeed = false;
  int i = r.indexOf("410C");
  if (i >= 0 && (int)r.length() >= i + 8) {
    b[0] = hexAt(r, i + 4); b[1] = hexAt(r, i + 6);
    rpm = (b[0] * 256 + b[1]) / 4; *gotRpm = true;
    int q = i + 8;                                              // chained: 41 0C aa bb 0D cc
    if (r.substring(q, q + 2) == "0D" && (int)r.length() >= q + 4) { mph = (int)(hexAt(r, q + 2) * 0.621f + 0.5f); *gotSpeed = true; }
  }
  int j = r.indexOf("410D");
  if (j >= 0 && (int)r.length() >= j + 6) {
    mph = (int)(hexAt(r, j + 4) * 0.621f + 0.5f); *gotSpeed = true;
    int q = j + 6;                                              // chained: 41 0D cc 0C aa bb
    if (!*gotRpm && r.substring(q, q + 2) == "0C" && (int)r.length() >= q + 8) {
      rpm = (hexAt(r, q + 2) * 256 + hexAt(r, q + 4)) / 4; *gotRpm = true;
    }
  }
  uint32_t now = millis();
  if (*gotRpm) tsRpm = now;
  if (*gotSpeed) tsMph = now;
  return *gotRpm || *gotSpeed;
}

// Fast values (speed, RPM) are what you watch, so they are polled every cycle. ELM327 can answer two
// PIDs in one request ("010C0D"), which halves the Bluetooth round trips. If the adapter/car does not
// support that, we fall back to separate requests. Slow values (coolant, oil) are polled less often.
// Query helper. Several modules answer a PID request on this car (the reply comes back twice), and the
// adapter waits a fixed time for more replies when it does not know how many to expect. Two fixes,
// both detected at connect time and dropped automatically if the adapter does not support them:
//  - useFilter: ATCRA7E8 so only the engine computer's replies are received
//  - useCount : a trailing "1" on the request = "expect exactly one reply", so the adapter answers at once
// Both were tried on the Mazda 3 + CodeReader31B: the filter was not accepted and the count suffix made
// requests MUCH slower (about 1 cycle/s instead of 5-6), so they are OFF. Set to 1 to experiment.
#define USE_ENGINE_FILTER 0
#define USE_COUNT_SUFFIX  0
static bool useFilter = false, useCount = false;
String obdQuery(const char *pid) {
  char cmd[12];
  snprintf(cmd, sizeof(cmd), "%s%s", pid, useCount ? "1" : "");
  return sendCmd(cmd);
}

// Combined speed+RPM requests ("010C0D") did not work on this car (see Debug page), so they are off.
#define USE_COMBINED_PID 0
static bool combinedOk = USE_COMBINED_PID;
static uint8_t combinedFails = 0;
static uint32_t pollCount = 0;   // completed requests, for the serial rate counter

bool pollRpm()   { int b[2]; if (parsePid(obdQuery("010C"), "410C", 2, b)) { rpm = (b[0] * 256 + b[1]) / 4; tsRpm = lastValidMs = millis(); return true; } return false; }
bool pollSpeed() { int b[2]; if (parsePid(obdQuery("010D"), "410D", 1, b)) { mph = (int)(b[0] * 0.621f + 0.5f); tsMph = lastValidMs = millis(); return true; } return false; }

static uint8_t speedMisses = 0;

void pollFast() {
  pollCount++;
  if (combinedOk) {
    String r = sendCmd("010C0D");
    bool gotRpm, gotSpeed;
    bool got = parseRpmSpeed(r, &gotRpm, &gotSpeed);
    if (got) { lastValidMs = millis(); combinedFails = 0; }
    else if (++combinedFails >= 3) { combinedOk = false; Serial.println("combined 010C0D not answered, using separate requests"); }
    // Speed missing from the combined reply (RPM only) 3 times in a row: ask for speed on its own.
    if (got && !gotSpeed) { if (++speedMisses >= 3) { combinedOk = false; Serial.println("speed missing from combined reply, using separate requests"); } }
    else if (gotSpeed) speedMisses = 0;
  } else {
    pollSpeed();
    pollRpm();
  }
}

void pollSlow(int which) {   // 0 coolant, 1 oil
  pollCount++;
  int b[2];
  if (which == 0) {
    if (parsePid(obdQuery("0105"), "4105", 1, b)) { coolantC = b[0] - 40; tsCool = lastValidMs = millis(); }
  } else {
    if (parsePid(obdQuery("015C"), "415C", 1, b)) { oilC = b[0] - 40; oilValid = true; oilFails = 0; tsOil = lastValidMs = millis(); }
    else if (oilFails < 255) oilFails++;
  }
}

// Oil temp (PID 015C) is optional and often unsupported. After a few failures only retry now and then
// so it does not slow down the other readings.
bool oilDue() {
  static uint8_t skip = 0;
  if (oilFails < 5) return true;
  if (++skip >= 40) { skip = 0; return true; }
  return false;
}

// Fake data that sweeps through every mood: idle, driving, then a slow overheat.
void demoStep() {
  float t = millis() / 1000.0f;
  rpm = 800 + (int)(3400 * (0.5f + 0.5f * sinf(t * 0.6f)));   // idle .. 4200 (excited at the top)
  mph = (int)(70 * (0.5f + 0.5f * sinf(t * 0.3f)));
  float cyc = fmodf(t, 60.0f);                       // 60 s loop
  coolantC = 88 + (int)(cyc < 40 ? cyc * 0.2f : 8 + (cyc - 40) * 1.0f);   // 88 -> 96 -> 116
  oilC = 80 + (int)(cyc * 0.4f); oilValid = true;                           // 80 -> 104
  if (fmodf(t, 75.0f) <= 65.0f) tsRpm = tsMph = tsCool = tsOil = lastValidMs = millis();   // else: 10 s "engine off" gap
}

// Mood priority: sleepy > worried > uneasy (engine health first), then excited > calm > happy.
Mood computeMood() {
  uint32_t now = millis();
  if (now - lastValidMs > STALE_AFTER_MS) return SLEEPY;   // engine off / no data
  if (coolantC >= TEMP_WORRIED_C) return WORRIED;
  if (coolantC >= TEMP_UNEASY_C) return UNEASY;

  // Excited: high revs (with hysteresis so it doesn't flicker) or hard acceleration.
  static bool highRev = false;
  static int prevMph = 0;
  static uint32_t lastSpeedSample = 0, excitedUntil = 0;
  if (!highRev && rpm >= RPM_EXCITED_ON) highRev = true;
  else if (highRev && rpm < RPM_EXCITED_OFF) highRev = false;
  if (now - lastSpeedSample >= 1000) {
    if (mph - prevMph >= ACCEL_EXCITED_MPH_PER_S) excitedUntil = now + 3000;
    prevMph = mph;
    lastSpeedSample = now;
  }
  if (highRev || now < excitedUntil) return EXCITED;

  // Calm: relaxed highway cruising.
  static bool cruising = false;
  if (!cruising && mph >= CRUISE_MPH_ON && rpm < CRUISE_RPM_MAX) cruising = true;
  else if (cruising && (mph < CRUISE_MPH_OFF || rpm >= CRUISE_RPM_MAX)) cruising = false;
  if (cruising) return CALM;
  return HAPPY;
}

// ---------- microSD logging ----------
// One file per boot (tachi_000.csv, tachi_001.csv, ...). Flushed every row so pulling power loses little.
static File logFile;
static bool sdOk = false;

void sdInit() {
#if SD_LOG_ENABLED
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (!SD_MMC.begin("/sdcard", true /*1-bit*/)) { Serial.println("SD: no card / init failed (logging off)"); return; }
  char name[24];
  for (int n = 0; n < 1000; n++) {
    snprintf(name, sizeof(name), "/tachi_%03d.csv", n);
    if (!SD_MMC.exists(name)) break;
  }
  logFile = SD_MMC.open(name, FILE_WRITE);
  if (!logFile) { Serial.println("SD: cannot create log file"); return; }
  logFile.println("ms,rpm,mph,coolant_c,oil_c");
  logFile.flush();
  sdOk = true;
  Serial.printf("SD: logging to %s\n", name);
#endif
}

void sdLogTick() {
  static uint32_t lastLog = 0, lastFlush = 0;
  if (!sdOk || demo) return;
  uint32_t now = millis();
  if (now - lastLog < SD_LOG_INTERVAL_MS || now - lastValidMs > STALE_AFTER_MS) return;
  lastLog = now;
  if (oilValid) logFile.printf("%lu,%d,%d,%d,%d\n", (unsigned long)now, rpm, mph, coolantC, oilC);
  else          logFile.printf("%lu,%d,%d,%d,\n", (unsigned long)now, rpm, mph, coolantC);
  if (millis() - lastFlush >= 5000) { lastFlush = millis(); uint32_t t = millis(); logFile.flush(); sdWriteMs = millis() - t; }
}

// ---------- LED ----------
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

void setLedMood(Mood m) {
  switch (m) {
    case HAPPY:   setLed(0, 40, 0);  break;
    case UNEASY:  setLed(40, 30, 0); break;
    case WORRIED: setLed(60, 0, 0);  break;
    case SLEEPY:  setLed(0, 0, 25);  break;
    case EXCITED: setLed(50, 0, 50); break;
    case CALM:    setLed(0, 30, 30); break;
  }
}

// ---------- Drawing ----------
const int FACE_DY = 30;   // face page has no readout, so the face sits lower / centered
const int EYE_Y = 115 + FACE_DY, EYE_X1 = 48, EYE_X2 = 124;
const int MOUTH_X = LCD_W / 2, MOUTH_Y = 200 + FACE_DY;

void showStatus(const char *msg, uint16_t color) {
  gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2, 150);
  gfx->print(msg);
  Serial.println(msg);
  forceDraw = true;   // face must be redrawn after a status screen
}

// Draw a smile (curve > 0), flat line (0) or frown (curve < 0).
void drawMouth(int cx, int cy, int width, int curve, uint16_t color) {
  for (int t = -width / 2; t <= width / 2; t++) {
    float u = (2.0f * t) / width;
    int y = cy + (int)(curve * (1.0f - u * u));
    gfx->fillCircle(cx + t, y, 3, color);
  }
}

void drawEyes(Mood m, bool eyesOpen) {
  gfx->fillRect(0, 70 + FACE_DY, LCD_W, 90, BLACK);
  int r = (m == WORRIED) ? 26 : (m == EXCITED) ? 28 : 22;
  if (m == SLEEPY || !eyesOpen) {
    gfx->fillRect(EYE_X1 - 20, EYE_Y - 3, 40, 6, WHITE);
    gfx->fillRect(EYE_X2 - 20, EYE_Y - 3, 40, 6, WHITE);
  } else {
    int pupil = (m == WORRIED) ? 5 : (m == EXCITED) ? 13 : 9;
    gfx->fillCircle(EYE_X1, EYE_Y, r, WHITE);
    gfx->fillCircle(EYE_X2, EYE_Y, r, WHITE);
    gfx->fillCircle(EYE_X1, EYE_Y, pupil, BLACK);
    gfx->fillCircle(EYE_X2, EYE_Y, pupil, BLACK);
    if (m == EXCITED) {                       // sparkle highlight in each pupil
      gfx->fillCircle(EYE_X1 - 4, EYE_Y - 4, 4, WHITE);
      gfx->fillCircle(EYE_X2 - 4, EYE_Y - 4, 4, WHITE);
    } else if (m == CALM) {                   // relaxed half-closed lids
      gfx->fillRect(EYE_X1 - r, EYE_Y - r, 2 * r + 1, r, BLACK);
      gfx->fillRect(EYE_X2 - r, EYE_Y - r, 2 * r + 1, r, BLACK);
      gfx->fillRect(EYE_X1 - r, EYE_Y - 2, 2 * r + 1, 3, WHITE);
      gfx->fillRect(EYE_X2 - r, EYE_Y - 2, 2 * r + 1, 3, WHITE);
    }
  }
}

void drawMouthFor(Mood m) {
  gfx->fillRect(0, 165 + FACE_DY, LCD_W, 75, BLACK);
  switch (m) {
    case HAPPY:   drawMouth(MOUTH_X, MOUTH_Y, 70, 16, GREEN);       break;
    case UNEASY:  drawMouth(MOUTH_X, MOUTH_Y, 60, 0, YELLOW);       break;
    case WORRIED: drawMouth(MOUTH_X, MOUTH_Y + 8, 60, -16, RED);    break;
    case SLEEPY:  gfx->drawCircle(MOUTH_X, MOUTH_Y, 8, 0x7BEF);     break;
    case CALM:    drawMouth(MOUTH_X, MOUTH_Y, 50, 8, 0x07FF);       break;
    case EXCITED:                                                    // open grin, filled
      for (int t = -40; t <= 40; t++) {
        float u = t / 40.0f;
        gfx->drawFastVLine(MOUTH_X + t, MOUTH_Y - 6, (int)(8 + 26 * (1.0f - u * u)), 0xFD20);
      }
      break;
  }
}

// Full-screen data page: title, one big number, unit. Mood is still shown by the LED, and the
// coolant/oil numbers turn yellow/red at the uneasy/worried thresholds.
static bool fresh(uint32_t ts, uint32_t maxAge) { return ts != 0 && millis() - ts < maxAge; }

void pageInfo(int p, int *val, bool *valid, const char **title, const char **unit, uint16_t *color) {
  *valid = true; *color = WHITE;
  switch (p) {
    case PAGE_MPH:     *val = mph;      *title = "SPEED";     *unit = "MPH"; *valid = fresh(tsMph, FAST_FRESH_MS); break;
    case PAGE_RPM:     *val = rpm;      *title = "ENGINE";    *unit = "RPM"; *valid = fresh(tsRpm, FAST_FRESH_MS); break;
    case PAGE_COOLANT: *val = coolantC; *title = "COOLANT";   *unit = "deg C"; *valid = fresh(tsCool, SLOW_FRESH_MS);
      if (coolantC >= TEMP_WORRIED_C) *color = RED; else if (coolantC >= TEMP_UNEASY_C) *color = YELLOW; break;
    default:           *val = oilC;     *title = "OIL TEMP";  *unit = "deg C"; *valid = oilValid && fresh(tsOil, SLOW_FRESH_MS); break;
  }
}

// full = also draw the static parts (title, unit, page dots); otherwise only the number is redrawn.
void drawDataPage(bool stale, bool full) {
  int val; bool valid; const char *title, *unit; uint16_t color;
  pageInfo(page, &val, &valid, &title, &unit, &color);
  int16_t x1, y1; uint16_t w, h;
  if (full) {
    gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
    gfx->setTextSize(3);
    gfx->setTextColor(0x7BEF);
    gfx->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((LCD_W - w) / 2, 40);
    gfx->print(title);
    gfx->getTextBounds(unit, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((LCD_W - w) / 2, 230);
    gfx->print(unit);
    for (int i = 0; i < PAGE_COUNT; i++)   // page dots at the bottom
      gfx->fillCircle(LCD_W / 2 + (i - PAGE_COUNT / 2) * 16 + 8, 300, 4, i == page ? WHITE : 0x39E7);
  }
  char big[16];
  if (!valid || stale) { snprintf(big, sizeof(big), "--"); color = 0x7BEF; }
  else snprintf(big, sizeof(big), "%d", val);
  gfx->fillRect(0, 100, LCD_W, 110, BLACK);   // number area only
  gfx->setTextSize(strlen(big) <= 3 ? 8 : 6);
  gfx->setTextColor(color);
  gfx->getTextBounds(big, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2, 135);
  gfx->print(big);
}

// Debug page: link state and the last raw adapter exchanges (newest at the bottom).
void drawDebugPage() {
  gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(0x07FF);
  gfx->setCursor(8, 6);
  gfx->print("DEBUG");
  gfx->setTextSize(1);
  gfx->setTextColor(WHITE);
  uint32_t now = millis();
  gfx->setCursor(4, 30);  gfx->printf("link:%s f:%s c:%s %lu req/s", linked ? "OK" : "--", useFilter ? "Y" : "N", useCount ? "Y" : "N", (unsigned long)reqPerSec);
  gfx->setCursor(4, 42);  gfx->printf("mph:%d  age:%ldms", mph, tsMph ? (long)(now - tsMph) : -1L);
  gfx->setCursor(4, 54);  gfx->printf("rpm:%d  age:%ldms", rpm, tsRpm ? (long)(now - tsRpm) : -1L);
  gfx->setCursor(4, 66);  gfx->printf("cool:%d oil:%d", coolantC, oilValid ? oilC : -999);
  gfx->setTextColor(0x7BEF);
  gfx->setCursor(4, 78);  gfx->printf("cmd:%lums wr:%lums gap:%lums", (unsigned long)lastCmdMs, (unsigned long)lastWriteMs, (unsigned long)maxTickGapMs);
  gfx->setCursor(4, 90);  gfx->printf("sd:%s %lums", sdOk ? "on" : "off", (unsigned long)sdWriteMs);
  int y = 110;
  for (int k = 0; k < 5; k++) {   // oldest first
    DbgEntry &d = dbg[(dbgHead + k) % 5];
    if (!d.cmd[0]) continue;
    gfx->setCursor(4, y);  gfx->setTextColor(0xFFE0); gfx->printf("> %s%s", d.cmd, d.timeout ? " (timeout)" : ""); y += 10;
    for (int off = 0; off < (int)strlen(d.reply) && off < 56; off += 27) {   // wrap reply at 27 chars
      char line[28]; strncpy(line, d.reply + off, 27); line[27] = 0;
      gfx->setCursor(4, y); gfx->setTextColor(WHITE); gfx->print(line); y += 10;
    }
    y += 4;
  }
  for (int i = 0; i < PAGE_COUNT; i++)
    gfx->fillCircle(LCD_W / 2 + (i - PAGE_COUNT / 2) * 16 + 8, 308, 4, i == page ? WHITE : 0x39E7);
}

// ---------- Input ----------
// Short press: next page (Face > MPH > RPM > Coolant > Oil temp). Long press (LONG_PRESS_MS): toggle demo mode.
// Edges are caught by an interrupt, so a quick click is never missed while the main loop is busy
// talking to the adapter or redrawing. handleButton() then acts on what the interrupt recorded.
static volatile bool btnDown = false, btnLongFired = false;
static volatile uint32_t btnDownMs = 0, btnLastEdgeMs = 0;
static volatile uint8_t btnShortPresses = 0;

void IRAM_ATTR onButtonEdge() {
  uint32_t ms = millis();
  if (ms - btnLastEdgeMs < 30) return;                 // contact bounce
  if (digitalRead(PIN_BUTTON) == LOW) {
    if (!btnDown) { btnDown = true; btnDownMs = ms; btnLongFired = false; btnLastEdgeMs = ms; }
  } else if (btnDown) {
    btnDown = false; btnLastEdgeMs = ms;
    if (!btnLongFired && btnShortPresses < 5) btnShortPresses = btnShortPresses + 1;
  }
}

void handleButton() {
  if (btnDown && !btnLongFired && millis() - btnDownMs >= LONG_PRESS_MS) {
    btnLongFired = true;
    demo = !demo;
    Serial.printf("demo mode %s\n", demo ? "ON" : "OFF");
    lastValidMs = 0;
    forceDraw = true;
  }
  noInterrupts();
  uint8_t n = btnShortPresses;
  btnShortPresses = 0;
  interrupts();
  if (n) {
    page = (page + n) % PAGE_COUNT;
    forceDraw = true;
  }
}

// Everything that must stay alive while we wait on Bluetooth: button, blink, redraw.
void uiTick() {
  static uint32_t lastTick = 0, gapWindowStart = 0, gapMax = 0;
  uint32_t t = millis();
  if (lastTick && t - lastTick > gapMax) gapMax = t - lastTick;   // longest stall between UI updates
  lastTick = t;
  if (t - gapWindowStart >= 5000) { maxTickGapMs = gapMax; gapMax = 0; gapWindowStart = t; }
  handleButton();

  uint32_t now = millis();
  if (!blinking && now >= nextBlink) { blinking = true; blinkEnd = now + 150; }
  if (blinking && now >= blinkEnd) { blinking = false; nextBlink = now + random(2500, 5500); }
  bool open = !blinking;

  // Values the car has stopped answering must not linger on screen or drive the mood.
  if (!fresh(tsMph, FAST_FRESH_MS)) mph = 0;
  if (!fresh(tsRpm, FAST_FRESH_MS)) rpm = 0;
  Mood m = computeMood();
  bool moodChanged = (int)m != lastMood;
  if (moodChanged || forceDraw) setLedMood(m);   // LED follows the mood on every page

  if (page == PAGE_FACE) {
    if (forceDraw || moodChanged || drawnPage != PAGE_FACE) {
      gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);  // wipe status text / previous page
      drawEyes(m, open);
      drawMouthFor(m);
      lastOpen = open;
    } else if (m != SLEEPY && open != lastOpen) {
      drawEyes(m, open);                         // blink: only the eyes change
      lastOpen = open;
    }
  } else if (page == PAGE_DEBUG) {
    static uint32_t lastDbgDraw = 0;
    if (forceDraw || drawnPage != PAGE_DEBUG || now - lastDbgDraw >= 500) { drawDebugPage(); lastDbgDraw = now; }
  } else {
    int val; bool valid; const char *t, *u; uint16_t c;
    pageInfo(page, &val, &valid, &t, &u, &c);
    bool stale = (m == SLEEPY);
    bool full = forceDraw || drawnPage != page;
    if (full || val != shownVal || valid != shownValid || stale != shownStale) {
      drawDataPage(stale, full);
      shownVal = val; shownValid = valid; shownStale = stale;
    }
  }
  drawnPage = page;
  lastMood = (int)m;
  forceDraw = false;
}

// ---------- Connection ----------
bool connectAdapter() {
  int tmpBytes[2];
  showStatus("Scanning...", 0x07FF);
  setLed(0, 20, 40);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->getResults(SCAN_TIME_MS, false);
  const NimBLEAdvertisedDevice *found = nullptr;
  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice *d = results.getDevice(i);
    String n = d->getName().c_str();
    Serial.printf("seen: '%s'\n", n.c_str());
    if (n.indexOf(ADAPTER_NAME_SUBSTR) >= 0) { found = d; break; }
  }
  if (!found) return false;

  showStatus("Connecting...", YELLOW);
  if (!client) client = NimBLEDevice::createClient();
  if (!client->connect(found)) { Serial.println("connect failed"); return false; }

  NimBLERemoteService *svc = client->getService(NimBLEUUID(SERVICE_UUID_STR));
  if (!svc) { Serial.println("service not found"); client->disconnect(); return false; }
  rxChar = svc->getCharacteristic(NimBLEUUID(RX_UUID_STR));
  txChar = svc->getCharacteristic(NimBLEUUID(TX_UUID_STR));
  if (!rxChar || !txChar) { Serial.println("characteristics not found"); client->disconnect(); return false; }
  rxChar->subscribe(true, onNotify);

  const char *init[] = {"ATZ", "ATE0", "ATL0", "ATH0", "ATS0", "ATSP0"};
  for (auto c : init) sendCmd(c, INIT_TIMEOUT_MS);
  sendCmd("0100", 8000);                      // let the adapter find the car's protocol now (can take seconds)
  useFilter = useCount = false;
#if USE_ENGINE_FILTER
  String f = sendCmd("ATCRA7E8", INIT_TIMEOUT_MS);   // only listen to the engine computer
  if (f.indexOf("OK") >= 0) {
    useFilter = parsePid(sendCmd("010D"), "410D", 1, tmpBytes);
    if (!useFilter) sendCmd("ATCRA", INIT_TIMEOUT_MS);   // filter blocked everything: remove it
  }
#endif
#if USE_COUNT_SUFFIX
  useCount = parsePid(sendCmd("010D1"), "410D", 1, tmpBytes);   // does the adapter accept "expect 1 reply"?
#endif
  Serial.printf("init: filter=%s count=%s\n", useFilter ? "on" : "off", useCount ? "on" : "off");
  lastValidMs = 0;
  linked = true;
  return true;
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never stall when nothing is reading the USB serial port (car power)
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonEdge, CHANGE);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(BLACK);
  led.begin();
  led.setBrightness(60);
  sdInit();
  NimBLEDevice::init("");
  Serial.printf("Tachi starting, demo mode %s\n", demo ? "ON" : "OFF");
}

void loop() {
  static int cycle = 0, slowIdx = 0;

  if (demo) {
    if (client && client->isConnected()) client->disconnect();
    linked = false;
    demoStep();
    uiTick();
    delay(20);
    return;
  }

  if (!client || !client->isConnected()) {
    if (linked) {
      linked = false;
      showStatus("Link lost", RED);
      setLed(60, 0, 0);
      // Wait out the reconnect delay without freezing the button (long press -> demo).
      uint32_t t0 = millis();
      while (millis() - t0 < RECONNECT_WAIT_MS && !demo) { handleButton(); delay(10); }
    }
    if (demo) return;
    if (!connectAdapter()) {
      showStatus("Not found", YELLOW);
      uint32_t t0 = millis();
      while (millis() - t0 < 1500 && !demo) { handleButton(); delay(10); }
    }
    forceDraw = true;
    return;
  }

  // Back-to-back polling: next request goes out as soon as the previous reply is in.
  // Every SLOW_EVERY-th cycle one slow value (coolant, then oil) is polled instead of waiting a full round.
  pollFast();
  if (++cycle >= SLOW_EVERY) {
    cycle = 0;
    if (slowIdx == 1 && !oilDue()) slowIdx = 0;
    pollSlow(slowIdx);
    slowIdx = (slowIdx + 1) % 2;
  }
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 1000) {   // one summary line per second on the serial monitor
    lastPrint = millis();
    static uint32_t lastCount = 0;
    Serial.printf("rpm=%d mph=%d coolant=%dC oil=%s%d page=%d  (%lu req/s)\n", rpm, mph, coolantC, oilValid ? "" : "n/a:", oilC, page,
                  (unsigned long)(pollCount - lastCount));
    reqPerSec = pollCount - lastCount;
    lastCount = pollCount;
  }
  sdLogTick();
  uiTick();
}
