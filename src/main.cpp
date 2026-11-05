// Tachi: ESP32-S3 OBD-II dash pet for the Waveshare ESP32-S3-Touch-LCD-2.8C (round 480x480 touch display).
// See README.md.
#include <Arduino.h>
#include <NimBLEDevice.h>
#define private public    // see display_round28.h: the vsync hook needs Arduino_ESP32RGBPanel's panel handle
#include <Arduino_GFX_Library.h>
#undef private
#include <SD_MMC.h>
#include <esp_system.h>
#include "config.h"
#include "display_round28.h"   // ST7701 RGB panel + GT911 touch of the round 2.8C board
#include "mochi.h"
#include "gear.h"           // gear estimate from RPM and speed
#include "round_ui.h"   // round dial pages: sport cluster, analog tach / speedo, vitals rings
#include "obd_parse.h"
#include "car_events.h"
#include "screen_sleep.h"
#include "trip.h"
#include "dtc.h"
#include "report_view.h"
#include "settings.h"
#include "theme.h"

// Arduino_GFX 1.6.x names its colors RGB565_*; keep short names for readability.
#define BLACK  RGB565_BLACK
#define WHITE  RGB565_WHITE
#define GREEN  RGB565_GREEN
#define YELLOW RGB565_YELLOW
#define RED    RGB565_RED

enum Mood { HAPPY, UNEASY, WORRIED, SLEEPY, EXCITED, CALM };

// ---------- Display (round 480x480 RGB panel) ----------
// Everything draws into a full-frame canvas in PSRAM and is pushed to the panel with flush() once per frame, so the
// panel never scans out a half-drawn picture.
Arduino_GFX *gfx = nullptr;                    // the canvas, created in setup()
static Arduino_Canvas *fullCanvas = nullptr;

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
static char rxBuf[384];                 // long enough for a multi-line fault code reply
static volatile bool rxKeepLines = false;   // while codes are read, line breaks are kept as '|' (see dtc.h)
static size_t rxLen = 0;
static volatile bool gotPrompt = false;

int rpm = 0, mph = 0, coolantC = 0, oilC = 0;
uint32_t tsRpm = 0, tsMph = 0, tsCool = 0, tsOil = 0;   // when each value was last updated by the car
#define FAST_FRESH_MS 3000   // speed / RPM older than this are treated as unknown (shown as --, counted as 0)
#define SLOW_FRESH_MS 10000  // coolant / oil are polled less often
static bool fresh(uint32_t ts, uint32_t maxAge) { return ts != 0 && millis() - ts < maxAge; }
bool oilValid = false;       // oil temp PID (015C) answered at least once; many cars do not support it
static uint8_t oilFails = 0; // consecutive failed oil polls; after OIL_GIVE_UP we only retry occasionally

// The user's settings (units, warning levels, theme): loaded in setup(), changed on the Settings pages, saved by the main loop (cfgSaveReq).
static settings::Settings cfg;
// Face page background: a gradient from the top colour to the bottom colour (0..255). Classic keeps the original navy / indigo; the other
// looks use a soft, muted version of the theme colour (the accent washed toward grey and kept dark, so the eyes stay the brightest thing).
static float faceBgTop[3] = {5, 10, 26}, faceBgBot[3] = {20, 12, 46};
static void applyLook() {                            // the theme's accent colours the plain Tachi faces and their background too
  rui::applyTheme(cfg.theme, cfg.mode);
  if (cfg.theme == 0 && cfg.mode == 0) { const float t[3] = {5, 10, 26}, b[3] = {20, 12, 46}; for (int i = 0; i < 3; i++) { faceBgTop[i] = t[i]; faceBgBot[i] = b[i]; } }
  else {
    const float a[3] = {(float)rui::LOOK.accent.r, (float)rui::LOOK.accent.g, (float)rui::LOOK.accent.b};
    const float grey = (a[0] + a[1] + a[2]) / 3.0f;
    for (int i = 0; i < 3; i++) { float soft = a[i] * 0.75f + grey * 0.25f; faceBgTop[i] = 4 + soft * 0.13f; faceBgBot[i] = 6 + soft * 0.30f; }
  }
  mochi::setAccent(rui::LOOK.accent.r, rui::LOOK.accent.g, rui::LOOK.accent.b, cfg.theme == 0 && cfg.mode == 0);
}
static volatile uint32_t cfgGen = 1;                 // bumped on every change so the trip tracker and the event detector pick up new levels
static volatile bool cfgSaveReq = false;
// The car reports deg C. Mood thresholds stay in deg C; only what is shown/logged is converted.
static int tempOut(int c) { return settings::tempShown(c, cfg); }   // rounded
static const char *tempUnit() { return cfg.tempC ? "deg C" : "deg F"; }
static bool oilUnsupported() { return !oilValid && oilFails >= 5; }   // no usable oil reply so far (keeps retrying)
static char oilReply[40] = "(not asked yet)";                          // last raw reply to the oil request, for the Debug page
static uint32_t lastOilTry = 0;
#define OIL_FRESH_MS 20000
uint32_t lastValidMs = 0;   // last time any PID returned valid data
bool linked = false;
volatile bool demo = DEMO_MODE_DEFAULT;
enum Page { PAGE_FACE, PAGE_CLUSTER, PAGE_DIAL_TACH, PAGE_DIAL_SPEEDO, PAGE_ARC, PAGE_DIAL_VITALS, PAGE_CUSTOM, PAGE_QUAD, PAGE_DUAL, PAGE_GEAR, PAGE_TRIP, PAGE_HISTORY, PAGE_CODES, PAGE_SETTINGS, PAGE_COUNT };   // Settings is always last: Brightness and Debug are inside it, not pages of their own
static_assert(PAGE_SETTINGS == PAGE_COUNT - 1, "Settings must stay the last page: add new pages before it");
volatile int page = PAGE_FACE;       // a tap or swipe changes the page (see pageVisible)

// ---------- Extra car-data gauges (battery, air temps, fuel, throttle, load, boost) ----------
// Which of them the car answers is read at connect time (supported-PID bitmaps); the dial pages show what is available.
enum Gauge { G_BATT, G_INTAKE, G_AMBIENT, G_FUEL, G_THROTTLE, G_LOAD, G_MAP, G_BARO, G_COUNT };
static const uint8_t GAUGE_PID[G_COUNT] = {0 /* adapter voltage, ATRV */, 0x0F, 0x46, 0x2F, 0x11, 0x04, 0x0B /* MAP: boost */, 0x33 /* barometric */};
static float gVal[G_COUNT];                       // battery V, temps in deg C, others in percent
static uint32_t gTs[G_COUNT], gLastTry[G_COUNT];
static uint8_t gFails[G_COUNT];
static bool gSupported[G_COUNT], gOk[G_COUNT];    // gOk: answered at least once
static obd::SupportSet sup;                       // supported PIDs of the engine computer
static char supText[2][28] = {"sup: (not read yet)", ""};

static void resetGauges() {
  for (int g = 0; g < G_COUNT; g++) { gVal[g] = 0; gTs[g] = gLastTry[g] = 0; gFails[g] = 0; gSupported[g] = gOk[g] = false; }
}
static bool dialFailed = false;                  // the dial pages' buffers could not be allocated
static bool boostVisible() { return gSupported[G_MAP] && (gOk[G_MAP] || gFails[G_MAP] < 5); }   // the car answers MAP (intake manifold pressure)
static bool pageVisible(int p) {
  if (p == PAGE_CLUSTER) return SHOW_PAGE_CLUSTER && !dialFailed;
  if (p == PAGE_DIAL_TACH || p == PAGE_DIAL_SPEEDO || p == PAGE_DIAL_VITALS) return SHOW_PAGE_DIALS && !dialFailed;
  if (p == PAGE_CUSTOM) return SHOW_PAGE_CUSTOM && SHOW_PAGE_STYLES && !dialFailed && boostVisible();
  if (p == PAGE_ARC) return SHOW_PAGE_STYLES && !dialFailed && boostVisible();
  if (p == PAGE_GEAR) return SHOW_PAGE_GEAR && SHOW_PAGE_STYLES && !dialFailed;
  if (p >= PAGE_QUAD && p <= PAGE_DUAL) return SHOW_PAGE_STYLES && !dialFailed;
  if (p == PAGE_TRIP || p == PAGE_HISTORY) return SHOW_PAGE_TRIP && !dialFailed;
  if (p == PAGE_CODES) return SHOW_PAGE_CODES && !dialFailed;
  if (p == PAGE_SETTINGS) return !dialFailed;
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
static volatile bool forceDraw = true;
static int drawnPage = -1;
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

// ---------- Gear test: listen to raw CAN frames (ELM327 monitor mode) for the gear selector message ----------
// The comma.ai open Mazda definitions (mazda_3_2019.dbc) say: CAN ID 0x01F, signal GEAR at bit 36, 4 bits, with
// P = 13, R = 12, N = 11, D = 1..6. Nobody has confirmed it through an OBD port, so this test only RECORDS what arrives:
// the frames seen, which bytes change, and the values of byte 4 in the order they appeared. Run once per power-up.
#if GEAR_SNIFF_TEST
struct SniffEv { uint32_t ms; uint8_t d[8]; };
static volatile bool sniffOn = false;
static char sniffLine[40]; static uint8_t sniffLen = 0;
static volatile uint32_t sniffFrames = 0, sniffTotal = 0, sniffT0 = 0;       // frames with the gear ID / all frames
static uint8_t sniffPrev[8]; static bool sniffHave = false;
static volatile uint32_t sniffChg[8];                                         // how often each byte changed
static uint8_t sniffVals[12]; static volatile uint8_t sniffNVals = 0;         // distinct values of byte 4, in order of appearance
static SniffEv sniffRing[24]; static volatile uint8_t sniffHead = 0, sniffTail = 0;
static char sniffRaw[3][24]; static volatile uint8_t sniffNRaw = 0;           // first lines that were not gear frames (e.g. NO DATA, ?)
static char gearTxt[2][40] = {"gear test: not run", ""};
static int hexNib(char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1)); }
static void sniffLineDone() {                                                 // runs in the BLE callback: no printing, no heap
  sniffLine[sniffLen] = 0;
  if (sniffLen == 0) return;
  sniffTotal = sniffTotal + 1;
  bool gear = sniffLen >= 3 + 10 && sniffLine[0] == '0' && sniffLine[1] == '1' && (sniffLine[2] == 'F' || sniffLine[2] == 'f');
  uint8_t d[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  if (gear) {
    int n = (sniffLen - 3) / 2; if (n > 8) n = 8;
    for (int i = 0; i < n; i++) { int h = hexNib(sniffLine[3 + 2 * i]), l = hexNib(sniffLine[4 + 2 * i]); if (h < 0 || l < 0) { gear = false; break; } d[i] = (uint8_t)(h * 16 + l); }
  }
  if (!gear) {
    uint8_t k = sniffNRaw;
    if (k < 3) { strncpy(sniffRaw[k], sniffLine, 23); sniffRaw[k][23] = 0; sniffNRaw = k + 1; }
    return;
  }
  sniffFrames = sniffFrames + 1;
  bool b4new = !sniffHave;
  if (sniffHave) for (int i = 0; i < 8; i++) if (d[i] != sniffPrev[i]) { sniffChg[i] = sniffChg[i] + 1; if (i == 4) b4new = true; }
  if (b4new) {
    uint8_t nxt = (sniffHead + 1) % 24;
    if (nxt != sniffTail) { sniffRing[sniffHead].ms = millis() - sniffT0; memcpy(sniffRing[sniffHead].d, d, 8); sniffHead = nxt; }
    bool seen = false; for (int i = 0; i < sniffNVals; i++) if (sniffVals[i] == d[4]) seen = true;
    if (!seen && sniffNVals < 12) { sniffVals[sniffNVals] = d[4]; sniffNVals = sniffNVals + 1; }
  }
  memcpy(sniffPrev, d, 8); sniffHave = true;
}
static void sniffFeed(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '>') { portENTER_CRITICAL(&rxMux); gotPrompt = true; portEXIT_CRITICAL(&rxMux); }
    else if (c == '\r' || c == '\n') { sniffLineDone(); sniffLen = 0; }
    else if (c != ' ' && sniffLen < sizeof(sniffLine) - 1) sniffLine[sniffLen++] = c;
  }
}
#endif

// ---------- BLE receive ----------
void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
#if GEAR_SNIFF_TEST
  if (sniffOn) { sniffFeed(data, len); return; }
#endif
  portENTER_CRITICAL(&rxMux);
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '>') gotPrompt = true;                                // ELM327 "ready" prompt
    else if ((c == '\r' || c == '\n') && rxKeepLines) { if (rxLen > 0 && rxBuf[rxLen - 1] != '|' && rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = '|'; }
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
    char cmd[8]; snprintf(cmd, sizeof(cmd), "01%02X", GAUGE_PID[g]);
    String r = obdQuery(cmd);
    if (obd::dataBytes(r.c_str(), GAUGE_PID[g], 1, b)) {
      gVal[g] = (g == G_INTAKE || g == G_AMBIENT) ? (float)obd::tempC(b[0]) : (g == G_MAP || g == G_BARO) ? (float)b[0] : obd::percent(b[0]);   // MAP, BARO: kPa
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
  if ((p == PAGE_ARC || p == PAGE_CUSTOM) && boostVisible()) return 2 + G_MAP;   // the boost pages need MAP often
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
  for (int g = 0; g < G_COUNT; g++) gSupported[g] = (GAUGE_PID[g] == 0) ? true : sup.supported(GAUGE_PID[g]);   // battery: ATRV is tried
  static const uint8_t listed[] = {0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0F, 0x11, 0x2F, 0x33, 0x42, 0x46, 0x5C};
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

// Colours for the multi-value and report pages, built with rui::rgb(): the short names WHITE / RED / YELLOW are macros in this file.
static const uint16_t UI_WHITE = rui::rgb(240, 244, 255), UI_YELLOW = rui::rgb(255, 208, 50), UI_RED = rui::rgb(255, 60, 56), UI_ORANGE = rui::rgb(255, 120, 40);

// >>> report-main-begin
// ---------- Drive report and fault codes: state shared between the tasks, and the main task's adapter work ----------
// The UI task draws and tracks the trip; only the main task talks to the adapter (the UI sets codeReq, the main loop does the work).
static trip::Config tripConfig() {
  trip::Config c; c.endMs = TRIP_END_MS; c.minSeconds = TRIP_MIN_S; c.minMiles = TRIP_MIN_MILES; c.uneasyC = cfg.coolWarnC; c.worriedC = cfg.coolHotC; c.highRpm = cfg.redlineRpm - 1000;
  return c;
}
static trip::Tracker tracker(tripConfig());          // UI task only
static trip::History tripHist;                       // newest 5 finished trips; written by the UI task, copied for saving under tripMux
static trip::Record liveSnap;                        // the running trip, saved every minute so a power cut loses little
static portMUX_TYPE tripMux = portMUX_INITIALIZER_UNLOCKED, codeMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool tripSaveReq = false, liveSaveReq = false;
static report::CodeList codeList;                    // main task writes, UI task reads, under codeMux
static char codeStatus[40] = ""; static uint16_t codeStatusCol = 0; static uint32_t codeStatusAt = 0;   // under codeMux
static volatile uint8_t codeReq = 0;                 // UI -> main: 1 = read the codes, 2 = clear them
static int lastMilCount = -1;

static void setCodeStatus(const char *msg, uint16_t col) {
  portENTER_CRITICAL(&codeMux); snprintf(codeStatus, sizeof(codeStatus), "%s", msg); codeStatusCol = col; codeStatusAt = millis(); portEXIT_CRITICAL(&codeMux);
}

static void loadTrips() {                            // setup(): the saved history, and a trip that was running when the power went
  Preferences pr; if (!pr.begin("tachi", true)) return;
  trip::History h;
  if (pr.getBytes("trips", &h, sizeof(h)) == sizeof(h) && h.valid()) tripHist = h;
  trip::Record r;
  bool haveLive = pr.getBytes("live", &r, sizeof(r)) == sizeof(r);
  pr.end();
  if (haveLive && r.durationS >= TRIP_MIN_S && r.distCentiMi >= (uint16_t)(TRIP_MIN_MILES * 100)) { r.seq = tripHist.nextSeq; tripHist.push(r); tripSaveReq = true; Serial.println("recovered a trip that was running at power loss"); }
  else if (haveLive) tripSaveReq = true;             // too short: just drop it
  tracker.setNextSeq(tripHist.nextSeq);
}
static void loadSettings() {                         // setup(): the saved settings (anything missing or damaged leaves the defaults)
  Preferences pr; if (!pr.begin("tachi", true)) return;
  settings::Settings c;
  if (pr.getBytes("cfg", &c, sizeof(c)) == sizeof(c) && settings::valid(c)) cfg = c;
  pr.end();
  applyLook();
  tracker.setConfig(tripConfig());
}
static void persistTick() {                          // main loop
  if (cfgSaveReq) {
    cfgSaveReq = false;
    settings::Settings c = cfg;
    Preferences pr; if (pr.begin("tachi", false)) { pr.putBytes("cfg", &c, sizeof(c)); pr.end(); Serial.println("saved settings"); }
  }
  if (tripSaveReq) {
    tripSaveReq = false;
    trip::History h; portENTER_CRITICAL(&tripMux); h = tripHist; portEXIT_CRITICAL(&tripMux);
    Preferences pr; if (pr.begin("tachi", false)) { pr.putBytes("trips", &h, sizeof(h)); pr.remove("live"); pr.end(); Serial.printf("saved %d trips\n", h.count); }
  }
  if (liveSaveReq) {
    liveSaveReq = false;
    trip::Record r = liveSnap;
    Preferences pr; if (pr.begin("tachi", false)) { pr.putBytes("live", &r, sizeof(r)); pr.end(); }
  }
}

// Reads the check-engine status (0101) and the stored (03) and pending (07) codes. Main task only.
static void readAllCodes() {
  setCodeStatus("READING...", UI_WHITE);
  rxKeepLines = true;
  String r1 = sendCmd("0101", 3000), r3 = sendCmd("03", 4000), r7 = sendCmd("07", 4000);
  rxKeepLines = false;
  dtc::Mil mil = dtc::parseMil(r1.c_str());
  dtc::Result st = dtc::parseReply(r3.c_str(), 3), pe = dtc::parseReply(r7.c_str(), 7);
  if (!st.answered && !pe.answered && !mil.valid) { setCodeStatus("NO ANSWER FROM THE CAR", UI_ORANGE); return; }
  portENTER_CRITICAL(&codeMux);
  report::CodeList old = codeList;
  codeList.clear();
  codeList.answered = true; codeList.mil = mil.valid ? mil.on : false; codeList.milCount = mil.valid ? mil.count : -1;
  for (int i = 0; i < st.n; i++) codeList.add(st.codes[i], 0);
  for (int i = 0; i < pe.n; i++) codeList.add(pe.codes[i], 1);
  for (int i = 0; i < codeList.n; i++) { int k = old.find(codeList.code[i]); if (k >= 0) codeList.isNew[i] = old.isNew[k]; }   // keep the NEW tag
  int total = codeList.n;
  portEXIT_CRITICAL(&codeMux);
  if (mil.valid) lastMilCount = mil.count;
  char msg[40];
  if (mil.valid && mil.count > st.n) snprintf(msg, sizeof(msg), "COUNT SAYS %d, READ %d", mil.count, st.n);
  else if (total == 0) snprintf(msg, sizeof(msg), "NO CODES");
  else snprintf(msg, sizeof(msg), "UPDATED: %d CODE%s", total, total == 1 ? "" : "S");
  setCodeStatus(msg, (total > 0 || (mil.valid && mil.count > st.n)) ? UI_YELLOW : UI_WHITE);
  Serial.printf("codes: %d stored/pending, MIL %s, count %d\n", total, mil.valid ? (mil.on ? "on" : "off") : "?", mil.valid ? mil.count : -1);
}

// Clears the codes (mode 04). Only with the engine not running; this also resets the readiness monitors and the freeze frame.
static void clearAllCodes() {
  if (fresh(tsRpm, FAST_FRESH_MS) && rpm >= 400) { setCodeStatus("TURN THE ENGINE OFF FIRST", UI_ORANGE); return; }
  setCodeStatus("CLEARING...", UI_WHITE);
  rxKeepLines = true; String r = sendCmd("04", 5000); rxKeepLines = false;
  bool ok = r.indexOf("44") >= 0;
  Serial.printf("fault codes cleared: %s (reply %s)\n", ok ? "yes" : "NO", r.c_str());
  if (!ok) { setCodeStatus("CLEAR FAILED", UI_RED); return; }
  readAllCodes();
  portENTER_CRITICAL(&codeMux); int left = codeList.n; portEXIT_CRITICAL(&codeMux);
  char msg[40]; if (left == 0) snprintf(msg, sizeof(msg), "CLEARED"); else snprintf(msg, sizeof(msg), "CLEARED, %d REMAIN", left);
  setCodeStatus(msg, left == 0 ? UI_WHITE : UI_YELLOW);
}

// Main loop: a request from the UI, and a cheap check of the check-engine status every CODE_CHECK_MS (a rising count triggers a full read).
static void codeTick() {
  uint8_t rq = codeReq;
  if (rq) { codeReq = 0; if (rq == 2) clearAllCodes(); else readAllCodes(); return; }
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < CODE_CHECK_MS) return;
  lastCheck = millis();
  String r = sendCmd("0101", 2000);
  dtc::Mil mil = dtc::parseMil(r.c_str());
  if (!mil.valid) return;
  portENTER_CRITICAL(&codeMux); bool needRead = !codeList.answered; codeList.mil = mil.on; codeList.milCount = mil.count; portEXIT_CRITICAL(&codeMux);
  if (needRead || mil.count != lastMilCount) readAllCodes();
}
// <<< report-main-end

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
  gVal[G_BARO] = 101.3f; gVal[G_MAP] = 101.3f + (-69.0f + 160.0f * thr / 100.0f);   // -10 psi at idle up to +15 psi at full throttle
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
  if (coolOk && coolantC >= cfg.coolHotC) return WORRIED;
  if (coolOk && coolantC >= cfg.coolWarnC) return UNEASY;

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
  logFile.println((!cfg.tempC) ? "ms,rpm,mph,coolant_f,oil_f,batt_v,intake_f,ambient_f,fuel_pct,throttle_pct,load_pct"
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

// Status text ("Scanning...", "Link lost", ...) is requested from the main task and drawn by the UI task
// (only one task may touch the display). It stays until clearStatus(); the welcome animation has priority.
static portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
static char statusMsg[24];
static uint16_t statusColor = 0;
static volatile uint32_t statusVer = 0;
static volatile bool statusActive = false;
static volatile bool statusHidden = false;   // the user touched the status screen: show the normal pages until the link is up
static uint32_t statusSince = 0;             // when this wait began (the status went from off to on)

void showStatus(const char *msg, uint16_t color) {
  portENTER_CRITICAL(&statusMux);
  strncpy(statusMsg, msg, sizeof(statusMsg) - 1); statusMsg[sizeof(statusMsg) - 1] = 0;
  if (!statusActive) statusSince = millis();
  statusColor = color; statusVer = statusVer + 1; statusActive = true;
  portEXIT_CRITICAL(&statusMux);
  Serial.println(msg);
}
void clearStatus() { statusHidden = false; if (!statusActive) return; statusActive = false; forceDraw = true; }   // no-op when nothing is shown (no needless redraws)

// Scanning / Connecting / Not found / Link lost: an animated page (src/round_ui.h statusPage). Called every UI frame while a status is shown;
// full = the status text changed or the page was just entered (redraw everything), otherwise only the animation is redrawn and sent.
static void drawStatusScreen(uint32_t now, bool full) {
  char msg[24]; uint16_t col;
  portENTER_CRITICAL(&statusMux);
  memcpy(msg, statusMsg, sizeof(msg)); col = statusColor;
  portEXIT_CRITICAL(&statusMux);
  int kind = strncmp(msg, "Scan", 4) == 0 ? 0 : strncmp(msg, "Conn", 4) == 0 ? 1 : strncmp(msg, "Not f", 5) == 0 ? 2 : strncmp(msg, "Link", 4) == 0 ? 3 : 4;
  static const char *titles[4] = {"SCANNING", "CONNECTING", "ADAPTER NOT FOUND", "LINK LOST"};
  static const char *subs[4] = {"LOOKING FOR OBD ADAPTER", "SETTING UP THE ADAPTER", "CHECK ADAPTER IS PLUGGED IN", "RECONNECTING..."};
  char title[24] = "";
  if (kind == 4) { size_t n = 0; for (; msg[n] && n < sizeof(title) - 1; n++) title[n] = (msg[n] >= 'a' && msg[n] <= 'z') ? msg[n] - 32 : msg[n]; while (n && title[n - 1] == '.') n--; title[n] = 0; }
  if (!rui::begin()) { Serial.println("round UI buffers failed"); return; }
  uint16_t *fb = fullCanvas->getFramebuffer();
  int waitSec = -1;                                                           // custom messages show no waiting time
  if (kind != 4) { waitSec = (int)((now - statusSince) / 1000); if (waitSec < 0) waitSec = 0; }
  rui::statusPage(fb, kind, col, kind == 4 ? title : titles[kind], kind == 4 ? "" : subs[kind], now, full, waitSec);
  if (full) { gfx->flush(); rui::flushDirty([](int, int, int) {}); }        // whole page: one push, then forget the marks
  else rui::flushDirty([&](int x, int y, int w) { rgbDisp->draw16bitRGBBitmap(x, y, fb + y * LCD_W + x, w, 1); });
}
// ---------- Face-page background (round board): a soft vertical gradient, dithered so it shows no bands ----------
static void paintFaceBg(int x0, int y0, int w, int h) {
  static const uint8_t bay[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  uint16_t *fb = fullCanvas->getFramebuffer();
  if (x0 < 0) { w += x0; x0 = 0; } if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > LCD_W) w = LCD_W - x0; if (y0 + h > LCD_H) h = LCD_H - y0;
  for (int y = y0; y < y0 + h; y++) {
    float f = (float)y / (LCD_H - 1);
    int r12 = (int)((faceBgTop[0] + (faceBgBot[0] - faceBgTop[0]) * f) * 16), g12 = (int)((faceBgTop[1] + (faceBgBot[1] - faceBgTop[1]) * f) * 16), b12 = (int)((faceBgTop[2] + (faceBgBot[2] - faceBgTop[2]) * f) * 16);   // the theme gradient (x16)
    uint16_t *row = fb + y * LCD_W, pat[4];
    for (int k = 0; k < 4; k++) {                                   // the dither repeats every 4 pixels: work out 4 colours per row, then copy
      int d = bay[y & 3][k];
      int rq = (r12 + d * 8) >> 7, gq = (g12 + d * 4) >> 6, bq = (b12 + d * 8) >> 7;
      pat[k] = (uint16_t)(((rq > 31 ? 31 : rq) << 11) | ((gq > 63 ? 63 : gq) << 5) | (bq > 31 ? 31 : bq));
    }
    for (int x = x0; x < x0 + w; x++) row[x] = pat[x & 3];
  }
}
// Clears a rectangle to the page's background: the gradient on the face page, black elsewhere.
static void clearBg(int x, int y, int w, int h) { if (page == PAGE_FACE) paintFaceBg(x, y, w, h); else gfx->fillRect(x, y, w, h, BLACK); }

// One dot per visible page, the current one white. Spacing shrinks when there are many pages.
// The round panel clips the corners, so the dots sit on a short row at the top (y is ignored).
static void drawPageDots(int) {
  int n = visibleCount(), cur = visibleIndex(page);
  int step = n > 9 ? 14 : 18;
  const int y = 28;
  clearBg(120, y - 8, 240, 16);
  for (int i = 0; i < n; i++)
    gfx->fillCircle(LCD_W / 2 + (int)((i - (n - 1) / 2.0f) * step), y, 5, i == cur ? WHITE : 0x39E7);
}

// Debug page: link state and the last raw adapter exchanges (newest at the bottom).
// Debug page: a status row, readout cards for the key values, grouped system lines and the last adapter exchanges.
// Drawn with the round UI fonts; the short colour names (WHITE, RED, ...) are macros in this file, so the colours are built here.
void drawDebugPage() {
  uint16_t *fb = fullCanvas->getFramebuffer();
  const uint16_t cBg = rui::rgb(5, 7, 12), cText = rui::rgb(240, 244, 255), cDim = rui::rgb(118, 130, 160), cFaint = rui::rgb(70, 80, 108),
                 cCyan = rui::rgb(0, 200, 255), cGreen = rui::rgb(80, 230, 110), cRed = rui::rgb(255, 80, 70), cAmber = rui::rgb(255, 170, 60);
  for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = cBg;
  rui::ringAA(fb, rui::CX, rui::CY, 235.5f, 238.5f, rui::rgb(30, 37, 54));
  uint32_t now = millis();
  char b[64];
  const int CX = LCD_W / 2;

  rui::textC(fb, rui::F_TTL, "DEBUG", CX, 40, cCyan);
  snprintf(b, sizeof(b), "%s  %s", FW_TAG, __DATE__ " " __TIME__);
  rui::textC(fb, rui::F_SML, b, CX, 66, cFaint);

  // status chips
  snprintf(b, sizeof(b), "%lu REQ/S", (unsigned long)reqPerSec);
  rui::chip(fb, CX - 174, 88, 96, 26, true, linked ? cGreen : cRed, linked ? "LINK OK" : "NO LINK");
  rui::chip(fb, CX - 70, 88, 96, 26, false, cDim, b);
  snprintf(b, sizeof(b), "CMD %lu MS", (unsigned long)lastCmdMs);
  rui::chip(fb, CX + 34, 88, 140, 26, false, cDim, b);

  // readout cards: label on top, value below
  struct Card { const char *label; char val[12]; uint16_t col; };
  Card cards[6]; memset(cards, 0, sizeof(cards));
  auto setCard = [&](int k, const char *label, uint16_t col, const char *fmt, int v, float f, bool useF) {
    cards[k].label = label; cards[k].col = col;
    if (useF) snprintf(cards[k].val, sizeof(cards[k].val), fmt, f); else snprintf(cards[k].val, sizeof(cards[k].val), fmt, v);
  };
  setCard(0, "RPM", cText, "%d", rpm, 0, false);
  setCard(1, cfg.speedKmh ? "KMH" : "MPH", cText, "%d", (int)lroundf(settings::speedShown((float)mph, cfg)), 0, false);
  setCard(2, (!cfg.tempC) ? "COOL F" : "COOL C", cText, "%d", tempOut(coolantC), 0, false);
  if (oilValid) setCard(3, (!cfg.tempC) ? "OIL F" : "OIL C", cText, "%d", tempOut(oilC), 0, false); else { cards[3].label = "OIL"; cards[3].col = cDim; strcpy(cards[3].val, "--"); }
  if (gOk[G_BATT]) setCard(4, "BATT V", cText, "%.1f", 0, gVal[G_BATT], true); else { cards[4].label = "BATT V"; cards[4].col = cDim; strcpy(cards[4].val, "--"); }
  setCard(5, "FPS", cText, "%d", (int)faceFps, 0, false);
  for (int k = 0; k < 6; k++) {
    int cx = CX + (k % 3 - 1) * 112, y = 126 + (k / 3) * 64;
    rui::pill(fb, cx, y, 104, 56, cards[k].label);
    rui::textC(fb, rui::F_MED, cards[k].val, cx, y + 28, cards[k].col);
  }

  // system lines
  int y = 262;
  auto line = [&](uint16_t col, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    rui::textC(fb, rui::F_SML, b, CX, y, col); y += 15;
  };
  static const char *moodNames[6] = {"HAPPY", "UNEASY", "WORRIED", "SLEEPY", "EXCITED", "CALM"};
  line(cDim, "SPEED AGE %ld MS   RPM AGE %ld MS", tsMph ? (long)(now - tsMph) : -1L, tsRpm ? (long)(now - tsRpm) : -1L);
  line(cDim, "GAP %lu MS   SD %s   MOOD %s", (unsigned long)maxTickGapMs, sdOk ? "ON" : "OFF", (lastMood >= 0 && lastMood < 6) ? moodNames[lastMood] : "-");
  line(cDim, "%s", bootText);
  line(cCyan, "%s", supText[0]);
  if (supText[1][0]) line(cCyan, "%s", supText[1]);
#if GEAR_SNIFF_TEST
  line(cAmber, "%s", gearTxt[0]);
  if (gearTxt[1][0]) line(cAmber, "%s", gearTxt[1]);
#endif

  // last adapter exchanges (newest last)
  y += 4;
  rui::taper(fb, CX - 90, y, CX + 90, y, 0.6f, 0.6f, rui::rgb(40, 48, 70)); y += 10;
  DbgEntry *list[3]; int n = 0;                                       // the newest three, drawn oldest first
  for (int k = 4; k >= 0 && n < 3; k--) { DbgEntry &d = dbg[(dbgHead + k) % 5]; if (d.cmd[0]) list[n++] = &d; }
  for (int k = n - 1; k >= 0 && y < 410; k--) {
    DbgEntry &d = *list[k]; char rep[20]; strncpy(rep, d.reply, 18); rep[18] = 0;
    snprintf(b, sizeof(b), "> %s  %s%s", d.cmd, d.timeout ? "TIMEOUT" : "", d.timeout ? "" : rep);
    rui::textC(fb, rui::F_SML, b, CX, y, d.timeout ? cRed : cDim); y += 14;
  }
  rui::backChip(fb);                                                       // a sub-screen: BACK instead of the page dots
  rui::flushDirty([](int, int, int) {});                                   // the whole frame is pushed below; forget the marks
  rui::st().page = -1;                                                     // the hub and the dial pages must rebuild when they are shown again
  gfx->flush();
}

// ---------- Input ----------
// Short press: next page (Face > MPH > RPM > Coolant > Oil temp). Long press (LONG_PRESS_MS): toggle demo mode.
// Edges are caught by an interrupt, so a quick click is never missed while the main loop is busy
// talking to the adapter or redrawing. handleButton() then acts on what the interrupt recorded.
static volatile bool btnDown = false, btnLongFired = false;
static volatile uint32_t btnDownMs = 0, btnLastEdgeMs = 0;
static volatile uint8_t btnShortPresses = 0, btnPrevPresses = 0;   // taps / right swipes (previous page)

static volatile uint32_t lastTouchMs = 0;       // last finger-down or BOOT press (wakes a sleeping screen)
static volatile bool screenLit = true;          // false while the backlight is off because the car has been silent
static volatile bool swallowTouch = false;      // the touch that woke the screen must not also act as a tap, swipe or long press

void IRAM_ATTR onButtonEdge() {
  uint32_t ms = millis();
  if (ms - btnLastEdgeMs < 30) return;                 // contact bounce
  if (digitalRead(PIN_BUTTON) == LOW) {
    if (!btnDown) { btnDown = true; btnDownMs = ms; btnLongFired = false; btnLastEdgeMs = ms; lastTouchMs = ms; swallowTouch = !screenLit; }
  } else if (btnDown) {
    btnDown = false; btnLastEdgeMs = ms;
    bool woke = swallowTouch; swallowTouch = false;
    if (!woke && !btnLongFired && btnShortPresses < 5) btnShortPresses = btnShortPresses + 1;
  }
}

// UI-side state of the two report pages
// Settings page: the hub, or one of its sub-screens (UI task only). Leaving the page always returns to the hub.
enum { SUB_HUB, SUB_BRIGHT, SUB_DEBUG, SUB_UNITS, SUB_WARN, SUB_THEME };
static int settingsSub = SUB_HUB;                    // which Settings screen is showing
static const int SETTINGS_ROWS = 5;                  // rows of the hub
static const int ROW_SUB[SETTINGS_ROWS] = {SUB_BRIGHT, SUB_UNITS, SUB_WARN, SUB_THEME, SUB_DEBUG};
static int optCount(int sub) { return sub == SUB_UNITS ? 3 : sub == SUB_WARN ? settings::W_COUNT : sub == SUB_THEME ? 2 : 0; }   // rows of the options screens
static int optKind(int sub) { return sub == SUB_WARN ? 1 : 0; }                                  // 1 = - value + stepper, 0 = tap to change
// A tap on an options row: change the setting, apply it, ask the main loop to save it.
static void optApply(int sub, int row, int part) {
  if (sub == SUB_UNITS) { if (row == 0) cfg.speedKmh ^= 1; else if (row == 1) cfg.tempC ^= 1; else cfg.pressBar ^= 1; }
  else if (sub == SUB_WARN) settings::warnAdjust(cfg, row, part == 2 ? 1 : -1);
  else if (sub == SUB_THEME) { if (row == 0) cfg.theme = (cfg.theme + 1) % settings::THEMES; else cfg.mode = (cfg.mode + 1) % settings::MODES; applyLook(); }
  cfgGen = cfgGen + 1; cfgSaveReq = true; forceDraw = true;
}
static bool inBrightness() { return page == PAGE_SETTINGS && settingsSub == 1; }
static int tripSel = 0;                              // which trip the Drive report shows: 0 = this / the last trip, 1.. = earlier ones (opened from the history)
static float codeClearProg = 0;                      // 0..1 while CLEAR is being held
static int tripChipsAvailable() { return tracker.active() ? tripHist.count : (tripHist.count > 0 ? tripHist.count - 1 : 0); }
static int histRows() { return tripChipsAvailable() + ((tracker.active() || tripHist.count > 0) ? 1 : 0); }   // trips listed on the history page (demo: 4)

// The touch screen acts as the button: a tap is a short press, holding a finger down LONG_PRESS_MS toggles demo mode, and a
// horizontal swipe changes the page: swipe left = next page, swipe right = previous page.
static const int SWIPE_MIN_PX = 60, TAP_MAX_PX = 24;
static void pollTouch() {
  static uint32_t lastPoll = 0, lastReport = 0;
  static int sx = 0, sy = 0, lx = 0, ly = 0;
  static bool slider = false;               // the finger went down on the brightness slider: it drags the slider, nothing else
  static bool preset = false;               // ... or on one of the preset chips: it jumps to that level
  static int tripChip = -1, codeBtn = -1, histRow = -1;   // drive report chip / fault codes button the finger went down on (-1 none)
  static int setRow = -1, optRow = -1, optPart = 0; static bool backBtn = false;   // Settings hub row / options row (and its - / + part) / BACK chip the finger went down on
  static uint32_t holdStart = 0; static bool clearFired = false;
  uint32_t ms = millis();
  if (ms - lastPoll < 15) return;
  lastPoll = ms;
  bool dn = false; int x = 0, y = 0;
  bool got = touchPoll(&dn, &x, &y);
  if (got) lastReport = ms;
  bool release = (got && !dn) || (btnDown && !got && ms - lastReport > 250);   // no report for a while also counts as lifted
  if (got && dn) {
    lastTouchMs = ms;
    if (!btnDown) { swallowTouch = !screenLit; btnDown = true; btnDownMs = ms; btnLongFired = false; sx = x; sy = y; slider = (inBrightness() && y >= rui::BR_ZONE_Y0 && y <= rui::BR_ZONE_Y1); preset = (inBrightness() && rui::presetAt(x, y) >= 0);
      tripChip = (!swallowTouch && page == PAGE_TRIP) ? rui::tripChipAt(x, y) : -1; codeBtn = (!swallowTouch && page == PAGE_CODES) ? rui::codesButtonAt(x, y) : -1;
      histRow = (!swallowTouch && page == PAGE_HISTORY) ? rui::histRowAt(x, y) : -1; if (histRow >= (demo ? 4 : histRows())) histRow = -1;   // a touch that only wakes the screen never presses a button
      setRow = (!swallowTouch && page == PAGE_SETTINGS && settingsSub == SUB_HUB) ? rui::settingsRowAt(x, y) : -1;
      optRow = (!swallowTouch && page == PAGE_SETTINGS && optCount(settingsSub) > 0) ? rui::settingsRowAt(x, y) : -1;
      if (optRow >= optCount(settingsSub)) optRow = -1;
      optPart = optRow >= 0 ? rui::optionPart(optKind(settingsSub), x) : -1; if (optPart < 0) optRow = -1;   // a stepper's label and value are not buttons
      if (setRow >= SETTINGS_ROWS) setRow = -1;                                // the empty part of the hub is an ordinary tap area
      backBtn = (!swallowTouch && page == PAGE_SETTINGS && settingsSub != 0 && rui::backAt(x, y) > 0 && !slider && !preset);
      if (tripChip >= 0 || codeBtn >= 0 || histRow >= 0 || setRow >= 0 || optRow >= 0 || backBtn) btnLongFired = true;   // a button press: not a page tap, a swipe or the demo long press
      holdStart = ms; clearFired = false; }
    lx = x; ly = y;
    if (codeBtn == 1 && !clearFired && !swallowTouch) {                       // CLEAR has to be held (and never from a touch that woke the screen)
      float pr = (ms - holdStart) / (float)CODE_CLEAR_HOLD_MS; codeClearProg = pr > 1 ? 1 : pr;
      if (pr >= 1) { clearFired = true; codeReq = 2; }
    }
    if (preset) { btnLongFired = true; int pv = rui::presetAt(sx, sy); if (pv >= 0 && pv != backlightPct) setBacklight(pv); }
    else if (slider) { btnLongFired = true; int pct = rui::pctFromX(x); if (pct != backlightPct) setBacklight(pct); }   // live: the screen dims or brightens under the finger
    else if (!btnLongFired && (abs(lx - sx) > TAP_MAX_PX || abs(ly - sy) > TAP_MAX_PX)) btnLongFired = true;   // the finger is moving: not a tap, not a long press
  } else if (release && btnDown) {
    btnDown = false;
    if (swallowTouch) { swallowTouch = false; tripChip = codeBtn = histRow = setRow = optRow = -1; backBtn = false; codeClearProg = 0; return; }      // this touch only woke the screen
    if (statusActive && !statusHidden && !demo) { statusHidden = true; forceDraw = true; tripChip = codeBtn = histRow = setRow = optRow = -1; backBtn = false; return; }   // a touch on the status screen skips it
    if (setRow >= 0 || optRow >= 0 || backBtn) {                                                           // Settings: open a row, change an option, or go back to the hub
      int sr = setRow, orow = optRow, opart = optPart; bool bk = backBtn; setRow = optRow = -1; backBtn = false;
      bool moved = abs(lx - sx) > TAP_MAX_PX || abs(ly - sy) > TAP_MAX_PX;       // a finger that travelled is a swipe, not a press: handled below
      if (!moved) {
        if (sr >= 0 && rui::settingsRowAt(lx, ly) == sr) { settingsSub = ROW_SUB[sr]; forceDraw = true; }
        if (orow >= 0 && rui::settingsRowAt(lx, ly) == orow && rui::optionPart(optKind(settingsSub), lx) == opart) optApply(settingsSub, orow, opart);
        if (bk && rui::backAt(lx, ly) > 0) { settingsSub = 0; forceDraw = true; }
        return;
      }
    }
    if (tripChip >= 0 || codeBtn >= 0 || histRow >= 0) {
      int tc = tripChip, cb = codeBtn, hr = histRow; tripChip = codeBtn = histRow = -1; codeClearProg = 0;
      bool movedH = abs(lx - sx) > TAP_MAX_PX || abs(ly - sy) > TAP_MAX_PX;
      if (tc >= 0 && rui::tripChipAt(lx, ly) == tc) { page = PAGE_HISTORY; forceDraw = true; }                  // TRIPS button: the list of trips
      if (hr >= 0 && !movedH && rui::histRowAt(lx, ly) == hr) { tripSel = hr; page = PAGE_TRIP; forceDraw = true; }   // a row: open that trip's report
      if (cb == 0 && rui::codesButtonAt(lx, ly) == 0) { codeReq = 1; setCodeStatus("ASKING THE CAR...", UI_WHITE); }
      if (!(movedH && cb < 0)) return;                                          // a finger that travelled over a trip button / row is a page swipe: handled below
    }
    if (preset) { preset = false; saveBacklight(); Serial.printf("brightness %d %%\n", backlightPct); return; }
    if (slider) { slider = false; saveBacklight(); Serial.printf("brightness %d %%\n", backlightPct); return; }   // keep the level across restarts
    int dx = lx - sx, dy = ly - sy;
    Serial.printf("touch: from %d,%d to %d,%d (dx %d dy %d) -> %s\n", sx, sy, lx, ly, dx, dy, (abs(dx) >= SWIPE_MIN_PX && abs(dx) * 2 > abs(dy) * 3) ? (((dx < 0) != (SWIPE_NEXT_ON_RIGHT != 0)) ? "swipe next" : "swipe previous") : (btnLongFired ? "ignored" : "tap"));
    if (page == PAGE_SETTINGS && settingsSub != 0) {                              // inside a sub-screen only BACK or a swipe leaves it; a stray tap does nothing
      if (abs(dx) >= SWIPE_MIN_PX && abs(dx) * 2 > abs(dy) * 3) { settingsSub = 0; forceDraw = true; }
      return;
    }
    if (abs(dx) >= SWIPE_MIN_PX && abs(dx) * 2 > abs(dy) * 3) {                 // mostly horizontal
      bool next = (dx < 0) != (SWIPE_NEXT_ON_RIGHT != 0);                    // SWIPE_NEXT_ON_RIGHT flips the direction (config.h)
      if (next) { if (btnShortPresses < 5) btnShortPresses = btnShortPresses + 1; }
      else      { if (btnPrevPresses < 5) btnPrevPresses = btnPrevPresses + 1; }
    } else if (!btnLongFired && btnShortPresses < 5) btnShortPresses = btnShortPresses + 1;   // a tap
  }
}

void handleButton() {
  pollTouch();
  if (btnDown && !btnLongFired && !swallowTouch && millis() - btnDownMs >= LONG_PRESS_MS) {
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
#include "scaled_gfx.h"

static mochi::Expr moodExpr(Mood m) {
  switch (m) {
    case UNEASY:  return mochi::E_DEFAULT;
    case WORRIED: return mochi::E_ANGRY;
    case SLEEPY:  return mochi::E_SLEEPY;
    case EXCITED: return mochi::E_TURBO;
    case CALM:    return mochi::E_DEFAULT;
    default:      return mochi::E_DEFAULT;
  }
}

#if CAR_EVENTS
// Short face reactions to one-off car events (engine start, hard braking, low battery / fuel). The detector keeps
// sampling during the welcome / settle period (so its state stays right) but nothing is played until the face is settled.
static void carEventReact(uint32_t now, bool settling) {
  auto mkCfg = [] {
    carevents::Config c;
    c.engineOffMs = EVENT_ENGINE_OFF_MS; c.brakeMinMph = EVENT_BRAKE_MIN_MPH; c.brakeDropMph = EVENT_BRAKE_DROP_MPH;
    c.battCritV = settings::battCritV(cfg); c.fuelLowPct = cfg.fuelLowPct / 2;
    c.redlineRpm = cfg.redlineRpm; c.launchMinRpm = EVENT_LAUNCH_MIN_RPM; c.warmC = EVENT_WARM_C;
    return c;
  };
  static carevents::Detector det(mkCfg());
  static uint32_t seenGen = cfgGen;
  if (seenGen != cfgGen) { seenGen = cfgGen; det.setConfig(mkCfg()); }       // levels changed in Settings
  carevents::Input in;
  in.now = now;
  in.rpmFresh = fresh(tsRpm, FAST_FRESH_MS);   in.rpm = rpm;
  in.speedFresh = fresh(tsMph, FAST_FRESH_MS); in.mph = mph;
  in.battFresh = gOk[G_BATT] && fresh(gTs[G_BATT], GAUGE_FRESH_MS); in.battV = gVal[G_BATT];
  in.fuelFresh = gOk[G_FUEL] && fresh(gTs[G_FUEL], GAUGE_FRESH_MS); in.fuelPct = gVal[G_FUEL];
  in.coolFresh = fresh(tsCool, SLOW_FRESH_MS * 3); in.coolC = coolantC;
  carevents::Event ev = det.update(in);
  if (settling) return;
  switch (ev) {
    case carevents::EV_ENGINE_START: character.react(mochi::E_STARS, 1800, now); break;
    case carevents::EV_HARD_BRAKE:   character.react(mochi::E_ANGRY, 1500, now); break;
    case carevents::EV_REDLINE:      character.react(mochi::E_GLITCH, 1000, now); break;       // overload flash at the limiter
    case carevents::EV_HARD_LAUNCH:  character.react(mochi::E_LAUGH, 1500, now); break;        // whee
    case carevents::EV_ENGINE_OFF:   character.react(mochi::E_SLEEPY, 3000, now); break;       // drifts off when the engine stops
    case carevents::EV_ENGINE_WARM:  character.react(mochi::E_LOVE, 2200, now); break;         // warmed up, ready to go
    case carevents::EV_LOW_BATTERY:
    case carevents::EV_LOW_FUEL:     character.react(mochi::E_CRYING, 2500, now); break;
    default: break;
  }
}
#endif

// One animation frame. Demo mode shows every expression in turn (with its name) for checking at a desk.
// Sends a rectangle of the frame buffer to the panel, row by row straight from the canvas (no extra copy).
// The Face page never waited for the panel's scan (the old code pushed the whole frame in one call): waiting cost ~37 ms per frame
// on the round panel, more than the copying itself, so the animation ran at only ~13 fps. The dial pages keep their tear-free sync.
static void flushRect(const uint16_t *fb, int sx, int sy, int sw, int sh) {
  for (int y = sy; y < sy + sh; y++) rgbDisp->draw16bitRGBBitmap(sx, y, (uint16_t *)(fb + y * LCD_W + sx), sw, 1);
}

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
  bool fullFlush = entering;                      // true when something outside the face area changed: then the whole frame is sent to the panel
  if (entering) {
    paintFaceBg(0, 0, LCD_W, LCD_H);
    drawPageDots(300);
    if (!settling) character.setBase(moodExpr(m), now);
  }
  bool d = demo;
  if (d && !lastDemoSeen) { character.startIntro(now); showIdx = 0; showAt = now + 5200; fullFlush = true; }   // demo ON replays the welcome
  if (!d && lastDemoSeen) {                       // demo just turned OFF: back to the real mood face
    character.setBase(moodExpr(m), now);
    clearBg(UI_LABEL_X0, UI_LABEL_Y, UI_LABEL_W, UI_LABEL_H);      // remove the expression name label
    fullFlush = true;
  }
  lastDemoSeen = d;
  if (d) {
    if (!character.introActive() && now >= showAt) {
      mochi::Expr e = (mochi::Expr)showIdx;
      for (int guard = 0; guard < mochi::E_COUNT && !mochi::exprKept(e); guard++) { showIdx = (showIdx + 1) % mochi::E_COUNT; e = (mochi::Expr)showIdx; }   // demo shows only the kept faces
      showIdx = (showIdx + 1) % mochi::E_COUNT;
      character.setBase(e, now);
      showAt = now + 2800;
      clearBg(UI_LABEL_X0, UI_LABEL_Y, UI_LABEL_W, UI_LABEL_H);
      gfx->setTextSize(UI_LABEL_SIZE); gfx->setTextColor(0x7BEF);
      int16_t x1, y1; uint16_t w, h; const char *nm = mochi::exprName(e);
      gfx->getTextBounds(nm, 0, 0, &x1, &y1, &w, &h);
      gfx->setCursor((LCD_W - w) / 2, UI_LABEL_TY); gfx->print(nm);
      fullFlush = true;
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
  const int FW = (int)ceilf(mochi::W * FACE_SCALE) + 4, FH = (int)ceilf(mochi::H * FACE_SCALE) + 4;      // area the scaled face can touch
  const int FX = (LCD_W - FW) / 2, FY = (LCD_H - FH) / 2;
  const int IM = 52;                                                  // the welcome's road lines and light cones reach this far past the face canvas
  const int IX = FX - IM, IY = FY - IM, IW = FW + 2 * IM, IH = FH + 2 * IM;
  bool introRect = false;
  {
    static bool wasIntroDraw = false;
    bool introDraw = character.introActive();
    if (introDraw && !wasIntroDraw) { paintFaceBg(0, 0, LCD_W, LCD_H); fullFlush = true; }   // first welcome frame: clear everything (hides the page dots)
    else if (introDraw && character.introWide(now)) { paintFaceBg(IX, IY, IW, IH); introRect = true; }   // the drive-in scene reaches past the face canvas
    else if (introDraw) paintFaceBg(FX, FY, FW, FH);                                           // the title writing stays inside the face canvas: small area, much faster
    else if (wasIntroDraw) { paintFaceBg(0, 0, LCD_W, LCD_H); drawPageDots(300); fullFlush = true; }   // welcome over: bring the page dots back
    else paintFaceBg(FX, FY, FW, FH);
    wasIntroDraw = introDraw;
    ScaledGfx<Arduino_GFX> sg{gfx, FACE_SCALE, FX + 2, FY + 2};
    character.render(&sg, now, false);                              // the gradient is the background
  }
  uint32_t tFl = micros();
  if (fullFlush) gfx->flush();
  else {                                                            // send only the rows that can have changed (the face area, or the welcome's area)
    uint16_t *fb = fullCanvas->getFramebuffer();
    const int sx = introRect ? IX : FX, sy = introRect ? IY : FY, sw = introRect ? IW : FW, sh = introRect ? IH : FH;
    flushRect(fb, sx, sy, sw, sh);
  }
#if FACE_TIMING_LOG
  {                                                                 // frame rate of the Face page and the welcome, once per 2 s, on the UART port
    static uint32_t flSum = 0, flN = 0, win = 0; flSum += micros() - tFl; flN++;
    if (now - win >= 2000) {
      if (win && flN) {
        char ln[96]; snprintf(ln, sizeof(ln), "face: %lu fps, flush %lu us (%s), frame %lu ms\n", (unsigned long)(flN * 1000 / (now - win)), (unsigned long)(flSum / flN),
                              fullFlush ? "full" : (introRect ? "welcome area" : "face area"), (unsigned long)(millis() - t0));
        Serial0.print(ln);
      }
      flSum = flN = 0; win = now;
    }
  }
#endif

  uint32_t dt = millis() - t0;
  static uint32_t frames = 0, winStart = 0;
  faceFrameMs = dt; frames++;
  if (now - winStart >= 1000) { faceFps = frames; frames = 0; winStart = now; }
}

// Round-board dial pages (src/round_ui.h): sport cluster, analog tach, analog speedo, vitals rings.
// ---------- Multi-value pages (arc / custom / quad / dual / gear): what they show ----------
// Colours are built here with rui::rgb(): the short names WHITE / RED / YELLOW are macros in this file.

// Boost = intake manifold pressure (MAP) minus barometric pressure, in kPa. Barometric falls back to 101.3 kPa (sea level).
static bool boostValue(float *kpa) {
  if (!(gOk[G_MAP] && fresh(gTs[G_MAP], 6000))) return false;
  float baro = (gOk[G_BARO] && fresh(gTs[G_BARO], GAUGE_FRESH_MS * 3)) ? gVal[G_BARO] : 101.3f;
  *kpa = gVal[G_MAP] - baro;
  return true;
}

static void fillReadout(rui::Readout &r, int id) {
  r = rui::Readout();
  r.col = UI_WHITE;
  auto have = [&](int g) { return gOk[g] && fresh(gTs[g], GAUGE_FRESH_MS); };
  const char *tu = (!cfg.tempC) ? "F" : "C";
  switch (id) {
    case RD_COOL:
      r.have = fresh(tsCool, SLOW_FRESH_MS * 3) && coolantC > -30; r.icon = rui::IC_THERM; strcpy(r.label, "COOLANT"); strcpy(r.unit, tu);
      snprintf(r.text, sizeof(r.text), "%d", tempOut(coolantC)); r.frac = (coolantC - 40) / 90.0f;
      r.col = coolantC >= cfg.coolHotC ? UI_RED : (coolantC >= cfg.coolWarnC ? UI_YELLOW : UI_WHITE); break;
    case RD_INTAKE: case RD_AMBIENT: {
      int g = id == RD_INTAKE ? G_INTAKE : G_AMBIENT;
      r.have = have(g); r.icon = rui::IC_AIR; strcpy(r.label, id == RD_INTAKE ? "INTAKE" : "OUTSIDE"); strcpy(r.unit, tu);
      snprintf(r.text, sizeof(r.text), "%d", tempOut((int)lroundf(gVal[g]))); r.frac = (gVal[g] + 20) / 100.0f; break;
    }
    case RD_BATT:
      r.have = have(G_BATT); r.icon = rui::IC_BATT; strcpy(r.label, "BATTERY"); strcpy(r.unit, "V");
      snprintf(r.text, sizeof(r.text), "%.1f", gVal[G_BATT]); r.frac = (gVal[G_BATT] - 10.5f) / 5.0f;
      r.col = gVal[G_BATT] < settings::battCritV(cfg) ? UI_RED : (gVal[G_BATT] < (rpm > 600 ? settings::battLowRunV(cfg) : settings::battLowOffV(cfg)) ? UI_YELLOW : UI_WHITE); break;
    case RD_FUEL:
      r.have = have(G_FUEL); r.icon = rui::IC_FUEL; strcpy(r.label, "FUEL"); strcpy(r.unit, "%");
      snprintf(r.text, sizeof(r.text), "%d", (int)lroundf(gVal[G_FUEL])); r.frac = gVal[G_FUEL] / 100.0f;
      r.col = gVal[G_FUEL] < cfg.fuelLowPct / 2 ? UI_RED : (gVal[G_FUEL] < cfg.fuelLowPct ? UI_YELLOW : UI_WHITE); break;
    case RD_LOAD: case RD_THROTTLE: {
      int g = id == RD_LOAD ? G_LOAD : G_THROTTLE;
      r.have = have(g); r.icon = id == RD_LOAD ? rui::IC_LOAD : rui::IC_THROTTLE; strcpy(r.label, id == RD_LOAD ? "LOAD" : "THROTTLE"); strcpy(r.unit, "%");
      snprintf(r.text, sizeof(r.text), "%d", (int)lroundf(gVal[g])); r.frac = gVal[g] / 100.0f; break;
    }
    case RD_BOOST: {
      float kpa = 0; r.have = boostValue(&kpa); r.icon = rui::IC_BOOST; strcpy(r.label, "BOOST");
      if (!cfg.pressBar) { float v = kpa * 0.145038f; strcpy(r.unit, "PSI"); snprintf(r.text, sizeof(r.text), "%.1f", v); r.frac = (v + 20) / 50.0f; }
      else { float v = kpa / 100.0f; strcpy(r.unit, "BAR"); snprintf(r.text, sizeof(r.text), "%.2f", v); r.frac = (v + 1.0f) / 3.0f; }
      break;
    }
    default: break;
  }
}

// Estimated gear (OBD has no gear value): N shows at once, a digit only after it has held steady for half a second.
static char gearShown() {
  static char cand = 'N', cur = 'N'; static uint32_t since = 0;
  char g = gear::estimateGear(rpm, mph);
  uint32_t now = millis();
  if (g != cand) { cand = g; since = now; }
  if (g != cur && (g == 'N' || now - since >= 500)) cur = g;
  return cur;
}

static void fillStyleData(rui::Data &d, int p) {
  if (p == PAGE_ARC || p == PAGE_CUSTOM) {
    float kpa = 0; d.haveArc = boostValue(&kpa);
    float v;
    if (!cfg.pressBar) { v = kpa * 0.145038f; d.arcMin = -20; d.arcMax = 30; d.arcMajor = 10; d.arcMinor = 2; d.arcRed = 24; d.arcDec = 0; strcpy(d.arcUnit, "PSI"); snprintf(d.arcText, sizeof(d.arcText), "%.1f", v); }
    else { v = kpa / 100.0f; d.arcMin = -1; d.arcMax = 2; d.arcMajor = 0.5f; d.arcMinor = 0.1f; d.arcRed = 1.6f; d.arcDec = 1; strcpy(d.arcUnit, "BAR"); snprintf(d.arcText, sizeof(d.arcText), "%.2f", v); }
    d.arcVal = v; strcpy(d.arcLabel, "BOOST");
  }
  if (p == PAGE_CUSTOM) { static const int rows[3] = CUSTOM_ROWS; for (int i = 0; i < 3; i++) fillReadout(d.rd[i], rows[i]); }
  if (p == PAGE_QUAD) { static const int cells[4] = QUAD_CELLS; strcpy(d.title, "ENGINE"); for (int i = 0; i < 4; i++) fillReadout(d.rd[i], cells[i]); }
  if (p == PAGE_DUAL) { static const int rows[2] = DUAL_ROWS; strcpy(d.title, "TEMPS"); for (int i = 0; i < 2; i++) fillReadout(d.rd[i], rows[i]); }
  if (p == PAGE_GEAR) d.gear = gearShown();
}

// >>> report-ui-begin
// Feeds the trip tracker once a second (UI task, also while the screen is dark) and reacts to its events.
static report::CodeList tripBase; static bool haveTripBase = false;       // the codes that were there when the trip started
static void tripTick(uint32_t now) {
  { static uint32_t seenGen = cfgGen; if (seenGen != cfgGen) { seenGen = cfgGen; tracker.setConfig(tripConfig()); } }   // levels changed in Settings
  static uint32_t last = 0, lastLive = 0; static bool wasDemo = false;
  if (demo) {                                                                // made-up values must never become a trip
    if (!wasDemo) { uint16_t sq = tracker.nextSeq(); tracker = trip::Tracker(tripConfig()); tracker.setNextSeq(sq); }
    wasDemo = true; return;
  }
  wasDemo = false;
  if (now - last < 1000) return;
  last = now;
  trip::Sample sm; sm.now = now;
  sm.rpmFresh = fresh(tsRpm, FAST_FRESH_MS); sm.rpm = rpm; sm.speedFresh = fresh(tsMph, FAST_FRESH_MS); sm.mph = mph;
  sm.coolFresh = fresh(tsCool, SLOW_FRESH_MS * 3) && coolantC > -30; sm.coolC = coolantC;
  sm.intakeFresh = gOk[G_INTAKE] && fresh(gTs[G_INTAKE], GAUGE_FRESH_MS); sm.intakeC = (int)lroundf(gVal[G_INTAKE]);
  sm.battFresh = gOk[G_BATT] && fresh(gTs[G_BATT], GAUGE_FRESH_MS); sm.battV = gVal[G_BATT];
  float kpa = 0; sm.boostFresh = boostValue(&kpa); sm.boostPsi = kpa * 0.145038f;
  sm.fuelFresh = gOk[G_FUEL] && fresh(gTs[G_FUEL], GAUGE_FRESH_MS); sm.fuelPct = gVal[G_FUEL];
  trip::Tracker::Event ev = tracker.update(sm);
  if (ev == trip::Tracker::T_STARTED) {
    portENTER_CRITICAL(&codeMux); tripBase = codeList; haveTripBase = codeList.answered; portEXIT_CRITICAL(&codeMux);
    codeReq = 1;                                                             // a fresh look at the codes at the start of the drive
    Serial.println("trip started");
  }
  if (tracker.active()) {
    portENTER_CRITICAL(&codeMux);                                            // a code that was not there at the start is NEW for this trip
    if (!haveTripBase && codeList.answered) { tripBase = codeList; haveTripBase = true; }
    for (int i = 0; i < codeList.n; i++) { bool isNew = haveTripBase && tripBase.find(codeList.code[i]) < 0; codeList.isNew[i] = isNew; if (isNew) tracker.addNewCode(codeList.code[i]); }
    portEXIT_CRITICAL(&codeMux);
    if (now - lastLive >= 60000) { lastLive = now; liveSnap = tracker.live(); liveSaveReq = true; }
  }
  if (ev == trip::Tracker::T_ENDED) {
    trip::Record r;
    if (tracker.takeFinished(&r)) {
      portENTER_CRITICAL(&tripMux); tripHist.push(r); portEXIT_CRITICAL(&tripMux);
      tripSaveReq = true;
      Serial.printf("trip ended: %u s, %.2f mi, top %u mph, rating %u\n", (unsigned)r.durationS, r.distCentiMi / 100.0f, (unsigned)r.topMph, (unsigned)r.rating);
#if DRIVE_REPORT_AUTO_OPEN
      tripSel = 0; page = PAGE_TRIP; forceDraw = true; lastTouchMs = now;   // show the report; the touch time keeps the screen on for a while
#endif
    }
  } else if (ev == trip::Tracker::T_DISCARDED) Serial.println("trip discarded (too short)");
}

// What the two report pages show. Demo mode shows made-up content and never touches the car.
static void fillReportData(rui::Data &d, int p) {
  report::Units u; u.mph = !cfg.speedKmh; u.tempF = !cfg.tempC; u.psi = !cfg.pressBar;
  if (p == PAGE_TRIP) {
    static trip::Record demoRec; static bool demoInit = false;
    if (!demoInit) { demoInit = true; demoRec.seq = 12; demoRec.durationS = 1980; demoRec.idleS = 150; demoRec.distCentiMi = 1120; demoRec.topMph = 68; demoRec.avgMph = 34; demoRec.maxRpm = 4300; demoRec.maxCoolC = 96; demoRec.maxIntakeC = 41;
      demoRec.peakBoostDeciPsi = -18; demoRec.minBattDeciV = 132; demoRec.maxBattDeciV = 142; demoRec.hardBrakes = 1; demoRec.hardLaunches = 2; demoRec.rating = 88; }
    const trip::Record *r = nullptr; trip::Record live; bool isLive = false; int chips = tripChipsAvailable();
    if (tripSel > chips) tripSel = 0;
    if (demo) { r = &demoRec; chips = 3; if (tripSel > chips) tripSel = 0; }
    else if (tracker.active() && tripSel == 0) { live = tracker.live(); r = &live; isLive = true; }
    else r = tripHist.get(tracker.active() ? tripSel - 1 : tripSel);
    report::tripView(d.trip, r, isLive, tripSel, chips, u);
    d.ver = tripSel + 8 * chips + 64 * (isLive ? 1 : 0) + 128 * (r ? 1 : 0) + 256 * (int)tripHist.nextSeq;
  } else if (p == PAGE_HISTORY) {
    rui::HistoryView &hv = d.hist; hv = rui::HistoryView();
    uint32_t h = 2166136261u;
    if (demo) {
      for (int i = 0; i < 4; i++) { trip::Record r; r.seq = (uint16_t)(12 - i); r.durationS = (uint16_t)(1980 - 400 * i); r.distCentiMi = (uint16_t)(1120 - 230 * i); r.rating = (uint8_t)(88 - 11 * i); report::histRow(hv.row[i], r, false, u); }
      hv.n = 4;
    } else {
      int rows = histRows(); if (rows > 6) rows = 6;
      for (int i = 0; i < rows; i++) {
        bool isLive = tracker.active() && i == 0; trip::Record live; const trip::Record *r;
        if (isLive) { live = tracker.live(); r = &live; } else r = tripHist.get(tracker.active() ? i - 1 : i);
        if (!r) continue;
        report::histRow(hv.row[hv.n++], *r, isLive, u);
      }
    }
    for (int i = 0; i < hv.n; i++) { for (const char *c = hv.row[i].tag; *c; c++) h = report::hashStep(h, (uint8_t)*c); for (const char *c = hv.row[i].mid; *c; c++) h = report::hashStep(h, (uint8_t)*c); for (const char *c = hv.row[i].right; *c; c++) h = report::hashStep(h, (uint8_t)*c); h = report::hashStep(h, hv.row[i].col); }
    d.ver = (int)(h & 0x3FFFFFFF);
  } else if (p == PAGE_CODES) {
    report::CodeList cl; char st[40]; uint16_t sc; uint32_t sa;
    if (demo) {
      cl.answered = true; cl.mil = true; cl.add("P0420", 0); cl.add("P0171", 1); cl.add("P0301", 0); cl.isNew[cl.find("P0301")] = true;
      snprintf(st, sizeof(st), "DEMO: NOTHING IS SENT"); sc = UI_YELLOW; sa = millis();
    } else {
      portENTER_CRITICAL(&codeMux); cl = codeList; snprintf(st, sizeof(st), "%s", codeStatus); sc = codeStatusCol; sa = codeStatusAt; portEXIT_CRITICAL(&codeMux);
      if (!linked) { snprintf(st, sizeof(st), "NOT CONNECTED TO THE CAR"); sc = UI_ORANGE; sa = millis(); }
    }
    uint32_t v = report::codesView(d.codes, cl);
    d.ver = (int)(v & 0x3FFFFFFF);
    d.codes.clearProg = codeClearProg;
    if (codeClearProg > 0) { snprintf(d.codes.status, sizeof(d.codes.status), "KEEP HOLDING TO CLEAR"); d.codes.statusCol = UI_ORANGE; }
    else if (st[0] && (millis() - sa < 10000 || !strcmp(st, "READING...") || !strcmp(st, "CLEARING..."))) { snprintf(d.codes.status, sizeof(d.codes.status), "%s", st); d.codes.statusCol = sc; }
    else { snprintf(d.codes.status, sizeof(d.codes.status), "HOLD CLEAR 3 SECONDS TO ERASE"); d.codes.statusCol = rui::DIM; }
  }
}
// <<< report-ui-end

// The Settings hub: one row per section with its current value.
static void fillSettingsData(rui::Data &d) {
  rui::SettingsView &v = d.settings; v = rui::SettingsView();
  v.n = 5;
  auto row = [&](int i, const char *label, const char *value, uint8_t icon) { snprintf(v.row[i].label, sizeof(v.row[i].label), "%s", label); snprintf(v.row[i].value, sizeof(v.row[i].value), "%s", value); v.row[i].icon = icon; };
  char b[16];
  snprintf(b, sizeof(b), "%d %%", backlightPct);                                       row(0, "BRIGHTNESS", b, rui::IC_SUN);
  snprintf(b, sizeof(b), "%s %s %s", settings::speedUnit(cfg), settings::tempUnit(cfg), settings::pressUnit(cfg)); row(1, "UNITS", b, rui::IC_BOOST);
  int ch = settings::warnChanged(cfg); if (ch) snprintf(b, sizeof(b), "%d CHANGED", ch); else snprintf(b, sizeof(b), "STANDARD"); row(2, "WARNINGS", b, rui::IC_ENGINE);
  row(3, "THEME", theme::look(cfg.theme).name, rui::IC_AIR);
  row(4, "DEBUG", FW_TAG, rui::IC_BUG);
  d.ver = backlightPct + 128 * ch + 4096 * cfg.theme + 65536 * cfg.speedKmh + 131072 * cfg.tempC + 262144 * cfg.pressBar;
}

// The options screens: units (tap to switch), warnings (- value +), theme (tap to switch look / colour mode).
static void fillOptionsData(rui::Data &d, int sub) {
  rui::OptionsView &o = d.opts; o = rui::OptionsView(); o.n = optCount(sub);
  auto row = [&](int i, const char *label, const char *value) { snprintf(o.row[i].label, sizeof(o.row[i].label), "%s", label); snprintf(o.row[i].value, sizeof(o.row[i].value), "%s", value); o.row[i].kind = (uint8_t)optKind(sub); };
  if (sub == SUB_UNITS) {
    snprintf(o.title, sizeof(o.title), "UNITS");
    row(0, "SPEED", settings::speedUnit(cfg)); row(1, "TEMPERATURE", settings::tempUnit(cfg)); row(2, "PRESSURE", settings::pressUnit(cfg));
  } else if (sub == SUB_WARN) {
    snprintf(o.title, sizeof(o.title), "WARNINGS");
    for (int i = 0; i < settings::W_COUNT; i++) { char t[16]; settings::warnText(i, cfg, t, sizeof(t)); row(i, settings::warnLabel(i), t); }
  } else {
    snprintf(o.title, sizeof(o.title), "THEME"); o.swatch = true;
    row(0, "LOOK", theme::look(cfg.theme).name); row(1, "COLOUR", theme::modeName(cfg.mode));
  }
  d.ver = (int)((uint32_t)(sub * 1000003u + cfg.speedKmh * 7u + cfg.tempC * 11u + cfg.pressBar * 13u + cfg.theme * 17u + cfg.mode * 19u + (uint32_t)cfg.coolWarnC * 23u + (uint32_t)cfg.coolHotC * 29u + cfg.battLowDeciV * 31u + cfg.fuelLowPct * 37u + cfg.redlineRpm * 41u) & 0x3FFFFFFF);
}

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
  d.redline = cfg.redlineRpm; d.hotC = cfg.coolWarnC; d.worryC = cfg.coolHotC; d.fuelLowPct = cfg.fuelLowPct;
  d.battCritV = settings::battCritV(cfg); d.battLowV = settings::battLowOffV(cfg); d.unitF = !cfg.tempC; d.kmh = cfg.speedKmh != 0; d.theme = rui::themeGen;
  d.dotN = visibleCount(); d.dotCur = visibleIndex(page);
  fillStyleData(d, p);
  fillReportData(d, p);
  if (p == PAGE_SETTINGS) { if (optCount(settingsSub) > 0) fillOptionsData(d, settingsSub); else fillSettingsData(d); }
  d.ver = (int)(((uint32_t)d.ver * 8u + (cfg.speedKmh ? 1u : 0u) + (cfg.tempC ? 2u : 0u) + (cfg.pressBar ? 4u : 0u)) & 0x3FFFFFFF);   // unit labels are part of the static content
  int pg = p == PAGE_CLUSTER ? rui::PG_SPORT : p == PAGE_DIAL_TACH ? rui::PG_TACH : p == PAGE_DIAL_SPEEDO ? rui::PG_SPEEDO : p == PAGE_ARC ? rui::PG_ARC
         : p == PAGE_CUSTOM ? rui::PG_CUSTOM : p == PAGE_QUAD ? rui::PG_QUAD : p == PAGE_DUAL ? rui::PG_DUAL : p == PAGE_GEAR ? rui::PG_GEAR : p == PAGE_TRIP ? rui::PG_TRIP : p == PAGE_HISTORY ? rui::PG_HISTORY : p == PAGE_CODES ? rui::PG_CODES : p == PAGE_SETTINGS ? (optCount(settingsSub) > 0 ? rui::PG_OPTIONS : rui::PG_SETTINGS) : rui::PG_VITALS;
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

// Brightness page (round board): redrawn only when the level changes.
static void roundBrightnessFrame(bool full) {
  static int shown = -1;
  if (!rui::begin()) { Serial.println("round UI buffers failed"); dialFailed = true; forceDraw = true; page = nextPage(page, 1); return; }
  if (!full && shown == backlightPct) return;
  uint16_t *fb = fullCanvas->getFramebuffer();
  rui::brightnessPage(fb, backlightPct, 0, 0, full);                     // a sub-screen: no page dots, a BACK button instead
  if (full) rui::backChip(fb);
  shown = backlightPct;
  rui::flushDirty([&](int x, int y, int w) { waitBeamPast(y); rgbDisp->draw16bitRGBBitmap(x, y, fb + y * LCD_W + x, w, 1); });
}


// Draws whatever the current page needs. Runs only in the UI task; the main task never touches the display.
void uiTick() {
  static uint32_t lastTick = 0, gapWindowStart = 0, gapMax = 0;
  uint32_t t = millis();
  if (lastTick && t - lastTick > gapMax) gapMax = t - lastTick;   // longest stall between UI updates
  lastTick = t;
  if (t - gapWindowStart >= 5000) { maxTickGapMs = gapMax; gapMax = 0; gapWindowStart = t; }
  handleButton();
  { static int lastPg = -1; if (page != lastPg) { if (lastPg == PAGE_SETTINGS) settingsSub = 0; if (page != PAGE_TRIP && page != PAGE_HISTORY) tripSel = 0; lastPg = page; } }   // leaving Settings always returns it to the hub
  if (!pageVisible(page)) { page = nextPage(page, 1); forceDraw = true; }   // the page on screen vanished (e.g. demo ended)

  uint32_t now = millis();
  // Values the car has stopped answering must not linger on screen or drive the mood.
  if (!fresh(tsMph, FAST_FRESH_MS)) mph = 0;
  if (!fresh(tsRpm, FAST_FRESH_MS)) rpm = 0;
  Mood m = computeMood();
  bool moodChanged = (int)m != lastMood;
  tripTick(now);                                       // trips are tracked even while the screen is dark
#if SCREEN_OFF_AFTER_MS > 0
  {   // engine off for a while: backlight off (the car's USB port stays powered). Car data or a touch lights it again.
    static bool everSeen = false; if (lastValidMs) everSeen = true;
    static const screensleep::ScreenSleep sleeper([] {
      screensleep::Config c;
      c.staleMs = STALE_AFTER_MS; c.offAfterMs = SCREEN_OFF_AFTER_MS; c.wakeMs = SCREEN_WAKE_MS; c.bootGraceMs = BOOT_SLEEP_GRACE_MS;
      return c;
    }());
    bool lit = sleeper.update(now, lastValidMs, everSeen, lastTouchMs, demo);
    if (lit != screenLit) {
      screenLit = lit;
      if (lit) { backlightRestore(); forceDraw = true; drawnPage = -1; } else backlightOff();
      Serial.printf("screen %s\n", lit ? "on" : "off");
    }
    if (!screenLit) { lastMood = (int)m; return; }                  // nothing is drawn while the screen is dark
  }
#endif

  // Status text (scanning, connecting, link lost) takes over the screen, except during the welcome animation.
  static bool statusShown = false; static uint32_t shownStatusVer = 0;
  bool introOn = MOCHI_INTRO && character.introActive() && page == PAGE_FACE;
  if (statusActive && !introOn && !demo && !statusHidden) {
    drawStatusScreen(now, !statusShown || statusVer != shownStatusVer); shownStatusVer = statusVer; statusShown = true;   // animated: redrawn every frame
    drawnPage = -1;
    lastMood = (int)m;
    return;
  }
  if (statusShown) { statusShown = false; forceDraw = true; drawnPage = -1; }

  if (page == PAGE_FACE) {
    faceFrame(m, moodChanged, forceDraw || drawnPage != PAGE_FACE, now);
  } else if (page == PAGE_CLUSTER || (page >= PAGE_DIAL_TACH && page <= PAGE_CODES)) {
    roundDialFrame(page, now);
  } else if (page == PAGE_SETTINGS) {
    static int drawnSub = -1;
    bool fullS = forceDraw || drawnPage != PAGE_SETTINGS || drawnSub != settingsSub;
    drawnSub = settingsSub;
    if (settingsSub == 1) roundBrightnessFrame(fullS);
    else if (settingsSub == 2) { static uint32_t lastDbgDraw = 0; if (fullS || now - lastDbgDraw >= 500) { drawDebugPage(); lastDbgDraw = now; } }
    else roundDialFrame(PAGE_SETTINGS, now);
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
    uint32_t period = !screenLit ? 250 : (page == PAGE_FACE) ? (1000 / UI_FPS) : (page == PAGE_CLUSTER || (page >= PAGE_DIAL_TACH && page <= PAGE_CODES)) ? (1000 / DIAL_FPS) : 30;
    uint32_t used = millis() - t0;
    vTaskDelay(pdMS_TO_TICKS(used >= period ? 1 : period - used));
  }
}

#if GEAR_SNIFF_TEST
static void gearSniffDrain() {                                                // main task: print what the callback queued
  while (sniffTail != sniffHead) {
    SniffEv e = sniffRing[sniffTail]; sniffTail = (sniffTail + 1) % 24;
    Serial.printf("gear 01F t=%.1fs  %02X %02X %02X %02X [%02X] %02X %02X %02X   byte4>>1&15=%d byte4>>4=%d byte4&15=%d\n", e.ms / 1000.0f,
                  e.d[0], e.d[1], e.d[2], e.d[3], e.d[4], e.d[5], e.d[6], e.d[7], (e.d[4] >> 1) & 15, e.d[4] >> 4, e.d[4] & 15);
  }
}
static void gearSniffRestore() {
  const char *init[] = {"ATZ", "ATE0", "ATL0", "ATH0", "ATS0", "ATSP0"};      // the same setup as at connect: clears the filter and headers
  for (auto c : init) sendCmd(c, INIT_TIMEOUT_MS);
  discoverPids(sendCmd("0100", 8000));
}
static void runGearSniff() {
  static bool done = false;
  if (done) return;
  done = true;
  Serial.println("=== gear test: listening for CAN ID 01F ===");
  showStatus("SHIFT P R N D", YELLOW);
  sendCmd("ATH1");                                           // show frame headers (the CAN ID)
  String f = sendCmd("ATCRA01F");                            // only frames from ID 01F, if the adapter supports it
  bool filtered = f.indexOf("OK") >= 0;
  sniffTotal = sniffFrames = 0; sniffNVals = 0; sniffNRaw = 0; sniffHave = false; sniffLen = 0; sniffHead = sniffTail = 0;
  for (int i = 0; i < 8; i++) sniffChg[i] = 0;
  portENTER_CRITICAL(&rxMux); gotPrompt = false; portEXIT_CRITICAL(&rxMux);
  sniffT0 = millis(); sniffOn = true;
  const char *ma = "ATMA\r";
  txChar->writeValue((const uint8_t *)ma, 5, false);
  uint32_t t0 = millis();
  while (millis() - t0 < (uint32_t)GEAR_SNIFF_SEC * 1000 && client && client->isConnected()) {
    gearSniffDrain();
    snprintf(gearTxt[0], sizeof(gearTxt[0]), "gear test: %lu fr / %lu", (unsigned long)sniffFrames, (unsigned long)sniffTotal);
    delay(20);
  }
  const char *stop = "\r";                                  // any character stops the monitor
  txChar->writeValue((const uint8_t *)stop, 1, false);
  uint32_t ts = millis();
  while (!gotPrompt && millis() - ts < 2000) delay(5);
  delay(300);
  sniffOn = false;
  gearSniffDrain();
  Serial.printf("=== gear test done: filter %s, %lu frames of 01F, %lu lines in all ===\n", filtered ? "accepted" : "not accepted (all traffic)", (unsigned long)sniffFrames, (unsigned long)sniffTotal);
  Serial.print("byte changes b0..b7:"); for (int i = 0; i < 8; i++) Serial.printf(" %lu", (unsigned long)sniffChg[i]); Serial.println();
  Serial.print("byte4 values in order:"); for (int i = 0; i < sniffNVals; i++) Serial.printf(" %02X", sniffVals[i]); Serial.println();
  for (int i = 0; i < sniffNRaw; i++) Serial.printf("other line %d: %s\n", i, sniffRaw[i]);
  snprintf(gearTxt[0], sizeof(gearTxt[0]), "gear test: %lu fr of %lu%s", (unsigned long)sniffFrames, (unsigned long)sniffTotal, filtered ? "" : " nofilt");
  gearTxt[1][0] = 0;
  if (sniffFrames == 0) snprintf(gearTxt[1], sizeof(gearTxt[1]), "none: %s", sniffNRaw ? sniffRaw[0] : "no reply");
  else { size_t n = snprintf(gearTxt[1], sizeof(gearTxt[1]), "b4:"); for (int i = 0; i < sniffNVals && n < sizeof(gearTxt[1]) - 4; i++) n += snprintf(gearTxt[1] + n, sizeof(gearTxt[1]) - n, " %02X", sniffVals[i]); }
  gearSniffRestore();                                        // back to normal OBD polling
  clearStatus();
}
#endif

// ---------- Connection ----------
// Throws the BLE client away so the next attempt starts clean (a half-broken client object can keep failing for ever).
static void dropClient() {
  rxChar = txChar = nullptr;
  if (client) { if (client->isConnected()) client->disconnect(); NimBLEDevice::deleteClient(client); client = nullptr; }
}
static bool lastCmdTimedOut() { return dbg[(dbgHead + 4) % 5].timeout; }   // the newest entry of the Debug page's command log

bool connectAdapter() {
  int tmpBytes[2];
  showStatus("Scanning...", 0x07FF);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->clearResults();
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
  client->setConnectTimeout(10000);                       // never wait longer than 10 s for the adapter to accept the link
  if (!client->connect(found)) { Serial.println("connect failed"); dropClient(); return false; }

  NimBLERemoteService *svc = client->getService(NimBLEUUID(SERVICE_UUID_STR));
  if (!svc) { Serial.println("service not found"); dropClient(); return false; }
  rxChar = svc->getCharacteristic(NimBLEUUID(RX_UUID_STR));
  txChar = svc->getCharacteristic(NimBLEUUID(TX_UUID_STR));
  if (!rxChar || !txChar) { Serial.println("characteristics not found"); dropClient(); return false; }
  rxChar->subscribe(true, onNotify);

  const char *init[] = {"ATZ", "ATE0", "ATL0", "ATH0", "ATS0", "ATSP0"};
  int silent = 0;
  for (auto c : init) {
    sendCmd(c, INIT_TIMEOUT_MS);
    if (lastCmdTimedOut()) { if (++silent >= 2) { Serial.println("adapter does not answer the setup commands: starting over"); dropClient(); return false; } }   // two in a row: not worth waiting out the rest
    else silent = 0;
  }
  discoverPids(sendCmd("0100", 8000));        // finds the car's protocol (can take seconds), then reads which values it supports
#if GEAR_SNIFF_TEST
  runGearSniff();                             // one-off test, see the comment at the top of that section
#endif
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
  codeReq = 1; lastMilCount = -1;                       // read the fault codes right after connecting
  clearStatus();
  return true;
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
#if FACE_TIMING_LOG
  Serial0.begin(115200);
#endif   // never stall when nothing is reading the USB serial port (car power)
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonEdge, CHANGE);
  if (!roundDisplayInit()) Serial.println("display init failed");
  fullCanvas = new Arduino_Canvas(LCD_W, LCD_H, rgbDisp, 0, 0);
  if (!fullCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)) Serial.println("canvas allocation failed");
  gfx = fullCanvas;
  gfx->fillScreen(BLACK);
  gfx->flush();
  touchInit();
  character.begin(esp_random());
  detectBoot();
#if MOCHI_INTRO
  character.startIntro(millis(), bootQuickRestart);
#endif
  sdInit();
  loadSettings();
  loadTrips();
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
      statusHidden = false;                                  // a lost link is worth showing again
      showStatus("Link lost", RED);
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
    Serial.printf("rpm=%d mph=%d coolant=%d%c oil=%s%d page=%d  (%lu req/s)\n", rpm, mph, tempOut(coolantC), (!cfg.tempC) ? 'F' : 'C', oilValid ? "" : "n/a:", tempOut(oilC), page,
                  (unsigned long)(pollCount - lastCount));
    reqPerSec = pollCount - lastCount;
    if (page == PAGE_FACE) reqPerSecFace = reqPerSec;   // polling rate while the animation is running
    lastCount = pollCount;
  }
  codeTick();                                               // fault codes: UI requests and the periodic check-engine check
  persistTick();                                            // save finished trips and the running trip
  sdLogTick();
}
