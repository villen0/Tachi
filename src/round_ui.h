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
#ifdef RUI_LAYOUT_CHECK
#include <vector>
#endif
#if defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

namespace rui {

constexpr int W = 480, H = 480, CX = 240, CY = 240;
enum Pg { PG_SPORT = 0, PG_TACH, PG_SPEEDO, PG_VITALS, PG_ARC, PG_CUSTOM, PG_QUAD, PG_DUAL, PG_GEAR, PG_TRIP, PG_CODES, PG_COUNT };

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
const Font F_NUM = {roundfont::NUM_CHARS, roundfont::NUM_G, roundfont::NUM_D, roundfont::NUM_H, 2};
const Font F_MED = {roundfont::MED_CHARS, roundfont::MED_G, roundfont::MED_D, roundfont::MED_H, 2};
const Font F_TTL = {roundfont::TTL_CHARS, roundfont::TTL_G, roundfont::TTL_D, roundfont::TTL_H, 2};
const Font F_SML = {roundfont::SML_CHARS, roundfont::SML_G, roundfont::SML_D, roundfont::SML_H, 1};
const Font F_LBL = {roundfont::LBL_CHARS, roundfont::LBL_G, roundfont::LBL_D, roundfont::LBL_H, 1};
inline int glyphIndex(const Font &f, char ch) { if (ch >= 'a' && ch <= 'z') ch -= 32; const char *p = ch ? strchr(f.chars, ch) : nullptr; return p ? (int)(p - f.chars) : -1; }
inline int textW(const Font &f, const char *s) { int w = 0; for (int i = 0; s[i]; i++) { int k = glyphIndex(f, s[i]); if (k >= 0) w += f.g[k].adv; } return w > f.gap ? w - f.gap : w; }
#ifdef RUI_LAYOUT_CHECK
struct LayoutRec { int x0, y0, x1, y1; char kind; int fontH; char s[48]; };      // kind: 't' text, 'c' container (pill / chip / button)
inline std::vector<LayoutRec> &layoutLog() { static std::vector<LayoutRec> v; return v; }
#endif
inline void text(uint16_t *b, const Font &f, const char *s, int x, int y, uint16_t col) {
#ifdef RUI_LAYOUT_CHECK
  if (s[0]) { LayoutRec r{x, y, x + textW(f, s) - 1, y + f.h - 1, 't', f.h, {0}}; strncpy(r.s, s, 47); layoutLog().push_back(r); }
#endif
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
enum Icon : uint8_t { IC_NONE = 0, IC_THERM, IC_AIR, IC_BATT, IC_FUEL, IC_LOAD, IC_BOOST, IC_THROTTLE, IC_ENGINE };
struct Readout {                 // one value on the multi-value pages (custom, quad, dual)
  bool have = false; uint8_t icon = IC_NONE; char label[14] = ""; char text[12] = "--"; char unit[8] = ""; uint16_t col = 0xFFFF; float frac = 0;
};
// Drive report page: the caller formats everything (units, rounding), this code only lays it out.
struct TripTile { char label[12] = ""; char value[14] = "--"; uint16_t col = 0xFFFF; };
struct TripView {
  bool have = false, live = false;       // have: there is a trip to show; live: it is still being driven
  int rating = 0; char grade = '-';
  char title[16] = "DRIVE REPORT";
  TripTile t[6];                         // top speed, max rpm, max coolant, peak boost, volts, new codes
  char foot1[40] = "", foot2[40] = "";
  int sel = 0, nSaved = 0;               // chips: 0 = this trip, 1..nSaved = earlier trips
};
// Fault codes page.
struct CodeRow { char code[6] = ""; char desc[30] = ""; char tag[8] = ""; uint16_t col = 0xFFFF; };
struct CodesView {
  bool mil = false, answered = false;    // check-engine light on; the car has answered a code request
  int total = 0, nRows = 0;              // codes found (nRows of them have a row, at most 4)
  CodeRow row[4];
  char status[40] = ""; uint16_t statusCol = 0xFFFF;
  float clearProg = 0;                   // 0..1 while the CLEAR button is being held
};
struct Data {
  TripView trip; CodesView codes;
  int ver = 0;                           // must change whenever any STATIC content changes (trip title/labels/chips/live, code rows/tags/mil/answered/total) so the page is rebuilt;
                                         // live numbers (tile values, footers, status, clear progress) are redrawn every frame without it
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

struct Slot { char s[44]; uint16_t col; int16_t x0, x1; };   // x0..x1: where the text was drawn
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
  int ver = -1;                                                // content version last built (Data::ver)
  int8_t brChip = -2;                                          // brightness page: which preset chip was drawn lit (-1 none, -2 not drawn yet)
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
  float r = 12; int x = cx - w / 2;
#ifdef RUI_LAYOUT_CHECK
  { LayoutRec c{x, y, x + w - 1, y + h - 1, 'c', 0, {0}}; layoutLog().push_back(c); }
#endif
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    float px = i + 0.5f, py = j + 0.5f, qx = fabsf(px - w / 2.0f) - (w / 2.0f - r), qy = fabsf(py - h / 2.0f) - (h / 2.0f - r);
    float d = sqrtf((qx > 0 ? qx : 0) * (qx > 0 ? qx : 0) + (qy > 0 ? qy : 0) * (qy > 0 ? qy : 0)) + fminf(fmaxf(qx, qy), 0.0f) - r;   // signed distance to the rounded rect
    blendPx(b, x + i, y + j, PANEL, (int)(clamp01(0.5f - d) * 255));
    blendPx(b, x + i, y + j, PANEL_EDGE, (int)(clamp01(0.5f - fabsf(d + 1.0f)) * 255));
  }
  textC(b, F_LBL, label, cx, y + 10, DIM);
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

// ---- drive report / fault codes layout constants and touch zones -----------------------------------------------------
constexpr int TR_CHIP_Y = 356, TR_CHIP_H = 26, TR_CHIP_GAP = 6;
// Eight pills in three rows with the Sport page's pill size (58 px high, label at y+10, value at y+30, values in F_MED):
// GRADE and RATING; top speed, max rpm, max coolant; peak boost, volts (wide, for a "13.1-14.2" range) and new codes.
constexpr int TR_PILL_H = 58;
inline void tripTile(int i, int *cx, int *y, int *w) {
  static const int xs[8] = {CX - 70, CX + 70, CX - 120, CX, CX + 120, 98, 254, 396}, ws[8] = {130, 130, 112, 112, 112, 112, 184, 84}, ys[8] = {100, 100, 168, 168, 168, 236, 236, 236};
  *cx = xs[i]; *w = ws[i]; *y = ys[i];
}
inline int tripChipW(int k) { return k == 0 ? 70 : 36; }                    // chip 0 is "NOW" / "LAST", then 1..5
inline int tripChipX(int k) { int total = 70 + 5 * 36 + 5 * TR_CHIP_GAP, x = CX - total / 2; for (int i = 0; i < k; i++) x += tripChipW(i) + TR_CHIP_GAP; return x; }
// which chip (0 = this trip, 1..5 = earlier trips) is under the finger, or -1
inline int tripChipAt(int x, int y) {
  if (y < TR_CHIP_Y - 8 || y > TR_CHIP_Y + TR_CHIP_H + 8) return -1;
  for (int k = 0; k < 6; k++) { int x0 = tripChipX(k); if (x >= x0 - 4 && x <= x0 + tripChipW(k) + 4) return k; }
  return -1;
}
constexpr int CD_BTN_Y = 366, CD_BTN_H = 42, CD_BTN_W = 110;                  // the preset chips' height on the brightness page
inline int cdBtnX(int k) { return k == 0 ? CX - 120 : CX + 10; }              // 0 = READ, 1 = CLEAR (hold)
inline int codesButtonAt(int x, int y) {
  if (y < CD_BTN_Y - 8 || y > CD_BTN_Y + CD_BTN_H + 8) return -1;
  for (int k = 0; k < 2; k++) if (x >= cdBtnX(k) - 6 && x <= cdBtnX(k) + CD_BTN_W + 6) return k;
  return -1;
}
inline void chip(uint16_t *b, int x, int y, int w, int h, bool on, uint16_t col, const char *label);   // defined with the brightness page

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
    case IC_ENGINE:
      taper(b, x - 8, y - 3, x + 8, y - 3, 1.3f, 1.3f, c); taper(b, x - 8, y + 9, x + 8, y + 9, 1.3f, 1.3f, c);
      taper(b, x - 8, y - 3, x - 8, y + 9, 1.3f, 1.3f, c); taper(b, x + 8, y - 3, x + 8, y + 9, 1.3f, 1.3f, c);
      taper(b, x - 4, y - 8, x + 4, y - 8, 1.3f, 1.3f, c); taper(b, x - 4, y - 8, x - 4, y - 3, 1.3f, 1.3f, c); taper(b, x + 4, y - 8, x + 4, y - 3, 1.3f, 1.3f, c);
      taper(b, x - 8, y + 3, x - 12, y + 3, 1.3f, 1.3f, c); taper(b, x + 8, y + 3, x + 12, y + 3, 1.3f, 1.3f, c); break;
    default: break;
  }
}

// static layer of the multi-value pages: arc, custom, quad, dual, gear
inline void buildStyle(State &S, int pg, const Data &d) {
  uint16_t *b = S.bg; char t[16];
  if (pg == PG_ARC || pg == PG_CUSTOM) {
    const bool big = pg == PG_ARC;
    buildRing(S, 0, CX, CY, 214, 226, 90, 0.8f); S.nring = 1;
    float span = d.arcMax - d.arcMin; if (span < 0.001f) span = 1;
    float minor = d.arcMinor > 0.0001f ? d.arcMinor : 1; int per = (int)(d.arcMajor / minor + 0.5f); if (per < 1) per = 1;
    int n = (int)(span / minor + 0.5f);
    for (int i = 0; i <= n; i++) {
      float v = d.arcMin + i * minor; bool major = (i % per) == 0;
      tick(b, v - d.arcMin, span, major ? 190.0f : 197.0f, 207.0f, major ? 1.6f : 0.8f, major ? WHITE : FAINT);
      if (major) { snprintf(t, sizeof(t), "%.*f", d.arcDec, v); numeral(b, v - d.arcMin, span, big ? 150.0f : 152.0f, t, DIM); }
    }
    if (d.arcLabel[0]) textC(b, F_LBL, d.arcLabel, CX, big ? 338 : 348, CYAN);
    if (d.arcUnit[0]) textC(b, F_LBL, d.arcUnit, CX, big ? 262 : 182, DIM);
    if (!big) {                                      // three icon readouts under the number
      for (int i = 0; i < 3; i++) {
        const int y = 214 + 40 * i;
        iconDraw(b, d.rd[i].icon, CX - 78, y + 12, CYAN);
        if (d.rd[i].unit[0]) text(b, F_LBL, d.rd[i].unit, CX + 66, y + 5, DIM);
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
    if (d.title[0]) textC(b, F_LBL, d.title, CX, 64, CYAN);
    taper(b, CX, 118, CX, 384, 0.7f, 0.7f, FAINT); taper(b, 84, CY + 2, 396, CY + 2, 0.7f, 0.7f, FAINT);
    static const int cxs[4] = {CX - 96, CX + 96, CX - 96, CX + 96}, cys[4] = {CY - 80, CY - 80, CY + 84, CY + 84};
    for (int i = 0; i < 4; i++) {
      iconDraw(b, d.rd[i].icon, cxs[i], cys[i] - 68, CYAN);
      textC(b, F_LBL, d.rd[i].label, cxs[i], cys[i] - 46, DIM);
      textC(b, F_LBL, d.rd[i].unit, cxs[i], cys[i] + 30, DIM);
    }
  } else if (pg == PG_DUAL) {
    if (d.title[0]) textC(b, F_LBL, d.title, CX, 74, CYAN);
    for (int i = 0; i < 2; i++) {
      const int y0 = i ? 262 : 120;
      iconDraw(b, d.rd[i].icon, CX - 84, y0 + 7, CYAN);
      char lab[24]; snprintf(lab, sizeof(lab), "%s %s", d.rd[i].label, d.rd[i].unit);
      text(b, F_LBL, lab, CX - 62, y0, DIM);
      const int bx0 = CX - 120, by = y0 + 88;
      taper(b, bx0 + 5, by + 5, bx0 + 235, by + 5, 5, 5, PANEL); discAA(b, bx0 + 5, by + 5, 5, PANEL); discAA(b, bx0 + 235, by + 5, 5, PANEL);
    }
  } else if (pg == PG_TRIP) {
    const TripView &T = d.trip;
    textC(b, F_LBL, T.title, CX, 74, CYAN);
    for (int i = 0; i < 8; i++) { int cx, y, w; tripTile(i, &cx, &y, &w); pill(b, cx, y, w, TR_PILL_H, i == 0 ? "GRADE" : i == 1 ? "RATING" : T.t[i - 2].label); }
    for (int k = 0; k <= 5; k++) {
      char l[8]; if (k == 0) snprintf(l, sizeof(l), T.live ? "NOW" : "LAST"); else snprintf(l, sizeof(l), "%d", k);
      bool avail = k == 0 ? true : k <= T.nSaved;
      chip(b, tripChipX(k), TR_CHIP_Y, tripChipW(k), TR_CHIP_H, k == T.sel, avail ? CYAN : FAINT, l);
    }
  } else if (pg == PG_CODES) {
    const CodesView &C = d.codes;
    textC(b, F_LBL, "FAULT CODES", CX, 74, CYAN);
    iconDraw(b, IC_ENGINE, CX - 104, 108, C.mil ? ORANGE : DIM);
    text(b, F_LBL, "CHECK ENGINE", CX - 80, 96, WHITE);
    text(b, F_LBL, C.mil ? "ON" : "OFF", CX - 80 + textW(F_LBL, "CHECK ENGINE") + 12, 96, C.mil ? ORANGE : MINT);
    char hl[36];
    if (!C.answered) snprintf(hl, sizeof(hl), "NOT READ YET"); else if (C.total == 0) snprintf(hl, sizeof(hl), "NO CODES STORED"); else snprintf(hl, sizeof(hl), "%d CODE%s FOUND", C.total, C.total == 1 ? "" : "S");
    text(b, F_SML, hl, CX - 80, 116, C.total > 0 ? YELLOW : DIM);
    for (int i = 0; i < C.nRows && i < 4; i++) {
      const int y0 = 138 + 50 * i;
      text(b, F_TTL, C.row[i].code, CX - 140, y0, C.row[i].col);
      if (C.row[i].tag[0]) text(b, F_SML, C.row[i].tag, CX + 140 - textW(F_SML, C.row[i].tag), y0 + 3, C.row[i].col);
      text(b, F_SML, C.row[i].desc[0] ? C.row[i].desc : "SEE SERVICE MANUAL", CX - 140, y0 + 22, DIM);
      taper(b, CX - 140, y0 + 38, CX + 140, y0 + 38, 0.6f, 0.6f, FAINT);
    }
    if (C.nRows == 0) textC(b, F_TTL, C.answered ? "ALL CLEAR" : "TAP READ", CX, 214, C.answered ? MINT : DIM);
    chip(b, cdBtnX(0), CD_BTN_Y, CD_BTN_W, CD_BTN_H, false, CYAN, "READ");
    chip(b, cdBtnX(1), CD_BTN_Y, CD_BTN_W, CD_BTN_H, false, RED, "CLEAR");
    taper(b, cdBtnX(1) + 6, CD_BTN_Y + CD_BTN_H + 8, cdBtnX(1) + CD_BTN_W - 6, CD_BTN_Y + CD_BTN_H + 8, 3, 3, PANEL);
  } else {   // PG_GEAR: the ladder 1..6, the current gear drawn large, the speed below
    textC(b, F_LBL, "GEAR", CX, 92, DIM);
    for (int g = 0; g < 6; g++) { snprintf(t, sizeof(t), "%d", g + 1); textC(b, F_MED, t, CX + (int)((g - 2.5f) * 48), 120, FAINT); }
    textC(b, F_LBL, "MPH", CX, 352, DIM);
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
    buildRing(S, 0, CX, CY, 214, 226, 90, 0.9f); S.nring = 1;
    for (int v = 0; v <= mx; v += 500) tick(b, (float)v, (float)mx, v % 1000 ? 197.0f : 191.0f, 207, v % 1000 ? 0.9f : 1.5f, v % 1000 ? FAINT : DIM);
    textC(b, F_LBL, "RPM", CX, 102, DIM);
    textC(b, F_LBL, "MPH", CX, 258, DIM);
    const int PW = 88, PY = 298, PH = 58;
    pill(b, CX - 94, PY, PW, PH, "COOL"); pill(b, CX, PY, PW, PH, "FUEL"); pill(b, CX + 94, PY, PW, PH, "BATT");
    textC(b, F_LBL, "TACHI", CX, 390, FAINT);
  } else if (pg == PG_TACH || pg == PG_SPEEDO) {
    bool tach = pg == PG_TACH; float vmax = tach ? (float)mx : 160.0f;
    buildRing(S, 0, CX, CY, 214, 226, 90, 0.7f); S.nring = 1;
    float majorStep = tach ? 1000.0f : 20.0f, midStep = tach ? 500.0f : 10.0f, minStep = tach ? 250.0f : 5.0f;
    for (float v = 0; v <= vmax + 0.01f; v += minStep) {
      bool major = fmodf(v, majorStep) < 0.01f, mid = !major && fmodf(v, midStep) < 0.01f;
      tick(b, v, vmax, major ? 190.0f : (mid ? 196.0f : 200.0f), 207, major ? 1.8f : (mid ? 1.2f : 0.8f), major ? WHITE : (mid ? DIM : FAINT));
    }
    for (float v = 0; v <= vmax + 0.01f; v += majorStep) { snprintf(t, sizeof(t), "%d", (int)(tach ? v / 1000 + 0.5f : v + 0.5f)); numeral(b, v, vmax, 148.0f, t, v >= d.redline && tach ? RED : WHITE); }
    textC(b, F_LBL, tach ? "X1000 RPM" : "MPH", CX, 290, DIM);
    textC(b, F_LBL, tach ? "MPH" : "RPM", CX, 374, DIM);
  } else if (pg == PG_VITALS) {   // vitals: four ring gauges
    static const int cxs[4] = {CX - 104, CX + 104, CX - 104, CX + 104}, cys[4] = {CY - 98, CY - 98, CY + 98, CY + 98};
    for (int i = 0; i < 4; i++) buildRing(S, i, (float)cxs[i], (float)cys[i], 66, 78, 45, 1.8f);
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
  restore(fb, S.bg, CX - 18, CY - 18, 36, 36); mark(S, CX - 18, CY - 18, CX + 18, CY + 18);
  float tx, ty, ex, ey; polar(190, q, &tx, &ty); polar(-34, q, &ex, &ey);
  Box a = taper(fb, ex, ey, tx, ty, 5.0f, 1.2f, col, S.nmin, S.nmax);
  discAA(fb, CX, CY, 13, rgb(205, 212, 228)); discAA(fb, CX, CY, 8, BG); discAA(fb, CX, CY, 3.5f, col);
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
    num(S, fb, 0, F_BIG, d.haveArc ? d.arcText : none, CX, big ? 206 : 126, 0, d.haveArc ? WHITE : DIM);
    if (!big) for (int i = 0; i < 3; i++) num(S, fb, 1 + i, F_MED, d.rd[i].have ? d.rd[i].text : none, CX + 4, 214 + 40 * i, 0, d.rd[i].have ? d.rd[i].col : DIM);
  } else if (pg == PG_QUAD) {
    static const int cxs[4] = {CX - 96, CX + 96, CX - 96, CX + 96}, cys[4] = {CY - 80, CY - 80, CY + 84, CY + 84};
    for (int i = 0; i < 4; i++) num(S, fb, i, F_NUM, d.rd[i].have ? d.rd[i].text : none, cxs[i], cys[i] - 18, 0, d.rd[i].have ? d.rd[i].col : DIM);
  } else if (pg == PG_DUAL) {
    for (int i = 0; i < 2; i++) {
      const int y0 = i ? 262 : 120;
      num(S, fb, i, F_NUM, d.rd[i].have ? d.rd[i].text : none, CX, y0 + 32, 0, d.rd[i].have ? d.rd[i].col : DIM);
      barFill(S, fb, i, CX - 120, y0 + 88, 240, 10, d.rd[i].have ? d.rd[i].frac : 0, d.rd[i].col);
    }
  } else if (pg == PG_TRIP) {
    const TripView &T = d.trip;
    char g[2] = {T.have ? T.grade : '-', 0}; char r[8]; if (T.have) snprintf(r, sizeof(r), "%d", T.rating); else snprintf(r, sizeof(r), "--");
    uint16_t gc = !T.have ? DIM : (T.rating >= 80 ? MINT : (T.rating >= 60 ? YELLOW : RED));
    for (int i = 0; i < 8; i++) {
      int cx, y, w; tripTile(i, &cx, &y, &w);
      const char *v = i == 0 ? g : i == 1 ? r : (T.have ? T.t[i - 2].value : "--");
      uint16_t c = i < 2 ? gc : (T.have ? T.t[i - 2].col : DIM);
      num(S, fb, i, F_MED, v, cx, y + 30, 0, c);
    }
    num(S, fb, 8, F_LBL, T.have ? T.foot1 : "NO TRIP RECORDED YET", CX, 306, 0, DIM);
    num(S, fb, 9, F_LBL, T.have ? T.foot2 : "", CX, 324, 0, DIM);
  } else if (pg == PG_CODES) {
    const CodesView &C = d.codes;
    num(S, fb, 0, F_SML, C.status, CX, 340, 0, C.statusCol);
    barFill(S, fb, 0, cdBtnX(1) + 6, CD_BTN_Y + CD_BTN_H + 5, CD_BTN_W - 12, 6, C.clearProg, RED);
  } else {   // PG_GEAR
    char g[2] = {d.gear, 0}; const bool known = d.gear >= '1' && d.gear <= '6';
    num(S, fb, 0, F_HUGE, g, CX, 178, 0, known ? WHITE : DIM);
    char l[2] = {known ? d.gear : (char)0, 0}; int gi = known ? d.gear - '1' : 2;
    num(S, fb, 1, F_MED, l, CX + (int)((gi - 2.5f) * 48), 120, 0, CYAN);
    char sp[12]; snprintf(sp, sizeof(sp), "%d", (int)lroundf(S.mph)); num(S, fb, 2, F_NUM, sp, CX, 306, 0, WHITE);
  }
}

// full = the frame buffer holds something else (another page was shown): rebuild and redraw everything.
inline void frame(uint16_t *fb, int pg, const Data &d, uint32_t now, bool full = false) {
  State &S = st(); if (!S.ok) return;
  float k = S.lastT ? 1.0f - expf(-(float)(now - S.lastT) / 90.0f) : 1.0f; S.lastT = now;
  S.rpm += (d.rpm - S.rpm) * k; S.mph += (d.mph - S.mph) * k;
  if (full || pg != S.page || d.dotN != S.dotN || d.dotCur != S.dotCur || d.unitF != S.unitF || d.redline != S.redline || d.ver != S.ver) {
    buildPage(S, pg, d);
    memcpy(fb, S.bg, (size_t)W * H * 2);
    S.page = pg; S.dotN = d.dotN; S.dotCur = d.dotCur; S.unitF = d.unitF; S.redline = d.redline; S.ver = d.ver;
    for (Slot &s : S.slot) { s.s[0] = '\x01'; s.s[1] = 0; s.col = 0; s.x0 = 0; s.x1 = -1; }
    S.needle = {0, 0, -1, -1}; S.needleAng = 1e9f; S.forceRings = true; S.nr0 = 0; S.nr1 = -1; S.barPx[0] = S.barPx[1] = -1;
    mark(S, 0, 0, W - 1, H - 1);
  }
  int rpm = (int)lroundf(S.rpm), mph = (int)lroundf(S.mph), mx = rpmMax(d.redline);
  char t[16];
  if (pg == PG_SPORT) {
    ringLevel(S, 0, S.rpm / mx);
    snprintf(t, sizeof(t), "%d", rpm); num(S, fb, 0, F_MED, t, CX, 126, 150, rpm >= d.redline ? RED : WHITE);
    snprintf(t, sizeof(t), "%d", mph); num(S, fb, 1, F_HUGE, t, CX, 174, 290, WHITE);
    const int PY = 298 + 30;
    if (d.haveCool) snprintf(t, sizeof(t), "%d", d.coolShown); else snprintf(t, sizeof(t), "--");
    num(S, fb, 2, F_MED, t, CX - 94, PY, 84, !d.haveCool ? DIM : (d.coolC >= d.worryC ? RED : (d.coolC >= d.hotC ? YELLOW : WHITE)));
    if (d.haveFuel) snprintf(t, sizeof(t), "%d", d.fuelPct); else snprintf(t, sizeof(t), "--");
    num(S, fb, 3, F_MED, t, CX, PY, 84, !d.haveFuel ? DIM : (d.fuelPct < d.fuelLowPct / 2 ? RED : (d.fuelPct < d.fuelLowPct ? YELLOW : WHITE)));
    if (d.haveBatt) snprintf(t, sizeof(t), "%.1f", d.battV); else snprintf(t, sizeof(t), "--");
    num(S, fb, 4, F_MED, t, CX + 94, PY, 84, !d.haveBatt ? DIM : (d.battV < d.battCritV ? RED : (d.battV < d.battLowV ? YELLOW : WHITE)));
  } else if (pg == PG_TACH) {
    ringLevel(S, 0, S.rpm / mx);
    needle(S, fb, angOf(fminf(S.rpm, (float)mx), (float)mx), S.rpm >= d.redline ? RED : ORANGE);
    snprintf(t, sizeof(t), "%d", mph); num(S, fb, 0, F_MED, t, CX, 338, 130, WHITE);
  } else if (pg == PG_SPEEDO) {
    ringLevel(S, 0, S.mph / 160.0f);
    needle(S, fb, angOf(fminf(S.mph, 160.0f), 160.0f), CYAN);
    snprintf(t, sizeof(t), "%d", rpm); num(S, fb, 0, F_MED, t, CX, 338, 110, WHITE);
  } else if (pg == PG_VITALS) {
    static const int cxs[4] = {CX - 104, CX + 104, CX - 104, CX + 104}, cys[4] = {CY - 98, CY - 98, CY + 98, CY + 98};
    float f0 = d.haveCool ? (d.coolC - 40) / 90.0f : 0, f1 = d.haveFuel ? d.fuelPct / 100.0f : 0, f2 = d.haveBatt ? (d.battV - 10.5f) / 5.0f : 0, f3 = d.haveLoad ? d.loadPct / 100.0f : 0;
    ringLevel(S, 0, f0); ringLevel(S, 1, f1); ringLevel(S, 2, f2); ringLevel(S, 3, f3);
    const bool have[4] = {d.haveCool, d.haveFuel, d.haveBatt, d.haveLoad};
    for (int i = 0; i < 4; i++) {
      if (!have[i]) snprintf(t, sizeof(t), "--");
      else if (i == 0) snprintf(t, sizeof(t), "%d", d.coolShown);
      else if (i == 1) snprintf(t, sizeof(t), "%d", d.fuelPct);
      else if (i == 2) snprintf(t, sizeof(t), "%.1f", d.battV);
      else snprintf(t, sizeof(t), "%d", d.loadPct);
      num(S, fb, i, F_NUM, t, cxs[i], cys[i] - 22, 110, have[i] ? WHITE : DIM);
    }
  } else {
    frameStyle(S, fb, pg, d);                    // arc, custom, quad, dual, gear
  }
  drawRings(S, fb);
}

// ---- brightness page: sun icon, percentage and a slider ----
constexpr int BR_X0 = 90, BR_X1 = 390, BR_Y = 322, BR_ZONE_Y0 = 276, BR_ZONE_Y1 = 368, BR_MIN = 5;   // slider track, touch zone, lowest level
inline int pctFromX(int x) { int p = BR_MIN + (x - BR_X0) * (100 - BR_MIN) / (BR_X1 - BR_X0); return p < BR_MIN ? BR_MIN : (p > 100 ? 100 : p); }
// Modern layout: a soft glow that follows the level, a line-art sun, the percentage, a glass slider whose fill glows, and four preset chips.
constexpr int BR_PRESET_Y0 = 376, BR_PRESET_Y1 = 418, BR_CHIP_W = 60, BR_CHIP_GAP = 8;
constexpr int BR_PRESETS[4] = {25, 50, 75, 100};
inline int presetX0() { return CX - (4 * BR_CHIP_W + 3 * BR_CHIP_GAP) / 2; }
// which preset (percent) is under the finger, or -1
inline int presetAt(int x, int y) {
  if (y < BR_PRESET_Y0 - 6 || y > BR_PRESET_Y1 + 6) return -1;
  for (int k = 0; k < 4; k++) { int x0 = presetX0() + k * (BR_CHIP_W + BR_CHIP_GAP); if (x >= x0 - 4 && x <= x0 + BR_CHIP_W + 4) return BR_PRESETS[k]; }
  return -1;
}
inline void chip(uint16_t *b, int x, int y, int w, int h, bool on, uint16_t col, const char *label) {
  float r = h / 2.0f;
#ifdef RUI_LAYOUT_CHECK
  { LayoutRec c{x, y, x + w - 1, y + h - 1, 'c', 0, {0}}; layoutLog().push_back(c); }
#endif
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    float px = i + 0.5f, py = j + 0.5f, qx = fabsf(px - w / 2.0f) - (w / 2.0f - r), qy = fabsf(py - h / 2.0f) - (h / 2.0f - r);
    float d = sqrtf((qx > 0 ? qx : 0) * (qx > 0 ? qx : 0) + (qy > 0 ? qy : 0) * (qy > 0 ? qy : 0)) + fminf(fmaxf(qx, qy), 0.0f) - r;
    blendPx(b, x + i, y + j, on ? col : PANEL, (int)(clamp01(0.5f - d) * 255));
    if (!on) blendPx(b, x + i, y + j, PANEL_EDGE, (int)(clamp01(0.5f - fabsf(d + 1.0f)) * 255));
  }
  textC(b, F_LBL, label, x + w / 2, y + (h - F_LBL.h) / 2, on ? BG : DIM);
}
// full = draw the whole page; otherwise only the part that changes with the level. Marks what it touched (see flushDirty).
inline void brightnessPage(uint16_t *fb, int pct, int dotN, int dotCur, bool full) {
  State &S = st(); if (!S.ok) return;
  if (full) {
    for (int i = 0; i < W * H; i++) fb[i] = BG;
    ringAA(fb, CX, CY, 235.5f, 238.5f, BEZEL);
    pageDots(fb, dotN, dotCur);
    S.page = -1;                                   // the dial pages must rebuild their static layer next time
    mark(S, 0, 0, W - 1, H - 1);
  }
  float f = (pct - BR_MIN) / (float)(100 - BR_MIN);
  uint16_t sun = lerpc(255, 160, 40, 255, 238, 170, f);

  // The page redraws on every drag step, so it only touches what changes: the sun / number area and the slider row.
  // The glow uses a lookup table (no square root per pixel) and the preset chips are redrawn only when the lit one changes.
  const int GX = CX, GY = 164, GR = 104;
  static uint8_t lut[GR * GR]; static bool lutOk = false;
  if (!lutOk) { for (int d2 = 0; d2 < GR * GR; d2++) { float t = 1.0f - sqrtf((float)d2) / GR; lut[d2] = (uint8_t)(255 * t * t); } lutOk = true; }
  const float GA = 0.06f + 0.26f * f;
  uint16_t pal[256]; for (int a = 0; a < 256; a++) pal[a] = mix(BG, sun, (int)(a * GA));
  for (int y = 60; y <= 272; y++) {
    int dy = y - GY;
    for (int x = GX - GR - 20; x < GX + GR + 20; x++) {                  // a little wider than the glow: the "%" reaches past it at 100
      int dx = x - GX, d2 = dx * dx + dy * dy;
      fb[y * W + x] = d2 < GR * GR ? pal[lut[d2]] : BG;
    }
  }
  mark(S, GX - GR - 20, 60, GX + GR + 19, 272);
  for (int y = 292; y <= 352; y++) for (int x = 48; x < 432; x++) fb[y * W + x] = BG;
  mark(S, 48, 292, 431, 352);
  textC(fb, F_LBL, "BRIGHTNESS", CX, 84, DIM);

  // sun: filled core, a thin ring and short rays that lengthen with the level
  discAA(fb, CX, GY - 6, 12, sun);
  ringAA(fb, CX, GY - 6, 19.5f, 21, mix(BG, sun, 150));
  for (int k = 0; k < 8; k++) {
    float a = k * 0.785398f, ri = 27, ro = 31 + 7 * f;
    taper(fb, CX + ri * sinf(a), GY - 6 - ri * cosf(a), CX + ro * sinf(a), GY - 6 - ro * cosf(a), 1.5f, 1.5f, sun);
  }

  // percentage
  char t[8]; snprintf(t, sizeof(t), "%d", pct);
  textC(fb, F_BIG, t, CX - 8, 218, WHITE);
  text(fb, F_MED, "%", CX - 8 + textW(F_BIG, t) / 2 + 8, 218 + F_BIG.h - F_MED.h, DIM);

  // slider: glass track, a glow under the fill, gradient fill (colour belongs to the position), knob with a halo
  const float TH = 8;
  float kx = BR_X0 + f * (BR_X1 - BR_X0);
  taper(fb, BR_X0, BR_Y, BR_X1, BR_Y, TH, TH, PANEL); discAA(fb, BR_X0, BR_Y, TH, PANEL); discAA(fb, BR_X1, BR_Y, TH, PANEL);
  taper(fb, BR_X0, BR_Y, kx, BR_Y, TH + 7, TH + 7, mix(BG, sun, 38)); discAA(fb, BR_X0, BR_Y, TH + 7, mix(BG, sun, 38));
  for (int x = BR_X0; x <= (int)kx; x++) {
    float u = (x - BR_X0) / (float)(BR_X1 - BR_X0);
    uint16_t c = lerpc(255, 140, 30, 255, 238, 170, u);
    for (int y = BR_Y - (int)TH; y < BR_Y + (int)TH; y++) fb[y * W + x] = c;
  }
  discAA(fb, BR_X0, BR_Y, TH, lerpc(255, 140, 30, 255, 140, 30, 0));
  discAA(fb, kx, BR_Y, 27, mix(BG, sun, 46));                         // halo
  discAA(fb, kx, BR_Y, 20, WHITE);
  discAA(fb, kx, BR_Y, 7, sun);
  discAA(fb, BR_X0 - 28, BR_Y, 4, FAINT);                             // dim dot at the low end, small sun at the high end
  discAA(fb, BR_X1 + 28, BR_Y, 5, DIM);
  for (int k = 0; k < 8; k++) {
    float a = k * 0.785398f;
    taper(fb, BR_X1 + 28 + 8.5f * sinf(a), BR_Y - 8.5f * cosf(a), BR_X1 + 28 + 12.5f * sinf(a), BR_Y - 12.5f * cosf(a), 1.1f, 1.1f, DIM);
  }

  // preset chips: only redrawn when the lit one changes
  int lit = -1; for (int k = 0; k < 4; k++) if (pct == BR_PRESETS[k]) lit = k;
  if (full || lit != S.brChip) {
    for (int y = 373; y <= 424; y++) for (int x = 100; x < 380; x++) fb[y * W + x] = BG;
    for (int k = 0; k < 4; k++) {
      char lb[6]; snprintf(lb, sizeof(lb), "%d", BR_PRESETS[k]);
      chip(fb, presetX0() + k * (BR_CHIP_W + BR_CHIP_GAP), BR_PRESET_Y0, BR_CHIP_W, BR_PRESET_Y1 - BR_PRESET_Y0, k == lit, sun, lb);
    }
    mark(S, 100, 373, 379, 424);
    S.brChip = (int8_t)lit;
  }
}

// ---- scanning / connecting / status page ---------------------------------------------------------------------------------
// kind: 0 scanning (radar pulses), 1 connecting (two counter-rotating arcs), 2 not found, 3 link lost, 4 anything else (spinner).
// accent is the colour the caller asked for. The animation area is redrawn every frame; the title, subtitle and the three-step
// row are drawn when full is true (page entered or text changed). Marks what it touched (see flushDirty).
constexpr int ST_AX = CX, ST_AY = 186, ST_R = 66;
// waitSec: how long this wait has lasted (-1 = do not show). After 15 s the line also says the screen can be tapped to skip it.
inline void statusPage(uint16_t *fb, int kind, uint16_t accent, const char *title, const char *sub, uint32_t now, bool full, int waitSec = -1) {
  State &S = st(); if (!S.ok) return;
  static int shownSec = -2;
  if (full) {
    shownSec = -2;
    for (int i = 0; i < W * H; i++) fb[i] = BG;
    ringAA(fb, CX, CY, 235.5f, 238.5f, BEZEL);
    textC(fb, F_TTL, title, CX, 274, WHITE);
    if (sub[0]) textC(fb, F_SML, sub, CX, 304, DIM);
    // three steps: scan -> connect -> live. The current one glows in the accent colour (error states point at the step that failed).
    int cur = kind == 0 ? 0 : kind == 1 ? 1 : kind == 2 ? 0 : kind == 3 ? 2 : -1;
    static const char *lab[3] = {"SCAN", "CONNECT", "LIVE"};
    for (int k = 0; k < 3 && cur >= 0; k++) {
      float x = CX + (k - 1) * 84.0f, y = 352;
      if (k < 2) taper(fb, x + 10, y, x + 74, y, 0.7f, 0.7f, k < cur ? mix(BG, accent, 110) : FAINT);
      if (k == cur) { discAA(fb, x, y, 8, mix(BG, accent, 60)); discAA(fb, x, y, 4.5f, accent); }
      else if (k < cur) discAA(fb, x, y, 3.5f, mix(BG, accent, 140));
      else ringAA(fb, x, y, 2, 3.5f, FAINT);
      textC(fb, F_SML, lab[k], (int)x, 366, k == cur ? WHITE : (k < cur ? DIM : FAINT));
    }
    S.page = -1;                                   // the dial pages must rebuild their static layer next time
    mark(S, 0, 0, W - 1, H - 1);
  }
  if (waitSec != shownSec) {                       // the waiting time, redrawn once a second
    shownSec = waitSec;
    for (int y = 318; y <= 334; y++) for (int x = CX - 130; x <= CX + 130; x++) fb[y * W + x] = BG;
    if (waitSec >= 0) { char t[40]; if (waitSec >= 15) snprintf(t, sizeof(t), "%d S   TAP TO SKIP", waitSec); else snprintf(t, sizeof(t), "%d S", waitSec); textC(fb, F_SML, t, CX, 320, FAINT); }
    mark(S, CX - 130, 318, CX + 130, 334);
  }
  const int AX = ST_AX, AY = ST_AY;
  for (int y = AY - ST_R; y <= AY + ST_R; y++) for (int x = AX - ST_R; x <= AX + ST_R; x++) fb[y * W + x] = BG;
  const float t = (float)now;
  // a comet: a bright head with a fading tail running round a ring (dir +1 clockwise, -1 counter-clockwise)
  auto comet = [&](float r0, float r1, float speed, int dir, float tail, float trackA) {
    float head = fmodf(t * speed, 360.0f); if (dir < 0) head = 360.0f - head;
    annulus(AX, AY, r0, r1, [&](int x, int y, float d, float dx, float dy) {
      float th = atan2f(dx, -dy) * 57.29578f; if (th < 0) th += 360.0f;
      float delta = dir > 0 ? head - th : th - head; delta = fmodf(delta + 720.0f, 360.0f);
      float a = delta < tail ? (1.0f - delta / tail) : 0.0f; a = a * a;
      a = trackA + (1.0f - trackA) * a;
      float edge = clamp01(fminf(d - r0, r1 - d) + 0.5f);
      blendPx(fb, x, y, mix(BG, accent, (int)(a * 255)), (int)(edge * 255));
    });
  };
  const float pulse = 0.5f + 0.5f * sinf(t * 0.004f);
  if (kind == 0) {                                  // radar: two pulses spreading out from a dot, plus a slow ring
    for (int k = 0; k < 2; k++) {
      float ph = fmodf(t / 2200.0f + k * 0.5f, 1.0f), r = 8 + 52 * ph, a = (1.0f - ph) * (1.0f - ph) * 0.85f;
      ringAA(fb, AX, AY, r - 0.9f, r + 0.9f, mix(BG, accent, (int)(a * 255)));
    }
    comet(40, 44, 0.20f, 1, 110, 0.10f);
    discAA(fb, AX, AY, 9, mix(BG, accent, 50)); discAA(fb, AX, AY, 4.5f, accent);
  } else if (kind == 1) {                           // connecting: outer arc clockwise, inner arc the other way, a breathing core
    comet(46, 50, 0.34f, 1, 150, 0.10f);
    comet(29, 33, 0.46f, -1, 120, 0.10f);
    discAA(fb, AX, AY, 7 + 2 * pulse, mix(BG, accent, 60)); discAA(fb, AX, AY, 4, accent);
  } else if (kind == 2) {                           // not found: a quiet ring and an exclamation mark
    ringAA(fb, AX, AY, 41, 44.5f, mix(BG, accent, (int)((0.22f + 0.18f * pulse) * 255)));
    taper(fb, AX, AY - 20, AX, AY + 3, 3.8f, 2.8f, accent); discAA(fb, AX, AY + 15, 3.8f, accent);
  } else if (kind == 3) {                           // link lost: a broken ring and a cross
    uint16_t c = mix(BG, accent, (int)((0.55f + 0.30f * pulse) * 255));
    arcAA(fb, AX, AY, 41, 44.5f, 28, 152, c); arcAA(fb, AX, AY, 41, 44.5f, -152, -28, c);
    taper(fb, AX - 11, AY - 11, AX + 11, AY + 11, 3.2f, 3.2f, accent); taper(fb, AX + 11, AY - 11, AX - 11, AY + 11, 3.2f, 3.2f, accent);
  } else {
    comet(40, 44.5f, 0.30f, 1, 150, 0.10f);
    discAA(fb, AX, AY, 4, accent);
  }
  mark(S, AX - ST_R, AY - ST_R, AX + ST_R, AY + ST_R);
}

}  // namespace rui
