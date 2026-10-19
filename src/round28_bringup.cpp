// Bring-up sketch for the Waveshare ESP32-S3-Touch-LCD-2.8C (env waveshare_s3_round28_test).
// Shows a test picture on the round 480x480 panel and draws a dot wherever the GT911 reports a touch.
// Serial (115200) prints the I2C scan, the expander address found and every touch point.
// Use it to prove the display, backlight and touch before running the full firmware (env waveshare_s3_round28).
#include "display_round28.h"

// Arduino_GFX 1.6.x names its colors RGB565_*; keep short names for readability.
#define BLACK   RGB565_BLACK
#define WHITE   RGB565_WHITE
#define RED     RGB565_RED
#define GREEN   RGB565_GREEN
#define BLUE    RGB565_BLUE
#define YELLOW  RGB565_YELLOW
#define CYAN    RGB565_CYAN
#define MAGENTA RGB565_MAGENTA

static void drawTestPicture() {
  rgbDisp->fillScreen(BLACK);
  const int cx = LCD_W / 2, cy = LCD_H / 2;
  const uint16_t bars[] = {RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA};
  for (int i = 0; i < 6; i++) rgbDisp->fillRect(i * (LCD_W / 6), 0, LCD_W / 6, 70, bars[i]);
  rgbDisp->drawCircle(cx, cy, 238, WHITE);          // panel edge
  rgbDisp->drawCircle(cx, cy, 180, 0x7BEF);
  rgbDisp->drawCircle(cx, cy, 120, 0x7BEF);
  rgbDisp->drawFastHLine(0, cy, LCD_W, 0x39E7);
  rgbDisp->drawFastVLine(cx, 0, LCD_H, 0x39E7);
  rgbDisp->setTextColor(WHITE);
  rgbDisp->setTextSize(4);
  rgbDisp->setCursor(cx - 60, cy - 50); rgbDisp->print("TACHI");
  rgbDisp->setTextSize(2);
  rgbDisp->setCursor(cx - 100, cy + 10); rgbDisp->print("round 2.8C bring-up");
  rgbDisp->setCursor(cx - 90, cy + 40); rgbDisp->print("touch the screen");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nTachi round 2.8C bring-up: " __DATE__ " " __TIME__);
  i2cBegin();
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("I2C device at 0x%02X\n", a);
  }
  roundDisplayInit();
  drawTestPicture();
  touchInit();
}

void loop() {
  bool down; int x, y;
  if (touchPoll(&down, &x, &y) && down) {
    Serial.printf("touch %d,%d\n", x, y);
    rgbDisp->fillCircle(x, y, 8, WHITE);
  }
  if (digitalRead(PIN_BUTTON) == LOW) drawTestPicture();   // BOOT button clears the drawing
  delay(10);
}
