#pragma once
// Revsense welcome screen: the logo's gauge draws itself (ring, arc, needle sweep, hub pulse), then the wordmark fades in.
// Pure drawing into the 480x480 RGB565 frame buffer through the round-UI primitives, so it is previewed and tested on a PC
// (tools/preview/brand_preview.cpp). frame() only touches two boxes and marks them dirty, so the board sends little per frame.
#include "round_ui.h"
#include "brand_logo.h"

namespace brand {

constexpr uint32_t DURATION_MS = 3400;                 // the whole welcome, tap skips it
constexpr float CXG = 240, CYG = 205;                  // gauge centre
constexpr float RING_R0 = 119, RING_R1 = 130;          // white ring
constexpr float ARC_R0 = 92.5f, ARC_R1 = 107.5f;       // the arc band (grey track, red fill)
constexpr float NEEDLE_R0 = 18, NEEDLE_R1 = 88, HUB_R = 13;
constexpr float REST_LEVEL = 0.61f;                    // where the needle rests, as in the logo
constexpr int WM_X = 240 - brandlogo::WM_W / 2, WM_Y = 385 - brandlogo::WM_H / 2;   // wordmark position
constexpr int GX0 = 100, GY0 = 65, GX1 = 380, GY1 = 345;                          // box of the gauge
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
inline void roundCap(uint16_t *fb, float r, float deg, float w, uint16_t c) {
  float x, y; polarG(r, deg, &x, &y); rui::discAA(fb, x, y, w / 2, c);
}

// Draws frame t (ms since the welcome started). Returns false once the welcome is over.
inline bool frame(uint16_t *fb, uint32_t t) {
  if (t >= DURATION_MS) return false;
  const uint16_t BG = rui::BG;
  for (int y = GY0; y <= GY1; y++) for (int x = GX0; x <= GX1; x++) fb[y * rui::W + x] = BG;
  for (int y = WY0; y <= WY1; y++) for (int x = WX0; x <= WX1; x++) fb[y * rui::W + x] = BG;

  // ring: draws on clockwise from 12 o'clock during the first 0.6 s
  float rp = seg(t, 0.0f, 0.6f);
  if (rp >= 0.999f) rui::ringAA(fb, CXG, CYG, RING_R0, RING_R1, rui::WHITE);
  else if (rp > 0.002f) {
    float e = 360.0f * rp;                                   // arcAA measures -180..180, so a sweep past 6 o'clock is drawn in two parts
    if (e <= 180.0f) rui::arcAA(fb, CXG, CYG, RING_R0, RING_R1, 0, e, rui::WHITE);
    else { rui::arcAA(fb, CXG, CYG, RING_R0, RING_R1, 0, 180.0f, rui::WHITE); rui::arcAA(fb, CXG, CYG, RING_R0, RING_R1, -180.0f, e - 360.0f, rui::WHITE); }
    roundCap(fb, (RING_R0 + RING_R1) / 2, 0, RING_R1 - RING_R0, rui::WHITE); roundCap(fb, (RING_R0 + RING_R1) / 2, 360.0f * rp, RING_R1 - RING_R0, rui::WHITE);
  }
  // arc track, red fill, needle and hub appear once the ring is nearly closed
  if (t >= 450) {
    float lv = level(t), w = ARC_R1 - ARC_R0, rm = (ARC_R0 + ARC_R1) / 2;
    rui::arcAA(fb, CXG, CYG, ARC_R0, ARC_R1, -135, 135, track()); roundCap(fb, rm, -135, w, track()); roundCap(fb, rm, 135, w, track());
    float a = -135 + 270 * lv;
    if (lv > 0.002f) { rui::arcAA(fb, CXG, CYG, ARC_R0, ARC_R1, -135, a, red()); roundCap(fb, rm, -135, w, red()); roundCap(fb, rm, a, w, red()); }
    float bx, by, tx, ty; polarG(NEEDLE_R0, a, &bx, &by); polarG(NEEDLE_R1, a, &tx, &ty);
    rui::taper(fb, bx, by, tx, ty, 3.5f, 3.5f, rui::WHITE); rui::discAA(fb, tx, ty, 3.5f, rui::WHITE);
    float pulse = 1.0f; if (t > 2000 && t < 2500) pulse = 1.0f + 0.25f * sinf((t - 2000) / 500.0f * 3.14159f);
    rui::discAA(fb, CXG, CYG, HUB_R * pulse, red());
  }
  // wordmark: "Rev" white, "sense" red, fades in from 1.8 s to 2.4 s
  float wa = seg(t, 1.8f, 2.4f);
  if (wa > 0.002f) {
    for (int j = 0; j < brandlogo::WM_H; j++) for (int i = 0; i < brandlogo::WM_W; i++) {
      int a8 = brandlogo::WM_A[j * brandlogo::WM_W + i]; if (!a8) continue;
      rui::blendPx(fb, WM_X + i, WM_Y + j, i < brandlogo::WM_SPLIT ? rui::WHITE : red(), (int)(a8 * wa));
    }
  }
  // fade out over the last 0.4 s: both boxes dim to the background
  float fo = seg(t, 3.0f, 3.4f);
  if (fo > 0.002f) {
    int a = (int)(fo * 255);
    for (int y = GY0; y <= GY1; y++) for (int x = GX0; x <= GX1; x++) fb[y * rui::W + x] = rui::mix(fb[y * rui::W + x], BG, a);
    for (int y = WY0; y <= WY1; y++) for (int x = WX0; x <= WX1; x++) fb[y * rui::W + x] = rui::mix(fb[y * rui::W + x], BG, a);
  }
  rui::mark(rui::st(), GX0, GY0, GX1, GY1);
  rui::mark(rui::st(), WX0, WY0, WX1, WY1);
  return true;
}

}  // namespace brand
