#pragma once
// "Rally cluster" page, styled after the digital rally dash the user picked (screen 1 of their video):
// big mint gear letter, warning lamps, speed + RPM in heavy digits, shift lights, a curved RPM sweep that rises
// from the lower left, three wide tiles, six small tiles and a LIVE footer.
// Landscape canvas 320 x 172 (the panel is turned sideways on this page). Pure drawing code over a GFX-like type G
// (same idea as mochi.h), so tools/preview/cluster_preview.cpp renders the exact same picture as the board.
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

namespace cluster {

constexpr int W = 320, H = 172;

inline uint16_t rgb(int r, int g, int b) {
  r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---- colours (blue LCD look of the reference) ---------------------------------------------------------------
const uint16_t WHITE = rgb(248, 250, 255), MINT = rgb(120, 240, 200), YELLOW = rgb(255, 214, 40), ORANGE = rgb(255, 106, 40),
               RED = rgb(255, 70, 60), GREEN = rgb(70, 230, 100), DIMTXT = rgb(120, 168, 228), LAMP_OFF = rgb(98, 146, 214),
               TRACK = rgb(14, 36, 88), TRACK_RED = rgb(120, 44, 80), TILE = rgb(18, 48, 112), PLATE = rgb(24, 66, 140), AMBERLAMP = rgb(255, 180, 40);

// ---- 5x7 label font -----------------------------------------------------------------------------------------
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
  static const uint8_t PCT[7] = {0x19,0x19,0x02,0x04,0x08,0x13,0x13}, DOT[7] = {0,0,0,0,0,0x0C,0x0C}, COL[7] = {0,0x0C,0x0C,0,0x0C,0x0C,0}, DASH[7] = {0,0,0,0x1F,0,0,0}, SL[7] = {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, MID[7] = {0,0,0x04,0x0E,0x04,0,0};
  if (c >= 'A' && c <= 'Z') return L[c - 'A'];
  if (c >= '0' && c <= '9') return D[c - '0'];
  switch (c) { case '%': return PCT; case '.': return DOT; case ':': return COL; case '-': return DASH; case '/': return SL; case '+': return MID; default: return nullptr; }
}
template <class G> void text(G *g, const char *s, int x, int y, int sc, uint16_t col) {
  for (int i = 0; s[i]; i++) {
    const uint8_t *gl = font(s[i]);
    if (gl) for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (gl[r] & (0x10 >> c)) g->fillRect(x + i * 6 * sc + c * sc, y + r * sc, sc, sc, col);
  }
}
inline int textW(const char *s, int sc) { int n = (int)strlen(s); return n ? n * 6 * sc - sc : 0; }
template <class G> void textC(G *g, const char *s, int cx, int y, int sc, uint16_t col) { text(g, s, cx - textW(s, sc) / 2, y, sc, col); }
template <class G> void textR(G *g, const char *s, int xr, int y, int sc, uint16_t col) { text(g, s, xr - textW(s, sc), y, sc, col); }

// ---- heavy stroke digits ------------------------------------------------------------------------------------
// Each glyph is polylines on a 0..1 grid (x right, y down); 9 ends a stroke, 99 ends the glyph. Drawn with round pens.
inline const float *strokes(char c) {
  static const float G0[] = {0.25f,0, 0.75f,0, 1,0.2f, 1,0.8f, 0.75f,1, 0.25f,1, 0,0.8f, 0,0.2f, 0.25f,0, 99};
  static const float G1[] = {0.1f,0.25f, 0.6f,0, 0.6f,1, 9, 0.2f,1, 1,1, 99};
  static const float G2[] = {0,0.22f, 0.25f,0, 0.75f,0, 1,0.22f, 1,0.42f, 0,1, 1,1, 99};
  static const float G3[] = {0,0.12f, 0.25f,0, 0.75f,0, 1,0.15f, 1,0.35f, 0.75f,0.5f, 0.3f,0.5f, 9, 0.75f,0.5f, 1,0.65f, 1,0.85f, 0.75f,1, 0.25f,1, 0,0.88f, 99};
  static const float G4[] = {0.72f,1, 0.72f,0, 0,0.7f, 1,0.7f, 99};
  static const float G5[] = {1,0, 0.08f,0, 0,0.5f, 0.7f,0.44f, 1,0.6f, 1,0.85f, 0.75f,1, 0.25f,1, 0,0.88f, 99};
  static const float G6[] = {0.92f,0.05f, 0.3f,0, 0,0.25f, 0,0.78f, 0.25f,1, 0.75f,1, 1,0.78f, 1,0.62f, 0.75f,0.46f, 0.25f,0.46f, 0,0.62f, 99};
  static const float G7[] = {0,0, 1,0, 0.35f,1, 99};
  static const float G8[] = {0.25f,0, 0.75f,0, 0.95f,0.12f, 0.95f,0.38f, 0.75f,0.5f, 0.25f,0.5f, 0.05f,0.38f, 0.05f,0.12f, 0.25f,0, 9,
                             0.25f,0.5f, 0.75f,0.5f, 1,0.62f, 1,0.88f, 0.75f,1, 0.25f,1, 0,0.88f, 0,0.62f, 0.25f,0.5f, 99};
  static const float G9[] = {0.08f,0.95f, 0.7f,1, 1,0.75f, 1,0.22f, 0.75f,0, 0.25f,0, 0,0.22f, 0,0.38f, 0.25f,0.54f, 0.75f,0.54f, 1,0.38f, 99};
  static const float GN[] = {0,1, 0,0, 1,1, 1,0, 99};
  static const float GD[] = {0,0.5f, 1,0.5f, 99};
  switch (c) { case '0': return G0; case '1': return G1; case '2': return G2; case '3': return G3; case '4': return G4; case '5': return G5;
               case '6': return G6; case '7': return G7; case '8': return G8; case '9': return G9; case 'N': return GN; case '-': return GD; default: return nullptr; }
}
template <class G> void pen(G *g, int x0, int y0, int x1, int y1, int t, uint16_t col) {
  int dx = x1 - x0, dy = y1 - y0, n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)); int step = t > 3 ? t / 3 : 1; n = n / step + 1;
  int r = t / 2;
  for (int i = 0; i <= n; i++) {
    int x = x0 + dx * i / n, y = y0 + dy * i / n;
    if (r >= 1) g->fillCircle(x, y, r, col); else g->fillRect(x, y, t, t, col);
  }
}
// One glyph in the box (x, y, w, h) with pen thickness t.
template <class G> void glyph(G *g, char c, int x, int y, int w, int h, int t, uint16_t col) {
  const float *s = strokes(c); if (!s) return;
  int px = 0, py = 0; bool have = false;
  int ix = x + t / 2, iy = y + t / 2, iw = w - t, ih = h - t;
  for (int i = 0; s[i] != 99; i++) {
    if (s[i] == 9) { have = false; continue; }
    int cx = ix + (int)(s[i] * iw), cy = iy + (int)(s[i + 1] * ih); i++;
    if (have) pen(g, px, py, cx, cy, t, col);
    px = cx; py = cy; have = true;
  }
}
// Right aligned heavy number; width of one digit w, gap between digits.
template <class G> void number(G *g, const char *s, int xr, int y, int w, int h, int t, int gap, uint16_t col) {
  int n = (int)strlen(s);
  for (int i = 0; i < n; i++) {
    int x = xr - (n - i) * w - (n - 1 - i) * gap;
    if (s[i] == '.') { g->fillCircle(x + w / 2 - gap / 2, y + h - t / 2, t / 2 + 1, col); continue; }
    if (s[i] == ':') { g->fillCircle(x + w / 2, y + h / 3, t / 2, col); g->fillCircle(x + w / 2, y + 2 * h / 3, t / 2, col); continue; }
    glyph(g, s[i], x, y, w, h, t, col);
  }
}
inline int numberW(const char *s, int w, int gap) { int n = (int)strlen(s); return n * w + (n - 1) * gap; }

// ---- gear estimate -----------------------------------------------------------------------------------------
// OBD has no gear value; it is estimated from RPM / speed. Ratios are rpm per mph per gear (tune from a drive log).
struct GearTable { float rpmPerMph[6]; };
inline char estimateGear(int rpm, int mph, const GearTable &t) {
  if (mph < 2) return 'N';
  if (rpm < 500) return '-';
  float r = (float)rpm / (float)mph, best = 1e9f; int bi = -1;
  for (int i = 0; i < 6; i++) { float e = fabsf(logf(r / t.rpmPerMph[i])); if (e < best) { best = e; bi = i; } }
  return best < 0.16f ? (char)('1' + bi) : '-';   // within about 16 % of a known ratio, else slipping / between gears
}

// ---- data + page --------------------------------------------------------------------------------------------
struct Data {
  int rpm = 0, mph = 0;
  bool haveCool = false, haveIntake = false, haveAmb = false, haveFuel = false, haveBatt = false, haveLoad = false, haveThr = false;
  int coolF = 0, intakeF = 0, ambF = 0, fuelPct = 0, loadPct = 0, thrPct = 0;
  float battV = 0;
  int runSec = 0;
  bool live = false;
  int redline = 6500;
  int rate = 0;          // requests per second (footer)
  bool tempF = true;
  int hotF = 226;        // coolant warning
};

// Thick arc strip between two radii and two angles (radians, y down, increasing angle = clockwise).
template <class G> void arcBand(G *g, int cx, int cy, float r0, float r1, float a0, float a1, uint16_t col) {
  int n = (int)(fabsf(a1 - a0) / 0.03f) + 1;
  for (int i = 0; i < n; i++) {
    float u0 = a0 + (a1 - a0) * i / n, u1 = a0 + (a1 - a0) * (i + 1) / n + 0.01f;
    int x0 = cx + (int)lroundf(cosf(u0) * r0), y0 = cy + (int)lroundf(sinf(u0) * r0), x1 = cx + (int)lroundf(cosf(u0) * r1), y1 = cy + (int)lroundf(sinf(u0) * r1);
    int x2 = cx + (int)lroundf(cosf(u1) * r1), y2 = cy + (int)lroundf(sinf(u1) * r1), x3 = cx + (int)lroundf(cosf(u1) * r0), y3 = cy + (int)lroundf(sinf(u1) * r0);
    g->fillTriangle(x0, y0, x1, y1, x2, y2, col); g->fillTriangle(x0, y0, x2, y2, x3, y3, col);
  }
}

template <class G> void smallTile(G *g, int x, int y, int w, int h, const char *label, const char *val, uint16_t col, int sc) {
  g->fillRoundRect(x, y, w, h, 4, TILE);
  textR(g, val, x + w - 5, y + (h - 7 * sc) / 2, sc, col);
  textC(g, label, x + w / 2, y + h + 2, 1, DIMTXT);
}

template <class G> void draw(G *g, const Data &d, uint32_t now) {
  static const GearTable gt = {{167, 95, 68, 47, 33, 28}};
  // background: vertical blue gradient
  for (int y = 0; y < H; y += 4) {
    int k = y * 40 / H;
    g->fillRect(0, y, W, 4, rgb(46 - k, 110 - k, 196 - k));
  }

  // --- gear letter on a darker plate (left)
  char gear = estimateGear(d.rpm, d.mph, gt);
  g->fillRoundRect(2, 6, 70, 68, 10, rgb(34, 90, 170));
  glyph(g, gear, 12, 14, 50, 52, 13, MINT);

  // --- warning lamps (dim until something is wrong): CEL OIL / CLT GAS / BAT AFR
  bool hot = d.haveCool && d.coolF >= d.hotF, lowBatt = d.haveBatt && d.battV < (d.rpm > 600 ? 13.2f : 12.0f), lowFuel = d.haveFuel && d.fuelPct < 15;
  struct Lamp { const char *n; int x, y; bool on; uint16_t c; } lamps[6] = {
    {"CEL", 80, 12, false, AMBERLAMP}, {"OIL", 108, 12, false, RED}, {"CLT", 80, 30, hot, RED}, {"GAS", 108, 30, lowFuel, AMBERLAMP}, {"BAT", 80, 48, lowBatt, AMBERLAMP}, {"AFR", 108, 48, false, AMBERLAMP}};
  for (auto &l : lamps) text(g, l.n, l.x, l.y, 1, l.on ? l.c : LAMP_OFF);

  // --- shift lights across the top right: green -> yellow -> red -> yellow -> green, lit from both ends toward the middle
  float frac = d.rpm / (float)d.redline; if (frac < 0) frac = 0; if (frac > 1.05f) frac = 1.05f;
  int lit = frac < 0.55f ? 0 : (int)((frac - 0.55f) / 0.45f * 5 + 0.999f); if (lit > 5) lit = 5;
  bool flash = frac >= 1.0f && ((now / 90) & 1);
  for (int i = 0; i < 10; i++) {
    int k = i < 5 ? i : 9 - i;                       // 0..4 from the outside in
    uint16_t on = k < 2 ? GREEN : (k < 4 ? YELLOW : RED);
    bool l = (k < lit) || flash;
    g->fillRoundRect(142 + i * 17, 4, 14, 5, 2, l ? on : rgb(40, 96, 170));
  }

  // --- speed and RPM (heavy digits), unit labels in yellow
  char b[12];
  snprintf(b, sizeof b, "%d", d.mph);  number(g, b, 190, 14, 17, 30, 5, 4, WHITE);
  snprintf(b, sizeof b, "%d", d.rpm);  number(g, b, 314, 14, 17, 30, 5, 4, WHITE);
  textC(g, d.tempF ? "MPH" : "KM/H", 190 - 20, 50, 1, YELLOW);
  textR(g, "RPM", 314, 50, 1, YELLOW);

  // --- curved RPM sweep: starts at the lower left, curves up, runs flat to the right edge
  const int R = 46, bandH = 24, ytop = 76, xc = R, cy = ytop + R;
  const float PI2 = 1.5707963f, arcLen = PI2 * (R - bandH / 2), flatLen = (float)(W - xc), Ltot = arcLen + flatLen;
  float sfill = Ltot * (d.rpm > 7000 ? 7000 : d.rpm) / 7000.0f, sred = Ltot * d.redline / 7000.0f;
  auto strip = [&](float s0, float s1, uint16_t col) {          // paints the band between path positions s0..s1
    if (s1 <= s0) return;
    if (s0 < arcLen) { float e = s1 < arcLen ? s1 : arcLen; arcBand(g, xc, cy, R - bandH, R, 3.1415927f + s0 / arcLen * PI2, 3.1415927f + e / arcLen * PI2, col); }
    if (s1 > arcLen) { float a = s0 > arcLen ? s0 : arcLen; g->fillRect(xc + (int)(a - arcLen), ytop, (int)(s1 - a) + 1, bandH, col); }
  };
  strip(0, sred, TRACK);
  strip(sred, Ltot, TRACK_RED);
  strip(0, sfill < sred ? sfill : sred, WHITE);
  strip(sred, sfill, RED);
  arcBand(g, xc, cy, R - 2, R, 3.1415927f, 3.1415927f + PI2, ORANGE);              // orange top edge
  g->fillRect(xc, ytop, W - xc, 2, ORANGE);
  for (int k = 1; k <= 7; k++) {                                   // tick numbers under the band
    float s = Ltot * k / 7.35f; uint16_t c = k * 1000 >= d.redline - 500 ? ORANGE : DIMTXT; char n[2] = {(char)('0' + k), 0};
    if (s >= arcLen) { int x = xc + (int)(s - arcLen); g->fillRect(x - 1, ytop + bandH + 1, 2, 3, c); textC(g, n, x, ytop + bandH + 5, 1, c); }
    else { float a = 3.1415927f + s / arcLen * PI2; int x = xc + (int)(cosf(a) * (R - bandH - 7)), y = cy + (int)(sinf(a) * (R - bandH - 7)); textC(g, n, x, y - 3, 1, c); }
  }

  // --- three wide tiles (right of the curve): LOAD %, THROTTLE %, RUN TIME
  { const int ty = 113, th = 16, tw = 82, tx0 = 58, gp = 6; char v[12];
    snprintf(v, sizeof v, "%d", d.loadPct); smallTile(g, tx0, ty, tw, th, "LOAD %", d.haveLoad ? v : "--", WHITE, 2);
    snprintf(v, sizeof v, "%d", d.thrPct);  smallTile(g, tx0 + tw + gp, ty, tw, th, "THROTTLE %", d.haveThr ? v : "--", WHITE, 2);
    snprintf(v, sizeof v, "%d:%02d", d.runSec / 60 % 100, d.runSec % 60); smallTile(g, tx0 + 2 * (tw + gp), ty, tw, th, "RUN TIME", v, WHITE, 2); }

  // --- six small tiles: COOL, INTAKE, AMBIENT, FUEL %, BATT V, ECU
  { const int ty = 141, th = 13, tw = 48, tx0 = 6, gp = 4; char v[12];
    snprintf(v, sizeof v, "%d", d.coolF);   smallTile(g, tx0, ty, tw, th, "COOL", d.haveCool ? v : "--", hot ? RED : WHITE, 1);
    snprintf(v, sizeof v, "%d", d.intakeF); smallTile(g, tx0 + (tw + gp), ty, tw, th, "INTAKE", d.haveIntake ? v : "--", WHITE, 1);
    snprintf(v, sizeof v, "%d", d.ambF);    smallTile(g, tx0 + 2 * (tw + gp), ty, tw, th, "AMBIENT", d.haveAmb ? v : "--", WHITE, 1);
    snprintf(v, sizeof v, "%d", d.fuelPct); smallTile(g, tx0 + 3 * (tw + gp), ty, tw, th, "FUEL %", d.haveFuel ? v : "--", lowFuel ? YELLOW : WHITE, 1);
    snprintf(v, sizeof v, "%.1f", d.battV); smallTile(g, tx0 + 4 * (tw + gp), ty, tw, th, "BATT V", d.haveBatt ? v : "--", lowBatt ? YELLOW : WHITE, 1);
    g->fillRoundRect(tx0 + 5 * (tw + gp), ty, tw, th, 4, TILE);
    textC(g, d.live ? "OK" : "--", tx0 + 5 * (tw + gp) + tw / 2, ty + 3, 1, d.live ? GREEN : RED);
    textC(g, "ECU", tx0 + 5 * (tw + gp) + tw / 2, ty + th + 2, 1, DIMTXT); }

  // --- footer
  char st[24];
  g->fillCircle(10, 168, 2, d.live ? GREEN : RED);
  snprintf(st, sizeof st, "%s  %d REQ/S", d.live ? "LIVE" : "NO DATA", d.rate);
  text(g, st, 16, 165, 1, d.live ? WHITE : RED);
  textR(g, d.tempF ? "MPH  F" : "KM/H  C", 314, 165, 1, WHITE);
}

}  // namespace cluster
