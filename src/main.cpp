// Tachi: ESP32-S3 OBD-II desk/dash pet for the Waveshare 1.47" display board.
// STATUS: compiles (PlatformIO); not yet run on hardware. See README.md.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

// Arduino_GFX 1.6.x names its colors RGB565_*; keep short names for readability.
#define BLACK  RGB565_BLACK
#define WHITE  RGB565_WHITE
#define GREEN  RGB565_GREEN
#define YELLOW RGB565_YELLOW
#define RED    RGB565_RED

enum Mood { HAPPY, UNEASY, WORRIED, SLEEPY };

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

int rpm = 0, mph = 0, coolantC = 0;
uint32_t lastValidMs = 0;   // last time any PID returned valid data
bool linked = false;
bool demo = DEMO_MODE_DEFAULT;
int page = 0;               // readout: 0 RPM, 1 mph, 2 coolant, 3 hidden

// ---------- UI state (shared by loop() and the wait loops inside sendCmd) ----------
static int lastMood = -1;
static bool lastOpen = true;
static bool forceDraw = true;
static int shownRpm = -1, shownMph = -1, shownTemp = -999;
static bool blinking = false;
static uint32_t nextBlink = 0, blinkEnd = 0;

Mood computeMood();
void uiTick();

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
  txChar->writeValue((const uint8_t *)out.c_str(), out.length(), false);

  uint32_t t0 = millis();
  while (!gotPrompt && millis() - t0 < timeoutMs) {
    uiTick();            // keep blinking / button alive while waiting for the adapter
    delay(5);
  }
  char tmp[sizeof(rxBuf)];
  portENTER_CRITICAL(&rxMux);
  memcpy(tmp, rxBuf, sizeof(tmp));   // copy under the lock, allocate the String outside it
  portEXIT_CRITICAL(&rxMux);
  String r(tmp);
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

void pollOne(int which) {
  int b[2];
  if (which == 0) {
    if (parsePid(sendCmd("010C"), "410C", 2, b)) { rpm = (b[0] * 256 + b[1]) / 4; lastValidMs = millis(); }
  } else if (which == 1) {
    if (parsePid(sendCmd("010D"), "410D", 1, b)) { mph = (int)(b[0] * 0.621f + 0.5f); lastValidMs = millis(); }
  } else {
    if (parsePid(sendCmd("0105"), "4105", 1, b)) { coolantC = b[0] - 40; lastValidMs = millis(); }
  }
}

// Fake data that sweeps through every mood: idle, driving, then a slow overheat.
void demoStep() {
  float t = millis() / 1000.0f;
  rpm = 800 + (int)(2200 * (0.5f + 0.5f * sinf(t * 0.6f)));
  mph = (int)(45 * (0.5f + 0.5f * sinf(t * 0.3f)));
  float cyc = fmodf(t, 60.0f);                       // 60 s loop
  coolantC = 88 + (int)(cyc < 40 ? cyc * 0.2f : 8 + (cyc - 40) * 1.0f);   // 88 -> 96 -> 116
  lastValidMs = (fmodf(t, 75.0f) > 65.0f) ? lastValidMs : millis();       // 10 s "engine off" gap
}

Mood computeMood() {
  if (millis() - lastValidMs > STALE_AFTER_MS) return SLEEPY;   // engine off / no data
  if (coolantC >= TEMP_WORRIED_C) return WORRIED;
  if (coolantC >= TEMP_UNEASY_C) return UNEASY;
  return HAPPY;
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
  }
}

// ---------- Drawing ----------
const int EYE_Y = 115, EYE_X1 = 48, EYE_X2 = 124;
const int MOUTH_X = LCD_W / 2, MOUTH_Y = 200;

void showStatus(const char *msg, uint16_t color) {
  gfx->fillRect(0, 50, LCD_W, 190, BLACK);
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
  gfx->fillRect(0, 70, LCD_W, 90, BLACK);
  int r = (m == WORRIED) ? 26 : 22;
  if (m == SLEEPY || !eyesOpen) {
    gfx->fillRect(EYE_X1 - 20, EYE_Y - 3, 40, 6, WHITE);
    gfx->fillRect(EYE_X2 - 20, EYE_Y - 3, 40, 6, WHITE);
  } else {
    int pupil = (m == WORRIED) ? 5 : 9;
    gfx->fillCircle(EYE_X1, EYE_Y, r, WHITE);
    gfx->fillCircle(EYE_X2, EYE_Y, r, WHITE);
    gfx->fillCircle(EYE_X1, EYE_Y, pupil, BLACK);
    gfx->fillCircle(EYE_X2, EYE_Y, pupil, BLACK);
  }
}

void drawMouthFor(Mood m) {
  gfx->fillRect(0, 165, LCD_W, 75, BLACK);
  switch (m) {
    case HAPPY:   drawMouth(MOUTH_X, MOUTH_Y, 70, 16, GREEN);       break;
    case UNEASY:  drawMouth(MOUTH_X, MOUTH_Y, 60, 0, YELLOW);       break;
    case WORRIED: drawMouth(MOUTH_X, MOUTH_Y + 8, 60, -16, RED);    break;
    case SLEEPY:  gfx->drawCircle(MOUTH_X, MOUTH_Y, 8, 0x7BEF);     break;
  }
}

void drawReadout() {
  gfx->fillRect(0, 240, LCD_W, 80, BLACK);
  if (page == 3) return;
  char big[16];
  const char *label;
  if (page == 0)      { snprintf(big, sizeof(big), "%d", rpm);      label = "RPM"; }
  else if (page == 1) { snprintf(big, sizeof(big), "%d", mph);      label = "MPH"; }
  else                { snprintf(big, sizeof(big), "%d", coolantC); label = "COOLANT C"; }
  int16_t x1, y1; uint16_t w, h;
  gfx->setTextSize(4);
  gfx->setTextColor(WHITE);
  gfx->getTextBounds(big, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2, 252);
  gfx->print(big);
  gfx->setTextSize(2);
  gfx->setTextColor(0x7BEF);
  gfx->getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2, 295);
  gfx->print(label);
}

// ---------- Input ----------
// Short press: cycle readout. Long press (LONG_PRESS_MS): toggle demo mode.
void handleButton() {
  static bool last = HIGH, longDone = false;
  static uint32_t pressedAt = 0;
  bool now = digitalRead(PIN_BUTTON);
  uint32_t ms = millis();
  if (last == HIGH && now == LOW) { pressedAt = ms; longDone = false; }
  if (now == LOW && !longDone && ms - pressedAt >= LONG_PRESS_MS) {
    longDone = true;
    demo = !demo;
    Serial.printf("demo mode %s\n", demo ? "ON" : "OFF");
    lastValidMs = 0;
    forceDraw = true;
  }
  if (last == LOW && now == HIGH && !longDone && ms - pressedAt > 40) {
    page = (page + 1) % 4;
    drawReadout();
  }
  last = now;
}

// Everything that must stay alive while we wait on Bluetooth: button, blink, face, readout.
void uiTick() {
  handleButton();

  uint32_t now = millis();
  if (!blinking && now >= nextBlink) { blinking = true; blinkEnd = now + 150; }
  if (blinking && now >= blinkEnd) { blinking = false; nextBlink = now + random(2500, 5500); }
  bool open = !blinking;

  Mood m = computeMood();
  bool moodChanged = (int)m != lastMood;
  if (forceDraw || moodChanged) {
    drawEyes(m, open);
    drawMouthFor(m);
    setLedMood(m);
    lastMood = (int)m;
    lastOpen = open;
    forceDraw = false;
    shownRpm = -1;                       // force a readout refresh after a full redraw
  } else if (m != SLEEPY && open != lastOpen) {
    drawEyes(m, open);                   // blink: only the eyes change
    lastOpen = open;
  }

  if (rpm != shownRpm || mph != shownMph || coolantC != shownTemp) {
    drawReadout();
    shownRpm = rpm; shownMph = mph; shownTemp = coolantC;
  }
}

// ---------- Connection ----------
bool connectAdapter() {
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
  lastValidMs = 0;
  linked = true;
  return true;
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(BLACK);
  led.begin();
  led.setBrightness(60);
  NimBLEDevice::init("");
  Serial.printf("Tachi starting, demo mode %s\n", demo ? "ON" : "OFF");
}

void loop() {
  static int which = 0;
  static uint32_t lastPoll = 0;

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

  uint32_t now = millis();
  if (now - lastPoll >= POLL_INTERVAL_MS) {
    pollOne(which);
    which = (which + 1) % 3;
    lastPoll = millis();
  }
  uiTick();
}
