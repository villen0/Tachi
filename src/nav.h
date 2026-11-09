#pragma once
// Turn-by-turn card: what the phone app sends over Bluetooth and how the board shows it. Pure C++ (no Arduino), unit-tested on a PC:
// tools/tests/nav_test.cpp
//
// The phone writes one short text line to the board's NAV characteristic (see NAV_SERVICE_UUID in config.h):
//   N|<code>|<meters>|<street>     a turn: code = S L R SL SR HL HR UL UR RB A (see parse), distance to it in metres, street name (may be empty)
//   C                              the route ended: clear the card
// The board answers on its LIVE characteristic (see formatLive).
// Example: "N|R|350|Main St"  ->  a right turn in 350 m onto MAIN ST.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "trip.h"
#include "settings.h"

namespace nav {

enum Dir : uint8_t { D_NONE = 0, D_STRAIGHT, D_LEFT, D_RIGHT, D_SLIGHT_L, D_SLIGHT_R, D_SHARP_L, D_SHARP_R, D_UTURN_L, D_UTURN_R, D_ROUNDABOUT, D_ARRIVE, D_COUNT };

struct Turn {
  bool active = false; Dir dir = D_NONE; uint32_t distM = 0; char street[40] = "";
};

inline Dir dirFromCode(const char *c) {
  struct E { const char *k; Dir d; };
  static const E t[] = {{"S", D_STRAIGHT}, {"L", D_LEFT}, {"R", D_RIGHT}, {"SL", D_SLIGHT_L}, {"SR", D_SLIGHT_R}, {"HL", D_SHARP_L}, {"HR", D_SHARP_R},
                        {"UL", D_UTURN_L}, {"UR", D_UTURN_R}, {"RB", D_ROUNDABOUT}, {"A", D_ARRIVE}};
  for (const E &e : t) if (strcmp(e.k, c) == 0) return e.d;
  return D_NONE;
}

// The display font has capitals, digits and a little punctuation: upper-case the name, turn anything else into a space, squeeze spaces.
inline void cleanStreet(const char *in, char *out, size_t n) {
  size_t o = 0; bool sp = true;
  for (; *in && o + 1 < n; in++) {
    char ch = *in; if (ch >= 'a' && ch <= 'z') ch -= 32;
    bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '/' || ch == ',';
    if (!ok) ch = ' ';
    if (ch == ' ') { if (sp) continue; sp = true; } else sp = false;
    out[o++] = ch;
  }
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = 0;
}

// One message from the phone. Returns true if it was understood (and t updated): a turn sets t.active, "C" clears it.
inline bool parse(const char *s, size_t n, Turn &t) {
  char buf[96]; if (n == 0 || n >= sizeof(buf)) return false;
  memcpy(buf, s, n); buf[n] = 0;
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
  if (strcmp(buf, "C") == 0) { t = Turn(); return true; }
  if (buf[0] != 'N' || buf[1] != '|') return false;
  char *code = buf + 2, *bar = strchr(code, '|'); if (!bar) return false;
  *bar = 0; char *dist = bar + 1, *bar2 = strchr(dist, '|'); const char *street = "";
  if (bar2) { *bar2 = 0; street = bar2 + 1; }
  Dir d = dirFromCode(code); if (d == D_NONE) return false;
  char *end = nullptr; long m = strtol(dist, &end, 10); if (end == dist || m < 0 || m > 999999) return false;
  t.active = true; t.dir = d; t.distM = (uint32_t)m; cleanStreet(street, t.street, sizeof(t.street));
  return true;
}

// Distance text: metres / kilometres, or feet / miles. num is the number, unit the small label under it.
inline void formatDist(uint32_t m, bool metric, char *num, size_t nn, char *unit, size_t un) {
  if (metric) {
    if (m < 1000) { snprintf(num, nn, "%u", (unsigned)((m + 5) / 10 * 10)); snprintf(unit, un, "M"); }
    else { float km = m / 1000.0f; snprintf(num, nn, km < 9.95f ? "%.1f" : "%.0f", km); snprintf(unit, un, "KM"); }
  } else {
    float ft = m * 3.28084f;
    if (ft < 528.0f) { snprintf(num, nn, "%u", (unsigned)((int)(ft / 50.0f + 0.5f) * 50)); snprintf(unit, un, "FT"); }
    else { float mi = m / 1609.344f; snprintf(num, nn, mi < 9.95f ? "%.1f" : "%.0f", mi); snprintf(unit, un, "MI"); }
  }
}

// 0 far away .. 1 at the turn (the bar under the distance fills as the turn gets close)
inline float closeness(uint32_t m) { float f = 1.0f - m / 500.0f; return f < 0 ? 0 : (f > 1 ? 1 : f); }

// The live values the board streams to the app (notification on the LIVE characteristic, about twice a second):
//   L|rpm|mph|coolC|battDeciV|fuelPct|loadPct|units|intakeC|torqueNm|torquePct
// rpm..loadPct: -1 = no fresh value; intakeC, torqueNm, torquePct: NO_VALUE (-999) = none. units: bit0 km/h, bit1 deg C, bit2 bar.
// Speed is always mph and temperature deg C here; the app converts with the units bits (the same choice the board's Settings page shows).
// torqueNm is the real torque when the car gives its reference torque, else NO_VALUE and the app shows the percent.
constexpr int NO_VALUE = -999;
inline void formatLive(char *out, size_t n, int rpm, int mph, int coolC, int battDeciV, int fuel, int load, int units, int intakeC, int torqueNm, int torquePct) {
  snprintf(out, n, "L|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d", rpm, mph, coolC, battDeciV, fuel, load, units, intakeC, torqueNm, torquePct);
}

// ---- trips (the app asks with "Q|T"; the board answers on the TRIPS characteristic, newest first, then "E|count") ----
//   R|live|seq|durationS|distCentiMi|topMph|avgMph|maxRpm|maxCoolC|peakBoostDeciPsi|minBattDeciV|maxBattDeciV|codes
// live = 1 for the trip that is still running; -32768 = unknown (maxCoolC, peakBoost); 0 = unknown battery; codes = new fault codes joined by ';'.
// The board has no clock, so no date is sent (the app notes when it first saw a trip). The rating is not sent (the grading was removed).
inline bool isTripQuery(const char *s, size_t n) { return n >= 3 && s[0] == 'Q' && s[1] == '|' && s[2] == 'T'; }
inline void formatTrip(char *out, size_t n, const trip::Record &r, bool live) {
  char codes[4 * 6 + 1]; size_t o = 0;
  for (int i = 0; i < r.newCodeCount && i < 4; i++) { if (i) codes[o++] = ';'; for (int k = 0; k < 5 && r.newCodes[i][k]; k++) codes[o++] = r.newCodes[i][k]; }
  codes[o] = 0;
  snprintf(out, n, "R|%d|%u|%u|%u|%u|%u|%u|%d|%d|%u|%u|%s", live ? 1 : 0, (unsigned)r.seq, (unsigned)r.durationS, (unsigned)r.distCentiMi, (unsigned)r.topMph, (unsigned)r.avgMph,
           (unsigned)r.maxRpm, (int)r.maxCoolC, (int)r.peakBoostDeciPsi, (unsigned)r.minBattDeciV, (unsigned)r.maxBattDeciV, codes);
}

// ---- settings (the app asks with "S?" and changes one setting with "S|key|value"; the board answers on the CONFIG characteristic) ----
//   C|speedKmh|tempC|pressBar|theme|mode|gauge|coolWarnC|coolHotC|battLowDeciV|fuelLowPct|redlineRpm|brightness      (the same settings as the board's Settings page)
// Keys: sp tp pr = units (0 / 1), th = look (0..4), md = colour mode (0..6), ga = gauge style (0 dial, 1 arc, 2 ticks), br = brightness (5..100),
//       wc wh wb wf wr = one step up (+1) or down (-1) of coolant warm / coolant hot / battery low / fuel low / redline (the board's own step sizes and limits).
struct Change { char key[4] = ""; int v = 0; };
inline bool isConfigQuery(const char *s, size_t n) { return n >= 2 && s[0] == 'S' && s[1] == '?'; }
inline bool parseChange(const char *s, size_t n, Change &c) {
  if (n < 5 || n > 24 || s[0] != 'S' || s[1] != '|') return false;
  char buf[32]; memcpy(buf, s, n); buf[n] = 0;
  char *k = buf + 2, *bar = strchr(k, '|'); if (!bar || bar == k || bar - k > 3) return false;
  *bar = 0; char *end = nullptr; long v = strtol(bar + 1, &end, 10); if (end == bar + 1 || v < -100000 || v > 100000) return false;
  strcpy(c.key, k); c.v = (int)v; return true;
}
// Applies one change; returns false for an unknown key or a value out of range (nothing changes then).
inline bool applyChange(settings::Settings &cfg, uint8_t &gauge, int &brightness, const Change &c) {
  const char *k = c.key; int v = c.v;
  if (!strcmp(k, "sp")) cfg.speedKmh = v != 0; else if (!strcmp(k, "tp")) cfg.tempC = v != 0; else if (!strcmp(k, "pr")) cfg.pressBar = v != 0;
  else if (!strcmp(k, "th")) { if (v < 0 || v >= settings::THEMES) return false; cfg.theme = (uint8_t)v; }
  else if (!strcmp(k, "md")) { if (v < 0 || v >= settings::MODES) return false; cfg.mode = (uint8_t)v; }
  else if (!strcmp(k, "ga")) { if (v < 0 || v > 2) return false; gauge = (uint8_t)v; }
  else if (!strcmp(k, "br")) brightness = v < 5 ? 5 : (v > 100 ? 100 : v);
  else if (!strcmp(k, "wc")) settings::warnAdjust(cfg, settings::W_COOL_WARN, v >= 0 ? 1 : -1);
  else if (!strcmp(k, "wh")) settings::warnAdjust(cfg, settings::W_COOL_HOT, v >= 0 ? 1 : -1);
  else if (!strcmp(k, "wb")) settings::warnAdjust(cfg, settings::W_BATT_LOW, v >= 0 ? 1 : -1);
  else if (!strcmp(k, "wf")) settings::warnAdjust(cfg, settings::W_FUEL_LOW, v >= 0 ? 1 : -1);
  else if (!strcmp(k, "wr")) settings::warnAdjust(cfg, settings::W_REDLINE, v >= 0 ? 1 : -1);
  else return false;
  return true;
}
inline void formatConfig(char *out, size_t n, const settings::Settings &c, uint8_t gauge, int brightness) {
  snprintf(out, n, "C|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d", c.speedKmh, c.tempC, c.pressBar, c.theme, c.mode, (int)gauge, (int)c.coolWarnC, (int)c.coolHotC,
           (int)c.battLowDeciV, (int)c.fuelLowPct, (int)c.redlineRpm, brightness);
}

}  // namespace nav
