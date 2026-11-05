// Host test for src/settings.h.   g++ -std=c++17 -Wall -I../../src settings_test.cpp -o settings_test && ./settings_test
#include "settings.h"
#include <cstdio>
#include <cstring>
using namespace settings;
static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define STREQ(a, b) do { checks++; if (strcmp((a), (b)) != 0) { fails++; printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, (a), (b)); } } while (0)
int main() {
  Settings s;
  CHECK(valid(s) && s.size == sizeof(Settings));
  // defaults: the same levels the firmware used before settings existed
  CHECK(s.coolWarnC == 100 && s.coolHotC == 108 && s.fuelLowPct == 15 && s.redlineRpm == 6500 && !s.speedKmh && !s.tempC && !s.pressBar);
  CHECK(fabsf(battLowOffV(s) - 12.0f) < 0.001f && fabsf(battLowRunV(s) - 13.2f) < 0.001f && fabsf(battCritV(s) - 11.5f) < 0.001f);
  // units
  CHECK(tempShown(91, s) == 196 && tempShown(0, s) == 32 && tempShown(100, s) == 212 && tempShown(-10, s) == 14);
  STREQ(tempUnit(s), "F"); STREQ(speedUnit(s), "MPH"); STREQ(pressUnit(s), "PSI"); STREQ(distUnit(s), "MI");
  CHECK(fabsf(speedShown(60, s) - 60) < 0.001f && fabsf(pressShown(14.5f, s) - 14.5f) < 0.001f && fabsf(distShown(10, s) - 10) < 0.001f);
  s.speedKmh = s.tempC = s.pressBar = 1;
  CHECK(tempShown(91, s) == 91); STREQ(tempUnit(s), "C"); STREQ(speedUnit(s), "KMH"); STREQ(pressUnit(s), "BAR"); STREQ(distUnit(s), "KM");
  CHECK(fabsf(speedShown(60, s) - 96.56f) < 0.01f && fabsf(pressShown(14.5f, s) - 1.0f) < 0.001f && fabsf(distShown(10, s) - 16.09f) < 0.01f);
  // warning text in both temperature units
  { Settings f; char b[24]; warnText(W_COOL_WARN, f, b, sizeof b); STREQ(b, "212 F"); warnText(W_COOL_HOT, f, b, sizeof b); STREQ(b, "226 F");
    warnText(W_BATT_LOW, f, b, sizeof b); STREQ(b, "12.0 V"); warnText(W_FUEL_LOW, f, b, sizeof b); STREQ(b, "15 %"); warnText(W_REDLINE, f, b, sizeof b); STREQ(b, "6500");
    Settings c; c.tempC = 1; warnText(W_COOL_WARN, c, b, sizeof b); STREQ(b, "100 C"); }
  // steppers: 5 deg F steps in Fahrenheit, 2 deg C steps in Celsius, never crossing, always valid
  { Settings f; warnAdjust(f, W_COOL_WARN, +1); CHECK(tempShown(f.coolWarnC, f) == 217); warnAdjust(f, W_COOL_WARN, -1); CHECK(tempShown(f.coolWarnC, f) == 212);
    for (int i = 0; i < 40; i++) warnAdjust(f, W_COOL_WARN, +1); CHECK(f.coolHotC > f.coolWarnC && valid(f));            // pushing warm up pulls hot up with it
    for (int i = 0; i < 40; i++) warnAdjust(f, W_COOL_HOT, -1); CHECK(f.coolHotC >= f.coolWarnC + 2 && valid(f));       // hot can't go below warm
    for (int i = 0; i < 80; i++) warnAdjust(f, W_COOL_WARN, -1); CHECK(f.coolWarnC == 60 && valid(f));
    Settings c; c.tempC = 1; warnAdjust(c, W_COOL_WARN, +1); CHECK(c.coolWarnC == 102); warnAdjust(c, W_COOL_HOT, -1); CHECK(c.coolHotC == 106); }
  { Settings f; for (int i = 0; i < 100; i++) warnAdjust(f, W_BATT_LOW, +1); CHECK(f.battLowDeciV == 130 && valid(f)); for (int i = 0; i < 100; i++) warnAdjust(f, W_BATT_LOW, -1); CHECK(f.battLowDeciV == 105 && valid(f));
    for (int i = 0; i < 100; i++) warnAdjust(f, W_FUEL_LOW, +1); CHECK(f.fuelLowPct == 50); for (int i = 0; i < 100; i++) warnAdjust(f, W_FUEL_LOW, -1); CHECK(f.fuelLowPct == 3);
    for (int i = 0; i < 100; i++) warnAdjust(f, W_REDLINE, +1); CHECK(f.redlineRpm == 10000); for (int i = 0; i < 100; i++) warnAdjust(f, W_REDLINE, -1); CHECK(f.redlineRpm == 3000 && valid(f));
    warnAdjust(f, W_REDLINE, +1); CHECK(f.redlineRpm == 3250); }
  // every combination of steps keeps the struct valid
  { Settings f; unsigned x = 12345; for (int i = 0; i < 5000; i++) { x = x * 1103515245u + 12345u; int item = (x >> 16) % W_COUNT, dir = ((x >> 8) & 1) ? 1 : -1; if ((x >> 24) % 7 == 0) f.tempC = !f.tempC; warnAdjust(f, item, dir); if (!valid(f)) { CHECK(false); break; } } CHECK(valid(f)); }
  // reset / changed count
  { Settings f; CHECK(warnIsDefault(f) && warnChanged(f) == 0); warnAdjust(f, W_FUEL_LOW, +1); warnAdjust(f, W_REDLINE, -1); CHECK(!warnIsDefault(f) && warnChanged(f) == 2); f.theme = 2; warnReset(f); CHECK(warnIsDefault(f) && f.theme == 2); }
  // validity: a blob from another version, a damaged one, or out of range is rejected
  { Settings f; f.magic = 0; CHECK(!valid(f)); f = Settings(); f.version = 2; CHECK(!valid(f)); f = Settings(); f.size = 3; CHECK(!valid(f)); f = Settings(); f.theme = THEMES; CHECK(!valid(f));
    f = Settings(); f.mode = MODES; CHECK(!valid(f)); f = Settings(); f.coolHotC = f.coolWarnC; CHECK(!valid(f)); f = Settings(); f.redlineRpm = 20000; CHECK(!valid(f)); f = Settings(); f.tempC = 2; CHECK(!valid(f));
    uint8_t raw[sizeof(Settings)]; Settings g; g.theme = 3; g.mode = 5; g.speedKmh = 1; memcpy(raw, &g, sizeof g); Settings h; memcpy(&h, raw, sizeof h); CHECK(valid(h) && h.theme == 3 && h.mode == 5 && h.speedKmh == 1); }
  printf("%d checks, %d failed, struct %d bytes\n", checks, fails, (int)sizeof(Settings));
  return fails ? 1 : 0;
}
