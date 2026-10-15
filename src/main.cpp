// Tachi: ESP32-S3 OBD-II desk/dash pet for the Waveshare 1.47" display board.
// STATUS: compiles (PlatformIO); not yet run on hardware. See README.md.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_NeoPixel.h>
#include <SD_MMC.h>
#include <esp_system.h>
#include "config.h"
#include "mochi.h"
#include "obd_parse.h"

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

// ---------- Boot type (welcome animation: full on a fresh power-up, short after a quick restart) ----------
// The car's USB power can dip while the engine cranks and restart the board; replaying the long welcome each time would
// be annoying. No clock is available, so we use the reset reason plus a cookie in RAM that survives a restart.
static RTC_NOINIT_ATTR uint32_t rtcCookie;
static const uint32_t RTC_COOKIE = 0x7AC41001;
static bool bootQuickRestart = false;
static char bootText[24] = "boot: ?";
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
enum Page { PAGE_FACE, PAGE_MPH, PAGE_RPM, PAGE_COOLANT, PAGE_OIL, PAGE_BATT, PAGE_INTAKE, PAGE_AMBIENT, PAGE_FUEL, PAGE_THROTTLE, PAGE_LOAD, PAGE_DEBUG, PAGE_COUNT };
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
static bool pageVisible(int p) {
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

// One dot per visible page, the current one white. Spacing shrinks when there are many pages.
static void drawPageDots(int y) {
  int n = visibleCount(), cur = visibleIndex(page);
  int step = n > 9 ? 12 : 16;
  gfx->fillRect(0, y - 6, LCD_W, 12, BLACK);
  for (int i = 0; i < n; i++)
    gfx->fillCircle(LCD_W / 2 + (int)((i - (n - 1) / 2.0f) * step), y, 4, i == cur ? WHITE : 0x39E7);
}

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
  gfx->setTextColor(0x7BEF);
  int y = 162;
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
    if (!demo) { resetGauges(); oilValid = false; oilFails = 0; }   // drop the made-up demo values and pages
    forceDraw = true;
  }
  noInterrupts();
  uint8_t n = btnShortPresses;
  btnShortPresses = 0;
  interrupts();
  if (n) {
    page = nextPage(page, n);
    forceDraw = true;
  }
}

// ---------- Face page (animated character) ----------
static mochi::Mochi character;
static Arduino_Canvas *canvas = nullptr;
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

// One animation frame. Demo mode shows every expression in turn (with its name) for checking at a desk.
static void faceFrame(Mood m, bool moodChanged, bool entering, uint32_t now) {
  uint32_t t0 = millis();
  static int showIdx = 0; static uint32_t showAt = 0; static bool lastDemoSeen = false;
  if (entering) {
    gfx->fillRect(0, 0, LCD_W, LCD_H, BLACK);
    drawPageDots(300);
    character.setBase(moodExpr(m), now);
  }
  bool d = demo;
  if (d && !lastDemoSeen) { character.startIntro(now); showIdx = 0; showAt = now + 5200; }   // demo ON replays the welcome
  if (!d && lastDemoSeen) {                       // demo just turned OFF: back to the real mood face
    character.setBase(moodExpr(m), now);
    gfx->fillRect(0, 262, LCD_W, 24, BLACK);      // remove the expression name label
  }
  lastDemoSeen = d;
  if (d) {
    if (!character.introActive() && now >= showAt) {
      mochi::Expr e = (mochi::Expr)showIdx;
      showIdx = (showIdx + 1) % mochi::E_COUNT;
      if (e == mochi::E_TITLE) { e = (mochi::Expr)showIdx; showIdx = (showIdx + 1) % mochi::E_COUNT; }
      character.setBase(e, now);
      showAt = now + 2800;
      gfx->fillRect(0, 262, LCD_W, 24, BLACK);
      gfx->setTextSize(2); gfx->setTextColor(0x7BEF);
      int16_t x1, y1; uint16_t w, h; const char *nm = mochi::exprName(e);
      gfx->getTextBounds(nm, 0, 0, &x1, &y1, &w, &h);
      gfx->setCursor((LCD_W - w) / 2, 264); gfx->print(nm);
    }
  } else {
    if (moodChanged || entering) {
      character.setBase(moodExpr(m), now);
      if (lastMood == SLEEPY && m != SLEEPY) character.react(mochi::E_WAKE, 1000, now);   // car data is back: wake up
    }
    character.idleTick(now, mph == 0);
  }
  character.update(now);
  character.render(canvas, now);
  canvas->flush();

  uint32_t dt = millis() - t0;
  static uint32_t frames = 0, winStart = 0;
  faceFrameMs = dt; frames++;
  if (now - winStart >= 1000) { faceFps = frames; frames = 0; winStart = now; }
}

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
    uint32_t period = (page == PAGE_FACE) ? (1000 / UI_FPS) : 30;
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
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  gfx->begin(LCD_SPI_HZ);
  gfx->fillScreen(BLACK);
  canvas = new Arduino_Canvas(mochi::W, mochi::H, gfx, 0, FACE_Y);
  if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) Serial.println("canvas allocation failed");
  character.begin(esp_random());
  detectBoot();
#if MOCHI_FACE && MOCHI_INTRO
  character.startIntro(millis(), bootQuickRestart);
#endif
  led.begin();
  led.setBrightness(60);
  sdInit();
  NimBLEDevice::init("");
  Serial.printf("Tachi starting, demo mode %s\n", demo ? "ON" : "OFF");
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
