// Waveshare ESP32-S3 1.47" OBD pet - first version.
// STATUS: written without hardware access. NOT yet compiled or run. See README.md.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

enum Mood { HAPPY, UNEASY, WORRIED, SLEEPY };

// ---------- Display (driver + pins UNVERIFIED, see README) ----------
Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_LCD_RST, 0 /*rotation*/, true /*IPS*/, LCD_W, LCD_H,
                                      34, 0, 34, 0 /*col/row offsets for 172x320 panel*/);
Adafruit_NeoPixel led(1, PIN_LED, NEO_GRB + NEO_KHZ800);

// ---------- BLE / OBD state ----------
NimBLEClient *client = nullptr;
NimBLERemoteCharacteristic *rxChar = nullptr;  // notify: adapter -> board
NimBLERemoteCharacteristic *txChar = nullptr;  // write:  board -> adapter
String rxBuf;                                  // filled from the BLE callback task
volatile bool gotPrompt = false;

int rpm = 0, mph = 0, coolantC = 0;
uint32_t lastValidMs = 0;   // last time any PID returned valid data
bool linked = false;
int page = 0;               // readout: 0 RPM, 1 mph, 2 coolant, 3 hidden

// ---------- Helpers ----------
void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '>') gotPrompt = true;                       // ELM327 "ready" prompt
    else if (c != '\r' && c != '\n' && c != ' ') rxBuf += c;
  }
}

String sendCmd(const char *cmd, uint32_t timeoutMs = 1500) {
  rxBuf = "";
  gotPrompt = false;
  String out = String(cmd) + "\r";
  txChar->writeValue((const uint8_t *)out.c_str(), out.length(), false);
  uint32_t t0 = millis();
  while (!gotPrompt && millis() - t0 < timeoutMs) delay(5);
  String r = rxBuf;
  Serial.printf("> %s   < %s\n", cmd, r.c_str());   // raw log for debugging
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
void showStatus(const char *msg, uint16_t color) {
  gfx->fillRect(0, 50, LCD_W, 190, BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_W - w) / 2, 150);
  gfx->print(msg);
  Serial.println(msg);
}

// Draw a smile (curve > 0), flat line (0) or frown (curve < 0).
void drawMouth(int cx, int cy, int width, int curve, uint16_t color) {
  for (int t = -width / 2; t <= width / 2; t++) {
    float u = (2.0f * t) / width;
    int y = cy + (int)(curve * (1.0f - u * u));
    gfx->fillCircle(cx + t, y, 3, color);
  }
}

void drawFace(Mood m, bool eyesOpen) {
  gfx->fillRect(0, 50, LCD_W, 190, BLACK);
  const int ey = 115, ex1 = 48, ex2 = 124;
  int r = (m == WORRIED) ? 26 : 22;
  if (m == SLEEPY || !eyesOpen) {
    gfx->fillRect(ex1 - 20, ey - 3, 40, 6, WHITE);
    gfx->fillRect(ex2 - 20, ey - 3, 40, 6, WHITE);
  } else {
    int pupil = (m == WORRIED) ? 5 : 9;
    gfx->fillCircle(ex1, ey, r, WHITE);
    gfx->fillCircle(ex2, ey, r, WHITE);
    gfx->fillCircle(ex1, ey, pupil, BLACK);
    gfx->fillCircle(ex2, ey, pupil, BLACK);
  }
  const int mx = LCD_W / 2, my = 200;
  switch (m) {
    case HAPPY:   drawMouth(mx, my, 70, 16, GREEN);  break;
    case UNEASY:  drawMouth(mx, my, 60, 0, YELLOW);  break;
    case WORRIED: drawMouth(mx, my + 8, 60, -16, RED); break;
    case SLEEPY:  gfx->drawCircle(mx, my, 8, 0x7BEF); break;
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

// ---------- Connection ----------
bool connectAdapter() {
  showStatus("Scanning...", 0x07FF);
  setLed(0, 20, 40);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->getResults(5000, false);
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
  for (auto c : init) sendCmd(c, 3000);
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
}

void handleButton() {
  static bool last = HIGH;
  static uint32_t lastPress = 0;
  bool now = digitalRead(PIN_BUTTON);
  if (last == HIGH && now == LOW && millis() - lastPress > 250) {
    page = (page + 1) % 4;
    lastPress = millis();
    drawReadout();
  }
  last = now;
}

void loop() {
  static int which = 0;
  static uint32_t lastPoll = 0, nextBlink = 0, blinkEnd = 0;
  static bool blinking = false, lastOpen = true, forceDraw = true;
  static int lastMood = -1;
  static int shownRpm = -1, shownMph = -1, shownTemp = -999;

  handleButton();

  if (!client || !client->isConnected()) {
    if (linked) {
      linked = false;
      showStatus("Link lost", RED);
      setLed(60, 0, 0);
      delay(RECONNECT_WAIT_MS);
    }
    if (!connectAdapter()) {
      showStatus("Not found", YELLOW);
      delay(1500);
    }
    forceDraw = true;
    return;
  }

  uint32_t now = millis();
  if (now - lastPoll >= POLL_INTERVAL_MS) {
    pollOne(which);
    which = (which + 1) % 3;
    lastPoll = now;
  }

  // blink every few seconds
  if (!blinking && now >= nextBlink) { blinking = true; blinkEnd = now + 150; }
  if (blinking && now >= blinkEnd) { blinking = false; nextBlink = now + random(2500, 5500); }
  bool open = !blinking;

  Mood m = computeMood();
  if (forceDraw || (int)m != lastMood || (m != SLEEPY && open != lastOpen)) {
    drawFace(m, open);
    if ((int)m != lastMood || forceDraw) setLedMood(m);
    lastMood = (int)m;
    lastOpen = open;
    forceDraw = false;
    shownRpm = -1;  // face redraw does not touch the readout, but keep it fresh below
  }

  if (rpm != shownRpm || mph != shownMph || coolantC != shownTemp) {
    drawReadout();
    shownRpm = rpm; shownMph = mph; shownTemp = coolantC;
  }
}
