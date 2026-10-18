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
#include "cluster_font.h"

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

// ---- anti-aliased text ---------------------------------------------------------------------------------------
// Glyph bitmaps (Krona One, see tools/fontgen) are 8-bit coverage masks blended straight into the canvas frame buffer.
struct Cls { const char *chars; const clusterfont::Glyph *g; const uint8_t *d; int h; };
const Cls GEARF = {clusterfont::GEAR_CHARS, clusterfont::GEAR_G, clusterfont::GEAR_D, clusterfont::GEAR_H};
const Cls BIGF = {clusterfont::BIG_CHARS, clusterfont::BIG_G, clusterfont::BIG_D, clusterfont::BIG_H};
const Cls MEDF = {clusterfont::MED_CHARS, clusterfont::MED_G, clusterfont::MED_D, clusterfont::MED_H};
const Cls SMLF = {clusterfont::SML_CHARS, clusterfont::SML_G, clusterfont::SML_D, clusterfont::SML_H};
const Cls LBLF = {clusterfont::LBL_CHARS, clusterfont::LBL_G, clusterfont::LBL_D, clusterfont::LBL_H};

inline uint16_t mix(uint16_t bg, uint16_t fg, int a) {   // a = 0..255 coverage of fg over bg
  int br = bg >> 11, bgc = (bg >> 5) & 63, bb = bg & 31, fr = fg >> 11, fgc = (fg >> 5) & 63, fb = fg & 31;
  return (uint16_t)(((br + (fr - br) * a / 255) << 11) | ((bgc + (fgc - bgc) * a / 255) << 5) | (bb + (fb - bb) * a / 255));
}
// Software raster target covering rows y0 .. y0+h-1 of the 320 x 172 picture (so the page can be drawn in strips and needs
// only a 320 x h buffer instead of a 110 KB full-frame canvas). All coordinates are picture coordinates.
struct Raster {
  uint16_t *fb; int h, y0;
  Raster(uint16_t *buf, int rows, int top) : fb(buf), h(rows), y0(top) {}
  bool rows(int ya, int yb) const { return yb >= y0 && ya < y0 + h; }          // does the span ya..yb touch this strip?
  void px(int x, int y, uint16_t c) { y -= y0; if (x >= 0 && x < W && y >= 0 && y < h) fb[y * W + x] = c; }
  void blend(int x, int y, uint16_t c, int a) {
    y -= y0; if (a <= 0 || x < 0 || x >= W || y < 0 || y >= h) return;
    uint16_t &p = fb[y * W + x]; p = a >= 255 ? c : mix(p, c, a);
  }
  void fillRect(int x, int y, int w, int hh, uint16_t c) {
    int ya = y - y0, yb = ya + hh, xa = x < 0 ? 0 : x, xb = x + w > W ? W : x + w;
    if (ya < 0) ya = 0; if (yb > h) yb = h;
    for (int r = ya; r < yb; r++) for (int q = xa; q < xb; q++) fb[r * W + q] = c;
  }
  void fillRoundRect(int x, int y, int w, int hh, int r, uint16_t c) {
    if (r > w / 2) r = w / 2; if (r > hh / 2) r = hh / 2;
    for (int j = 0; j < hh; j++) {
      int yy = y + j; if (yy < y0 || yy >= y0 + h) continue;
      int inset = 0;
      if (j < r) { float dy = r - j - 0.5f; inset = r - (int)sqrtf((float)r * r - dy * dy + 0.25f); }
      else if (j >= hh - r) { float dy = j - (hh - r) + 0.5f; inset = r - (int)sqrtf((float)r * r - dy * dy + 0.25f); }
      fillRect(x + inset, yy, w - 2 * inset, 1, c);
    }
  }
  void fillCircle(int cx, int cy, int r, uint16_t c) {
    for (int j = -r; j <= r; j++) { int yy = cy + j; if (yy < y0 || yy >= y0 + h) continue; int dx = (int)sqrtf((float)(r * r - j * j)); fillRect(cx - dx, yy, 2 * dx + 1, 1, c); }
  }
};
template <class G> inline void blendPx(G *g, int x, int y, uint16_t col, int a) { g->blend(x, y, col, a); }
inline int glyphIndex(const Cls &c, char ch) { if (ch >= 'a' && ch <= 'z') ch -= 32; const char *p = strchr(c.chars, ch); return (p && ch) ? (int)(p - c.chars) : -1; }
inline int textW(const Cls &c, const char *s) { int w = 0, n = (int)strlen(s); for (int i = 0; i < n; i++) { int k = glyphIndex(c, s[i]); if (k >= 0) w += c.g[k].adv; } return w > 0 ? w - 1 : 0; }
template <class G> void text(G *g, const Cls &c, const char *s, int x, int y, uint16_t col) {
  for (int i = 0; s[i]; i++) {
    int k = glyphIndex(c, s[i]); if (k < 0) continue;
    const clusterfont::Glyph &gl = c.g[k];
    // digits are tabular (shared advance): centre the narrower ones inside it
    int ox = 0; if (s[i] >= '0' && s[i] <= '9' && gl.adv > gl.w + 1) ox = (gl.adv - 1 - gl.w) / 2;
    const uint8_t *d = c.d + gl.off;
    if (g->rows(y, y + c.h - 1)) for (int r = 0; r < c.h; r++) for (int q = 0; q < gl.w; q++) { int a = d[r * gl.w + q]; if (a) blendPx(g, x + ox + q, y + r, col, a); }
    x += gl.adv;
  }
}
template <class G> void textR(G *g, const Cls &c, const char *s, int xr, int y, uint16_t col) { text(g, c, s, xr - textW(c, s), y, col); }
template <class G> void textC(G *g, const Cls &c, const char *s, int cx, int y, uint16_t col) { text(g, c, s, cx - textW(c, s) / 2, y, col); }

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

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

template <class G> void smallTile(G *g, int x, int y, int w, int h, const char *label, const char *val, uint16_t col, const Cls &vf) {
  g->fillRoundRect(x, y, w, h, 4, TILE);
  textR(g, vf, val, x + w - 6, y + (h - vf.h) / 2, col);
  textC(g, LBLF, label, x + w / 2, y + h + 2, DIMTXT);
}

template <class G> void draw(G *g, const Data &d, uint32_t now) {
  static const GearTable gt = {{167, 95, 68, 47, 33, 28}};
  // background: vertical blue gradient
  for (int y = 0; y < H; y += 2) {
    int k = y * 44 / H;
    g->fillRect(0, y, W, 2, rgb(48 - k, 112 - k, 198 - k));
  }

  // --- gear letter on a darker plate (left)
  char gear = estimateGear(d.rpm, d.mph, gt); char gs[2] = {gear, 0};
  g->fillRoundRect(2, 6, 68, 66, 10, rgb(36, 92, 172));
  textC(g, GEARF, gs, 36, 14, MINT);

  // --- warning lamps (dim until something is wrong): CEL OIL / CLT GAS / BAT AFR
  bool hot = d.haveCool && d.coolF >= d.hotF, lowBatt = d.haveBatt && d.battV < (d.rpm > 600 ? 13.2f : 12.0f), lowFuel = d.haveFuel && d.fuelPct < 15;
  struct Lamp { const char *n; int x, y; bool on; uint16_t c; } lamps[6] = {
    {"CEL", 78, 14, false, AMBERLAMP}, {"OIL", 104, 14, false, RED}, {"CLT", 78, 32, hot, RED}, {"GAS", 104, 32, lowFuel, AMBERLAMP}, {"BAT", 78, 50, lowBatt, AMBERLAMP}, {"AFR", 104, 50, false, AMBERLAMP}};
  if (d.mph < 100) for (auto &l : lamps) text(g, LBLF, l.n, l.x, l.y, l.on ? l.c : LAMP_OFF);

  // --- shift lights across the top right: green -> yellow -> red -> yellow -> green, lit from both ends toward the middle
  float frac = d.rpm / (float)d.redline; if (frac < 0) frac = 0; if (frac > 1.05f) frac = 1.05f;
  int lit = frac < 0.55f ? 0 : (int)((frac - 0.55f) / 0.45f * 5 + 0.999f); if (lit > 5) lit = 5;
  bool flash = frac >= 1.0f && ((now / 90) & 1);
  for (int i = 0; i < 10; i++) {
    int k = i < 5 ? i : 9 - i;                       // 0..4 from the outside in
    uint16_t on = k < 2 ? GREEN : (k < 4 ? YELLOW : RED);
    bool l = (k < lit) || flash;
    g->fillRoundRect(142 + i * 17, 4, 14, 5, 2, l ? on : rgb(40, 98, 172));
  }

  // --- speed and RPM (heavy digits), unit labels in yellow
  char b[12];
  snprintf(b, sizeof b, "%d", d.mph);  textR(g, BIGF, b, 190, 16, WHITE);
  snprintf(b, sizeof b, "%d", d.rpm);  textR(g, BIGF, b, 314, 16, WHITE);
  textR(g, LBLF, d.tempF ? "MPH" : "KM/H", 190, 46, YELLOW);
  textR(g, LBLF, "RPM", 314, 46, YELLOW);

  // --- curved RPM sweep: starts at the lower left, curves up, runs flat to the right edge (anti-aliased)
  const int R = 46, bandH = 24, ytop = 76, xc = R, cy = ytop + R;
  const float PI2 = 1.5707963f, PI1 = 3.1415927f, arcLen = PI2 * (R - bandH / 2), flatLen = (float)(W - xc), Ltot = arcLen + flatLen;
  float sfill = Ltot * (d.rpm > 7000 ? 7000 : d.rpm) / 7000.0f, sred = Ltot * d.redline / 7000.0f;
  auto strip = [&](float s0, float s1, uint16_t col) {          // flat part of the band between path positions s0..s1
    float a = s0 > arcLen ? s0 : arcLen; if (s1 <= a) return;
    g->fillRect(xc + (int)(a - arcLen), ytop, (int)(s1 - a) + 1, bandH, col);
  };
  strip(0, sred, TRACK);
  strip(sred, Ltot, TRACK_RED);
  strip(0, sfill < sred ? sfill : sred, WHITE);
  strip(sred, sfill, RED);
  g->fillRect(xc, ytop, W - xc, 2, ORANGE);
  for (int y = ytop - 1; y <= cy; y++) if (g->rows(y, y)) for (int x = 0; x <= xc; x++) {   // the curved part, per pixel
    float dx = x + 0.5f - xc, dy = y + 0.5f - cy, r = sqrtf(dx * dx + dy * dy);
    if (dx > 0.5f || dy > 0.5f) continue;
    float cover = clamp01(r - (R - bandH) + 0.5f) * clamp01(R - r + 0.5f);
    if (cover <= 0) continue;
    float ang = atan2f(dy, dx) + 2 * PI1; if (ang > 2 * PI1) ang -= 2 * PI1;   // pi .. 3pi/2 on this quadrant
    float s = (ang - PI1) / PI2 * arcLen;
    blendPx(g, x, y, s < sfill ? WHITE : TRACK, (int)(cover * 255));
    float line = clamp01(r - (R - 2) + 0.5f) * clamp01(R + 0.5f - r);
    if (line > 0) blendPx(g, x, y, ORANGE, (int)(line * 255));
  }
  for (int k = 1; k <= 7; k++) {                                   // tick numbers under the band
    float sk = Ltot * k / 7.35f; uint16_t c = k * 1000 >= d.redline - 500 ? ORANGE : DIMTXT; char n[2] = {(char)('0' + k), 0};
    if (sk >= arcLen) { int x = xc + (int)(sk - arcLen); g->fillRect(x - 1, ytop + bandH + 2, 2, 3, c); textC(g, LBLF, n, x, ytop + bandH + 6, c); }
    else { float an = PI1 + sk / arcLen * PI2; int x = xc + (int)(cosf(an) * (R - bandH - 8)), y = cy + (int)(sinf(an) * (R - bandH - 8)); textC(g, LBLF, n, x, y - 3, c); }
  }

  // --- three wide tiles (right of the curve): LOAD %, THROTTLE %, RUN TIME
  { const int ty = 113, th = 15, tw = 82, tx0 = 58, gp = 6; char v[12];
    snprintf(v, sizeof v, "%d", d.loadPct); smallTile(g, tx0, ty, tw, th, "LOAD %", d.haveLoad ? v : "--", WHITE, MEDF);
    snprintf(v, sizeof v, "%d", d.thrPct);  smallTile(g, tx0 + tw + gp, ty, tw, th, "THROTTLE %", d.haveThr ? v : "--", WHITE, MEDF);
    snprintf(v, sizeof v, "%d:%02d", d.runSec / 60 % 100, d.runSec % 60); smallTile(g, tx0 + 2 * (tw + gp), ty, tw, th, "RUN TIME", v, WHITE, MEDF); }

  // --- six small tiles: COOL, INTAKE, AMBIENT, FUEL %, BATT V, ECU
  { const int ty = 139, th = 13, tw = 50, tx0 = 4, gp = 2; char v[12];
    snprintf(v, sizeof v, "%d", d.coolF);   smallTile(g, tx0, ty, tw, th, "COOL", d.haveCool ? v : "--", hot ? RED : WHITE, SMLF);
    snprintf(v, sizeof v, "%d", d.intakeF); smallTile(g, tx0 + (tw + gp), ty, tw, th, "INTK", d.haveIntake ? v : "--", WHITE, SMLF);
    snprintf(v, sizeof v, "%d", d.ambF);    smallTile(g, tx0 + 2 * (tw + gp), ty, tw, th, "AMB", d.haveAmb ? v : "--", WHITE, SMLF);
    snprintf(v, sizeof v, "%d", d.fuelPct); smallTile(g, tx0 + 3 * (tw + gp), ty, tw, th, "FUEL %", d.haveFuel ? v : "--", lowFuel ? YELLOW : WHITE, SMLF);
    snprintf(v, sizeof v, "%.1f", d.battV); smallTile(g, tx0 + 4 * (tw + gp), ty, tw, th, "BATT V", d.haveBatt ? v : "--", lowBatt ? YELLOW : WHITE, SMLF);
    smallTile(g, tx0 + 5 * (tw + gp), ty, tw, th, "ECU", d.live ? "OK" : "--", d.live ? GREEN : RED, SMLF); }

  // --- footer
  char st[24];
  g->fillCircle(9, 168, 2, d.live ? GREEN : RED);
  snprintf(st, sizeof st, "%s  %d REQ/S", d.live ? "LIVE" : "NO DATA", d.rate);
  text(g, LBLF, st, 15, 165, d.live ? WHITE : RED);
  textR(g, LBLF, d.tempF ? "MPH  F" : "KM/H  C", 314, 165, WHITE);
}

}  // namespace cluster
