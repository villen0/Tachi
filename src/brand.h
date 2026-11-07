#pragma once
// Revsense welcome screen: the logo's gauge draws itself (ring, arc, needle sweep, hub pulse), then the wordmark fades in.
// Pure drawing into the 480x480 RGB565 frame buffer through the round-UI primitives, so it is previewed and tested on a PC
// (tools/preview/brand_preview.cpp). It is incremental: every frame only the pixels that changed are redrawn and marked dirty
// (the arc uses the round UI's ring segments, the needle its own row spans, the ring draws on as a thin sliver), so the board
// sends a few thousand pixels per frame instead of the whole gauge and the sweep stays smooth.
#include "round_ui.h"
#include "brand_logo.h"

namespace brand {

constexpr uint32_t DURATION_MS = 3000;                 // the whole welcome, tap skips it
constexpr float CXG = 240, CYG = 205;                  // gauge centre
constexpr float RING_R0 = 119, RING_R1 = 130;          // white ring
constexpr float ARC_R0 = 92.5f, ARC_R1 = 107.5f;       // the arc band (grey track, red fill)
constexpr float NEEDLE_R0 = 18, NEEDLE_R1 = 82, HUB_R = 13;
constexpr float REST_LEVEL = 0.61f;                    // where the needle rests, as in the logo
constexpr int WM_X = 240 - brandlogo::WM_W / 2, WM_Y = 385 - brandlogo::WM_H / 2;   // wordmark position
constexpr int WX0 = WM_X - 2, WY0 = WM_Y - 2, WX1 = WM_X + brandlogo::WM_W + 2, WY1 = WM_Y + brandlogo::WM_H + 2;   // box of the wordmark

inline uint16_t red() { return rui::rgb(231, 70, 42); }
inline uint16_t track() { return rui::rgb(74, 74, 78); }
inline float ease(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return t * t * (3 - 2 * t); }
inline float seg(uint32_t t, float a, float b) { return ease(((float)t / 1000.0f - a) / (b - a)); }   // 0..1 progress between a and b seconds

// needle / arc level (0..1 of the 270 degree sweep): 0 until 0.5 s, up to the redline by 1.5 s, back to the rest position by 2.1 s
inline float level(uint32_t t) {
  float up = seg(t, 0.5f, 1.5f), back = seg(t, 1.5f, 2.1f);
  return up - (1.0f - REST_LEVEL) * back;
}

inline void polarG(float r, float deg, float *x, float *y) { float t = deg * 0.0174533f; *x = CXG + r * sinf(t); *y = CYG - r * cosf(t); }   // degrees clockwise from 12 o'clock, around the gauge centre
inline void cap(uint16_t *fb, float r, float deg, float w, uint16_t c) {
  float x, y; polarG(r, deg, &x, &y); rui::discAA(fb, x, y, w / 2, c);
  rui::mark(rui::st(), (int)(x - w / 2 - 2), (int)(y - w / 2 - 2), (int)(x + w / 2 + 2), (int)(y + w / 2 + 2));
}

// white ring, drawn on a piece at a time: the part between degrees t0 and t1 (clockwise from 12 o'clock), only inside its own small box
inline void ringSliver(uint16_t *fb, float t0, float t1) {
  float tm = (t0 + t1) * 0.5f, xm, ym; polarG((RING_R0 + RING_R1) / 2, tm, &xm, &ym);
  float half = (RING_R1 + RING_R0) * 0.5f * (t1 - t0) * 0.0174533f * 0.5f + 16;
  int xa = (int)floorf(xm - half), xb = (int)ceilf(xm + half), ya = (int)floorf(ym - half), yb = (int)ceilf(ym + half);
  for (int y = ya < 0 ? 0 : ya; y <= yb && y < rui::H; y++) for (int x = xa < 0 ? 0 : xa; x <= xb && x < rui::W; x++) {
    float dx = x + 0.5f - CXG, dy = y + 0.5f - CYG, d = sqrtf(dx * dx + dy * dy);
    if (d < RING_R0 - 1 || d > RING_R1 + 1) continue;
    float th = atan2f(dx, -dy) * 57.29578f; if (th < 0) th += 360.0f;
    float e = fminf(th - t0, t1 - th) * 0.0174533f * d;                     // soft end of the sliver (the next piece overlaps it, so no seam)
    rui::blendPx(fb, x, y, rui::WHITE, (int)(rui::clamp01(fminf(d - RING_R0, RING_R1 - d) + 0.5f) * rui::clamp01(e + 0.5f) * 255));
  }
  rui::mark(rui::st(), xa, ya, xb, yb);
}

struct Run {                                           // what has been drawn so far
  bool inited = false; uint32_t lastT = 0; float ringDone = 0; bool arcOn = false, capsRed = false, endRed = false, tipRound = false; float needleQ = 1e9f; int pulseQ = -1; int wmQ = -1; float tipX = -100, tipY = -100;
  int16_t nmin[rui::H], nmax[rui::H]; int nr0 = 0, nr1 = -1;
};
inline Run &run() { static Run r; return r; }

inline void reset(uint16_t *fb) {
  rui::State &S = rui::st(); Run &R = run();
  R.inited = true; R.ringDone = 0; R.arcOn = false; R.capsRed = false; R.endRed = false; R.tipRound = false; R.needleQ = 1e9f; R.pulseQ = -1; R.wmQ = -1; R.tipX = R.tipY = -100; R.nr0 = 0; R.nr1 = -1;
  for (int i = 0; i < rui::W * rui::H; i++) { fb[i] = rui::BG; S.bg[i] = rui::BG; }          // the background under the needle is plain black
  S.nmap = 0; S.nring = 0;
  rui::buildRing(S, 0, CXG, CYG, ARC_R0, ARC_R1, 128, -0.6f); S.nring = 1;
  rui::Ring &Rg = S.ring[0];
  for (int i = 0; i < Rg.nseg; i++) { Rg.lit[i] = red(); Rg.dim[i] = track(); }
  Rg.cur = 0; Rg.prev = -1; S.forceRings = true;
  rui::mark(S, 0, 0, rui::W - 1, rui::H - 1);
}

// Draws frame t (ms since the welcome started). Returns false once the welcome is over.
inline bool frame(uint16_t *fb, uint32_t t) {
  rui::State &S = rui::st(); Run &R = run();
  if (!R.inited || t < R.lastT) reset(fb);
  R.lastT = t;
  if (t >= DURATION_MS) {
    S.page = -1; S.nring = 0; S.nmap = 0; R.inited = false;                                 // the dial pages rebuild their own layers
    return false;
  }
  // ring: draws on clockwise from 12 o'clock during the first 0.6 s, a piece per frame
  float rp = seg(t, 0.0f, 0.6f), want = 360.0f * rp;
  if (want > R.ringDone + 0.2f || (rp >= 0.999f && R.ringDone < 360.0f)) {
    float a0 = R.ringDone > 3 ? R.ringDone - 3 : 0, a1 = rp >= 0.999f ? 360.0f : want;
    if (R.ringDone == 0) cap(fb, (RING_R0 + RING_R1) / 2, 0, RING_R1 - RING_R0, rui::WHITE);
    if (a1 > a0) ringSliver(fb, a0, a1);
    R.ringDone = a1;
  }
  // arc track, red fill, needle and hub appear once the ring is nearly closed
  if (t >= 450) {
    float lv = level(t), w = ARC_R1 - ARC_R0, rm = (ARC_R0 + ARC_R1) / 2;
    if (!R.arcOn) { R.arcOn = true; cap(fb, rm, -135, w, track()); cap(fb, rm, 135, w, track()); rui::mark(S, (int)CXG - 110, (int)CYG - 110, (int)CXG + 110, (int)CYG + 110); }   // the first draw of the whole track
    rui::ringLevel(S, 0, lv);
    rui::drawRings(S, fb);                                                                   // only the segments that changed
    if (lv > 0.01f && !R.capsRed) { R.capsRed = true; cap(fb, rm, -135, w, red()); }        // rounded start of the red arc
    bool endNow = lv >= 0.995f;
    if (endNow != R.endRed) { R.endRed = endNow; cap(fb, rm, 135, w, endNow ? red() : track()); }
    if (t >= 2100 && lv > 0.01f && !R.tipRound) { R.tipRound = true; cap(fb, rm, -135 + 270 * lv, w, red()); }   // the sweep is over: round the tip of the arc (once)
    float a = -135 + 270 * lv, q = roundf(a * 4) / 4;
    float pulse = 1.0f; if (t > 2000 && t < 2500) pulse = 1.0f + 0.25f * sinf((t - 2000) / 500.0f * 3.14159f);
    int pq = (int)(pulse * 20);
    if (q != R.needleQ || pq != R.pulseQ) {
      R.needleQ = q; R.pulseQ = pq;
      for (int y = R.nr0; y <= R.nr1; y++)                                                    // put the black back under last frame's needle, row by row
        if (R.nmax[y] >= R.nmin[y]) { rui::restore(fb, S.bg, R.nmin[y] - 1, y, R.nmax[y] - R.nmin[y] + 3, 1); rui::mark(S, R.nmin[y] - 1, y, R.nmax[y] + 1, y); }
      rui::restore(fb, S.bg, (int)(R.tipX - 5), (int)(R.tipY - 5), 10, 10); rui::mark(S, (int)(R.tipX - 5), (int)(R.tipY - 5), (int)(R.tipX + 5), (int)(R.tipY + 5));   // the round tip of last frame's needle
      rui::restore(fb, S.bg, (int)CXG - 20, (int)CYG - 20, 40, 40); rui::mark(S, (int)CXG - 20, (int)CYG - 20, (int)CXG + 20, (int)CYG + 20);
      float bx, by, tx, ty; polarG(NEEDLE_R0, q, &bx, &by); polarG(NEEDLE_R1, q, &tx, &ty);
      rui::Box b = rui::taper(fb, bx, by, tx, ty, 3.5f, 3.5f, rui::WHITE, R.nmin, R.nmax); rui::discAA(fb, tx, ty, 3.5f, rui::WHITE); R.tipX = tx; R.tipY = ty;
      rui::discAA(fb, CXG, CYG, HUB_R * pulse, red());
      R.nr0 = b.y0 < 0 ? 0 : b.y0; R.nr1 = b.y1 >= rui::H ? rui::H - 1 : b.y1;
      for (int y = R.nr0; y <= R.nr1; y++) if (R.nmax[y] >= R.nmin[y]) { rui::mark(S, R.nmin[y] - 1, y, R.nmax[y] + 1, y); }
      rui::mark(S, (int)(tx - 5), (int)(ty - 5), (int)(tx + 5), (int)(ty + 5));              // the round tip of the needle
    }
  }
  // wordmark: "Rev" white, "sense" red, fades in from 1.8 s to 2.4 s (the box is cleared and blended again only while it changes)
  float wa = seg(t, 1.8f, 2.4f); int wq = (int)(wa * 40);
  if (wq != R.wmQ && wa > 0.002f) {
    R.wmQ = wq;
    for (int y = WY0; y <= WY1; y++) for (int x = WX0; x <= WX1; x++) fb[y * rui::W + x] = rui::BG;
    for (int j = 0; j < brandlogo::WM_H; j++) for (int i = 0; i < brandlogo::WM_W; i++) {
      int a8 = brandlogo::WM_A[j * brandlogo::WM_W + i]; if (!a8) continue;
      rui::blendPx(fb, WM_X + i, WM_Y + j, i < brandlogo::WM_SPLIT ? rui::WHITE : red(), (int)(a8 * wq / 40));
    }
    rui::mark(S, WX0, WY0, WX1, WY1);
  }
  return true;
}

}  // namespace brand
