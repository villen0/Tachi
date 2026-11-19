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

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nRevsense AMOLED 1.75 bring-up: " __DATE__ " " __TIME__);
  if (!roundDisplayInit()) Serial.println("display init FAILED");
  drawTestPicture();
  i2cScan();
  touchInit();
  imuCheck();
  Serial.printf("touch axes: swap=%d flipX=%d flipY=%d (board_amoled175.h). Tap near each corner of the circle and compare raw and mapped.\n", TOUCH_SWAP_XY, TOUCH_FLIP_X, TOUCH_FLIP_Y);
  Serial.println("brightness steps every 6 s: 20%, 55%, 100%, 85% (the picture must get brighter, then settle)");
}

void loop() {
  static uint32_t lastStep = 0; static int step = 0;
  if (millis() - lastStep > 6000) { lastStep = millis(); static const int lv[4] = {20, 55, 100, 85}; setBacklight(lv[step]); Serial.printf("brightness %d %%\n", lv[step]); step = (step + 1) & 3; }
  bool dn = false; int x = 0, y = 0;
  if (touchPoll(&dn, &x, &y) && dn) {
    int px = x - PANEL_CROP_X, py = y - PANEL_CROP_Y;           // back to panel pixels for drawing
    rgbDisp->fillCircle(px, py, 8, YELLOW);
    Serial.printf("touch: mapped x=%d y=%d (panel %d,%d)\n", x, y, px, py);
  }
  delay(8);
}
