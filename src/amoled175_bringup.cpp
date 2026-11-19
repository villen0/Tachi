// Bring-up sketch for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (env waveshare_s3_amoled175_test).
// Proves, one step at a time, what the full firmware needs: the I2C bus (scan), the CO5300 display (test picture and brightness steps),
// the CST9217 touch (a dot where you touch, raw and mapped coordinates on the serial port) and the QMI8658 motion sensor.
// Serial (115200, the USB port of the board): the scan, the touch points, the IMU id. Flash this first, then the full firmware.
#include "display_amoled175.h"

#define BLACK   RGB565_BLACK
#define WHITE   RGB565_WHITE
#define RED     RGB565_RED
#define GREEN   RGB565_GREEN
#define BLUE    RGB565_BLUE
#define YELLOW  RGB565_YELLOW
#define CYAN    RGB565_CYAN
#define MAGENTA RGB565_MAGENTA

static void drawTestPicture() {                      // drawn on the panel directly: 466x466, no crop
  rgbDisp->fillScreen(BLACK);
  const int cx = PANEL_W / 2, cy = PANEL_H / 2;
  const uint16_t bars[] = {RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA};
  for (int i = 0; i < 6; i++) rgbDisp->fillRect(i * (PANEL_W / 6), 40, PANEL_W / 6, 50, bars[i]);   // the colours must look right (red is red)
  rgbDisp->drawCircle(cx, cy, PANEL_W / 2 - 1, WHITE);        // the panel edge: the whole circle must be visible
  rgbDisp->drawCircle(cx, cy, 180, 0x7BEF);
  rgbDisp->drawCircle(cx, cy, 120, 0x7BEF);
  rgbDisp->drawFastHLine(0, cy, PANEL_W, 0x39E7);
  rgbDisp->drawFastVLine(cx, 0, PANEL_H, 0x39E7);
  rgbDisp->fillRect(0, 0, 24, 24, RED); rgbDisp->fillRect(PANEL_W - 24, 0, 24, 24, GREEN);          // corner marks: top-left red, top-right green,
  rgbDisp->fillRect(0, PANEL_H - 24, 24, 24, BLUE); rgbDisp->fillRect(PANEL_W - 24, PANEL_H - 24, 24, 24, YELLOW);   // bottom-left blue, bottom-right yellow (clipped by the circle)
  rgbDisp->setTextColor(WHITE); rgbDisp->setTextSize(3); rgbDisp->setCursor(cx - 78, cy - 40); rgbDisp->print("REVSENSE");
  rgbDisp->setTextSize(2); rgbDisp->setCursor(cx - 92, cy + 4); rgbDisp->print("AMOLED 1.75 test");
  rgbDisp->setTextColor(0x7BEF); rgbDisp->setCursor(cx - 100, cy + 40); rgbDisp->print("touch the screen");
}

// The exact path the full firmware uses: a 480x480 frame buffer in PSRAM pushed row by row with panelPush (cropped to the panel, rows sent in pairs).
// A rainbow with a white frame, numbered stripes and a red / blue / green / yellow square in each corner of the 466 px panel must appear.
static void framebufferTest() {
  uint16_t *fb = (uint16_t *)ps_malloc((size_t)LCD_W * LCD_H * 2);
  if (!fb) { Serial.println("framebuffer test: no PSRAM"); return; }
  // eight vertical colour bars (white, yellow, cyan, green, magenta, red, blue, grey), dark at the bottom: if rows were shifted, the bars would lean.
  const uint16_t bar[8] = {WHITE, YELLOW, CYAN, GREEN, MAGENTA, RED, BLUE, 0x7BEF};
  for (int y = 0; y < LCD_H; y++) for (int x = 0; x < LCD_W; x++) fb[y * LCD_W + x] = (y > 360) ? 0x0000 : bar[x * 8 / LCD_W];
  auto box = [&](int x0, int y0, int x1, int y1, uint16_t c) { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) fb[y * LCD_W + x] = c; };
  for (int k = 0; k < 4; k++) { box(PANEL_CROP_X + k, PANEL_CROP_Y + k, LCD_W - 1 - PANEL_CROP_X - k, PANEL_CROP_Y + k, WHITE); box(PANEL_CROP_X + k, LCD_H - 1 - PANEL_CROP_Y - k, LCD_W - 1 - PANEL_CROP_X - k, LCD_H - 1 - PANEL_CROP_Y - k, WHITE);
    box(PANEL_CROP_X + k, PANEL_CROP_Y + k, PANEL_CROP_X + k, LCD_H - 1 - PANEL_CROP_Y - k, WHITE); box(LCD_W - 1 - PANEL_CROP_X - k, PANEL_CROP_Y + k, LCD_W - 1 - PANEL_CROP_X - k, LCD_H - 1 - PANEL_CROP_Y - k, WHITE); }
  box(PANEL_CROP_X + 20, PANEL_CROP_Y + 90, PANEL_CROP_X + 60, PANEL_CROP_Y + 130, RED); box(LCD_W - 1 - PANEL_CROP_X - 60, PANEL_CROP_Y + 90, LCD_W - 1 - PANEL_CROP_X - 20, PANEL_CROP_Y + 130, GREEN);
  box(PANEL_CROP_X + 20, LCD_H - 1 - PANEL_CROP_Y - 130, PANEL_CROP_X + 60, LCD_H - 1 - PANEL_CROP_Y - 90, BLUE); box(LCD_W - 1 - PANEL_CROP_X - 60, LCD_H - 1 - PANEL_CROP_Y - 130, LCD_W - 1 - PANEL_CROP_X - 20, LCD_H - 1 - PANEL_CROP_Y - 90, YELLOW);
  for (int y = PANEL_CROP_Y + 8; y < LCD_H - PANEL_CROP_Y - 8; y += 20) box(PANEL_CROP_X + 8, y, PANEL_CROP_X + ((y / 20) % 5 == 0 ? 60 : 30), y + 1, 0x0000);   // ruler ticks: every 20 rows, longer every 100
  uint32_t t0 = millis();
  setBacklight(45);                                    // steady and not too bright for a photo
  for (int y = 0; y < LCD_H; y++) panelPush(0, y, LCD_W, fb + y * LCD_W);
  Serial.printf("framebuffer test: full frame pushed in %u ms\n", (unsigned)(millis() - t0));
  // partial updates like the dials: a few spans on odd and even rows, shifted by one pixel each time
  for (int y = 200; y < 215; y++) { for (int x = 100; x < 180; x++) fb[y * LCD_W + x] = WHITE; panelPush(99 + (y & 1), y, 82, fb + y * LCD_W + 99 + (y & 1)); }
  free(fb);
}

static void i2cScan() {
  Serial.println("I2C scan (expected: 0x18 ES8311, 0x20 TCA9554, 0x34 AXP2101, 0x40 ES7210, 0x51 PCF85063, 0x5A CST9217, 0x6B QMI8658):");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) { Serial.printf("  found 0x%02X\n", a); n++; } }
  Serial.printf("  %d device(s)\n", n);
}

static void imuCheck() {
  for (uint8_t a : {(uint8_t)0x6B, (uint8_t)0x6A}) {
    Wire.beginTransmission(a); Wire.write(0x00);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)a, 1) != 1) continue;
    uint8_t id = Wire.read(); Serial.printf("IMU at 0x%02X, WHO_AM_I 0x%02X (QMI8658 answers 0x05)\n", a, id); return;
  }
  Serial.println("IMU: no answer at 0x6B / 0x6A");
}

// Calibration targets: touch the CENTRE of each numbered cross in order (1 to 8), about a second each. The sketch prints the average raw point of each touch.
static const int CALX[8] = {233, 233, 233, 233, 233, 233, 70, 396}, CALY[8] = {40, 130, 230, 330, 390, 440, 233, 233};
static void drawTargets() {
  rgbDisp->fillScreen(BLACK);
  rgbDisp->drawCircle(PANEL_W / 2, PANEL_H / 2, PANEL_W / 2 - 1, WHITE);
  for (int i = 0; i < 8; i++) {
    rgbDisp->drawFastHLine(CALX[i] - 14, CALY[i], 29, YELLOW); rgbDisp->drawFastVLine(CALX[i], CALY[i] - 14, 29, YELLOW);
    rgbDisp->setTextSize(2); rgbDisp->setTextColor(WHITE); rgbDisp->setCursor(CALX[i] + 18, CALY[i] - 8); rgbDisp->printf("%d", i + 1);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nRevsense AMOLED 1.75 touch calibration: " __DATE__ " " __TIME__);
  if (!roundDisplayInit()) Serial.println("display init FAILED");
  setBacklight(45);
  drawTargets();
  touchInit();
  Serial.println("CAL: touch the centre of cross 1, 2, ... 8 in order (one second each, lift between).");
  for (int i = 0; i < 8; i++) Serial.printf("CAL target %d is at panel %d,%d\n", i + 1, CALX[i], CALY[i]);
}

void loop() {
  static long sx = 0, sy = 0; static int n = 0, touchNo = 0;
  bool dn = false; int x = 0, y = 0;
  if (touchPoll(&dn, &x, &y)) {
    if (dn) { sx += touchRawX; sy += touchRawY; n++; rgbDisp->fillCircle(x - PANEL_CROP_X, y - PANEL_CROP_Y, 3, RED); }
    else if (n > 0) { touchNo++; Serial.printf("CAL touch %d: raw average %ld,%ld over %d samples\n", touchNo, sx / n, sy / n, n); sx = sy = 0; n = 0; }
  }
  delay(8);
}
