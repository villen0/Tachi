#pragma once
// "Rally cluster" page: big estimated gear + speed, big RPM readout, a curved RPM sweep bar with a shift-light strip,
// and a grid of small tiles. Pure drawing code over a GFX-like type G (same idea as mochi.h), so the host preview
// tool (tools/preview/cluster_preview.cpp) renders the exact same picture as the board.
// Canvas size: 172 x 320 (the whole panel, portrait).
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

namespace cluster {

constexpr int W = 172, H = 320;

inline uint16_t rgb(int r, int g, int b) {
  r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---- colours -------------------------------------------------------------------------------------------
const uint16_t BG = rgb(6, 16, 40), TILE = rgb(14, 36, 82), TILE_EDGE = rgb(30, 70, 140), OFFSEG = rgb(16, 34, 70);
const uint16_t WHITE = rgb(245, 248, 255), CYAN = rgb(40, 235, 205), AMBER = rgb(255, 170, 30), ORANGE = rgb(255, 110, 30);
const uint16_t RED = rgb(255, 60, 50), GREEN = rgb(60, 230, 90), YELLOW = rgb(255, 220, 50), LABEL = rgb(255, 200, 60), DIM = rgb(110, 150, 215);

// ---- tiny 5x7 uppercase font for labels ------------------------------------------------------------------
inline const uint8_t *font(char c) {
  static const uint8_t L[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x01,0x01,0x01,0x01,0x11,0x11,0x0E},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};
  static const uint8_t D[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
  static const uint8_t PCT[7] = {0x19,0x19,0x02,0x04,0x08,0x13,0x13}, DOT[7] = {0, 0, 0, 0, 0, 0x0C, 0x0C}, COL[7] = {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0}, DASH[7] = {0, 0, 0, 0x1F, 0, 0, 0}, DEG[7] = {0x06, 0x09, 0x09, 0x06, 0, 0, 0}, SL[7] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
  if (c >= 'A' && c <= 'Z') return L[c - 'A'];
  if (c >= '0' && c <= '9') return D[c - '0'];
  switch (c) { case '%': return PCT; case '.': return DOT; case ':': return COL; case '-': return DASH; case '*': return DEG; case '/': return SL; default: return nullptr; }
}

// Pixel text, left edge x, top y, scale sc.
template <class G> void text(G *g, const char *s, int x, int y, int sc, uint16_t col) {
  for (int i = 0; s[i]; i++) {
    const uint8_t *gl = font(s[i]);
    if (gl) for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (gl[r] & (0x10 >> c)) g->fillRect(x + i * 6 * sc + c * sc, y + r * sc, sc, sc, col);
  }
}
inline int textW(const char *s, int sc) { int n = (int)strlen(s); return n ? n * 6 * sc - sc : 0; }
template <class G> void textC(G *g, const char *s, int cx, int y, int sc, uint16_t col) { text(g, s, cx - textW(s, sc) / 2, y, sc, col); }

// ---- big segment digits ---------------------------------------------------------------------------------
// Seven rounded bars per digit. w x h is the digit box, t the bar thickness.
template <class G> void segDigit(G *g, int x, int y, int w, int h, int t, char ch, uint16_t on, uint16_t off, bool showOff) {
  static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   // bits: a b c d e f g
  uint8_t m = 0;
  if (ch >= '0' && ch <= '9') m = SEG[ch - '0']; else if (ch == '-') m = 0x40;
  int hh = h / 2, r = t / 2;
  struct S { int x, y, w, h; } s[7] = {
    {x + t, y, w - 2 * t, t},                         // a top
    {x + w - t, y + t, t, hh - t - 1},                // b upper right
    {x + w - t, y + hh + 1, t, hh - t - 1},           // c lower right
    {x + t, y + h - t, w - 2 * t, t},                 // d bottom
    {x, y + hh + 1, t, hh - t - 1},                   // e lower left
    {x, y + t, t, hh - t - 1},                        // f upper left
    {x + t, y + hh - t / 2, w - 2 * t, t}};           // g middle
  for (int i = 0; i < 7; i++) {
    bool lit = (m >> i) & 1;
    if (lit || showOff) g->fillRoundRect(s[i].x, s[i].y, s[i].w, s[i].h, r, lit ? on : off);
  }
}
// Right aligned number made of `digits` boxes; leading zeros are drawn dim so the number keeps its place.
template <class G> void segNumber(G *g, int xRight, int y, int w, int h, int t, int gap, int value, int digits, uint16_t on, uint16_t off) {
  char buf[8]; snprintf(buf, sizeof buf, "%0*d", digits, value < 0 ? 0 : value);
  int len = (int)strlen(buf); if (len > digits) { memmove(buf, buf + len - digits, digits + 1); len = digits; }
  bool lead = true;
  for (int i = 0; i < len; i++) {
    if (buf[i] != '0' || i == len - 1) lead = false;
    int x = xRight - (len - i) * w - (len - 1 - i) * gap;
    if (lead) segDigit(g, x, y, w, h, t, '8', off, off, true); else segDigit(g, x, y, w, h, t, buf[i], on, off, false);
  }
}

// The gear letter: "N" is drawn as two bars and a slash; other gears are segment digits.
template <class G> void gearGlyph(G *g, int x, int y, int w, int h, int t, char ch, uint16_t col) {
  if (ch == 'N') {
    g->fillRoundRect(x, y, t, h, t / 2, col);
    g->fillRoundRect(x + w - t, y, t, h, t / 2, col);
    g->fillTriangle(x + t - 1, y, x + t + 4, y, x + w - t + 1, y + h, col);
    g->fillTriangle(x + t - 1, y, x + w - t + 1, y + h, x + w - t - 4, y + h, col);
  } else segDigit(g, x, y, w, h, t, ch, col, col, false);
}

// ---- gear estimate -----------------------------------------------------------------------------------------
// OBD has no gear value; it is estimated from RPM / speed. Ratios are rpm per mph for each gear (tune with a drive log).
struct GearTable { float rpmPerMph[6]; };
inline char estimateGear(int rpm, int mph, const GearTable &t) {
  if (mph < 2) return 'N';
  if (rpm < 500) return '-';
  float r = (float)rpm / (float)mph, best = 1e9f; int bi = -1;
  for (int i = 0; i < 6; i++) { float e = fabsf(logf(r / t.rpmPerMph[i])); if (e < best) { best = e; bi = i; } }
  return best < 0.16f ? (char)('1' + bi) : '-';   // within about 16 % of a known ratio, else slipping / between gears
}

// ---- the page -----------------------------------------------------------------------------------------------
struct Data {
  int rpm = 0, mph = 0;
  bool haveCool = false, haveIntake = false, haveFuel = false, haveBatt = false, haveLoad = false;
  int coolF = 0, intakeF = 0, fuelPct = 0, loadPct = 0;
  float battV = 0;
  int runSec = 0;
  bool live = false;
  int redline = 6500;
  int rate = 0;          // requests per second (status line)
  bool tempF = true;
};

template <class G> void polyArc(G *g, int cx, int cy, float r0, float r1, float a0, float a1, uint16_t col) {
  // thin quad strip between angles a0..a1 (radians, 0 = right, positive = clockwise on screen)
  int n = (int)(fabsf(a1 - a0) / 0.012f) + 1;
  for (int i = 0; i < n; i++) {
    float u0 = a0 + (a1 - a0) * i / n, u1 = a0 + (a1 - a0) * (i + 1) / n + 0.004f * ((a1 > a0) ? 1 : -1);
    int x0 = cx + (int)(cosf(u0) * r0), y0 = cy + (int)(sinf(u0) * r0), x1 = cx + (int)(cosf(u0) * r1), y1 = cy + (int)(sinf(u0) * r1);
    int x2 = cx + (int)(cosf(u1) * r1), y2 = cy + (int)(sinf(u1) * r1), x3 = cx + (int)(cosf(u1) * r0), y3 = cy + (int)(sinf(u1) * r0);
    g->fillTriangle(x0, y0, x1, y1, x2, y2, col); g->fillTriangle(x0, y0, x2, y2, x3, y3, col);
  }
}

template <class G> void tile(G *g, int x, int y, int w, int h, const char *label, const char *value, uint16_t valCol) {
  g->fillRoundRect(x, y, w, h, 6, TILE_EDGE);
  g->fillRoundRect(x + 1, y + 1, w - 2, h - 2, 5, TILE);
  int sc = strlen(value) <= 3 ? 3 : 2;
  textC(g, value, x + w / 2, y + 12 + (3 - sc) * 3, sc, valCol);
  textC(g, label, x + w / 2, y + h - 12, 1, LABEL);
}

template <class G> void draw(G *g, const Data &d, uint32_t now) {
  static const GearTable gt = {{167, 95, 68, 47, 33, 28}};   // estimated, see estimateGear
  g->fillRect(0, 0, W, H, BG);

  // --- shift lights: 12 segments, green -> yellow -> red, lit from the left as revs approach the redline
  float frac = d.rpm / (float)d.redline; if (frac < 0) frac = 0; if (frac > 1.05f) frac = 1.05f;
  int lit = (int)(frac * 12 + (d.rpm > 1200 ? 0.5f : 0)); if (frac < 0.5f) lit = 0; else lit = (int)((frac - 0.5f) / 0.5f * 12 + 0.999f);
  bool blink = frac >= 1.0f && ((now / 90) & 1);
  for (int i = 0; i < 12; i++) {
    uint16_t on = i < 5 ? GREEN : (i < 9 ? YELLOW : RED);
    bool l = (i < lit) && !blink;
    g->fillRoundRect(6 + i * 13, 6, 11, 7, 2, l ? on : OFFSEG);
  }

  // --- gear (left) and speed (right)
  char gear = estimateGear(d.rpm, d.mph, gt);
  gearGlyph(g, 6, 24, 50, 78, 12, gear, CYAN);
  segNumber(g, 166, 24, 30, 54, 7, 4, d.mph, 3, WHITE, OFFSEG);
  textC(g, "MPH", 126, 86, 1, LABEL);   // under the speed digits
  text(g, "GEAR", 8, 106, 1, LABEL);

  // --- RPM number + label
  segNumber(g, 166, 110, 22, 36, 5, 3, d.rpm, 4, WHITE, OFFSEG);
  text(g, "RPM", 8, 130, 2, LABEL);

  // --- curved RPM sweep bar: arc across the width with tick numbers 1..7 (x1000)
  const int acx = 86, acy = 308; const float R1 = 146, R0 = 134;
  const float amin = -1.5708f - 0.58f, amax = -1.5708f + 0.58f;
  polyArc(g, acx, acy, R0, R1, amin, amax, OFFSEG);
  float fill = d.rpm / 7000.0f; if (fill > 1) fill = 1;
  float aFill = amin + (amax - amin) * fill;
  float aRed = amin + (amax - amin) * (d.redline / 7000.0f);
  if (aFill > amin) {
    float aOr = aFill < aRed ? aFill : aRed;
    polyArc(g, acx, acy, R0, R1, amin, aOr, ORANGE);
    if (aFill > aRed) polyArc(g, acx, acy, R0, R1, aRed, aFill, RED);
  }
  for (int k = 0; k <= 7; k++) {
    float a = amin + (amax - amin) * (k / 7.0f);
    uint16_t c = k * 1000 >= d.redline - 250 ? RED : DIM;
    int x0 = acx + (int)(cosf(a) * (R1 + 1)), y0 = acy + (int)(sinf(a) * (R1 + 1)), x1 = acx + (int)(cosf(a) * (R1 + 5)), y1 = acy + (int)(sinf(a) * (R1 + 5));
    g->drawLine(x0, y0, x1, y1, c);
    char n[2] = {(char)('0' + k), 0};
    if (k >= 1) textC(g, n, acx + (int)(cosf(a) * (R1 + 11)), acy + (int)(sinf(a) * (R1 + 11)) - 3, 1, c);
  }

  // --- tiles: 3 x 2
  char v[12]; const int tw = 52, th = 48, gx = 4, x0 = 4, y0 = 200, y1 = y0 + th + 4;
  auto cell = [&](int col, int row, const char *label, bool have, const char *val, uint16_t c) { tile(g, x0 + col * (tw + gx), row ? y1 : y0, tw, th, label, have ? val : "--", have ? c : DIM); };
  snprintf(v, sizeof v, "%d", d.coolF);   cell(0, 0, "COOL", d.haveCool, v, d.coolF >= 226 ? RED : (d.coolF >= 212 ? YELLOW : WHITE));
  snprintf(v, sizeof v, "%d", d.intakeF); cell(1, 0, "INTAKE", d.haveIntake, v, WHITE);
  snprintf(v, sizeof v, "%d", d.fuelPct); cell(2, 0, "FUEL %", d.haveFuel, v, d.fuelPct < 15 ? YELLOW : WHITE);
  snprintf(v, sizeof v, "%.1f", d.battV); cell(0, 1, "BATT V", d.haveBatt, v, d.battV < 12.0f ? YELLOW : GREEN);
  snprintf(v, sizeof v, "%d", d.loadPct); cell(1, 1, "LOAD %", d.haveLoad, v, WHITE);
  snprintf(v, sizeof v, "%d:%02d", d.runSec / 60 % 100, d.runSec % 60); cell(2, 1, "RUN", true, v, WHITE);

  // --- status line
  g->fillCircle(10, 311, 3, d.live ? GREEN : RED);
  text(g, d.live ? "LIVE" : "NO DATA", 18, 307, 1, d.live ? GREEN : RED);
  char st[20]; snprintf(st, sizeof st, "%s %dR/S", d.tempF ? "MPH F" : "KMH C", d.rate);
  text(g, st, 166 - textW(st, 1), 307, 1, DIM);
}

}  // namespace cluster
