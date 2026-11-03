#pragma once
// Round dial pages for the 480x480 round board: sport cluster, analog tachometer, analog speedometer, vitals rings.
// Pure frame-buffer code (no Arduino / GFX types) so tools/preview/round_ui_preview.cpp renders the very same pictures on a computer.
//
// How a frame is made: every page has a static layer (dial face, ticks, numerals, labels) rendered once into a background buffer
// in PSRAM and copied to the frame buffer on page entry. After that a frame only touches what changed: the lit segments of the
// rings (a precomputed pixel list, only the segments that switched are written), the needle's bounding box (restored from the
// background first), and the numbers (their rectangle is restored, then the text is drawn).
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "round_font.h"
#if defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

namespace rui {

constexpr int W = 480, H = 480, CX = 240, CY = 240;
enum Pg { PG_SPORT = 0, PG_TACH, PG_SPEEDO, PG_VITALS, PG_ARC, PG_CUSTOM, PG_QUAD, PG_DUAL, PG_GEAR, PG_COUNT };

inline uint16_t rgb(int r, int g, int b) {
  r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
const uint16_t BG = rgb(5, 7, 12), BEZEL = rgb(30, 37, 54), WHITE = rgb(240, 244, 255), DIM = rgb(118, 130, 160), FAINT = rgb(50, 58, 80),
               PANEL = rgb(22, 30, 50), PANEL_EDGE = rgb(52, 66, 98), CYAN = rgb(0, 200, 255), MINT = rgb(110, 240, 190),
               YELLOW = rgb(255, 208, 50), ORANGE = rgb(255, 120, 40), RED = rgb(255, 60, 56), GREEN = rgb(80, 230, 110);

inline uint16_t mix(uint16_t bg, uint16_t fg, int a) {   // a = 0..255 coverage of fg over bg
  int br = bg >> 11, bgc = (bg >> 5) & 63, bb = bg & 31, fr = fg >> 11, fgc = (fg >> 5) & 63, fb = fg & 31;
  return (uint16_t)(((br + (fr - br) * a / 255) << 11) | ((bgc + (fgc - bgc) * a / 255) << 5) | (bb + (fb - bb) * a / 255));
}
inline uint16_t lerpc(int r0, int g0, int b0, int r1, int g1, int b1, float t) {
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return rgb((int)(r0 + (r1 - r0) * t), (int)(g0 + (g1 - g0) * t), (int)(b0 + (b1 - b0) * t));
}
inline uint16_t dimmed(uint16_t c) {   // the unlit version of a segment colour
  return rgb((((c >> 11) << 3) * 40 >> 8) + 6, ((((c >> 5) & 63) << 2) * 40 >> 8) + 8, (((c & 31) << 3) * 40 >> 8) + 12);
}
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

inline void *bigAlloc(size_t n) {
#if defined(ARDUINO)
  return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
#else
  return malloc(n);
#endif
}

// ---- anti-aliased primitives on a W x H RGB565 buffer ------------------------------------------------------------
inline void blendPx(uint16_t *b, int x, int y, uint16_t c, int a) {
  if (a <= 0 || x < 0 || x >= W || y < 0 || y >= H) return;
  uint16_t &p = b[y * W + x]; p = a >= 255 ? c : mix(p, c, a);
}
// Calls f(x, y, r) for every pixel whose centre is within r0-1 .. r1+1 of the point (cx, cy).
template <class F> inline void annulus(float cx, float cy, float r0, float r1, F f) {
  int ya = (int)floorf(cy - r1 - 1), yb = (int)ceilf(cy + r1 + 1);
  for (int y = ya < 0 ? 0 : ya; y <= yb && y < H; y++) {
    float dy = y + 0.5f - cy, ro = r1 + 1, ri = r0 - 1;
    float ho2 = ro * ro - dy * dy; if (ho2 < 0) continue;
    float ho = sqrtf(ho2), hi = (ri > 0 && fabsf(dy) < ri) ? sqrtf(ri * ri - dy * dy) : 0;
    int xa = (int)floorf(cx - ho), xb = (int)ceilf(cx + ho), ia = (int)ceilf(cx - hi), ib = (int)floorf(cx + hi);
    for (int x = xa < 0 ? 0 : xa; x <= xb && x < W; x++) {
      if (hi > 0 && x >= ia && x <= ib) { x = ib; continue; }
      float dx = x + 0.5f - cx; f(x, y, sqrtf(dx * dx + dy * dy), dx, dy);
    }
  }
}
inline void discAA(uint16_t *b, float cx, float cy, float r, uint16_t c) {
  annulus(cx, cy, 0, r, [&](int x, int y, float d, float, float) { blendPx(b, x, y, c, (int)(clamp01(r - d + 0.5f) * 255)); });
}
inline void ringAA(uint16_t *b, float cx, float cy, float r0, float r1, uint16_t c) {
  annulus(cx, cy, r0, r1, [&](int x, int y, float d, float, float) {
    blendPx(b, x, y, c, (int)(clamp01(fminf(d - r0, r1 - d) + 0.5f) * 255)); });
}
// Arc band between two angles (degrees clockwise from 12 o'clock), butt ends.
inline void arcAA(uint16_t *b, float cx, float cy, float r0, float r1, float t0, float t1, uint16_t c) {
  annulus(cx, cy, r0, r1, [&](int x, int y, float d, float dx, float dy) {
    float th = atan2f(dx, -dy) * 57.29578f;
    float e = fminf(th - t0, t1 - th) * 0.0174533f * d;
    blendPx(b, x, y, c, (int)(clamp01(fminf(d - r0, r1 - d) + 0.5f) * clamp01(e + 0.5f) * 255)); });
}
// Straight band from (x0,y0) to (x1,y1), half-width hw0 at the start and hw1 at the end. Returns the touched box.
struct Box { int x0, y0, x1, y1; };
inline Box taper(uint16_t *b, float x0, float y0, float x1, float y1, float hw0, float hw1, uint16_t c, int16_t *rmin = nullptr, int16_t *rmax = nullptr) {
  float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy); if (L < 0.5f) return {0, 0, -1, -1};
  float ux = dx / L, uy = dy / L, m = (hw0 > hw1 ? hw0 : hw1) + 1.5f;
  int xa = (int)floorf(fminf(x0, x1) - m), xb = (int)ceilf(fmaxf(x0, x1) + m), ya = (int)floorf(fminf(y0, y1) - m), yb = (int)ceilf(fmaxf(y0, y1) + m);
  for (int y = ya < 0 ? 0 : ya; y <= yb && y < H; y++) {
    if (rmin) { rmin[y] = W; rmax[y] = -1; }
    for (int x = xa < 0 ? 0 : xa; x <= xb && x < W; x++) {
      float px = x + 0.5f - x0, py = y + 0.5f - y0, u = px * ux + py * uy, v = fabsf(-px * uy + py * ux);
      float hw = hw0 + (hw1 - hw0) * (u / L);
      float cov = clamp01(hw - v + 0.5f) * clamp01(u + 0.5f) * clamp01(L - u + 0.5f);
      if (cov > 0.02f) { blendPx(b, x, y, c, (int)(cov * 255)); if (rmin) { if (x < rmin[y]) rmin[y] = x; if (x > rmax[y]) rmax[y] = x; } }
    }
  }
  return {xa, ya, xb, yb};
}
inline void restore(uint16_t *fb, const uint16_t *bg, int x, int y, int w, int h) {
  if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; } if (x + w > W) w = W - x; if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int r = 0; r < h; r++) memcpy(fb + (y + r) * W + x, bg + (y + r) * W + x, (size_t)w * 2);
}

// ---- text (Krona One anti-aliased glyphs, see tools/fontgen/fontgen_round.py) --------------------------------------
struct Font { const char *chars; const roundfont::Glyph *g; const uint8_t *d; int h, gap; };
const Font F_HUGE = {roundfont::HUGE_CHARS, roundfont::HUGE_G, roundfont::HUGE_D, roundfont::HUGE_H, 3};
const Font F_BIG = {roundfont::BIG_CHARS, roundfont::BIG_G, roundfont::BIG_D, roundfont::BIG_H, 2};
const Font F_MED = {roundfont::MED_CHARS, roundfont::MED_G, roundfont::MED_D, roundfont::MED_H, 2};
const Font F_LBL = {roundfont::LBL_CHARS, roundfont::LBL_G, roundfont::LBL_D, roundfont::LBL_H, 1};
inline int glyphIndex(const Font &f, char ch) { if (ch >= 'a' && ch <= 'z') ch -= 32; const char *p = ch ? strchr(f.chars, ch) : nullptr; return p ? (int)(p - f.chars) : -1; }
inline int textW(const Font &f, const char *s) { int w = 0; for (int i = 0; s[i]; i++) { int k = glyphIndex(f, s[i]); if (k >= 0) w += f.g[k].adv; } return w > f.gap ? w - f.gap : w; }
inline void text(uint16_t *b, const Font &f, const char *s, int x, int y, uint16_t col) {
  for (int i = 0; s[i]; i++) {
    int k = glyphIndex(f, s[i]); if (k < 0) continue;
    const roundfont::Glyph &gl = f.g[k];
    int ox = (s[i] >= '0' && s[i] <= '9' && gl.adv - f.gap > gl.w) ? (gl.adv - f.gap - gl.w) / 2 : 0;   // digits are tabular: centre inside the cell
    const uint8_t *d = f.d + gl.off;
    for (int r = 0; r < f.h; r++) for (int q = 0; q < gl.w; q++) { int a = d[r * gl.w + q]; if (a) blendPx(b, x + ox + q, y + r, col, a); }
    x += gl.adv;
  }
}
inline void textC(uint16_t *b, const Font &f, const char *s, int cx, int y, uint16_t col) { text(b, f, s, cx - textW(f, s) / 2, y, col); }

// ---- data the pages show ------------------------------------------------------------------------------------------
enum Icon : uint8_t { IC_NONE = 0, IC_THERM, IC_AIR, IC_BATT, IC_FUEL, IC_LOAD, IC_BOOST, IC_THROTTLE };
struct Readout {                 // one value on the multi-value pages (custom, quad, dual)
  bool have = false; uint8_t icon = IC_NONE; char label[14] = ""; char text[12] = "--"; char unit[8] = ""; uint16_t col = 0xFFFF; float frac = 0;
};
struct Data {
  // multi-value pages (filled by the caller): the arc gauge, up to four readouts, the page heading and the estimated gear
  bool haveArc = false; float arcVal = 0, arcMin = -1, arcMax = 2, arcMajor = 0.5f, arcMinor = 0.1f, arcRed = 1.6f; int arcDec = 1;
  char arcText[12] = "--", arcUnit[8] = "", arcLabel[14] = "", title[14] = "";
  Readout rd[4];
  char gear = 'N';               // 'N' stopped, '1'..'6', '-' unknown
  float rpm = 0, mph = 0;
  bool haveCool = false, haveFuel = false, haveBatt = false, haveLoad = false;
  int coolC = 0, coolShown = 0, fuelPct = 0, loadPct = 0;
  float battV = 0;
  int redline = 6500, hotC = 100, worryC = 108, fuelLowPct = 15;
  float battCritV = 11.5f, battLowV = 12.4f;
  bool unitF = true;
  int dotN = 1, dotCur = 0;      // page dots: how many pages, which one is shown
};

// ---- ring segments: a precomputed pixel list per ring --------------------------------------------------------------
// entry bits: 0-17 pixel index, 18-24 segment, 25-26 ring, 27-31 coverage (0..31)
constexpr int MAX_RINGS = 4, MAX_SEG = 90, MAP_CAP = 36000;
struct Ring { float cx, cy, r0, r1; int nseg; float segDeg, gapDeg; uint16_t lit[MAX_SEG], dim[MAX_SEG]; int cur, prev; bool valid; };

struct Slot { char s[12]; uint16_t col; int16_t x0, x1; };   // x0..x1: where the text was drawn
struct SBox { int16_t x0, y0, x1, y1; };
struct State {
  bool ok = false; uint16_t *bg = nullptr; uint32_t *map = nullptr; int nmap = 0;
  Ring ring[MAX_RINGS]; int nring = 0;
  int page = -1, dotN = -1, dotCur = -1; bool unitF = true; int redline = 0;
  float rpm = 0, mph = 0; uint32_t lastT = 0;
  Slot slot[10];
  Box needle = {0, 0, -1, -1}; float needleAng = 1e9f; bool forceRings = true;
  int16_t dmin[H], dmax[H];                 // per row: the span of pixels changed this frame (dmax < dmin: row is clean)
  int16_t nmin[H], nmax[H]; int nr0 = 0, nr1 = -1;   // per row: where the needle was drawn last frame
  SBox sbox[MAX_RINGS][MAX_SEG];            // bounding box of every ring segment
  int16_t barPx[2] = {-1, -1}; uint16_t barCol[2] = {0, 0};   // dual page: fill width and colour last drawn
};
inline State &st() { static State s; return s; }

// ---- dirty tracking: only the rows / spans that changed are sent to the panel ----
inline void mark(State &S, int x0, int y0, int x1, int y1) {
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= W) x1 = W - 1; if (y1 >= H) y1 = H - 1;
  for (int y = y0; y <= y1; y++) { if (x0 < S.dmin[y]) S.dmin[y] = x0; if (x1 > S.dmax[y]) S.dmax[y] = x1; }
}
inline void clearDirty(State &S) { for (int y = 0; y < H; y++) { S.dmin[y] = W; S.dmax[y] = -1; } }
// Calls cb(x, y, width) for every changed span, then forgets them. Call once after frame().
template <class F> inline void flushDirty(F cb) {
  State &S = st();
  for (int y = 0; y < H; y++) if (S.dmax[y] >= S.dmin[y]) cb((int)S.dmin[y], y, S.dmax[y] - S.dmin[y] + 1);
  clearDirty(S);
}

inline bool begin() {
  State &S = st(); if (S.ok) return true;
  S.bg = (uint16_t *)bigAlloc((size_t)W * H * 2);
  S.map = (uint32_t *)bigAlloc((size_t)MAP_CAP * 4);
  S.ok = S.bg && S.map;
  clearDirty(S);
  return S.ok;
}

inline void buildRing(State &S, int id, float cx, float cy, float r0, float r1, int nseg, float gapDeg) {
  for (int g = 0; g < MAX_SEG; g++) S.sbox[id][g] = {W, H, -1, -1};
  Ring &R = S.ring[id]; R.cx = cx; R.cy = cy; R.r0 = r0; R.r1 = r1; R.nseg = nseg; R.segDeg = 270.0f / nseg; R.gapDeg = gapDeg; R.cur = 0; R.prev = -1; R.valid = true;
  annulus(cx, cy, r0, r1, [&](int x, int y, float d, float dx, float dy) {
    float s = atan2f(dx, -dy) * 57.29578f + 135.0f;               // degrees along the sweep, 0 at the lower left end
    if (s < 0 || s >= 270.0f) return;
    int seg = (int)(s / R.segDeg); if (seg >= nseg) return;
    float fr = s - seg * R.segDeg, e = fminf(fr - R.gapDeg * 0.5f, R.segDeg - R.gapDeg * 0.5f - fr);
    float cov = clamp01(e * 0.0174533f * d + 0.5f) * clamp01(fminf(d - r0, r1 - d) + 0.5f);
    int c5 = (int)(cov * 31 + 0.5f); if (c5 <= 0 || S.nmap >= MAP_CAP) return;
    S.map[S.nmap++] = (uint32_t)(y * W + x) | ((uint32_t)seg << 18) | ((uint32_t)id << 25) | ((uint32_t)c5 << 27);
    SBox &bx = S.sbox[id][seg]; if (x < bx.x0) bx.x0 = x; if (x > bx.x1) bx.x1 = x; if (y < bx.y0) bx.y0 = y; if (y > bx.y1) bx.y1 = y;
  });
}
inline void ringLevel(State &S, int id, float frac) { S.ring[id].cur = (int)(clamp01(frac) * S.ring[id].nseg + 0.5f); }
inline void drawRings(State &S, uint16_t *fb) {
  bool any = S.forceRings;
  for (int i = 0; i < S.nring; i++) if (S.ring[i].cur != S.ring[i].prev) any = true;
  if (!any) return;                                      // nothing moved: leave the ring pixels alone
  for (int i = 0; i < S.nmap; i++) {
    uint32_t e = S.map[i]; int ri = (e >> 25) & 3, seg = (e >> 18) & 127; Ring &R = S.ring[ri];
    bool now = seg < R.cur, was = seg < R.prev;
    if (!S.forceRings && now == was) continue;
    int c5 = e >> 27, a = (c5 << 3) | (c5 >> 2);
    fb[e & 0x3FFFF] = mix(BG, now ? R.lit[seg] : R.dim[seg], a);
  }
  for (int i = 0; i < S.nring; i++) {
    Ring &R = S.ring[i];
    if (!S.forceRings) {
      int lo = R.cur < R.prev ? R.cur : R.prev, hi = R.cur < R.prev ? R.prev : R.cur;
      for (int g = lo; g < hi && g < R.nseg; g++) { const SBox &bx = S.sbox[i][g]; if (bx.x1 >= bx.x0) mark(S, bx.x0, bx.y0, bx.x1, bx.y1); }
    }
    R.prev = R.cur;
  }
  S.forceRings = false;
}

// ---- static layer helpers ------------------------------------------------------------------------------------------
inline float angOf(float v, float vmax) { return -135.0f + 270.0f * v / vmax; }
inline void polar(float r, float deg, float *x, float *y) { float t = deg * 0.0174533f; *x = CX + r * sinf(t); *y = CY - r * cosf(t); }
inline void tick(uint16_t *b, float v, float vmax, float rIn, float rOut, float hw, uint16_t c) {
  float x0, y0, x1, y1; polar(rOut, angOf(v, vmax), &x0, &y0); polar(rIn, angOf(v, vmax), &x1, &y1); taper(b, x0, y0, x1, y1, hw, hw, c);
}
inline void numeral(uint16_t *b, float v, float vmax, float r, const char *s, uint16_t c) {
  float x, y; polar(r, angOf(v, vmax), &x, &y); textC(b, F_MED, s, (int)(x + 0.5f), (int)(y - F_MED.h / 2 + 0.5f), c);
}
inline void pageDots(uint16_t *b, int n, int cur) {
  int step = n > 9 ? 14 : 18;
  for (int i = 0; i < n; i++) discAA(b, CX + (i - (n - 1) / 2.0f) * step, 450, i == cur ? 5.0f : 3.5f, i == cur ? WHITE : FAINT);
}
inline void pill(uint16_t *b, int cx, int y, int w, int h, const char *label) {
  float r = 14; int x = cx - w / 2;
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    float px = i + 0.5f, py = j + 0.5f, qx = fabsf(px - w / 2.0f) - (w / 2.0f - r), qy = fabsf(py - h / 2.0f) - (h / 2.0f - r);
    float d = sqrtf((qx > 0 ? qx : 0) * (qx > 0 ? qx : 0) + (qy > 0 ? qy : 0) * (qy > 0 ? qy : 0)) + fminf(fmaxf(qx, qy), 0.0f) - r;   // signed distance to the rounded rect
    blendPx(b, x + i, y + j, PANEL, (int)(clamp01(0.5f - d) * 255));
    blendPx(b, x + i, y + j, PANEL_EDGE, (int)(clamp01(0.5f - fabsf(d + 1.0f)) * 255));
  }
  textC(b, F_LBL, label, cx, y + 6, DIM);
}
// the gradient of the RPM ring: cyan -> mint -> yellow -> orange, red from the redline up
inline uint16_t rpmColor(float v, int redline) {
  if (v >= redline) return RED;
  float f = v / redline;
  if (f < 0.55f) return lerpc(0, 190, 255, 110, 240, 190, f / 0.55f);
  if (f < 0.85f) return lerpc(110, 240, 190, 255, 208, 50, (f - 0.55f) / 0.30f);
  return lerpc(255, 208, 50, 255, 110, 40, (f - 0.85f) / 0.15f);
}
inline int rpmMax(int redline) { int m = ((redline + 1999) / 1000) * 1000; return m > 10000 ? 10000 : m; }   // first whole thousand at least 1000 above the redline

inline void setRingColors(Ring &R, uint16_t (*f)(float frac, const void *), const void *ctx) {
  for (int i = 0; i < R.nseg; i++) { R.lit[i] = f((i + 0.5f) / R.nseg, ctx); R.dim[i] = dimmed(R.lit[i]); }
}

// ---- small icons for the multi-value pages (drawn with the primitives above, about 24 px) --------------------------
inline void iconDraw(uint16_t *b, int type, float x, float y, uint16_t c) {
  switch (type) {
    case IC_THERM:
      taper(b, x, y - 10, x, y + 3, 2.6f, 2.6f, c); discAA(b, x, y - 10, 2.6f, c); discAA(b, x, y + 6.5f, 5.5f, c);
      taper(b, x + 7, y - 7, x + 11, y - 7, 1.1f, 1.1f, c); taper(b, x + 7, y - 2, x + 11, y - 2, 1.1f, 1.1f, c); break;
    case IC_AIR:
      for (int k = -1; k <= 1; k++) taper(b, x - 10, y + k * 6.5f, x + 7, y + k * 6.5f, 1.4f, 1.4f, c);
      taper(b, x + 8, y, x + 3, y - 5, 1.4f, 1.4f, c); taper(b, x + 8, y, x + 3, y + 5, 1.4f, 1.4f, c); break;
    case IC_BATT:
      taper(b, x - 10, y - 6, x + 10, y - 6, 1.3f, 1.3f, c); taper(b, x - 10, y + 8, x + 10, y + 8, 1.3f, 1.3f, c);
      taper(b, x - 10, y - 6, x - 10, y + 8, 1.3f, 1.3f, c); taper(b, x + 10, y - 6, x + 10, y + 8, 1.3f, 1.3f, c);
      taper(b, x - 6, y - 10, x - 6, y - 6, 2.2f, 2.2f, c); taper(b, x + 6, y - 10, x + 6, y - 6, 2.2f, 2.2f, c);
      taper(b, x - 5, y + 1, x + 5, y + 1, 1.2f, 1.2f, c); break;
    case IC_FUEL:
      taper(b, x - 8, y - 10, x + 3, y - 10, 1.3f, 1.3f, c); taper(b, x - 8, y + 10, x + 3, y + 10, 1.3f, 1.3f, c);
      taper(b, x - 8, y - 10, x - 8, y + 10, 1.3f, 1.3f, c); taper(b, x + 3, y - 10, x + 3, y + 10, 1.3f, 1.3f, c);
      taper(b, x - 8, y - 3, x + 3, y - 3, 1.2f, 1.2f, c);
      taper(b, x + 3, y - 5, x + 9, y - 5, 1.3f, 1.3f, c); taper(b, x + 9, y - 5, x + 9, y + 4, 1.3f, 1.3f, c); break;
    case IC_LOAD:
      ringAA(b, x, y, 8, 10.5f, c); taper(b, x, y + 1, x + 5.5f, y - 5, 1.6f, 0.8f, c); discAA(b, x, y + 1, 2.4f, c); break;
    case IC_BOOST:
      ringAA(b, x, y, 6, 10, c);
      for (int k = 0; k < 3; k++) { float a = k * 2.0944f; taper(b, x, y, x + 7 * sinf(a), y - 7 * cosf(a), 2.0f, 0.8f, c); }
      discAA(b, x, y, 2.4f, c); break;
    case IC_THROTTLE:
      taper(b, x - 5, y + 10, x + 5, y - 9, 4.2f, 3.2f, c); taper(b, x - 9, y + 11, x + 3, y + 11, 1.2f, 1.2f, c); break;
    default: break;
  }
}

// static layer of the multi-value pages: arc, custom, quad, dual, gear
inline void buildStyle(State &S, int pg, const Data &d) {
  uint16_t *b = S.bg; char t[16];
  if (pg == PG_ARC || pg == PG_CUSTOM) {
    const bool big = pg == PG_ARC;
    buildRing(S, 0, CX, CY, big ? 204.0f : 210.0f, 228.0f, 90, 0.8f); S.nring = 1;
    float span = d.arcMax - d.arcMin; if (span < 0.001f) span = 1;
    float minor = d.arcMinor > 0.0001f ? d.arcMinor : 1; int per = (int)(d.arcMajor / minor + 0.5f); if (per < 1) per = 1;
    int n = (int)(span / minor + 0.5f);
    for (int i = 0; i <= n; i++) {
      float v = d.arcMin + i * minor; bool major = (i % per) == 0;
      tick(b, v - d.arcMin, span, major ? (big ? 180.0f : 188.0f) : (big ? 187.0f : 193.0f), big ? 198.0f : 202.0f, major ? 1.9f : 0.9f, major ? WHITE : FAINT);
      if (major) { snprintf(t, sizeof(t), "%.*f", d.arcDec, v); numeral(b, v - d.arcMin, span, big ? 142.0f : 152.0f, t, WHITE); }
    }
    if (d.arcLabel[0]) textC(b, F_LBL, d.arcLabel, CX, big ? 338 : 366, CYAN);
    if (d.arcUnit[0]) textC(b, F_LBL, d.arcUnit, CX, big ? 280 : 186, DIM);
    if (!big) {                                      // three icon readouts under the number
      for (int i = 0; i < 3; i++) {
        const int y = 228 + 38 * i;
        iconDraw(b, d.rd[i].icon, CX - 84, y + 14, CYAN);
        if (d.rd[i].unit[0]) text(b, F_LBL, d.rd[i].unit, CX + 68, y + 9, DIM);
      }
    }
    struct A { float zero, red; } a = {(0 - d.arcMin) / span, (d.arcRed - d.arcMin) / span};
    setRingColors(S.ring[0], [](float f, const void *p) {
      const A *a = (const A *)p;
      if (f >= a->red) return RED;
      if (a->zero > 0 && f < a->zero) return lerpc(60, 120, 255, 0, 200, 255, f / a->zero);
      float lo = a->zero > 0 ? a->zero : 0, u = (f - lo) / (a->red - lo > 0.01f ? a->red - lo : 0.01f);
      return u < 0.5f ? lerpc(0, 200, 255, 110, 240, 190, u / 0.5f) : lerpc(110, 240, 190, 255, 208, 50, (u - 0.5f) / 0.5f); }, &a);
  } else if (pg == PG_QUAD) {
    if (d.title[0]) textC(b, F_LBL, d.title, CX, 62, CYAN);
    taper(b, CX, 96, CX, 420, 0.8f, 0.8f, FAINT); taper(b, 62, CY + 4, 418, CY + 4, 0.8f, 0.8f, FAINT);
    static const int cxs[4] = {CX - 100, CX + 100, CX - 100, CX + 100}, cys[4] = {CY - 76, CY - 76, CY + 92, CY + 92};
    for (int i = 0; i < 4; i++) {
      iconDraw(b, d.rd[i].icon, cxs[i], cys[i] - 68, CYAN);
      textC(b, F_LBL, d.rd[i].label, cxs[i], cys[i] - 46, DIM);
      textC(b, F_LBL, d.rd[i].unit, cxs[i], cys[i] + 48, DIM);
    }
  } else if (pg == PG_DUAL) {
    if (d.title[0]) textC(b, F_LBL, d.title, CX, 64, CYAN);
    for (int i = 0; i < 2; i++) {
      const int y0 = i ? 262 : 108;
      iconDraw(b, d.rd[i].icon, CX - 74, y0 + 8, CYAN);
      char lab[24]; snprintf(lab, sizeof(lab), "%s %s", d.rd[i].label, d.rd[i].unit);
      text(b, F_LBL, lab, CX - 52, y0 + 0, DIM);
      const int bx0 = CX - 120, by = y0 + 108;
      taper(b, bx0 + 7, by + 7, bx0 + 233, by + 7, 7, 7, PANEL_EDGE); discAA(b, bx0 + 7, by + 7, 7, PANEL_EDGE); discAA(b, bx0 + 233, by + 7, 7, PANEL_EDGE);
    }
  } else {   // PG_GEAR: the ladder 1..6, the current gear drawn huge, the speed below
    textC(b, F_LBL, "GEAR", CX, 58, DIM);
    for (int g = 0; g < 6; g++) { snprintf(t, sizeof(t), "%d", g + 1); textC(b, F_MED, t, CX + (int)((g - 2.5f) * 54), 92, FAINT); }
    textC(b, F_LBL, "MPH", CX, 360, DIM);
  }
}

// ---- per page: static layer + ring setup ---------------------------------------------------------------------------
inline void buildPage(State &S, int pg, const Data &d) {
  uint16_t *b = S.bg;
  for (int i = 0; i < W * H; i++) b[i] = BG;
  ringAA(b, CX, CY, 235.5f, 238.5f, BEZEL);
  S.nmap = 0; S.nring = 0;
  int mx = rpmMax(d.redline);
  char t[16];
  if (pg == PG_SPORT) {
    buildRing(S, 0, CX, CY, 206, 228, 90, 0.9f); S.nring = 1;
    for (int v = 0; v <= mx; v += 500) tick(b, (float)v, (float)mx, v % 1000 ? 190.0f : 186.0f, 201, v % 1000 ? 1.0f : 1.7f, v % 1000 ? FAINT : DIM);
    textC(b, F_LBL, "RPM", CX, 78, DIM);
    textC(b, F_LBL, "MPH", CX, 276, DIM);
    const int PW = 92, PY = 308, PH = 58;
    pill(b, CX - 100, PY, PW, PH, "COOL"); pill(b, CX, PY, PW, PH, "FUEL"); pill(b, CX + 100, PY, PW, PH, "BATT");
    textC(b, F_LBL, "TACHI", CX, 396, FAINT);
  } else if (pg == PG_TACH || pg == PG_SPEEDO) {
    bool tach = pg == PG_TACH; float vmax = tach ? (float)mx : 160.0f;
    buildRing(S, 0, CX, CY, 213, 227, 90, 0.7f); S.nring = 1;
    float majorStep = tach ? 1000.0f : 20.0f, midStep = tach ? 500.0f : 10.0f, minStep = tach ? 250.0f : 5.0f;
    for (float v = 0; v <= vmax + 0.01f; v += minStep) {
      bool major = fmodf(v, majorStep) < 0.01f, mid = !major && fmodf(v, midStep) < 0.01f;
      tick(b, v, vmax, major ? 184.0f : (mid ? 192.0f : 198.0f), 207, major ? 2.0f : (mid ? 1.4f : 0.9f), major ? WHITE : (mid ? DIM : FAINT));
    }
    for (float v = 0; v <= vmax + 0.01f; v += majorStep) { snprintf(t, sizeof(t), "%d", (int)(tach ? v / 1000 + 0.5f : v + 0.5f)); numeral(b, v, vmax, strlen(t) >= 3 ? 140.0f : 160.0f, t, v >= d.redline && tach ? RED : WHITE); }
    textC(b, F_LBL, tach ? "X1000 RPM" : "MPH", CX, 132, DIM);
    textC(b, F_LBL, tach ? "MPH" : "RPM", CX, tach ? 400 : 388, DIM);
  } else if (pg == PG_VITALS) {   // vitals: four ring gauges
    static const int cxs[4] = {CX - 100, CX + 100, CX - 100, CX + 100}, cys[4] = {CY - 100, CY - 100, CY + 100, CY + 100};
    for (int i = 0; i < 4; i++) buildRing(S, i, (float)cxs[i], (float)cys[i], 74, 90, 45, 1.8f);
    S.nring = 4;
    const char *lab[4] = {d.unitF ? "COOL F" : "COOL C", "FUEL %", "BATT V", "LOAD %"};
    for (int i = 0; i < 4; i++) textC(b, F_LBL, lab[i], cxs[i], cys[i] + 36, DIM);
  } else {
    buildStyle(S, pg, d);                        // arc, custom, quad, dual, gear
  }
  pageDots(b, d.dotN, d.dotCur);

  // ring colours
  if (pg >= PG_ARC) {                            // the multi-value pages set their ring colours in buildStyle
  } else if (pg == PG_SPORT || pg == PG_TACH) {
    struct C { int mx, rl; } c = {mx, d.redline};
    setRingColors(S.ring[0], [](float f, const void *p) { const C *c = (const C *)p; return rpmColor(f * c->mx, c->rl); }, &c);
  } else if (pg == PG_SPEEDO) {
    setRingColors(S.ring[0], [](float f, const void *) { return f < 0.6f ? lerpc(0, 200, 255, 70, 110, 255, f / 0.6f) : lerpc(70, 110, 255, 200, 70, 255, (f - 0.6f) / 0.4f); }, nullptr);
  } else {
    struct V { const Data *d; } v = {&d};
    setRingColors(S.ring[0], [](float f, const void *p) {            // coolant 40..130 C
      const Data *d = ((const V *)p)->d; float c = 40 + f * 90;
      if (c >= d->worryC) return RED; if (c >= d->hotC) return YELLOW; if (c < 65) return lerpc(60, 140, 255, 0, 200, 255, (c - 40) / 25.0f); return lerpc(0, 200, 255, 110, 240, 190, (c - 65) / 30.0f); }, &v);
    setRingColors(S.ring[1], [](float f, const void *p) {            // fuel 0..100 %
      const Data *d = ((const V *)p)->d; float pc = f * 100;
      if (pc < d->fuelLowPct / 2.0f) return RED; if (pc < d->fuelLowPct) return ORANGE; if (pc < 30) return YELLOW; return lerpc(110, 240, 190, 0, 200, 255, (pc - 30) / 70.0f); }, &v);
    setRingColors(S.ring[2], [](float f, const void *p) {            // battery 10.5..15.5 V
      const Data *d = ((const V *)p)->d; float v = 10.5f + f * 5.0f;
      if (v < d->battCritV) return RED; if (v < d->battLowV) return YELLOW; return lerpc(110, 240, 190, 0, 200, 255, (v - d->battLowV) / 3.0f); }, &v);
    setRingColors(S.ring[3], [](float f, const void *) { return f < 0.7f ? lerpc(0, 200, 255, 255, 208, 50, f / 0.7f) : (f < 0.9f ? lerpc(255, 208, 50, 255, 70, 50, (f - 0.7f) / 0.2f) : RED); }, nullptr);
  }
}

// ---- dynamic parts -------------------------------------------------------------------------------------------------
inline void num(State &S, uint16_t *fb, int slot, const Font &f, const char *s, int cx, int y, int /*unused*/, uint16_t col) {
  Slot &sl = S.slot[slot];
  if (sl.col == col && strcmp(sl.s, s) == 0) return;
  int w = textW(f, s), nx0 = cx - w / 2 - 2, nx1 = cx - w / 2 + w + 2;       // extent of the new text (a little slack for anti-aliasing)
  int rx0 = sl.x1 >= sl.x0 && sl.x0 < nx0 ? sl.x0 : nx0, rx1 = sl.x1 >= sl.x0 && sl.x1 > nx1 ? sl.x1 : nx1;   // clear the old extent too
  strncpy(sl.s, s, sizeof(sl.s) - 1); sl.s[sizeof(sl.s) - 1] = 0; sl.col = col; sl.x0 = nx0; sl.x1 = nx1;
  restore(fb, S.bg, rx0, y, rx1 - rx0 + 1, f.h);
  textC(fb, f, s, cx, y, col);
  mark(S, rx0, y, rx1, y + f.h - 1);
}
inline void needle(State &S, uint16_t *fb, float deg, uint16_t col) {
  float q = roundf(deg * 4) / 4;
  if (q == S.needleAng) return;
  S.needleAng = q;
  for (int y = S.nr0; y <= S.nr1; y++)                    // put back the dial under last frame's needle, row by row
    if (S.nmax[y] >= S.nmin[y]) { restore(fb, S.bg, S.nmin[y] - 1, y, S.nmax[y] - S.nmin[y] + 3, 1); mark(S, S.nmin[y] - 1, y, S.nmax[y] + 1, y); }
  restore(fb, S.bg, CX - 20, CY - 20, 40, 40); mark(S, CX - 20, CY - 20, CX + 20, CY + 20);
  float tx, ty, ex, ey; polar(196, q, &tx, &ty); polar(-40, q, &ex, &ey);
  Box a = taper(fb, ex, ey, tx, ty, 6.5f, 1.4f, col, S.nmin, S.nmax);
  discAA(fb, CX, CY, 17, rgb(205, 212, 228)); discAA(fb, CX, CY, 10, BG); discAA(fb, CX, CY, 4.5f, col);
  S.nr0 = a.y0 < 0 ? 0 : a.y0; S.nr1 = a.y1 >= H ? H - 1 : a.y1;
  for (int y = S.nr0; y <= S.nr1; y++) if (S.nmax[y] >= S.nmin[y]) mark(S, S.nmin[y] - 1, y, S.nmax[y] + 1, y);
}

// dual page: a horizontal bar fill (the track is part of the static layer)
inline void barFill(State &S, uint16_t *fb, int i, int x0, int y, int w, int h, float frac, uint16_t col) {
  int px = (int)(clamp01(frac) * w + 0.5f);
  if (px == S.barPx[i] && col == S.barCol[i]) return;
  S.barPx[i] = px; S.barCol[i] = col;
  restore(fb, S.bg, x0 - 1, y - 1, w + 2, h + 2);
  if (px > 0) {
    int e = px < h ? h : px; float r = h / 2.0f;
    taper(fb, x0 + r, y + r, x0 + e - r, y + r, r, r, col); discAA(fb, x0 + r, y + r, r, col); discAA(fb, x0 + e - r, y + r, r, col);
  }
  mark(S, x0 - 1, y - 1, x0 + w, y + h);
}

// dynamic part of the multi-value pages
inline void frameStyle(State &S, uint16_t *fb, int pg, const Data &d) {
  const char *none = "--";
  if (pg == PG_ARC || pg == PG_CUSTOM) {
    float span = d.arcMax - d.arcMin; if (span < 0.001f) span = 1;
    ringLevel(S, 0, d.haveArc ? (d.arcVal - d.arcMin) / span : 0);
    const bool big = pg == PG_ARC;
    num(S, fb, 0, F_BIG, d.haveArc ? d.arcText : none, CX, big ? 214 : 122, 0, d.haveArc ? WHITE : DIM);
    if (!big) for (int i = 0; i < 3; i++) num(S, fb, 1 + i, F_MED, d.rd[i].have ? d.rd[i].text : none, CX + 6, 228 + 38 * i, 0, d.rd[i].have ? d.rd[i].col : DIM);
  } else if (pg == PG_QUAD) {
    static const int cxs[4] = {CX - 100, CX + 100, CX - 100, CX + 100}, cys[4] = {CY - 76, CY - 76, CY + 92, CY + 92};
    for (int i = 0; i < 4; i++) num(S, fb, i, F_BIG, d.rd[i].have ? d.rd[i].text : none, cxs[i], cys[i] - 16, 0, d.rd[i].have ? d.rd[i].col : DIM);
  } else if (pg == PG_DUAL) {
    for (int i = 0; i < 2; i++) {
      const int y0 = i ? 262 : 108;
      num(S, fb, i, F_BIG, d.rd[i].have ? d.rd[i].text : none, CX, y0 + 28, 0, d.rd[i].have ? d.rd[i].col : DIM);
      barFill(S, fb, i, CX - 120, y0 + 108, 240, 14, d.rd[i].have ? d.rd[i].frac : 0, d.rd[i].col);
    }
  } else {   // PG_GEAR
    char g[2] = {d.gear, 0}; const bool known = d.gear >= '1' && d.gear <= '6';
    num(S, fb, 0, F_HUGE, g, CX, 132, 0, known ? WHITE : DIM);
    char l[2] = {known ? d.gear : (char)0, 0}; int gi = known ? d.gear - '1' : 2;
    num(S, fb, 1, F_MED, l, CX + (int)((gi - 2.5f) * 54), 92, 0, CYAN);
    char sp[12]; snprintf(sp, sizeof(sp), "%d", (int)lroundf(S.mph)); num(S, fb, 2, F_BIG, sp, CX, 292, 0, WHITE);
  }
}

// full = the frame buffer holds something else (another page was shown): rebuild and redraw everything.
inline void frame(uint16_t *fb, int pg, const Data &d, uint32_t now, bool full = false) {
  State &S = st(); if (!S.ok) return;
  float k = S.lastT ? 1.0f - expf(-(float)(now - S.lastT) / 90.0f) : 1.0f; S.lastT = now;
  S.rpm += (d.rpm - S.rpm) * k; S.mph += (d.mph - S.mph) * k;
  if (full || pg != S.page || d.dotN != S.dotN || d.dotCur != S.dotCur || d.unitF != S.unitF || d.redline != S.redline) {
    buildPage(S, pg, d);
    memcpy(fb, S.bg, (size_t)W * H * 2);
    S.page = pg; S.dotN = d.dotN; S.dotCur = d.dotCur; S.unitF = d.unitF; S.redline = d.redline;
    for (Slot &s : S.slot) { s.s[0] = '\x01'; s.s[1] = 0; s.col = 0; s.x0 = 0; s.x1 = -1; }
    S.needle = {0, 0, -1, -1}; S.needleAng = 1e9f; S.forceRings = true; S.nr0 = 0; S.nr1 = -1; S.barPx[0] = S.barPx[1] = -1;
    mark(S, 0, 0, W - 1, H - 1);
  }
  int rpm = (int)lroundf(S.rpm), mph = (int)lroundf(S.mph), mx = rpmMax(d.redline);
  char t[16];
  if (pg == PG_SPORT) {
    ringLevel(S, 0, S.rpm / mx);
    snprintf(t, sizeof(t), "%d", rpm); num(S, fb, 0, F_MED, t, CX, 104, 150, rpm >= d.redline ? RED : WHITE);
    snprintf(t, sizeof(t), "%d", mph); num(S, fb, 1, F_HUGE, t, CX, 152, 290, WHITE);
    const int PY = 308 + 28;
    if (d.haveCool) snprintf(t, sizeof(t), "%d", d.coolShown); else snprintf(t, sizeof(t), "--");
    num(S, fb, 2, F_MED, t, CX - 100, PY, 84, !d.haveCool ? DIM : (d.coolC >= d.worryC ? RED : (d.coolC >= d.hotC ? YELLOW : WHITE)));
    if (d.haveFuel) snprintf(t, sizeof(t), "%d", d.fuelPct); else snprintf(t, sizeof(t), "--");
    num(S, fb, 3, F_MED, t, CX, PY, 84, !d.haveFuel ? DIM : (d.fuelPct < d.fuelLowPct / 2 ? RED : (d.fuelPct < d.fuelLowPct ? YELLOW : WHITE)));
    if (d.haveBatt) snprintf(t, sizeof(t), "%.1f", d.battV); else snprintf(t, sizeof(t), "--");
    num(S, fb, 4, F_MED, t, CX + 100, PY, 84, !d.haveBatt ? DIM : (d.battV < d.battCritV ? RED : (d.battV < d.battLowV ? YELLOW : WHITE)));
  } else if (pg == PG_TACH) {
    ringLevel(S, 0, S.rpm / mx);
    needle(S, fb, angOf(fminf(S.rpm, (float)mx), (float)mx), S.rpm >= d.redline ? RED : ORANGE);
    snprintf(t, sizeof(t), "%d", mph); num(S, fb, 0, F_BIG, t, CX, 336, 130, WHITE);
  } else if (pg == PG_SPEEDO) {
    ringLevel(S, 0, S.mph / 160.0f);
    needle(S, fb, angOf(fminf(S.mph, 160.0f), 160.0f), CYAN);
    snprintf(t, sizeof(t), "%d", rpm); num(S, fb, 0, F_MED, t, CX, 352, 110, WHITE);
  } else if (pg == PG_VITALS) {
    static const int cxs[4] = {CX - 100, CX + 100, CX - 100, CX + 100}, cys[4] = {CY - 100, CY - 100, CY + 100, CY + 100};
    float f0 = d.haveCool ? (d.coolC - 40) / 90.0f : 0, f1 = d.haveFuel ? d.fuelPct / 100.0f : 0, f2 = d.haveBatt ? (d.battV - 10.5f) / 5.0f : 0, f3 = d.haveLoad ? d.loadPct / 100.0f : 0;
    ringLevel(S, 0, f0); ringLevel(S, 1, f1); ringLevel(S, 2, f2); ringLevel(S, 3, f3);
    const bool have[4] = {d.haveCool, d.haveFuel, d.haveBatt, d.haveLoad};
    for (int i = 0; i < 4; i++) {
      if (!have[i]) snprintf(t, sizeof(t), "--");
      else if (i == 0) snprintf(t, sizeof(t), "%d", d.coolShown);
      else if (i == 1) snprintf(t, sizeof(t), "%d", d.fuelPct);
      else if (i == 2) snprintf(t, sizeof(t), "%.1f", d.battV);
      else snprintf(t, sizeof(t), "%d", d.loadPct);
      num(S, fb, i, F_MED, t, cxs[i], cys[i] - 22, 110, have[i] ? WHITE : DIM);
    }
  } else {
    frameStyle(S, fb, pg, d);                    // arc, custom, quad, dual, gear
  }
  drawRings(S, fb);
}

// ---- brightness page: sun icon, percentage and a slider ----
constexpr int BR_X0 = 90, BR_X1 = 390, BR_Y = 308, BR_ZONE_Y0 = 262, BR_ZONE_Y1 = 354, BR_MIN = 5;   // slider track, touch zone, lowest level
inline int pctFromX(int x) { int p = BR_MIN + (x - BR_X0) * (100 - BR_MIN) / (BR_X1 - BR_X0); return p < BR_MIN ? BR_MIN : (p > 100 ? 100 : p); }
// full = draw the whole page; otherwise only the part that changes with the level. Marks what it touched (see flushDirty).
inline void brightnessPage(uint16_t *fb, int pct, int dotN, int dotCur, bool full) {
  State &S = st(); if (!S.ok) return;
  if (full) {
    for (int i = 0; i < W * H; i++) fb[i] = BG;
    ringAA(fb, CX, CY, 235.5f, 238.5f, BEZEL);
    textC(fb, F_LBL, "BRIGHTNESS", CX, 92, DIM);
    textC(fb, F_LBL, "DRAG THE SLIDER", CX, 376, FAINT);
    pageDots(fb, dotN, dotCur);
    S.page = -1;                                   // the dial pages must rebuild their static layer next time
    mark(S, 0, 0, W - 1, H - 1);
  }
  for (int y = 122; y <= 344; y++) for (int x = 40; x <= 440; x++) fb[y * W + x] = BG;
  float f = (pct - BR_MIN) / (float)(100 - BR_MIN);
  uint16_t sun = lerpc(255, 170, 40, 255, 236, 150, f);
  discAA(fb, CX, 150, 10, sun);
  for (int k = 0; k < 8; k++) {                    // rays grow with the level
    float a = k * 0.785398f, ri = 17, ro = 21 + 11 * f;
    taper(fb, CX + ri * sinf(a), 150 - ri * cosf(a), CX + ro * sinf(a), 150 - ro * cosf(a), 1.8f, 1.8f, sun);
  }
  char t[8]; snprintf(t, sizeof(t), "%d", pct);
  textC(fb, F_BIG, t, CX - 6, 190, WHITE);
  text(fb, F_MED, "%", CX - 6 + textW(F_BIG, t) / 2 + 8, 190 + F_BIG.h - F_MED.h, DIM);
  float kx = BR_X0 + f * (BR_X1 - BR_X0);
  taper(fb, BR_X0, BR_Y, BR_X1, BR_Y, 8, 8, PANEL_EDGE); discAA(fb, BR_X0, BR_Y, 8, PANEL_EDGE); discAA(fb, BR_X1, BR_Y, 8, PANEL_EDGE);
  taper(fb, BR_X0, BR_Y, kx, BR_Y, 8, 8, sun); discAA(fb, BR_X0, BR_Y, 8, sun);
  discAA(fb, kx, BR_Y, 22, BEZEL); discAA(fb, kx, BR_Y, 19, WHITE); discAA(fb, kx, BR_Y, 13, sun);
  mark(S, 40, 122, 440, 344);
}

}  // namespace rui
