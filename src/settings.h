#pragma once
// The user's settings: units, warning levels, theme. Pure C++ (no Arduino) so it is unit-tested on a PC: tools/tests/settings_test.cpp
//
// Everything is stored in the car's own units (km/h-free: speed in mph, temperature in deg C, pressure in psi) and converted only for display.
// The struct is a plain blob that main.cpp saves in flash; valid() rejects anything saved by another version or damaged.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace settings {

enum WarnItem : uint8_t { W_COOL_WARN, W_COOL_HOT, W_BATT_LOW, W_FUEL_LOW, W_REDLINE, W_COUNT };

#pragma pack(push, 1)
struct Settings {
  uint32_t magic = 0x54534554;            // "TSET"
  uint16_t version = 1, size = 0;
  uint8_t speedKmh = 0, tempC = 0, pressBar = 0;   // 0 = mph / deg F / psi, 1 = km/h / deg C / bar
  uint8_t theme = 4, mode = 0;                     // look (see theme.h; 4 = REVSENSE, the default) and accent colour (0 = the theme's own)
  int16_t coolWarnC = 100, coolHotC = 108;         // coolant turns yellow / red
  uint8_t battLowDeciV = 120;                      // battery yellow below this with the engine off (running: 1.2 V higher); red 0.5 V below it
  uint8_t fuelLowPct = 15;                         // fuel yellow below this, red below half of it
  uint16_t redlineRpm = 6500;                      // shift lights, red zone of the rev sweep
  Settings() { size = sizeof(Settings); }
};
#pragma pack(pop)

constexpr int THEMES = 5, MODES = 8;               // modes: the theme's own colour, 6 accent colours, CUSTOM (any colour from the phone app; the colour itself is saved under its own key)

inline bool valid(const Settings &s) {
  Settings d;
  return s.magic == d.magic && s.version == d.version && s.size == sizeof(Settings) && s.theme < THEMES && s.mode < MODES &&
         s.speedKmh <= 1 && s.tempC <= 1 && s.pressBar <= 1 && s.coolWarnC >= 60 && s.coolWarnC <= 140 && s.coolHotC >= 62 && s.coolHotC <= 150 &&
         s.coolHotC > s.coolWarnC && s.battLowDeciV >= 105 && s.battLowDeciV <= 130 && s.fuelLowPct >= 3 && s.fuelLowPct <= 50 && s.redlineRpm >= 3000 && s.redlineRpm <= 10000;
}

// derived levels
inline float battLowOffV(const Settings &s) { return s.battLowDeciV / 10.0f; }
inline float battLowRunV(const Settings &s) { return s.battLowDeciV / 10.0f + 1.2f; }
inline float battCritV(const Settings &s) { return s.battLowDeciV / 10.0f - 0.5f; }

// ---- units: convert for display; the stored and internal values stay mph / deg C / psi ----
inline float speedShown(float mph, const Settings &s) { return s.speedKmh ? mph * 1.609344f : mph; }
inline const char *speedUnit(const Settings &s) { return s.speedKmh ? "KMH" : "MPH"; }
inline int tempShown(int c, const Settings &s) { return s.tempC ? c : (c * 9 + (c >= 0 ? 2 : -2)) / 5 + 32; }
inline const char *tempUnit(const Settings &s) { return s.tempC ? "C" : "F"; }
inline float pressShown(float psi, const Settings &s) { return s.pressBar ? psi * 0.0689476f : psi; }
inline const char *pressUnit(const Settings &s) { return s.pressBar ? "BAR" : "PSI"; }
inline float distShown(float miles, const Settings &s) { return s.speedKmh ? miles * 1.609344f : miles; }
inline const char *distUnit(const Settings &s) { return s.speedKmh ? "KM" : "MI"; }

// ---- warning levels: what the page shows and how a + / - press changes them ----
inline const char *warnLabel(int item) {
  static const char *l[W_COUNT] = {"COOL WARM", "COOL HOT", "BATT LOW", "FUEL LOW", "REDLINE"};
  return item >= 0 && item < W_COUNT ? l[item] : "";
}
inline void warnText(int item, const Settings &s, char *out, size_t n) {
  switch (item) {
    case W_COOL_WARN: snprintf(out, n, "%d %s", tempShown(s.coolWarnC, s), tempUnit(s)); break;
    case W_COOL_HOT:  snprintf(out, n, "%d %s", tempShown(s.coolHotC, s), tempUnit(s)); break;
    case W_BATT_LOW:  snprintf(out, n, "%.1f V", s.battLowDeciV / 10.0f); break;
    case W_FUEL_LOW:  snprintf(out, n, "%d %%", (int)s.fuelLowPct); break;
    case W_REDLINE:   snprintf(out, n, "%u", (unsigned)s.redlineRpm); break;
    default: out[0] = 0;
  }
}
// dir = +1 / -1. Steps: 5 deg F or 2 deg C, 0.1 V, 1 %, 250 rpm. The warm level stays below the hot level.
inline void warnAdjust(Settings &s, int item, int dir) {
  auto stepTemp = [&](int c, int d) { return s.tempC ? c + 2 * d : (int)lroundf(((tempShown(c, s) + 5 * d) - 32) * 5.0f / 9.0f); };
  switch (item) {
    case W_COOL_WARN: { int v = stepTemp(s.coolWarnC, dir); if (v < 60) v = 60; if (v > 138) v = 138; s.coolWarnC = (int16_t)v; if (s.coolHotC <= s.coolWarnC) s.coolHotC = (int16_t)(s.coolWarnC + 2); break; }
    case W_COOL_HOT:  { int v = stepTemp(s.coolHotC, dir); if (v < s.coolWarnC + 2) v = s.coolWarnC + 2; if (v > 150) v = 150; s.coolHotC = (int16_t)v; break; }
    case W_BATT_LOW:  { int v = s.battLowDeciV + dir; if (v < 105) v = 105; if (v > 130) v = 130; s.battLowDeciV = (uint8_t)v; break; }
    case W_FUEL_LOW:  { int v = s.fuelLowPct + dir; if (v < 3) v = 3; if (v > 50) v = 50; s.fuelLowPct = (uint8_t)v; break; }
    case W_REDLINE:   { int v = (int)s.redlineRpm + 250 * dir; if (v < 3000) v = 3000; if (v > 10000) v = 10000; s.redlineRpm = (uint16_t)v; break; }
  }
}
inline void warnReset(Settings &s) { Settings d; s.coolWarnC = d.coolWarnC; s.coolHotC = d.coolHotC; s.battLowDeciV = d.battLowDeciV; s.fuelLowPct = d.fuelLowPct; s.redlineRpm = d.redlineRpm; }
inline bool warnIsDefault(const Settings &s) { Settings d; return s.coolWarnC == d.coolWarnC && s.coolHotC == d.coolHotC && s.battLowDeciV == d.battLowDeciV && s.fuelLowPct == d.fuelLowPct && s.redlineRpm == d.redlineRpm; }
inline int warnChanged(const Settings &s) { Settings d; return (s.coolWarnC != d.coolWarnC) + (s.coolHotC != d.coolHotC) + (s.battLowDeciV != d.battLowDeciV) + (s.fuelLowPct != d.fuelLowPct) + (s.redlineRpm != d.redlineRpm); }

}  // namespace settings
