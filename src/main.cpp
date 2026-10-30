// Tachi: ESP32-S3 OBD-II desk/dash pet for the Waveshare 1.47" display board.
// STATUS: compiles (PlatformIO); not yet run on hardware. See README.md.
#include <Arduino.h>
#include <NimBLEDevice.h>
#ifdef BOARD_ROUND28
#define private public    // see display_round28.h: the vsync hook needs Arduino_ESP32RGBPanel's panel handle
#endif
#include <Arduino_GFX_Library.h>
#ifdef BOARD_ROUND28
#undef private
#endif
#include <SD_MMC.h>
#include <esp_system.h>
#include "config.h"
#if PIN_LED >= 0
#include <Adafruit_NeoPixel.h>
#endif
#ifdef BOARD_ROUND28
#include "display_round28.h"   // ST7701 RGB panel + GT911 touch of the round 2.8C board
#endif
#include "mochi.h"
#include "cluster.h"
#ifdef BOARD_ROUND28
#include "round_ui.h"   // round dial pages: sport cluster, analog tach / speedo, vitals rings
#endif
#include "obd_parse.h"
#include "car_events.h"

// Arduino_GFX 1.6.x names its colors RGB565_*; keep short names for readability.
#define BLACK  RGB565_BLACK
#define WHITE  RGB565_WHITE
#define GREEN  RGB565_GREEN
#define YELLOW RGB565_YELLOW
#define RED    RGB565_RED

enum Mood { HAPPY, UNEASY, WORRIED, SLEEPY, EXCITED, CALM };

#ifdef BOARD_ROUND28
// ---------- Display (round 480x480 RGB panel) ----------
// Everything draws into a full-frame canvas in PSRAM and is pushed to the panel with flush() once per frame, so the
// panel never scans out a half-drawn picture.
Arduino_GFX *gfx = nullptr;                    // the canvas, created in setup()
static Arduino_Canvas *fullCanvas = nullptr;
#else
// ---------- Display (ST7789 172x320, column offset 34) ----------
Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_LCD_RST, 0 /*rotation*/, true /*IPS*/, LCD_W, LCD_H,
                                      34, 0, 34, 0 /*col/row offsets for 172x320 panel*/);
#endif
#if PIN_LED >= 0
Adafruit_NeoPixel led(1, PIN_LED, NEO_GRB + NEO_KHZ800);
#endif

// ---------- Boot type (welcome animation: full on a fresh power-up, short after a quick restart) ----------
// The car's USB power can dip while the engine cranks and restart the board; replaying the long welcome each time would
// be annoying. No clock is available, so we use the reset reason plus a cookie in RAM that survives a restart.
static RTC_NOINIT_ATTR uint32_t rtcCookie;
static const uint32_t RTC_COOKIE = 0x7AC41001;
static bool bootQuickRestart = false;
static char bootText[24] = "boot: ?";
static uint8_t exciteWhy = 0;       // debug: why the face last turned excited (1 = revs, 2 = acceleration)
static uint32_t exciteAt = 0;
static const char *faceName = "-";
static uint32_t chgAt[3] = {0, 0, 0}; static const char *chgName[3] = {"-", "-", "-"}; static int chgHead = 0;   // debug: last expression changes   // debug: expression currently on the Face page
static void detectBoot() {
  esp_reset_reason_t rr = esp_reset_reason();
  bool cookie = (rtcCookie == RTC_COOKIE);
  bootQuickRestart = (rr != ESP_RST_POWERON) || cookie;
#if INTRO_FORCE == 1
  bootQuickRestart = false;
#elif INTRO_FORCE == 2
  bootQuickRestart = true;
#endif
  rtcCookie = RTC_COOKIE;
  snprintf(bootText, sizeof(bootText), "boot:%s r%d%s", bootQuickRestart ? "restart" : "power-on", (int)rr, cookie ? " k" : "");
  Serial.printf("%s -> %s welcome\n", bootText, bootQuickRestart ? "short" : "full");
}

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
static bool fresh(uint32_t ts, uint32_t maxAge) { return ts != 0 && millis() - ts < maxAge; }
bool oilValid = false;       // oil temp PID (015C) answered at least once; many cars do not support it
static uint8_t oilFails = 0; // consecutive failed oil polls; after OIL_GIVE_UP we only retry occasionally

// The car reports deg C. Mood thresholds stay in deg C; only what is shown/logged is converted.
static int tempOut(int c) { return TEMP_UNIT_F ? (c * 9 + 2) / 5 + 32 : c; }   // rounded
static const char *tempUnit() { return TEMP_UNIT_F ? "deg F" : "deg C"; }
static bool oilUnsupported() { return !oilValid && oilFails >= 5; }   // no usable oil reply so far (keeps retrying)
static char oilReply[40] = "(not asked yet)";                          // last raw reply to the oil request, for the Debug page
static uint32_t lastOilTry = 0;
#define OIL_FRESH_MS 20000
uint32_t lastValidMs = 0;   // last time any PID returned valid data
bool linked = false;
volatile bool demo = DEMO_MODE_DEFAULT;
enum Page { PAGE_FACE, PAGE_CLUSTER, PAGE_DIAL_TACH, PAGE_DIAL_SPEEDO, PAGE_DIAL_VITALS, PAGE_MPH, PAGE_RPM, PAGE_COOLANT, PAGE_OIL, PAGE_BATT, PAGE_INTAKE, PAGE_AMBIENT, PAGE_FUEL, PAGE_THROTTLE, PAGE_LOAD, PAGE_DEBUG, PAGE_BRIGHTNESS, PAGE_COUNT };
volatile int page = PAGE_FACE;       // BOOT short press cycles through the visible pages (see pageVisible)

// ---------- Extra car-data gauges (battery, air temps, fuel, throttle, load) ----------
// Which of them the car answers is read at connect time (supported-PID bitmaps) and only those get a page.
enum Gauge { G_BATT, G_INTAKE, G_AMBIENT, G_FUEL, G_THROTTLE, G_LOAD, G_COUNT };   // same order as PAGE_BATT..PAGE_LOAD
struct GaugeDef { const char *title; uint8_t pid; bool show; };                    // pid 0 = adapter voltage (ATRV)
static const GaugeDef GAUGES[G_COUNT] = {
  {"BATTERY", 0, SHOW_PAGE_BATTERY}, {"INTAKE AIR", 0x0F, SHOW_PAGE_INTAKE}, {"OUTSIDE", 0x46, SHOW_PAGE_OUTSIDE},
  {"FUEL", 0x2F, SHOW_PAGE_FUEL}, {"THROTTLE", 0x11, SHOW_PAGE_THROTTLE}, {"ENGINE LOAD", 0x04, SHOW_PAGE_LOAD}};
static float gVal[G_COUNT];                       // battery V, temps in deg C, others in percent
static uint32_t gTs[G_COUNT], gLastTry[G_COUNT];
static uint8_t gFails[G_COUNT];
static bool gSupported[G_COUNT], gOk[G_COUNT];    // gOk: answered at least once
static obd::SupportSet sup;                       // supported PIDs of the engine computer
static char supText[2][28] = {"sup: (not read yet)", ""};

static void resetGauges() {
  for (int g = 0; g < G_COUNT; g++) { gVal[g] = 0; gTs[g] = gLastTry[g] = 0; gFails[g] = 0; gSupported[g] = gOk[g] = false; }
}
static bool gaugeVisible(int g) { return GAUGES[g].show && gSupported[g] && (gOk[g] || gFails[g] < 5); }
static bool clusterFailed = false;               // the landscape canvas could not be allocated
static bool dialFailed = false;                  // round board: the dial pages' buffers could not be allocated
static bool pageVisible(int p) {
#ifdef BOARD_ROUND28
  if (p == PAGE_CLUSTER) return SHOW_PAGE_CLUSTER && !dialFailed;
  if (p >= PAGE_DIAL_TACH && p <= PAGE_DIAL_VITALS) return SHOW_PAGE_DIALS && !dialFailed;
  if (p == PAGE_BRIGHTNESS) return SHOW_PAGE_BRIGHTNESS && !dialFailed;
#else
  if (p == PAGE_CLUSTER) return SHOW_PAGE_CLUSTER && !clusterFailed;
  if (p >= PAGE_DIAL_TACH && p <= PAGE_DIAL_VITALS) return false;   // round board only
  if (p == PAGE_BRIGHTNESS) return false;                           // round board only
#endif
  if (p >= PAGE_BATT && p <= PAGE_LOAD) return gaugeVisible(p - PAGE_BATT);
  if (p == PAGE_OIL) return oilValid;               // the Mazda says NO DATA; only shown if it ever answers
  return true;
}
static int nextPage(int p, int n) {
  for (int k = 0; k < n; k++) {
    int q = p;
    for (int i = 0; i < PAGE_COUNT; i++) { q = (q + 1) % PAGE_COUNT; if (pageVisible(q)) break; }
    p = q;
  }
  return p;
}
static int prevPage(int p, int n) {
  for (int k = 0; k < n; k++) {
    int q = p;
    for (int i = 0; i < PAGE_COUNT; i++) { q = (q + PAGE_COUNT - 1) % PAGE_COUNT; if (pageVisible(q)) break; }
    p = q;
  }
  return p;
}
static int visibleCount() { int n = 0; for (int p = 0; p < PAGE_COUNT; p++) if (pageVisible(p)) n++; return n; }
static int visibleIndex(int p) { int n = 0; for (int q = 0; q < p; q++) if (pageVisible(q)) n++; return n; }

// ---------- UI state (shared by loop() and the wait loops inside sendCmd) ----------
static int lastMood = -1;
static bool lastOpen = true;
static volatile bool forceDraw = true;
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
static volatile uint32_t faceFps = 0, faceFrameMs = 0, reqPerSecFace = 0;            // animation speed / car polling while the Face page is shown

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
  while (!gotPrompt && millis() - t0 < timeoutMs) delay(1);   // the display has its own task, nothing to keep alive here
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

// tag is "41" + PID in hex, e.g. "410C". Shared, unit-tested logic lives in obd_parse.h.
bool parsePid(const String &r, const char *tag, int nBytes, int *out) {
  return obd::dataBytes(r.c_str(), (int)strtol(tag + 2, nullptr, 16), nBytes, out);
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
static uint32_t pollCount = 0;   // completed speed+RPM cycles (background polls are not counted), for the req/s figure

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

// Items polled in the background: 0 coolant, 1 oil, 2+g extra gauge g.
static void pollGauge(int g) {
  gLastTry[g] = millis();
  bool ok = false, carData = true;
  int b[2];
  if (g == G_BATT) {
    float v = 0;
    String r = sendCmd("ATRV");                       // the adapter measures the battery itself: works with the engine off
    if (obd::parseAtrv(r.c_str(), &v)) { gVal[g] = v; ok = true; carData = false; }
    else if (sup.supported(0x42)) {                   // fallback: control module voltage from the car
      String r2 = obdQuery("0142");
      if (obd::dataBytes(r2.c_str(), 0x42, 2, b)) { gVal[g] = obd::moduleVolts(b[0], b[1]); ok = true; }
    }
  } else {
    char cmd[8]; snprintf(cmd, sizeof(cmd), "01%02X", GAUGES[g].pid);
    String r = obdQuery(cmd);
    if (obd::dataBytes(r.c_str(), GAUGES[g].pid, 1, b)) {
      gVal[g] = (g == G_INTAKE || g == G_AMBIENT) ? (float)obd::tempC(b[0]) : obd::percent(b[0]);
      ok = true;
    }
  }
  if (ok) { gOk[g] = true; gFails[g] = 0; gTs[g] = millis(); if (carData) lastValidMs = millis(); }
  else if (gFails[g] < 255) gFails[g]++;
}

static void pollItem(int it) {
  if (it >= 2) { pollGauge(it - 2); return; }
  int b[2];
  if (it == 0) {
    if (parsePid(obdQuery("0105"), "4105", 1, b)) { coolantC = b[0] - 40; tsCool = lastValidMs = millis(); }
  } else {
    String r = obdQuery("015C");
    lastOilTry = millis();
    strncpy(oilReply, r.length() ? r.c_str() : "(no reply)", sizeof(oilReply) - 1); oilReply[sizeof(oilReply) - 1] = 0;
    if (parsePid(r, "415C", 1, b)) { oilC = b[0] - 40; oilValid = true; oilFails = 0; tsOil = lastValidMs = millis(); }
    else if (oilFails < 255) oilFails++;
  }
}

// Oil temp (PID 015C) is optional and often unsupported. After a few failures only retry now and then
// so it does not slow down the other readings.
bool oilDue() {
  if (oilFails < 5) return true;
  return millis() - lastOilTry >= 8000;   // no answer so far: keep trying, but only every 8 s
}

// focus = the value of the page on screen (poll it often); otherwise background rotation (poll it only when due).
static bool itemDue(int it, bool focus) {
  if (it == 0) return true;
  if (it == 1) return oilDue();
  int g = it - 2;
  if (!gSupported[g]) return false;
  uint32_t now = millis();
  if (gFails[g] >= 5) return now - gLastTry[g] >= 8000;                       // not answering: retry rarely
  return focus || now - gLastTry[g] >= (g == G_BATT ? 4000u : 1500u);
}
static int focusItem() {
  int p = page;
  if (p == PAGE_COOLANT) return 0;
  if (p == PAGE_OIL) return oilValid ? 1 : -1;
  if (p >= PAGE_BATT && p <= PAGE_LOAD && gaugeVisible(p - PAGE_BATT)) return 2 + (p - PAGE_BATT);
  return -1;
}
// Background rotation: coolant every second tick (the mood depends on it), the rest take turns.
static void pollSlowNext() {
  static bool coolTurn = true;
  static int cursor = 0;                                   // over oil + gauges
  if (coolTurn) { coolTurn = false; pollItem(0); return; }
  coolTurn = true;
  const int total = 1 + G_COUNT;
  for (int i = 0; i < total; i++) {
    int k = (cursor + i) % total;
    if (itemDue(1 + k, false)) { cursor = (k + 1) % total; pollItem(1 + k); return; }
  }
}

// Reads which standard values the engine computer supports (bitmaps 0100/0120/0140/0160), decides which
// extra pages exist, and prepares the compact list shown on the Debug page.
static void discoverPids(String r0) {
  sup.clear();
  for (int g = 0; g < G_COUNT; g++) gSupported[g] = gOk[g] = false;
  uint32_t m = 0;
  for (int attempt = 0; attempt < 3 && !obd::parseBitmap(r0.c_str(), 0x00, &m); attempt++) { delay(700); r0 = sendCmd("0100", 4000); }
  if (obd::parseBitmap(r0.c_str(), 0x00, &m)) sup.set(0x00, m);
  for (int base = 0x20; base <= 0x60; base += 0x20) {
    if (!sup.supported(base)) break;                       // PID base set = the next block exists
    char cmd[8]; snprintf(cmd, sizeof(cmd), "01%02X", base);
    String r = sendCmd(cmd, INIT_TIMEOUT_MS);
    if (obd::parseBitmap(r.c_str(), base, &m)) sup.set(base, m); else break;
  }
  for (int g = 0; g < G_COUNT; g++) gSupported[g] = (GAUGES[g].pid == 0) ? true : sup.supported(GAUGES[g].pid);   // battery: ATRV is tried
  static const uint8_t listed[] = {0x04, 0x05, 0x0C, 0x0D, 0x0F, 0x11, 0x2F, 0x42, 0x46, 0x5C};
  snprintf(supText[0], sizeof(supText[0]), "sup:%d", sup.count());
  supText[1][0] = 0;
  int n0 = 0;
  for (uint8_t p : listed) {
    if (!sup.supported(p)) continue;
    char *dst = (n0++ < 5) ? supText[0] : supText[1];
    size_t len = strlen(dst);
    snprintf(dst + len, sizeof(supText[0]) - len, " %02X", p);
  }
  if (!sup.have[0]) snprintf(supText[0], sizeof(supText[0]), "sup: none read");
  Serial.printf("discovery: %s%s%s\n", supText[0], supText[1][0] ? " |" : "", supText[1]);
}

// Fake data that sweeps through every mood: idle, driving, then a slow overheat.
void demoStep() {
  float t = millis() / 1000.0f;
  rpm = 800 + (int)(3400 * (0.5f + 0.5f * sinf(t * 0.6f)));   // idle .. 4200 (excited at the top)
  mph = (int)(70 * (0.5f + 0.5f * sinf(t * 0.3f)));
  float cyc = fmodf(t, 60.0f);                       // 60 s loop
  coolantC = 88 + (int)(cyc < 40 ? cyc * 0.2f : 8 + (cyc - 40) * 1.0f);   // 88 -> 96 -> 116
  oilC = 80 + (int)(cyc * 0.4f); oilValid = true;                           // 80 -> 104
  float thr = (rpm - 800) / 3400.0f * 100.0f; if (thr < 0) thr = 0; if (thr > 100) thr = 100;
  uint32_t nowMs = millis();
  for (int g = 0; g < G_COUNT; g++) { gSupported[g] = gOk[g] = true; gTs[g] = nowMs; gFails[g] = 0; }
  gVal[G_BATT] = rpm > 600 ? 14.1f + 0.15f * sinf(t) : 12.4f;
  gVal[G_INTAKE] = 28 + 14 * (0.5f + 0.5f * sinf(t * 0.2f));
  gVal[G_AMBIENT] = 21;
  gVal[G_FUEL] = 100 - fmodf(t, 200.0f) * 0.4f;
  gVal[G_THROTTLE] = thr;
  gVal[G_LOAD] = 20 + thr * 0.6f;
  if (fmodf(t, 75.0f) <= 65.0f) tsRpm = tsMph = tsCool = tsOil = lastValidMs = millis();   // else: 10 s "engine off" gap
}

// Mood priority: sleepy > worried > uneasy (engine health first), then excited > calm > happy.
static Mood rawMood() {
  uint32_t now = millis();
  // No data yet right after power-up is NOT sleep: the link needs a few seconds to come up. Only a car that has
  // been silent for a long time (or one that answered before and went quiet) makes the face sleepy.
  static bool everData = false;
  if (lastValidMs) everData = true;
  if (now - lastValidMs > STALE_AFTER_MS && (everData || now > BOOT_SLEEP_GRACE_MS)) return SLEEPY;   // engine off / no data
  // Coolant only counts when it is a recent, plausible reading (no stale / garbled value can turn the face angry).
  bool coolOk = fresh(tsCool, SLOW_FRESH_MS * 3) && coolantC > -30 && coolantC < 150;
  if (coolOk && coolantC >= TEMP_WORRIED_C) return WORRIED;
  if (coolOk && coolantC >= TEMP_UNEASY_C) return UNEASY;

  // Excited: high revs (with hysteresis so it doesn't flicker) or hard acceleration.
  // Acceleration is judged only between two consecutive fresh speed samples, so the first reading after
  // boot / a reconnect / a stale gap just seeds prevMph and can never look like a sudden jump.
  static bool highRev = false, prevValid = false;
  static int prevMph = 0;
  static uint32_t lastSpeedSample = 0, excitedUntil = 0;
  bool speedFresh = fresh(tsMph, FAST_FRESH_MS), rpmFresh = fresh(tsRpm, FAST_FRESH_MS);
  if (!speedFresh) { prevValid = false; excitedUntil = 0; }
  if (!rpmFresh) highRev = false;
  if (rpmFresh && !highRev && rpm >= RPM_EXCITED_ON) { highRev = true; exciteWhy = 1; exciteAt = now; }
  else if (highRev && rpm < RPM_EXCITED_OFF) highRev = false;
  if (speedFresh && now - lastSpeedSample >= 1000) {
    if (prevValid && mph - prevMph >= ACCEL_EXCITED_MPH_PER_S) { excitedUntil = now + 3000; exciteWhy = 2; exciteAt = now; }
    prevMph = mph; prevValid = true;
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

// The face only changes mood once the new mood has held steadily for a moment, so a one-off odd reading, a
// reconnect or a data gap can never flash another expression. Calm things settle slower than urgent ones.
Mood computeMood() {
  uint32_t now = millis();
  Mood raw = rawMood();
  static Mood cur = HAPPY, cand = HAPPY; static uint32_t candSince = 0;
  if (raw == cur) { cand = cur; return cur; }
  if (raw != cand) { cand = raw; candSince = now; }
  uint32_t hold = (raw == WORRIED || raw == UNEASY) ? 3000 : raw == SLEEPY ? 1500 : raw == EXCITED ? 600 : 1500;
  if (now - candSince >= hold) cur = raw;
  return cur;
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
  logFile.println(TEMP_UNIT_F ? "ms,rpm,mph,coolant_f,oil_f,batt_v,intake_f,ambient_f,fuel_pct,throttle_pct,load_pct"
                              : "ms,rpm,mph,coolant_c,oil_c,batt_v,intake_c,ambient_c,fuel_pct,throttle_pct,load_pct");
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
  char row[160];
  int len = snprintf(row, sizeof(row), "%lu,%d,%d,%d,", (unsigned long)now, rpm, mph, tempOut(coolantC));
  if (oilValid) len += snprintf(row + len, sizeof(row) - len, "%d", tempOut(oilC));
  for (int g = 0; g < G_COUNT; g++) {            // blank when unknown
    row[len++] = ',';
    if (!gOk[g] || !fresh(gTs[g], 60000)) continue;
    if (g == G_BATT) len += snprintf(row + len, sizeof(row) - len, "%.1f", gVal[g]);
    else if (g == G_INTAKE || g == G_AMBIENT) len += snprintf(row + len, sizeof(row) - len, "%d", tempOut((int)lroundf(gVal[g])));
    else len += snprintf(row + len, sizeof(row) - len, "%d", (int)(gVal[g] + 0.5f));
  }
  row[len++] = '\n'; row[len] = 0;
  logFile.print(row);
  if (millis() - lastFlush >= 5000) { lastFlush = millis(); uint32_t t = millis(); logFile.flush(); sdWriteMs = millis() - t; }
}

// ---------- LED ----------
void setLed(uint8_t r, uint8_t g, uint8_t b) {
#if PIN_LED >= 0
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
#else
  (void)r; (void)g; (void)b;                    // this board has no RGB LED
#endif
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

// Status text ("Scanning...", "Link lost", ...) is requested from the main task and drawn by the UI task
// (only one task may touch the display). It stays until clearStatus(); the welcome animation has priority.
static portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
static char statusMsg[24];
static uint16_t statusColor = 0;
static volatile uint32_t statusVer = 0;
static volatile bool statusActive = false;

void showStatus(const char *msg, uint16_t color) {
  portENTER_CRITICAL(&statusMux);
  strncpy(statusMsg, msg, sizeof(statusMsg) - 1); statusMsg[sizeof(statusMsg) - 1] = 0;
  statusColor = color; statusVer = statusVer + 1; statusActive = true;
  portEXIT_CRITICAL(&statusMux);
  Serial.println(msg);
}
void clearStatus() { if (!statusActive) return; statusActive = false; forceDraw = true; }   // no-op when nothing is shown (no needless redraws)

#ifdef BOARD_ROUND28
static void drawStatusScreen() {
  char msg[24]; uint16_t col;
  portENTER_CRITICAL(&statusMux);
  memcpy(msg, statusMsg, sizeof(msg)); col = statusColor;
  portEXIT_CRITICAL(&statusMux);
  gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
  gfx->setTextSize(4);
  gfx->setTextColor(col);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2 - x1, 214);
  gfx->print(msg);
  gfx->flush();
}

#else
static void drawStatusScreen() {
  char msg[24]; uint16_t col;
  portENTER_CRITICAL(&statusMux);
  memcpy(msg, statusMsg, sizeof(msg)); col = statusColor;
  portEXIT_CRITICAL(&statusMux);
  gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(col);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2, 150);
  gfx->print(msg);
}

#endif
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

// ---------- Face-page background (round board): a soft vertical gradient, dithered so it shows no bands ----------
#ifdef BOARD_ROUND28
static void paintFaceBg(int x0, int y0, int w, int h) {
  static const uint8_t bay[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  uint16_t *fb = fullCanvas->getFramebuffer();
  if (x0 < 0) { w += x0; x0 = 0; } if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > LCD_W) w = LCD_W - x0; if (y0 + h > LCD_H) h = LCD_H - y0;
  for (int y = y0; y < y0 + h; y++) {
    float f = (float)y / (LCD_H - 1);
    int r12 = (int)((5 + 15 * f) * 16), g12 = (int)((10 + 2 * f) * 16), b12 = (int)((26 + 20 * f) * 16);   // deep navy at the top, indigo at the bottom (x16)
    uint16_t *row = fb + y * LCD_W;
    for (int x = x0; x < x0 + w; x++) {
      int d = bay[y & 3][x & 3];
      int rq = (r12 + d * 8) >> 7, gq = (g12 + d * 4) >> 6, bq = (b12 + d * 8) >> 7;
      row[x] = (uint16_t)(((rq > 31 ? 31 : rq) << 11) | ((gq > 63 ? 63 : gq) << 5) | (bq > 31 ? 31 : bq));
    }
  }
}
// Clears a rectangle to the page's background: the gradient on the face page, black elsewhere.
static void clearBg(int x, int y, int w, int h) { if (page == PAGE_FACE) paintFaceBg(x, y, w, h); else gfx->fillRect(x, y, w, h, BLACK); }
#else
static void paintFaceBg(int x, int y, int w, int h) { gfx->fillRect(x, y, w, h, BLACK); }
static void clearBg(int x, int y, int w, int h) { gfx->fillRect(x, y, w, h, BLACK); }
#endif

// One dot per visible page, the current one white. Spacing shrinks when there are many pages.
#ifdef BOARD_ROUND28
// The round panel clips the corners, so the dots sit on a short row at the top (y is ignored).
static void drawPageDots(int) {
  int n = visibleCount(), cur = visibleIndex(page);
  int step = n > 9 ? 14 : 18;
  const int y = 28;
  clearBg(120, y - 8, 240, 16);
  for (int i = 0; i < n; i++)
    gfx->fillCircle(LCD_W / 2 + (int)((i - (n - 1) / 2.0f) * step), y, 5, i == cur ? WHITE : 0x39E7);
}

#else
static void drawPageDots(int y) {
  int n = visibleCount(), cur = visibleIndex(page);
  int step = n > 9 ? 12 : 16;
  gfx->fillRect(0, y - 6, LCD_W, 12, BLACK);
  for (int i = 0; i < n; i++)
    gfx->fillCircle(LCD_W / 2 + (int)((i - (n - 1) / 2.0f) * step), y, 4, i == cur ? WHITE : 0x39E7);
}

#endif
// Text, colour and freshness for a data page. big = the number as shown ("54", "12.6").
void pageInfo(int p, char *big, size_t n, bool *valid, const char **title, const char **unit, uint16_t *color) {
  *valid = true; *color = WHITE;
  switch (p) {
    case PAGE_MPH:     snprintf(big, n, "%d", mph); *title = "SPEED"; *unit = "MPH"; *valid = fresh(tsMph, FAST_FRESH_MS); break;
    case PAGE_RPM:     snprintf(big, n, "%d", rpm); *title = "ENGINE"; *unit = "RPM"; *valid = fresh(tsRpm, FAST_FRESH_MS); break;
    case PAGE_COOLANT: snprintf(big, n, "%d", tempOut(coolantC)); *title = "COOLANT"; *unit = tempUnit(); *valid = fresh(tsCool, SLOW_FRESH_MS);
      if (coolantC >= TEMP_WORRIED_C) *color = RED; else if (coolantC >= TEMP_UNEASY_C) *color = YELLOW; break;
    case PAGE_OIL:     snprintf(big, n, "%d", tempOut(oilC)); *title = "OIL TEMP"; *unit = tempUnit(); *valid = oilValid && fresh(tsOil, OIL_FRESH_MS); break;
    default: {         // extra gauges
      int g = p - PAGE_BATT;
      if (g < 0 || g >= G_COUNT) { snprintf(big, n, "--"); *title = "?"; *unit = ""; *valid = false; break; }
      *title = GAUGES[g].title;
      *valid = gOk[g] && fresh(gTs[g], GAUGE_FRESH_MS);
      float v = gVal[g];
      switch (g) {
        case G_BATT: {
          snprintf(big, n, "%.1f", v); *unit = "VOLTS";
          bool running = rpm > 600;
          if (v < BATT_CRIT_V) *color = RED; else if (v < (running ? BATT_LOW_RUN_V : BATT_LOW_OFF_V)) *color = YELLOW;
          break;
        }
        case G_INTAKE: case G_AMBIENT: snprintf(big, n, "%d", tempOut((int)lroundf(v))); *unit = tempUnit(); break;
        case G_FUEL: snprintf(big, n, "%d", (int)(v + 0.5f)); *unit = "PERCENT"; if (v < FUEL_LOW_PCT) *color = YELLOW; if (v < FUEL_LOW_PCT / 2) *color = RED; break;
        default: snprintf(big, n, "%d", (int)(v + 0.5f)); *unit = "PERCENT"; break;
      }
    }
  }
}

// full = also draw the static parts (title, unit, page dots); otherwise only the number is redrawn.
#ifdef BOARD_ROUND28
void drawDataPage(bool stale, bool full) {
  char big[16]; bool valid; const char *title, *unit; uint16_t color;
  pageInfo(page, big, sizeof(big), &valid, &title, &unit, &color);
  int16_t x1, y1; uint16_t w, h;
  if (full) {
    gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
    gfx->setTextSize(strlen(title) > 9 ? 4 : 5);      // long titles ("ENGINE LOAD") need the smaller size
    gfx->setTextColor(0x7BEF);
    gfx->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((LCD_W - w) / 2 - x1, 84);
    gfx->print(title);
    gfx->setTextSize(5);
    gfx->getTextBounds(unit, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((LCD_W - w) / 2 - x1, 336);
    gfx->print(unit);
    drawPageDots(0);
  }
  if (!valid || stale) { snprintf(big, sizeof(big), "--"); color = 0x7BEF; }
  gfx->fillRect(0, 150, LCD_W, 170, BLACK);   // number area only
  gfx->setTextSize(strlen(big) <= 3 ? 14 : 11);
  gfx->setTextColor(color);
  gfx->getTextBounds(big, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2 - x1, 235 - h / 2 - y1);
  gfx->print(big);
  gfx->flush();
}

#else
void drawDataPage(bool stale, bool full) {
  char big[16]; bool valid; const char *title, *unit; uint16_t color;
  pageInfo(page, big, sizeof(big), &valid, &title, &unit, &color);
  int16_t x1, y1; uint16_t w, h;
  if (full) {
    gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
    gfx->setTextSize(strlen(title) > 9 ? 2 : 3);      // long titles ("ENGINE LOAD") need the smaller size
    gfx->setTextColor(0x7BEF);
    gfx->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((LCD_W - w) / 2, 40);
    gfx->print(title);
    gfx->setTextSize(3);
    gfx->getTextBounds(unit, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((LCD_W - w) / 2, 230);
    gfx->print(unit);
    drawPageDots(300);
  }
  if (!valid || stale) { snprintf(big, sizeof(big), "--"); color = 0x7BEF; }
  gfx->fillRect(0, 100, LCD_W, 110, BLACK);   // number area only
  gfx->setTextSize(strlen(big) <= 3 ? 8 : 6);
  gfx->setTextColor(color);
  gfx->getTextBounds(big, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2, 135);
  gfx->print(big);
}

#endif
// Debug page: link state and the last raw adapter exchanges (newest at the bottom).
#ifdef BOARD_ROUND28
void drawDebugPage() {
  gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
  gfx->setTextSize(3);
  gfx->setTextColor(0x07FF);
  gfx->setCursor(195, 50);
  gfx->print("DEBUG");
  gfx->setTextSize(1);
  gfx->setTextColor(0xFFE0);
  gfx->setCursor(150, 78); gfx->print(FW_TAG " " __DATE__ " " __TIME__);
  uint32_t now = millis();
  const int X = 84, P = 18;                       // text size 2 (12 px glyphs), 27 chars fit inside the circle
  int y = 96;
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE);
  gfx->setCursor(X, y); gfx->printf("link:%s f:%s c:%s %lu req/s", linked ? "OK" : "--", useFilter ? "Y" : "N", useCount ? "Y" : "N", (unsigned long)reqPerSec); y += P;
  gfx->setCursor(X, y); gfx->printf("mph:%d  age:%ldms", mph, tsMph ? (long)(now - tsMph) : -1L); y += P;
  gfx->setCursor(X, y); gfx->printf("rpm:%d  age:%ldms", rpm, tsRpm ? (long)(now - tsRpm) : -1L); y += P;
  gfx->setCursor(X, y); gfx->printf("cool:%d oil:%d %s b:%.1fV", tempOut(coolantC), oilValid ? tempOut(oilC) : -999, TEMP_UNIT_F ? "F" : "C", gOk[G_BATT] ? gVal[G_BATT] : 0.0f); y += P;
  gfx->setTextColor(0x7BEF);
  gfx->setCursor(X, y); gfx->printf("cmd:%lums gap:%lums", (unsigned long)lastCmdMs, (unsigned long)maxTickGapMs); y += P;
  gfx->setCursor(X, y); gfx->printf("sd:%s %lums", sdOk ? "on" : "off", (unsigned long)sdWriteMs); y += P;
  gfx->setCursor(X, y); gfx->printf("oil: %s", oilReply); y += P;
  gfx->setCursor(X, y); gfx->printf("face:%lufps %lums", (unsigned long)faceFps, (unsigned long)faceFrameMs); y += P;
  gfx->setTextColor(0x07FF);
  gfx->setCursor(X, y); gfx->print(supText[0]); y += P;
  gfx->setCursor(X, y); gfx->print(supText[1]); y += P;
  gfx->setTextColor(0xFFE0);
  gfx->setCursor(X, y); gfx->print(bootText); y += P;
  gfx->setTextColor(0xFFFF);
  gfx->setCursor(X, y); gfx->printf("mood:%d %s", lastMood, faceName); y += P;
  gfx->setTextSize(1);
  gfx->setCursor(X, y); gfx->print("chg:");
  for (int k = 0; k < 3; k++) { int i = (chgHead + k) % 3; if (chgAt[i]) gfx->printf(" %lus %s", (unsigned long)(chgAt[i] / 1000), chgName[i]); }
  y += 16;
  for (int k = 0; k < 5 && y < 424; k++) {        // last adapter exchanges, oldest first
    DbgEntry &d = dbg[(dbgHead + k) % 5];
    if (!d.cmd[0]) continue;
    gfx->setCursor(120, y); gfx->setTextColor(0xFFE0); gfx->printf("> %s%s", d.cmd, d.timeout ? " (timeout)" : ""); y += 10;
    for (int off = 0; off < (int)strlen(d.reply) && off < 76 && y < 424; off += 38) {   // wrap reply at 38 chars
      char line[39]; strncpy(line, d.reply + off, 38); line[38] = 0;
      gfx->setCursor(120, y); gfx->setTextColor(WHITE); gfx->print(line); y += 10;
    }
    y += 4;
  }
  drawPageDots(0);
  gfx->flush();
}

#else
void drawDebugPage() {
  gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(0x07FF);
  gfx->setCursor(8, 6);
  gfx->print("DEBUG");
  gfx->setTextSize(1);
  gfx->setTextColor(0xFFE0);
  gfx->setCursor(86, 6);  gfx->print(FW_TAG);
  gfx->setCursor(86, 17); gfx->print(__DATE__ " " __TIME__);
  gfx->setTextColor(WHITE);
  uint32_t now = millis();
  gfx->setCursor(4, 30);  gfx->printf("link:%s f:%s c:%s %lu req/s", linked ? "OK" : "--", useFilter ? "Y" : "N", useCount ? "Y" : "N", (unsigned long)reqPerSec);
  gfx->setCursor(4, 42);  gfx->printf("mph:%d  age:%ldms", mph, tsMph ? (long)(now - tsMph) : -1L);
  gfx->setCursor(4, 54);  gfx->printf("rpm:%d  age:%ldms", rpm, tsRpm ? (long)(now - tsRpm) : -1L);
  gfx->setCursor(4, 66);  gfx->printf("cool:%d oil:%d %s b:%.1fV", tempOut(coolantC), oilValid ? tempOut(oilC) : -999, TEMP_UNIT_F ? "F" : "C", gOk[G_BATT] ? gVal[G_BATT] : 0.0f);
  gfx->setTextColor(0x7BEF);
  gfx->setCursor(4, 78);  gfx->printf("cmd:%lums wr:%lums gap:%lums", (unsigned long)lastCmdMs, (unsigned long)lastWriteMs, (unsigned long)maxTickGapMs);
  gfx->setCursor(4, 90);  gfx->printf("sd:%s %lums", sdOk ? "on" : "off", (unsigned long)sdWriteMs);
  gfx->setCursor(4, 102); gfx->printf("oil: %s", oilReply);
  gfx->setCursor(4, 114); gfx->printf("face:%lufps %lums req:%lu", (unsigned long)faceFps, (unsigned long)faceFrameMs, (unsigned long)reqPerSecFace);
  gfx->setTextColor(0x07FF);
  gfx->setCursor(4, 126); gfx->print(supText[0]);
  gfx->setCursor(4, 137); gfx->print(supText[1]);
  gfx->setTextColor(0xFFE0);
  gfx->setCursor(4, 148); gfx->print(bootText);
  gfx->setTextColor(0xFFFF);
  gfx->setCursor(4, 158);
  gfx->printf("mood:%d %s ex:%s %lds", lastMood, faceName, exciteWhy == 1 ? "rev" : exciteWhy == 2 ? "acc" : "-", exciteWhy ? (long)((now - exciteAt) / 1000) : 0L);
  gfx->setTextColor(0xFFFF);
  gfx->setCursor(4, 168); gfx->print("chg:");
  for (int k = 0; k < 3; k++) { int i = (chgHead + k) % 3; if (chgAt[i]) gfx->printf(" %lus %s", (unsigned long)(chgAt[i] / 1000), chgName[i]); }
  gfx->setTextColor(0x7BEF);
  int y = 182;
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
  drawPageDots(308);
}

#endif
// ---------- Input ----------
// Short press: next page (Face > MPH > RPM > Coolant > Oil temp). Long press (LONG_PRESS_MS): toggle demo mode.
// Edges are caught by an interrupt, so a quick click is never missed while the main loop is busy
// talking to the adapter or redrawing. handleButton() then acts on what the interrupt recorded.
static volatile bool btnDown = false, btnLongFired = false;
static volatile uint32_t btnDownMs = 0, btnLastEdgeMs = 0;
static volatile uint8_t btnShortPresses = 0, btnPrevPresses = 0;   // taps / right swipes (previous page)

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

#ifdef BOARD_ROUND28
// The touch screen acts as the button: a tap is a short press, holding a finger down LONG_PRESS_MS toggles demo mode, and a
// horizontal swipe changes the page: swipe left = next page, swipe right = previous page.
static const int SWIPE_MIN_PX = 60, TAP_MAX_PX = 24;
static void pollTouch() {
  static uint32_t lastPoll = 0, lastReport = 0;
  static int sx = 0, sy = 0, lx = 0, ly = 0;
  static bool slider = false;               // the finger went down on the brightness slider: it drags the slider, nothing else
  uint32_t ms = millis();
  if (ms - lastPoll < 15) return;
  lastPoll = ms;
  bool dn = false; int x = 0, y = 0;
  bool got = touchPoll(&dn, &x, &y);
  if (got) lastReport = ms;
  bool release = (got && !dn) || (btnDown && !got && ms - lastReport > 250);   // no report for a while also counts as lifted
  if (got && dn) {
    if (!btnDown) { btnDown = true; btnDownMs = ms; btnLongFired = false; sx = x; sy = y; slider = (page == PAGE_BRIGHTNESS && y >= rui::BR_ZONE_Y0 && y <= rui::BR_ZONE_Y1); }
    lx = x; ly = y;
    if (slider) { btnLongFired = true; int pct = rui::pctFromX(x); if (pct != backlightPct) setBacklight(pct); }   // live: the screen dims or brightens under the finger
    else if (!btnLongFired && (abs(lx - sx) > TAP_MAX_PX || abs(ly - sy) > TAP_MAX_PX)) btnLongFired = true;   // the finger is moving: not a tap, not a long press
  } else if (release && btnDown) {
    btnDown = false;
    if (slider) { slider = false; saveBacklight(); Serial.printf("brightness %d %%\n", backlightPct); return; }   // keep the level across restarts
    int dx = lx - sx, dy = ly - sy;
    Serial.printf("touch: from %d,%d to %d,%d (dx %d dy %d) -> %s\n", sx, sy, lx, ly, dx, dy, (abs(dx) >= SWIPE_MIN_PX && abs(dx) * 2 > abs(dy) * 3) ? (((dx < 0) != (SWIPE_NEXT_ON_RIGHT != 0)) ? "swipe next" : "swipe previous") : (btnLongFired ? "ignored" : "tap"));
    if (abs(dx) >= SWIPE_MIN_PX && abs(dx) * 2 > abs(dy) * 3) {                 // mostly horizontal
      bool next = (dx < 0) != (SWIPE_NEXT_ON_RIGHT != 0);                    // SWIPE_NEXT_ON_RIGHT flips the direction (config.h)
      if (next) { if (btnShortPresses < 5) btnShortPresses = btnShortPresses + 1; }
      else      { if (btnPrevPresses < 5) btnPrevPresses = btnPrevPresses + 1; }
    } else if (!btnLongFired && btnShortPresses < 5) btnShortPresses = btnShortPresses + 1;   // a tap
  }
}
#endif

void handleButton() {
#ifdef BOARD_ROUND28
  pollTouch();
#endif
  if (btnDown && !btnLongFired && millis() - btnDownMs >= LONG_PRESS_MS) {
    btnLongFired = true;
    demo = !demo;
    Serial.printf("demo mode %s\n", demo ? "ON" : "OFF");
    lastValidMs = 0;
    if (!demo) { coolantC = 0; tsCool = 0; resetGauges(); oilValid = false; oilFails = 0; }   // drop the made-up demo values and pages
    forceDraw = true;
  }
  noInterrupts();
  uint8_t n = btnShortPresses, pv = btnPrevPresses;
  btnShortPresses = 0; btnPrevPresses = 0;
  interrupts();
  if (n || pv) {
    if (n > pv) page = nextPage(page, n - pv); else if (pv > n) page = prevPage(page, pv - n);   // opposite swipes cancel out
    forceDraw = true;
  }
}

// ---------- Face page (animated character) ----------
static mochi::Mochi character;
static Arduino_Canvas *canvas = nullptr;   // stick: 172x180 face canvas pushed to the panel; round board: unused (the face is drawn scaled into the full-frame canvas)
#ifdef BOARD_ROUND28
#include "scaled_gfx.h"
#endif
static const int FACE_Y = (LCD_H - mochi::H) / 2;    // canvas position on the panel (centered)

static mochi::Expr moodExpr(Mood m) {
  switch (m) {
    case UNEASY:  return mochi::E_SWEAT;
    case WORRIED: return mochi::E_ANGRY;
    case SLEEPY:  return mochi::E_SLEEPY;
    case EXCITED: return mochi::E_TURBO;
    case CALM:    return mochi::E_SQUINT;
    default:      return mochi::E_DEFAULT;
  }
}

#if CAR_EVENTS
// Short face reactions to one-off car events (engine start, hard braking, low battery / fuel). The detector keeps
// sampling during the welcome / settle period (so its state stays right) but nothing is played until the face is settled.
static void carEventReact(uint32_t now, bool settling) {
  static carevents::Detector det([] {
    carevents::Config c;
    c.engineOffMs = EVENT_ENGINE_OFF_MS; c.brakeMinMph = EVENT_BRAKE_MIN_MPH; c.brakeDropMph = EVENT_BRAKE_DROP_MPH;
    c.battCritV = BATT_CRIT_V; c.fuelLowPct = EVENT_FUEL_LOW_PCT;
    return c;
  }());
  carevents::Input in;
  in.now = now;
  in.rpmFresh = fresh(tsRpm, FAST_FRESH_MS);   in.rpm = rpm;
  in.speedFresh = fresh(tsMph, FAST_FRESH_MS); in.mph = mph;
  in.battFresh = gOk[G_BATT] && fresh(gTs[G_BATT], GAUGE_FRESH_MS); in.battV = gVal[G_BATT];
  in.fuelFresh = gOk[G_FUEL] && fresh(gTs[G_FUEL], GAUGE_FRESH_MS); in.fuelPct = gVal[G_FUEL];
  carevents::Event ev = det.update(in);
  if (settling) return;
  switch (ev) {
    case carevents::EV_ENGINE_START: character.react(mochi::E_STARS, 1800, now); break;
    case carevents::EV_HARD_BRAKE:   character.react(mochi::E_STARTLE, 1200, now); break;
    case carevents::EV_LOW_BATTERY:
    case carevents::EV_LOW_FUEL:     character.react(mochi::E_CRYING, 2500, now); break;
    default: break;
  }
}
#endif

// One animation frame. Demo mode shows every expression in turn (with its name) for checking at a desk.
static void faceFrame(Mood m, bool moodChanged, bool entering, uint32_t now) {
  uint32_t t0 = millis();
  static int showIdx = 0; static uint32_t showAt = 0; static bool lastDemoSeen = false;
  // After the welcome the face stays on the plain default face for a quiet period: no mood change and no idle
  // reaction, so nothing can flash up while the link and the first readings are still settling.
  static bool wasIntro = true; static uint32_t settleUntil = 0; static int appliedMood = -1;
  bool introNow = character.introActive();
  if (wasIntro && !introNow) { settleUntil = now + FACE_SETTLE_MS; appliedMood = -1; }
  wasIntro = introNow;
  bool settling = introNow || now < settleUntil;
  if (entering) {
    paintFaceBg(0, 0, LCD_W, LCD_H);
    drawPageDots(300);
    if (!settling) character.setBase(moodExpr(m), now);
  }
  bool d = demo;
  if (d && !lastDemoSeen) { character.startIntro(now); showIdx = 0; showAt = now + 5200; }   // demo ON replays the welcome
  if (!d && lastDemoSeen) {                       // demo just turned OFF: back to the real mood face
    character.setBase(moodExpr(m), now);
    clearBg(UI_LABEL_X0, UI_LABEL_Y, UI_LABEL_W, UI_LABEL_H);      // remove the expression name label
  }
  lastDemoSeen = d;
  if (d) {
    if (!character.introActive() && now >= showAt) {
      mochi::Expr e = (mochi::Expr)showIdx;
      showIdx = (showIdx + 1) % mochi::E_COUNT;
      if (e == mochi::E_TITLE) { e = (mochi::Expr)showIdx; showIdx = (showIdx + 1) % mochi::E_COUNT; }
      character.setBase(e, now);
      showAt = now + 2800;
      clearBg(UI_LABEL_X0, UI_LABEL_Y, UI_LABEL_W, UI_LABEL_H);
      gfx->setTextSize(UI_LABEL_SIZE); gfx->setTextColor(0x7BEF);
      int16_t x1, y1; uint16_t w, h; const char *nm = mochi::exprName(e);
      gfx->getTextBounds(nm, 0, 0, &x1, &y1, &w, &h);
      gfx->setCursor((LCD_W - w) / 2, UI_LABEL_TY); gfx->print(nm);
    }
  } else {
    if (!settling && ((int)m != appliedMood || entering)) {
      character.setBase(moodExpr(m), now);
      if (lastMood == SLEEPY && m != SLEEPY) character.react(mochi::E_WAKE, 1000, now);   // car data is back: wake up
      appliedMood = (int)m;
    }
    if (!settling) character.idleTick(now, mph == 0);
#if CAR_EVENTS
    carEventReact(now, settling);
#endif
  }
  character.update(now);
  faceName = mochi::exprName(character.current());
  { static mochi::Expr lastE = mochi::E_COUNT;
    if (character.current() != lastE) { lastE = character.current(); chgAt[chgHead] = now; chgName[chgHead] = faceName; chgHead = (chgHead + 1) % 3; } }
#ifdef BOARD_ROUND28
  {
    static bool wasIntroDraw = false;
    bool introDraw = character.introActive();
    const int FX = (LCD_W - mochi::W * FACE_SCALE) / 2, FY = (LCD_H - mochi::H * FACE_SCALE) / 2;
    if (introDraw) paintFaceBg(0, 0, LCD_W, LCD_H);                  // the welcome's road lines reach past the character's own canvas: repaint everything
    else if (wasIntroDraw) { paintFaceBg(0, 0, LCD_W, LCD_H); drawPageDots(300); }   // welcome over: bring the page dots back
    else paintFaceBg(FX, FY, mochi::W * FACE_SCALE, mochi::H * FACE_SCALE);
    wasIntroDraw = introDraw;
    ScaledGfx<Arduino_GFX> sg{gfx, FACE_SCALE, FX, FY};
    character.render(&sg, now, false);                              // the gradient is the background
  }
  gfx->flush();
#else
  character.render(canvas, now);
  canvas->flush();
#endif

  uint32_t dt = millis() - t0;
  static uint32_t frames = 0, winStart = 0;
  faceFrameMs = dt; frames++;
  if (now - winStart >= 1000) { faceFps = frames; frames = 0; winStart = now; }
}

// ---------- Rally cluster page (landscape 320x172, drawn in two 320x86 strips) ----------
// No full-frame canvas: that needs a 110 KB contiguous block that the heap does not have next to the face canvas.
// The 62 KB face canvas buffer is reused as the strip buffer (the face is not drawn while this page is shown).
static uint16_t *clusterBuf = nullptr;
static const int CLUSTER_STRIP = 86;
static bool landscape = false;                      // the panel is currently rotated for the cluster
static void clusterFrame(uint32_t now) {
  if (!clusterBuf) {
#ifdef BOARD_ROUND28
    // Round board: the whole 320x172 picture in one buffer (internal RAM if it fits, else PSRAM), then scaled 1.25x into the canvas.
    clusterBuf = (uint16_t *)heap_caps_malloc(cluster::W * cluster::H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!clusterBuf) clusterBuf = (uint16_t *)heap_caps_malloc(cluster::W * cluster::H * 2, MALLOC_CAP_SPIRAM);
#else
#if MOCHI_FACE
    if (canvas) clusterBuf = canvas->getFramebuffer();
#endif
    if (!clusterBuf) clusterBuf = (uint16_t *)heap_caps_malloc(cluster::W * CLUSTER_STRIP * 2, MALLOC_CAP_8BIT);
#endif
    if (!clusterBuf) { Serial.println("cluster buffer allocation failed"); clusterFailed = true; page = nextPage(PAGE_CLUSTER, 1); forceDraw = true; return; }
  }
  // Values ease toward the real ones so the sweep and the digits glide instead of jumping at the 5 Hz poll rate.
  static float dRpm = 0, dMph = 0; static uint32_t lastT = 0, runStart = 0;
  float k = lastT ? 1.0f - expf(-(float)(now - lastT) / 90.0f) : 1.0f; lastT = now;
  dRpm += ((float)rpm - dRpm) * k; dMph += ((float)mph - dMph) * k;
  bool engineOn = fresh(tsRpm, FAST_FRESH_MS) && rpm > 400;
  if (engineOn) { if (!runStart) runStart = now; } else runStart = 0;
  cluster::Data d;
  d.rpm = (int)lroundf(dRpm); d.mph = (int)lroundf(dMph);
  d.haveCool = fresh(tsCool, SLOW_FRESH_MS * 3) && coolantC > -30; d.coolF = tempOut(coolantC); d.hotF = tempOut(TEMP_UNEASY_C);
  auto have = [&](int g) { return gOk[g] && fresh(gTs[g], GAUGE_FRESH_MS); };
  d.haveIntake = have(G_INTAKE); d.intakeF = tempOut((int)lroundf(gVal[G_INTAKE]));
  d.haveAmb = have(G_AMBIENT);   d.ambF = tempOut((int)lroundf(gVal[G_AMBIENT]));
  d.haveFuel = have(G_FUEL);     d.fuelPct = (int)lroundf(gVal[G_FUEL]);
  d.haveBatt = have(G_BATT);     d.battV = gVal[G_BATT];
  d.haveLoad = have(G_LOAD);     d.loadPct = (int)lroundf(gVal[G_LOAD]);
  d.haveThr = have(G_THROTTLE);  d.thrPct = (int)lroundf(gVal[G_THROTTLE]);
  d.runSec = runStart ? (int)((now - runStart) / 1000) : 0;
  d.live = now - lastValidMs <= STALE_AFTER_MS;
  d.redline = RPM_REDLINE; d.rate = (int)reqPerSec; d.tempF = TEMP_UNIT_F;
#ifdef BOARD_ROUND28
  cluster::Raster r(clusterBuf, cluster::H, 0);
  cluster::draw(&r, d, now);
  // Nearest-neighbour 1.25x (4 of every 5 source pixels, one repeated), centred; the 400x215 picture fits inside the circle.
  const int DW = cluster::W * 5 / 4, DH = cluster::H * 5 / 4;
  const int X0 = (LCD_W - DW) / 2, Y0 = (LCD_H - DH) / 2;
  static uint16_t xmap[cluster::W * 5 / 4];
  static bool mapped = false;
  if (!mapped) { for (int x = 0; x < DW; x++) xmap[x] = x * 4 / 5; mapped = true; }
  uint16_t *fb = fullCanvas->getFramebuffer();
  for (int y = 0; y < DH; y++) {
    const uint16_t *src = clusterBuf + (y * 4 / 5) * cluster::W;
    uint16_t *dst = fb + (Y0 + y) * LCD_W + X0;
    for (int x = 0; x < DW; x++) dst[x] = src[xmap[x]];
  }
  gfx->flush();
#else
  for (int y0 = 0; y0 < cluster::H; y0 += CLUSTER_STRIP) {
    int rows = cluster::H - y0 < CLUSTER_STRIP ? cluster::H - y0 : CLUSTER_STRIP;
    cluster::Raster r(clusterBuf, rows, y0);
    cluster::draw(&r, d, now);
    gfx->draw16bitRGBBitmap(0, y0, clusterBuf, cluster::W, rows);
  }
#endif
}

#ifdef BOARD_ROUND28
// Round-board dial pages (src/round_ui.h): sport cluster, analog tach, analog speedo, vitals rings.
static void roundDialFrame(int p, uint32_t now) {
  static bool started = false;
  if (!started) {
    started = true;
    if (!rui::begin()) { Serial.println("round UI buffers failed"); dialFailed = true; forceDraw = true; page = nextPage(page, 1); return; }
  }
  rui::Data d;
  d.rpm = (float)rpm; d.mph = (float)mph;
  d.haveCool = fresh(tsCool, SLOW_FRESH_MS * 3) && coolantC > -30; d.coolC = coolantC; d.coolShown = tempOut(coolantC);
  auto have = [&](int g) { return gOk[g] && fresh(gTs[g], GAUGE_FRESH_MS); };
  d.haveFuel = have(G_FUEL); d.fuelPct = (int)lroundf(gVal[G_FUEL]);
  d.haveBatt = have(G_BATT); d.battV = gVal[G_BATT];
  d.haveLoad = have(G_LOAD); d.loadPct = (int)lroundf(gVal[G_LOAD]);
  d.redline = RPM_REDLINE; d.hotC = TEMP_UNEASY_C; d.worryC = TEMP_WORRIED_C; d.fuelLowPct = FUEL_LOW_PCT;
  d.battCritV = BATT_CRIT_V; d.battLowV = BATT_LOW_OFF_V; d.unitF = TEMP_UNIT_F;
  d.dotN = visibleCount(); d.dotCur = visibleIndex(page);
  int pg = p == PAGE_CLUSTER ? rui::PG_SPORT : p == PAGE_DIAL_TACH ? rui::PG_TACH : p == PAGE_DIAL_SPEEDO ? rui::PG_SPEEDO : rui::PG_VITALS;
  uint32_t t0 = micros();
  uint16_t *fb = fullCanvas->getFramebuffer();
  rui::frame(fb, pg, d, now, forceDraw || drawnPage != page);   // redraw from scratch when another page was on screen
  uint32_t t1 = micros();
  // Send only the rows / spans that changed to the panel: a full 480x480 copy takes ~39 ms and tears while the panel scans out.
  // Rows go out in order, each one only after the panel's scan has passed it, so a row never shows two different versions.
  rui::flushDirty([&](int x, int y, int w) { waitBeamPast(y); rgbDisp->draw16bitRGBBitmap(x, y, fb + y * LCD_W + x, w, 1); });
  uint32_t t2 = micros();
  static uint32_t frames = 0, sumDraw = 0, sumFlush = 0, win = 0;   // timing line on the serial monitor, once per 2 s
  frames++; sumDraw += t1 - t0; sumFlush += t2 - t1;
  if (now - win >= 2000) {
    if (win) Serial.printf("dial %d: %u fps, draw %u us, flush %u us per frame\n", pg, (unsigned)(frames * 1000 / (now - win)), (unsigned)(sumDraw / frames), (unsigned)(sumFlush / frames));
    frames = sumDraw = sumFlush = 0; win = now;
  }
}
#endif

#ifdef BOARD_ROUND28
// Brightness page (round board): redrawn only when the level changes.
static void roundBrightnessFrame(bool full) {
  static int shown = -1;
  if (!rui::begin()) { Serial.println("round UI buffers failed"); dialFailed = true; forceDraw = true; page = nextPage(page, 1); return; }
  if (!full && shown == backlightPct) return;
  uint16_t *fb = fullCanvas->getFramebuffer();
  rui::brightnessPage(fb, backlightPct, visibleCount(), visibleIndex(page), full);
  shown = backlightPct;
  rui::flushDirty([&](int x, int y, int w) { waitBeamPast(y); rgbDisp->draw16bitRGBBitmap(x, y, fb + y * LCD_W + x, w, 1); });
}
#endif

// Draws whatever the current page needs. Runs only in the UI task; the main task never touches the display.
void uiTick() {
  static uint32_t lastTick = 0, gapWindowStart = 0, gapMax = 0;
  uint32_t t = millis();
  if (lastTick && t - lastTick > gapMax) gapMax = t - lastTick;   // longest stall between UI updates
  lastTick = t;
  if (t - gapWindowStart >= 5000) { maxTickGapMs = gapMax; gapMax = 0; gapWindowStart = t; }
  handleButton();
  if (!pageVisible(page)) { page = nextPage(page, 1); forceDraw = true; }   // the page on screen vanished (e.g. demo ended)

  uint32_t now = millis();
  // Values the car has stopped answering must not linger on screen or drive the mood.
  if (!fresh(tsMph, FAST_FRESH_MS)) mph = 0;
  if (!fresh(tsRpm, FAST_FRESH_MS)) rpm = 0;
  Mood m = computeMood();
  bool moodChanged = (int)m != lastMood;
  if (moodChanged || forceDraw) setLedMood(m);   // LED follows the mood on every page
#if MOCHI_FACE && MOCHI_INTRO
  {                                              // ...except during the welcome gauge, when it follows the needle
    static bool ledFromIntro = false; static uint8_t lr = 0, lg = 0, lb = 0;
    uint8_t r, g, b;
    if (character.introLed(&r, &g, &b)) {
      if (!ledFromIntro || r != lr || g != lg || b != lb) { setLed(r, g, b); lr = r; lg = g; lb = b; }
      ledFromIntro = true;
    } else if (ledFromIntro) { ledFromIntro = false; setLedMood(m); }
  }
#endif

  // Status text (scanning, connecting, link lost) takes over the screen, except during the welcome animation.
  static bool statusShown = false; static uint32_t shownStatusVer = 0;
  bool introOn = MOCHI_FACE && MOCHI_INTRO && character.introActive() && page == PAGE_FACE;
#ifndef BOARD_ROUND28
  {                                                // the cluster page is landscape; every other screen is portrait
    bool wantLand = page == PAGE_CLUSTER && !(statusActive && !demo);
    if (wantLand != landscape) { gfx->setRotation(wantLand ? CLUSTER_ROTATION : 0); landscape = wantLand; forceDraw = true; drawnPage = -1; }
  }
#endif
  if (statusActive && !introOn && !demo) {
    if (!statusShown || statusVer != shownStatusVer) { drawStatusScreen(); shownStatusVer = statusVer; statusShown = true; }
    drawnPage = -1;
    lastMood = (int)m;
    return;
  }
  if (statusShown) { statusShown = false; forceDraw = true; drawnPage = -1; }

  if (page == PAGE_FACE) {
#if MOCHI_FACE
    faceFrame(m, moodChanged, forceDraw || drawnPage != PAGE_FACE, now);
#else
    static bool blinkingOld = false; static uint32_t nextBlinkOld = 0, blinkEndOld = 0; static bool lastOpenOld = true;
    if (!blinkingOld && now >= nextBlinkOld) { blinkingOld = true; blinkEndOld = now + 150; }
    if (blinkingOld && now >= blinkEndOld) { blinkingOld = false; nextBlinkOld = now + random(2500, 5500); }
    bool open = !blinkingOld;
    if (forceDraw || moodChanged || drawnPage != PAGE_FACE) {
      gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
      drawEyes(m, open);
      drawMouthFor(m);
      lastOpenOld = open;
    } else if (m != SLEEPY && open != lastOpenOld) {
      drawEyes(m, open);
      lastOpenOld = open;
    }
#endif
  } else if (page == PAGE_CLUSTER || (page >= PAGE_DIAL_TACH && page <= PAGE_DIAL_VITALS)) {
#ifdef BOARD_ROUND28
    roundDialFrame(page, now);
#else
    clusterFrame(now);
#endif
  } else if (page == PAGE_BRIGHTNESS) {
#ifdef BOARD_ROUND28
    roundBrightnessFrame(forceDraw || drawnPage != PAGE_BRIGHTNESS);
#endif
  } else if (page == PAGE_DEBUG) {
    static uint32_t lastDbgDraw = 0;
    if (forceDraw || drawnPage != PAGE_DEBUG || now - lastDbgDraw >= 500) { drawDebugPage(); lastDbgDraw = now; }
  } else {
    char big[16]; bool valid; const char *tt, *u; uint16_t c;
    pageInfo(page, big, sizeof(big), &valid, &tt, &u, &c);
    bool stale = (m == SLEEPY) && page != PAGE_BATT;   // battery comes from the adapter, readable with the engine off
    static char shownBig[16]; static uint16_t shownColor = 0;
    bool full = forceDraw || drawnPage != page;
    if (full || strcmp(big, shownBig) != 0 || valid != shownValid || stale != shownStale || c != shownColor) {
      drawDataPage(stale, full);
      strncpy(shownBig, big, sizeof(shownBig) - 1); shownBig[sizeof(shownBig) - 1] = 0;
      shownValid = valid; shownStale = stale; shownColor = c;
    }
  }
  drawnPage = page;
  lastMood = (int)m;
  forceDraw = false;
}

// The UI task owns the display: it keeps animating no matter what the Bluetooth side is doing.
static void uiTask(void *) {
  for (;;) {
    uint32_t t0 = millis();
    uiTick();
    uint32_t period = (page == PAGE_FACE) ? (1000 / UI_FPS) : (page == PAGE_CLUSTER || (page >= PAGE_DIAL_TACH && page <= PAGE_DIAL_VITALS)) ? (1000 / DIAL_FPS) : 30;
    uint32_t used = millis() - t0;
    vTaskDelay(pdMS_TO_TICKS(used >= period ? 1 : period - used));
  }
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
  discoverPids(sendCmd("0100", 8000));        // finds the car's protocol (can take seconds), then reads which values it supports
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
  clearStatus();
  return true;
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never stall when nothing is reading the USB serial port (car power)
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonEdge, CHANGE);
#ifdef BOARD_ROUND28
  if (!roundDisplayInit()) Serial.println("display init failed");
  fullCanvas = new Arduino_Canvas(LCD_W, LCD_H, rgbDisp, 0, 0);
  if (!fullCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)) Serial.println("canvas allocation failed");
  gfx = fullCanvas;
  gfx->fillScreen(BLACK);
  gfx->flush();
  touchInit();
#else
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  gfx->begin(LCD_SPI_HZ);
  gfx->fillScreen(BLACK);
  canvas = new Arduino_Canvas(mochi::W, mochi::H, gfx, 0, FACE_Y);
  if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) Serial.println("canvas allocation failed");
#endif
  character.begin(esp_random());
  detectBoot();
#if MOCHI_FACE && MOCHI_INTRO
  character.startIntro(millis(), bootQuickRestart);
#endif
#if PIN_LED >= 0
  led.begin();
  led.setBrightness(LED_BRIGHTNESS);
#endif
  sdInit();
  NimBLEDevice::init("");
  Serial.printf("Tachi starting [%s, built %s %s], demo mode %s\n", FW_TAG, __DATE__, __TIME__, demo ? "ON" : "OFF");
  xTaskCreatePinnedToCore(uiTask, "ui", 10240, nullptr, 2, nullptr, 1);   // display + button + animation
}

void loop() {
  static int cycle = 0;

  if (demo) {
    if (client && client->isConnected()) client->disconnect();
    linked = false;
    clearStatus();
    demoStep();
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
      while (millis() - t0 < RECONNECT_WAIT_MS && !demo) delay(10);
    }
    if (demo) return;
    if (!connectAdapter()) {
      showStatus("Not found", YELLOW);
      uint32_t t0 = millis();
      while (millis() - t0 < 1500 && !demo) delay(10);
    }
    return;
  }

  // Back-to-back polling: next request goes out as soon as the previous reply is in.
  // Every SLOW_EVERY-th cycle one slow value (coolant, then oil) is polled instead of waiting a full round.
  pollFast();
  static uint32_t cyc = 0, lastDiscTry = 0;
  cyc++;
  int focus = focusItem();                                  // the value on screen stays live: polled every other cycle
  if (focus >= 0 && (cyc & 1) && itemDue(focus, true)) pollItem(focus);
  if (++cycle >= SLOW_EVERY) { cycle = 0; pollSlowNext(); } // everything else takes turns in the background
  if (!sup.have[0] && millis() - lastDiscTry > 15000) { lastDiscTry = millis(); discoverPids(sendCmd("0100", 4000)); }   // car was not ready: ask again
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 1000) {   // one summary line per second on the serial monitor
    lastPrint = millis();
    static uint32_t lastCount = 0;
    Serial.printf("rpm=%d mph=%d coolant=%d%c oil=%s%d page=%d  (%lu req/s)\n", rpm, mph, tempOut(coolantC), TEMP_UNIT_F ? 'F' : 'C', oilValid ? "" : "n/a:", tempOut(oilC), page,
                  (unsigned long)(pollCount - lastCount));
    reqPerSec = pollCount - lastCount;
    if (page == PAGE_FACE) reqPerSecFace = reqPerSec;   // polling rate while the animation is running
    lastCount = pollCount;
  }
  sdLogTick();
}
