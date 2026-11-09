#pragma once
// Turn-by-turn card: what the phone app sends over Bluetooth and how the board shows it. Pure C++ (no Arduino), unit-tested on a PC:
// tools/tests/nav_test.cpp
//
// The phone writes one short text line to the board's NAV characteristic (see NAV_SERVICE_UUID in config.h):
//   N|<code>|<meters>|<street>     a turn: code = S L R SL SR HL HR UL UR RB A (see parse), distance to it in metres, street name (may be empty)
//   C                              the route ended: clear the card
// Example: "N|R|350|Main St"  ->  a right turn in 350 m onto MAIN ST.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

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

}  // namespace nav
