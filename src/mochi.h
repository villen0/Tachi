#pragma once
// Mochi-style animated eyes for Tachi.
//
// Platform independent (no Arduino includes) so the same code runs on the ESP32 and in the host preview
// tool (tools/preview). Drawing goes through a template parameter G that must offer the Arduino_GFX calls
//   fillRect, fillRoundRect, fillCircle, fillEllipse, fillTriangle, drawLine
// with the same signatures as Arduino_GFX. Time is always passed in (milliseconds), nothing here reads a clock.
//
// Look: black screen, two glowing rounded-rectangle eyes and a tiny mouth; expressions are poses that the
// eyes ease into (not swapped pictures). Original drawing code and wording; not a copy of any commercial
// device's artwork.
#include <stdint.h>
#include <math.h>
#include <string.h>

namespace mochi {

constexpr int W = 172;   // canvas size; the face lives in this rectangle
constexpr int H = 180;

inline uint16_t rgb(int r, int g, int b) {
  r = r < 0 ? 0 : (r > 255 ? 255 : r);
  g = g < 0 ? 0 : (g > 255 ? 255 : g);
  b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

enum Expr : uint8_t {
  E_DEFAULT, E_SHRINK, E_LOOK_L, E_LOOK_R, E_LOOK_DOWN, E_RASPBERRY, E_LOVE, E_ANGRY, E_CRYING, E_STARS,
  E_SMIRK, E_SLEEPY, E_SNEEZE, E_LAUGH, E_SQUINT, E_DISTRACTED, E_UWU, E_COOKIE, E_TURBO, E_HEADLIGHTS,
  E_POLICE, E_GLITCH, E_SWEAT, E_STARTLE, E_WAKE, E_TITLE, E_RAINBOW, E_COUNT
};
inline const char *exprName(Expr e) {
  static const char *n[E_COUNT] = {"Default", "Shrink", "LookLeft", "LookRight", "LookDown", "Raspberry", "Love", "Angry",
    "Crying", "Stars", "Smirk", "Sleepy", "Sneeze", "Laugh", "Squint", "Distracted", "UwU", "Cookie", "Turbo",
    "Headlights", "Police", "Glitch", "Sweat", "Startle", "Wake", "Title", "Rainbow"};
  return e < E_COUNT ? n[e] : "?";
}

enum EyeStyle : uint8_t { ES_RECT, ES_ARC_UP, ES_U, ES_CHEVRON, ES_HEART, ES_TURBINE, ES_BEAM, ES_SIREN, ES_POLY, ES_NONE };
enum Mouth : uint8_t { M_NONE, M_SMILE, M_FLAT, M_OPEN, M_TONGUE, M_FROWN, M_O, M_CAT, M_CHOMP, M_ZIGZAG };
enum Overlay : uint8_t { O_NONE, O_ANGRY_MARK, O_TEARS, O_STARS, O_ZZZ, O_SWEAT, O_DROPLET, O_GLITCH, O_TITLE, O_BLUSH };

// All members are floats so a Pose can be eased field by field.
struct Eye {
  float w, h, r;            // size and corner radius
  float x, y;               // offset from the pair's position
  float slantIn, slantOut;  // top lid cuts (px dropped at the inner / outer top corner): angry / sad
  float lidTop, lidBot;     // fraction of eye height covered by the upper / lower lid (0..1)
};
struct Pose {
  Eye L, R;
  float gap;                // distance between the eye centres
  float ox, oy;             // look offset of the whole face
  float mouthW, mouthC, mouthY;  // mouth width, curve (+ smile / - frown) and y below the eye line
  float mouthOpen;          // 0..1 for open mouths
  float ovl;                // overlay strength 0..1
  float glow;               // glow intensity 0..1
  float cr, cg, cb;         // eye colour
  float gr, gg, gb;         // glow colour (soft halo around the eye)
};
static_assert(sizeof(Pose) % sizeof(float) == 0, "Pose must be all floats");

struct ExprDef { Pose pose; EyeStyle style; Mouth mouth; Overlay ovl; bool blinks; };

inline Eye baseEye() { return Eye{34, 54, 14, 0, 0, 0, 0, 0, 0}; }
inline Pose basePose() {
  Pose p;
  p.L = baseEye(); p.R = baseEye();
  p.gap = 66; p.ox = 0; p.oy = 0;
  p.mouthW = 16; p.mouthC = 5; p.mouthY = 22; p.mouthOpen = 0;
  p.ovl = 0; p.glow = 1;
  p.cr = 205; p.cg = 235; p.cb = 255;
  p.gr = 50; p.gg = 130; p.gb = 255;
  return p;
}
inline void setColor(Pose &p, float r, float g, float b, float gr, float gg, float gb) { p.cr = r; p.cg = g; p.cb = b; p.gr = gr; p.gg = gg; p.gb = gb; }
inline void both(Pose &p, float Eye::*f, float v) { p.L.*f = v; p.R.*f = v; }

inline ExprDef def(Expr e) {
  ExprDef d{basePose(), ES_RECT, M_SMILE, O_NONE, true};
  Pose &p = d.pose;
  switch (e) {
    case E_DEFAULT: break;
    case E_SHRINK:  both(p, &Eye::w, 26); both(p, &Eye::h, 40); both(p, &Eye::r, 11); p.gap = 56; p.mouthY = 18; p.mouthW = 12; break;
    case E_LOOK_L:  p.ox = -18; p.oy = -2; p.mouthC = 4; break;
    case E_LOOK_R:  p.ox = 18; p.oy = -2; p.mouthC = 4; break;
    case E_LOOK_DOWN: p.oy = 16; both(p, &Eye::h, 36); both(p, &Eye::lidTop, 0.1f); p.mouthC = 2; p.mouthY = 16; break;
    case E_RASPBERRY: d.style = ES_CHEVRON; d.mouth = M_TONGUE; p.mouthW = 20; p.mouthY = 24; break;
    case E_LOVE:    d.style = ES_HEART; d.mouth = M_OPEN; d.ovl = O_BLUSH; setColor(p, 255, 95, 150, 255, 40, 110); p.mouthW = 14; p.mouthOpen = 0.5f; p.mouthY = 26; break;
    case E_ANGRY:   p.L.slantIn = p.R.slantIn = 22; both(p, &Eye::h, 50); d.mouth = M_FROWN; p.mouthC = -7; p.mouthY = 26; d.ovl = O_ANGRY_MARK; setColor(p, 255, 110, 90, 255, 50, 30); break;
    case E_CRYING:  p.L.slantOut = p.R.slantOut = 16; both(p, &Eye::lidTop, 0.10f); both(p, &Eye::h, 58); d.mouth = M_FROWN; p.mouthC = -5; p.mouthW = 16; p.mouthY = 28; d.ovl = O_TEARS; setColor(p, 150, 228, 255, 40, 120, 255); break;
    case E_STARS:   both(p, &Eye::w, 32); both(p, &Eye::h, 58); d.ovl = O_STARS; p.mouthC = 7; break;
    case E_SMIRK:   p.L.lidTop = 0.36f; p.L.slantIn = 9; p.R.h = 50; p.mouthC = 3; p.ox = 5; p.mouthW = 16; break;
    case E_SLEEPY:  both(p, &Eye::lidTop, 0.92f); d.mouth = M_O; p.mouthW = 10; p.mouthY = 22; d.ovl = O_ZZZ; setColor(p, 160, 195, 235, 40, 80, 170); d.blinks = false; break;
    case E_SNEEZE:  d.style = ES_CHEVRON; d.mouth = M_OPEN; p.mouthW = 16; p.mouthOpen = 0.8f; p.mouthY = 26; d.ovl = O_DROPLET; break;
    case E_LAUGH:   d.style = ES_ARC_UP; d.mouth = M_OPEN; p.mouthW = 24; p.mouthOpen = 1.0f; p.mouthY = 24; d.blinks = false; break;
    case E_SQUINT:  both(p, &Eye::lidTop, 0.32f); both(p, &Eye::lidBot, 0.32f); p.mouthC = 4; break;
    case E_DISTRACTED: both(p, &Eye::w, 31); both(p, &Eye::h, 46); p.mouthC = 0; p.mouthW = 12; p.mouthY = 20; break;
    case E_UWU:     d.style = ES_U; d.mouth = M_CAT; p.mouthW = 20; p.mouthY = 22; d.blinks = false; d.ovl = O_BLUSH; break;
    case E_COOKIE:  both(p, &Eye::w, 31); both(p, &Eye::h, 44); d.mouth = M_CHOMP; p.mouthW = 16; p.mouthY = 24; p.ox = -4; break;
    case E_TURBO:   d.style = ES_TURBINE; both(p, &Eye::w, 46); both(p, &Eye::h, 46); p.gap = 70; d.mouth = M_FLAT; p.mouthW = 16; p.mouthC = 0; p.mouthY = 38; d.blinks = false; break;
    case E_HEADLIGHTS: d.style = ES_BEAM; p.gap = 80; d.mouth = M_SMILE; p.mouthY = 38; p.mouthW = 14; d.blinks = false; break;
    case E_POLICE:  d.style = ES_SIREN; p.gap = 74; d.mouth = M_FLAT; p.mouthW = 14; p.mouthC = 0; p.mouthY = 38; d.blinks = false; break;
    case E_GLITCH:  d.style = ES_POLY; p.L.slantIn = p.R.slantIn = 20; both(p, &Eye::h, 46); both(p, &Eye::w, 38); d.mouth = M_ZIGZAG; p.mouthW = 50; p.mouthY = 34; d.ovl = O_GLITCH; setColor(p, 255, 255, 255, 255, 120, 30); d.blinks = false; break;
    case E_SWEAT:   p.L.slantOut = p.R.slantOut = 10; both(p, &Eye::h, 50); d.mouth = M_FLAT; p.mouthW = 14; p.mouthC = 0; p.mouthY = 26; d.ovl = O_SWEAT; setColor(p, 240, 238, 170, 200, 170, 40); break;
    case E_STARTLE: both(p, &Eye::w, 38); both(p, &Eye::h, 70); both(p, &Eye::r, 18); d.mouth = M_O; p.mouthW = 12; p.mouthY = 34; break;
    case E_WAKE:    break;   // the entry logic closes the eyes first, then they open to this pose
    case E_TITLE:   d.style = ES_NONE; d.mouth = M_NONE; d.ovl = O_TITLE; d.blinks = false; break;
    case E_RAINBOW: both(p, &Eye::w, 36); both(p, &Eye::h, 56); p.mouthC = 6; break;
    default: break;
  }
  return d;
}

// ---- small helpers -------------------------------------------------------------------------------
struct Rng {
  uint32_t s = 2463534242u;
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  int range(int lo, int hi) { return lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
};
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline uint16_t scaled(const Pose &p, float k) { return rgb((int)(p.cr * k), (int)(p.cg * k), (int)(p.cb * k)); }
inline uint16_t glowc(const Pose &p, float k) { return rgb((int)(p.gr * k), (int)(p.gg * k), (int)(p.gb * k)); }
inline void hsv(float h, float &r, float &g, float &b) {   // h in 0..1, full saturation/value
  h = h - floorf(h); float f = h * 6; int i = (int)f; float q = f - i;
  switch (i % 6) { case 0: r = 1; g = q; b = 0; break; case 1: r = 1 - q; g = 1; b = 0; break; case 2: r = 0; g = 1; b = q; break;
    case 3: r = 0; g = 1 - q; b = 1; break; case 4: r = q; g = 0; b = 1; break; default: r = 1; g = 0; b = 1 - q; break; }
}

// 5x7 pixel font for the boot title and "Z" (only the letters we need)
inline const uint8_t *glyph(char c) {
  static const uint8_t T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
  static const uint8_t A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  static const uint8_t C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
  static const uint8_t Hh[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  static const uint8_t I[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
  switch (c) { case 'T': return T; case 'A': return A; case 'C': return C; case 'H': return Hh; case 'I': return I; default: return nullptr; }
}

// ---- the character --------------------------------------------------------------------------------
class Mochi {
 public:
  void begin(uint32_t seed = 1) {
    rng.s = seed ? seed : 1;
    base = E_DEFAULT; cur = tgt = def(E_DEFAULT).pose; style = ES_RECT; mouth = M_SMILE; ovl = O_NONE; blinks = true;
    reaction = E_COUNT; reactionEnd = 0; last = 0; nextBlink = 1500; blinkStart = 0; nextIdle = 6000; expr = E_DEFAULT;
    wanderAt = 0; flip = false;
  }

  // The mood the face returns to after a reaction ends.
  void setBase(Expr e, uint32_t now) { base = e; if (reaction == E_COUNT && !intro) apply(e, now); }
  // One-off reaction; goes back to the base afterwards (holdMs == 0: stays until another call).
  void react(Expr e, uint32_t holdMs, uint32_t now) {
    if (intro) return;
    reaction = e; reactionEnd = holdMs ? now + holdMs : 0; apply(e, now);
    if (!holdMs) base = e, reaction = E_COUNT;
  }
  // Scripted welcome animation (about 4.4 s): glowing line -> eyes open -> look around -> happy eyes + "TACHI"
  // written letter by letter -> settle into the normal face. Idle/reactions are held back while it runs.
  void startIntro(uint32_t now) { intro = true; introT0 = now; introStep = -1; titleN = 0; titleA = 1.0f; reaction = E_COUNT; blinks = false; blinkStart = 0;
    cur = def(E_WAKE).pose; cur.L.lidTop = cur.R.lidTop = 1.0f; tgt = cur; expr = E_WAKE; style = ES_RECT; mouth = M_NONE; ovl = O_NONE; }
  bool introActive() const { return intro; }
  bool reacting() const { return reaction != E_COUNT || intro; }
  Expr current() const { return expr; }

  // Occasionally picks a random idle reaction. parked = the car is not moving (wilder ones allowed).
  void idleTick(uint32_t now, bool parked) {
    if (intro || reaction != E_COUNT || base != E_DEFAULT) { if (now > nextIdle) nextIdle = now + 4000; return; }
    if (now < nextIdle) return;
    static const Expr calmPool[] = {E_LOOK_L, E_LOOK_R, E_LOOK_DOWN, E_SMIRK, E_SQUINT, E_DISTRACTED, E_UWU, E_COOKIE, E_SNEEZE,
                                    E_RASPBERRY, E_LOVE, E_LAUGH, E_STARS, E_SHRINK};
    static const uint16_t calmHold[] = {1400, 1400, 1400, 2200, 2000, 3000, 2500, 3000, 1800, 1800, 2500, 2200, 2200, 1500};
    static const Expr wildPool[] = {E_POLICE, E_HEADLIGHTS, E_GLITCH, E_RAINBOW};
    static const uint16_t wildHold[] = {3000, 2500, 1800, 3000};
    if (parked && rng.range(0, 3) == 0) { int i = rng.range(0, 3); react(wildPool[i], wildHold[i], now); }
    else { int i = rng.range(0, (int)(sizeof(calmPool) / sizeof(calmPool[0])) - 1); react(calmPool[i], calmHold[i], now); }
    nextIdle = now + (uint32_t)rng.range(6000, 15000);
  }

  void update(uint32_t now) {
    uint32_t dt = last ? now - last : 16;
    last = now;
    if (dt < 1) dt = 1;
    if (dt > 100) dt = 100;
    if (intro) runIntro(now);
    if (!intro && reaction != E_COUNT && reactionEnd && now >= reactionEnd) { reaction = E_COUNT; apply(base, now); }
    if (expr == E_DISTRACTED && now >= wanderAt) {   // eyes wander around
      tgt.ox = (float)rng.range(-16, 16); tgt.oy = (float)rng.range(-8, 8);
      tgt.L.h = (float)rng.range(44, 54); tgt.R.h = (float)rng.range(44, 54);
      wanderAt = now + (uint32_t)rng.range(350, 800);
    }
    float tau = (expr == E_WAKE) ? 160.0f : 75.0f;
    float k = 1.0f - expf(-(float)dt / tau);
    float *c = (float *)&cur; const float *t = (const float *)&tgt;
    for (size_t i = 0; i < sizeof(Pose) / sizeof(float); i++) c[i] += (t[i] - c[i]) * k;
    // blink: close 60 ms, hold 40 ms, open 90 ms
    if (blinks && now >= nextBlink && blinkStart == 0) blinkStart = now ? now : 1;
    blink = 1.0f;
    if (blinkStart) {
      uint32_t e = now - blinkStart;
      if (e < 60) blink = 1.0f - 0.92f * (e / 60.0f);
      else if (e < 100) blink = 0.08f;
      else if (e < 190) blink = 0.08f + 0.92f * ((e - 100) / 90.0f);
      else { blinkStart = 0; nextBlink = now + (uint32_t)rng.range(2200, 5200); }
    }
  }

  // Draws the whole face into g (canvas coordinates 0..W-1, 0..H-1). Clears the canvas first.
  template <class G> void render(G *g, uint32_t now) const {
    g->fillRect(0, 0, W, H, 0);
    if (intro && introStep < 0) { drawPowerBar(g, now - introT0); return; }
    if (ovl == O_GLITCH) drawGlitchBackground(g, now);
    if (ovl == O_STARS) drawStarfield(g, now);
    if (ovl == O_TITLE) { drawTitle(g, now); return; }
    float breath = 1.0f + 0.015f * sinf(now / 700.0f);
    Pose p = cur;
    if (expr == E_RAINBOW) { float r, gg, b; hsv(now / 1800.0f, r, gg, b); p.cr = 90 + 165 * r; p.cg = 90 + 165 * gg; p.cb = 90 + 165 * b; }
    float cy = H / 2 - 6 + p.oy;
    float lx = W / 2 - p.gap / 2 + p.ox + p.L.x, rx = W / 2 + p.gap / 2 + p.ox + p.R.x;
    drawEye(g, p, p.L, lx, cy + p.L.y, true, breath, now);
    drawEye(g, p, p.R, rx, cy + p.R.y, false, breath, now);
    drawMouth(g, p, W / 2 + p.ox * 0.6f, cy + p.mouthY, now);
    drawOverlay(g, p, lx, rx, cy, now);
    if (intro && titleN > 0) drawIntroTitle(g, now - introT0);
  }

 private:
  Rng rng;
  Expr base = E_DEFAULT, expr = E_DEFAULT, reaction = E_COUNT;
  Pose cur{}, tgt{};
  EyeStyle style = ES_RECT; Mouth mouth = M_SMILE; Overlay ovl = O_NONE; bool blinks = true, flip = false;
  uint32_t reactionEnd = 0, last = 0, nextBlink = 0, blinkStart = 0, nextIdle = 0, wanderAt = 0;
  float blink = 1.0f;
  // boot intro
  bool intro = false; uint32_t introT0 = 0; int introStep = -1; int titleN = 0; float titleA = 1.0f;

  void runIntro(uint32_t now) {
    uint32_t t = now - introT0;
    struct Step { uint32_t at; Expr e; };
    static const Step steps[] = {{450, E_WAKE}, {1050, E_LOOK_L}, {1400, E_LOOK_R}, {1750, E_DEFAULT}, {2050, E_LAUGH}};
    for (int i = 0; i < 5; i++) if (introStep < i && t >= steps[i].at) {
      introStep = i; apply(steps[i].e, now); blinks = (i == 3);
      if (steps[i].e == E_LAUGH) { tgt.oy = -26; tgt.mouthY = 22; }
      if (i == 0) { cur.L.lidTop = cur.R.lidTop = 1.0f; }
    }
    titleN = t < 2250 ? 0 : (int)((t - 2250) / 150) + 1; if (titleN > 5) titleN = 5;
    titleA = t < 3900 ? 1.0f : clampf(1.0f - (float)(t - 3900) / 400.0f, 0.0f, 1.0f);
    if (t >= 4300) { intro = false; titleN = 0; blinks = true; nextBlink = now + 1200; apply(base, now); }
  }

  void apply(Expr e, uint32_t now) {
    ExprDef d = def(e);
    expr = e; tgt = d.pose; style = d.style; mouth = d.mouth; ovl = d.ovl; blinks = d.blinks;
    if (e == E_WAKE) { cur.L.lidTop = cur.R.lidTop = 1.0f; }       // eyes start shut and open up
    if (blinks && nextBlink < now + 800) nextBlink = now + 800;
    blinkStart = 0;
    wanderAt = now;
  }

  // ---- drawing --------------------------------------------------------------------------------
  template <class G> static void dots(G *g, float x0, float y0, float x1, float y1, int r, uint16_t col) {   // thick line
    float dx = x1 - x0, dy = y1 - y0; int n = (int)(sqrtf(dx * dx + dy * dy)) + 1;
    for (int i = 0; i <= n; i++) g->fillCircle((int16_t)(x0 + dx * i / n), (int16_t)(y0 + dy * i / n), r, col);
  }
  template <class G> static void heart(G *g, float cx, float cy, float s, uint16_t col) {
    g->fillCircle((int16_t)(cx - s * 0.5f), (int16_t)(cy - s * 0.28f), (int16_t)(s * 0.52f), col);
    g->fillCircle((int16_t)(cx + s * 0.5f), (int16_t)(cy - s * 0.28f), (int16_t)(s * 0.52f), col);
    g->fillTriangle((int16_t)(cx - s * 0.98f), (int16_t)(cy - s * 0.12f), (int16_t)(cx + s * 0.98f), (int16_t)(cy - s * 0.12f),
                    (int16_t)cx, (int16_t)(cy + s * 1.05f), col);
  }
  template <class G> static void star4(G *g, float cx, float cy, float s, uint16_t col) {
    g->fillTriangle((int16_t)cx, (int16_t)(cy - s), (int16_t)(cx - s * 0.25f), (int16_t)cy, (int16_t)(cx + s * 0.25f), (int16_t)cy, col);
    g->fillTriangle((int16_t)cx, (int16_t)(cy + s), (int16_t)(cx - s * 0.25f), (int16_t)cy, (int16_t)(cx + s * 0.25f), (int16_t)cy, col);
    g->fillTriangle((int16_t)(cx - s), (int16_t)cy, (int16_t)cx, (int16_t)(cy - s * 0.25f), (int16_t)cx, (int16_t)(cy + s * 0.25f), col);
    g->fillTriangle((int16_t)(cx + s), (int16_t)cy, (int16_t)cx, (int16_t)(cy - s * 0.25f), (int16_t)cx, (int16_t)(cy + s * 0.25f), col);
  }
  template <class G> static void glowRR(G *g, float x, float y, float w, float h, float r, const Pose &p) {
    float gl = p.glow;
    if (gl > 0.02f) {
      g->fillRoundRect((int16_t)(x - 10), (int16_t)(y - 10), (int16_t)(w + 20), (int16_t)(h + 20), (int16_t)(r + 10), glowc(p, 0.10f * gl));
      g->fillRoundRect((int16_t)(x - 6), (int16_t)(y - 6), (int16_t)(w + 12), (int16_t)(h + 12), (int16_t)(r + 6), glowc(p, 0.25f * gl));
      g->fillRoundRect((int16_t)(x - 3), (int16_t)(y - 3), (int16_t)(w + 6), (int16_t)(h + 6), (int16_t)(r + 3), glowc(p, 0.55f * gl));
    }
    g->fillRoundRect((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r, scaled(p, 1.0f));
  }

  template <class G> void drawEye(G *g, const Pose &p, const Eye &e, float cx, float cy, bool isLeft, float breath, uint32_t now) const {
    float s = isLeft ? 1.0f : -1.0f;   // direction of the inner side
    switch (style) {
      case ES_NONE: return;
      case ES_ARC_UP: {   // ^ ^ happy eyes
        uint16_t col = scaled(p, 1.0f);
        float hw = e.w * 0.5f, a = e.h * 0.30f;
        float px = cx - hw, py = cy;
        for (int i = 1; i <= 16; i++) {
          float t = -1 + 2.0f * i / 16;
          float x = cx + t * hw, y = cy - a * (1 - t * t) + a * 0.3f;
          dots(g, px, py, x, y, 3, col); px = x; py = y;
        }
        return;
      }
      case ES_U: {        // U shaped eyes
        uint16_t col = scaled(p, 1.0f);
        float hw = e.w * 0.38f, top = cy - e.h * 0.22f, bot = cy + e.h * 0.12f;
        dots(g, cx - hw, top, cx - hw, bot - 6, 3, col);
        dots(g, cx + hw, top, cx + hw, bot - 6, 3, col);
        float px = cx - hw, py = bot - 6;
        for (int i = 1; i <= 12; i++) {
          float th = 3.14159f * i / 12;
          float x = cx - hw * cosf(th), y = bot - 6 + 6 * sinf(th) + (hw * 0.55f) * sinf(th);
          dots(g, px, py, x, y, 3, col); px = x; py = y;
        }
        return;
      }
      case ES_CHEVRON: {  // > <
        uint16_t col = scaled(p, 1.0f);
        float hw = e.w * 0.5f, hh = e.h * 0.26f;
        float xo = cx - s * hw, xi = cx + s * hw * 0.6f;   // outer corner and the point
        dots(g, xo, cy - hh, xi, cy, 4, col);
        dots(g, xo, cy + hh, xi, cy, 4, col);
        return;
      }
      case ES_HEART: {
        float k = 1.0f + 0.10f * sinf(now / 170.0f);
        heart(g, cx, cy - 2, e.w * 0.56f * k, scaled(p, 1.0f));
        g->fillCircle((int16_t)(cx - e.w * 0.34f), (int16_t)(cy - e.w * 0.34f), 3, rgb(255, 235, 240));   // shine
        return;
      }
      case ES_TURBINE: {
        float R = e.w * 0.5f;
        g->fillCircle((int16_t)cx, (int16_t)cy, (int16_t)(R + 8), glowc(p, 0.10f));
        g->fillCircle((int16_t)cx, (int16_t)cy, (int16_t)(R + 4), glowc(p, 0.30f));
        g->fillCircle((int16_t)cx, (int16_t)cy, (int16_t)R, scaled(p, 1.0f));
        g->fillCircle((int16_t)cx, (int16_t)cy, (int16_t)(R * 0.80f), rgb(6, 14, 36));
        float a0 = now * 0.014f * (isLeft ? 1.0f : -1.0f);
        for (int b = 0; b < 6; b++) {
          float a = a0 + b * 1.0472f;
          float x1 = cx + cosf(a - 0.20f) * R * 0.74f, y1 = cy + sinf(a - 0.20f) * R * 0.74f;
          float x2 = cx + cosf(a + 0.12f) * R * 0.74f, y2 = cy + sinf(a + 0.12f) * R * 0.74f;
          g->fillTriangle((int16_t)cx, (int16_t)cy, (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2, scaled(p, 0.95f));
        }
        g->fillCircle((int16_t)cx, (int16_t)cy, 4, scaled(p, 1.0f));
        return;
      }
      case ES_BEAM: {     // headlight cones pointing outwards
        float dir = -s;   // outward direction
        float x0 = cx - dir * 14, x1 = cx + dir * 36;
        g->fillTriangle((int16_t)x0, (int16_t)(cy - 12), (int16_t)x0, (int16_t)(cy + 12), (int16_t)x1, (int16_t)(cy - 34), glowc(p, 0.22f));
        g->fillTriangle((int16_t)x0, (int16_t)(cy + 12), (int16_t)x1, (int16_t)(cy + 34), (int16_t)x1, (int16_t)(cy - 34), glowc(p, 0.22f));
        float x2 = cx + dir * 24;
        g->fillTriangle((int16_t)x0, (int16_t)(cy - 9), (int16_t)x0, (int16_t)(cy + 9), (int16_t)x2, (int16_t)(cy - 22), glowc(p, 0.75f));
        g->fillTriangle((int16_t)x0, (int16_t)(cy + 9), (int16_t)x2, (int16_t)(cy + 22), (int16_t)x2, (int16_t)(cy - 22), glowc(p, 0.75f));
        g->fillCircle((int16_t)(cx - dir * 14), (int16_t)cy, 11, rgb(255, 255, 255));   // the lamp itself
        return;
      }
      case ES_SIREN: {
        bool on = ((now / 220) % 2 == 0) == isLeft;
        int r = on ? 255 : 70, gr = on ? 40 : 20, b = on ? 40 : 30;
        if (!isLeft) { r = on ? 40 : 20; gr = on ? 110 : 40; b = on ? 255 : 90; }
        float R = 24;
        g->fillCircle((int16_t)cx, (int16_t)cy, (int16_t)(R + (on ? 9 : 2)), rgb(r / 6, gr / 6, b / 6));
        g->fillCircle((int16_t)cx, (int16_t)cy, (int16_t)(R + (on ? 4 : 1)), rgb(r / 3, gr / 3, b / 3));
        g->fillCircle((int16_t)cx, (int16_t)cy, (int16_t)R, rgb(r, gr, b));
        g->fillCircle((int16_t)(cx - 8), (int16_t)(cy - 9), 5, rgb(255, 255, 255));   // dome highlight
        return;
      }
      case ES_POLY: {      // sharp angry eye as a polygon (no black cut-outs, so it works on busy backgrounds)
        float xo = cx - s * e.w / 2, xi = cx + s * e.w / 2, top = cy - e.h / 2, bot = cy + e.h / 2;
        uint16_t col = scaled(p, 1.0f);
        g->fillTriangle((int16_t)xo, (int16_t)(top + e.slantOut), (int16_t)xi, (int16_t)(top + e.slantIn), (int16_t)xi, (int16_t)bot, col);
        g->fillTriangle((int16_t)xo, (int16_t)(top + e.slantOut), (int16_t)xi, (int16_t)bot, (int16_t)xo, (int16_t)bot, col);
        return;
      }
      case ES_RECT: default: break;
    }
    // rounded-rectangle eye with lids
    float h = e.h * breath, w = e.w;
    float top = cy - h / 2 + e.lidTop * h, bot = cy + h / 2 - e.lidBot * h;
    top = cy + (top - cy) * blink; bot = cy + (bot - cy) * blink;
    float vh = bot - top;
    if (vh < 5) { top = cy - 2.5f; vh = 5; }
    float r = e.r; if (r > vh / 2) r = vh / 2; if (r > w / 2) r = w / 2;
    glowRR(g, cx - w / 2, top, w, vh, r, p);
    if (e.slantIn > 0.5f || e.slantOut > 0.5f) {
      float xOut = cx - s * w / 2, xIn = cx + s * w / 2;
      float x0 = xOut - s * 10, x1 = xIn + s * 10;
      float ya = top + e.slantOut + (e.slantIn - e.slantOut) * ((x0 - xOut) / (xIn - xOut));
      float yb = top + e.slantOut + (e.slantIn - e.slantOut) * ((x1 - xOut) / (xIn - xOut));
      float t0 = top - 12;
      g->fillTriangle((int16_t)x0, (int16_t)t0, (int16_t)x1, (int16_t)t0, (int16_t)x1, (int16_t)yb, 0);
      g->fillTriangle((int16_t)x0, (int16_t)t0, (int16_t)x1, (int16_t)yb, (int16_t)x0, (int16_t)ya, 0);
    }
  }

  template <class G> void drawMouth(G *g, const Pose &p, float mx, float my, uint32_t now) const {
    uint16_t col = scaled(p, 1.0f);
    float hw = p.mouthW * 0.5f;
    switch (mouth) {
      case M_NONE: return;
      case M_SMILE: case M_FROWN: case M_FLAT: {
        float px = mx - hw, py = my;
        for (int i = 1; i <= 12; i++) {
          float t = -1 + 2.0f * i / 12;
          float x = mx + t * hw, y = my + p.mouthC * (1 - t * t);
          dots(g, px, py, x, y, 1, col); px = x; py = y;
        }
        return;
      }
      case M_CAT: {   // "w"
        float q = hw * 0.5f;
        for (int k = -1; k <= 1; k += 2) {
          float cxm = mx + k * q, px = cxm - q, py = my;
          for (int i = 1; i <= 8; i++) { float t = -1 + 2.0f * i / 8; float x = cxm + t * q, y = my + 5 * (1 - t * t); dots(g, px, py, x, y, 1, col); px = x; py = y; }
        }
        return;
      }
      case M_O: { g->fillCircle((int16_t)mx, (int16_t)my, (int16_t)hw * 0 + 6, col); g->fillCircle((int16_t)mx, (int16_t)my, 3, 0); return; }
      case M_TONGUE: {   // flat mouth with a tongue sticking out
        dots(g, mx - hw, my, mx + hw, my, 1, col);
        g->fillRoundRect((int16_t)(mx - 6), (int16_t)my, 12, 13, 6, rgb(255, 110, 140));
        g->fillRect((int16_t)mx, (int16_t)(my + 1), 1, 9, rgb(190, 60, 90));
        return;
      }
      case M_OPEN: {     // D shaped open mouth
        float oh = 4 + p.mouthOpen * 15;
        g->fillEllipse((int16_t)mx, (int16_t)my, (int16_t)hw, (int16_t)oh, col);
        g->fillEllipse((int16_t)mx, (int16_t)my, (int16_t)(hw - 2), (int16_t)(oh - 2), rgb(60, 12, 28));
        g->fillEllipse((int16_t)(mx + 1), (int16_t)(my + oh * 0.55f), (int16_t)(hw * 0.5f), (int16_t)(oh * 0.42f), rgb(255, 110, 140));
        g->fillRect((int16_t)(mx - hw - 3), (int16_t)(my - oh - 3), (int16_t)(hw * 2 + 6), (int16_t)(oh + 3), 0);   // cut the top half
        g->fillRect((int16_t)(mx - hw), (int16_t)(my - 1), (int16_t)(hw * 2), 2, col);                                // flat top edge
        return;
      }
      case M_CHOMP: {
        float o = 0.5f + 0.5f * sinf(now / 110.0f);
        g->fillEllipse((int16_t)mx, (int16_t)my, (int16_t)(hw * 0.7f), (int16_t)(2 + 7 * o), col);
        g->fillEllipse((int16_t)mx, (int16_t)my, (int16_t)(hw * 0.7f - 2), (int16_t)(1 + 7 * o - 2), rgb(60, 12, 28));
        return;
      }
      case M_ZIGZAG: {
        float px = mx - hw, py = my;
        for (int i = 1; i <= 10; i++) { float x = mx - hw + (2 * hw) * i / 10; float y = my + ((i % 2) ? -7 : 5); dots(g, px, py, x, y, 1, col); px = x; py = y; }
        return;
      }
    }
  }

  template <class G> void drawOverlay(G *g, const Pose &p, float lx, float rx, float cy, uint32_t now) const {
    switch (ovl) {
      case O_BLUSH: {
        uint16_t c = rgb(255, 110, 150);
        g->fillEllipse((int16_t)(lx - 6), (int16_t)(cy + 32), 10, 4, rgb(120, 40, 70));
        g->fillEllipse((int16_t)(rx + 6), (int16_t)(cy + 32), 10, 4, rgb(120, 40, 70));
        g->fillEllipse((int16_t)(lx - 6), (int16_t)(cy + 32), 7, 3, c);
        g->fillEllipse((int16_t)(rx + 6), (int16_t)(cy + 32), 7, 3, c);
        break;
      }
      case O_ANGRY_MARK: {   // anger vein at the top right
        float k = 1.0f + 0.12f * sinf(now / 120.0f); uint16_t c = rgb(255, 70, 60);
        float x = rx + 20, y = cy - 36;
        dots(g, x - 9 * k, y - 3 * k, x - 3 * k, y - 3 * k, 1, c); dots(g, x - 3 * k, y - 3 * k, x - 3 * k, y - 9 * k, 1, c);
        dots(g, x + 3 * k, y - 9 * k, x + 3 * k, y - 3 * k, 1, c); dots(g, x + 3 * k, y - 3 * k, x + 9 * k, y - 3 * k, 1, c);
        dots(g, x - 9 * k, y + 3 * k, x - 3 * k, y + 3 * k, 1, c); dots(g, x - 3 * k, y + 3 * k, x - 3 * k, y + 9 * k, 1, c);
        dots(g, x + 3 * k, y + 9 * k, x + 3 * k, y + 3 * k, 1, c); dots(g, x + 3 * k, y + 3 * k, x + 9 * k, y + 3 * k, 1, c);
        break;
      }
      case O_TEARS: {
        for (int side = 0; side < 2; side++) {
          float cx = side ? rx : lx;
          for (int k = 0; k < 2; k++) {
            float t = fmodf(now / 900.0f + k * 0.5f + side * 0.2f, 1.0f);
            float x = cx + (k ? 8 : -8), y = cy + 22 + t * 46;
            uint16_t c = rgb((int)(110 * (1 - t) + 20), (int)(200 * (1 - t) + 30), (int)(255 * (1 - t) + 40));
            g->fillCircle((int16_t)x, (int16_t)y, 4, c);
            g->fillTriangle((int16_t)(x - 4), (int16_t)y, (int16_t)(x + 4), (int16_t)y, (int16_t)x, (int16_t)(y - 9), c);
          }
        }
        break;
      }
      case O_STARS: {   // sparkle highlights in the eyes
        float k = 0.7f + 0.3f * sinf(now / 260.0f);
        star4(g, lx - 6, cy - 14, 10 * k, rgb(255, 255, 255)); star4(g, rx - 6, cy - 14, 10 * k, rgb(255, 255, 255));
        break;
      }
      case O_ZZZ: {
        for (int k = 0; k < 3; k++) {
          float t = fmodf(now / 2400.0f + k / 3.0f, 1.0f);
          float x = W / 2 + 34 + t * 22, y = cy - 30 - t * 34, s = 5 + k * 2 + t * 4;
          uint16_t c = rgb((int)(200 * (1 - t)), (int)(220 * (1 - t)), (int)(255 * (1 - t)));
          dots(g, x - s, y - s, x + s, y - s, 1, c); dots(g, x + s, y - s, x - s, y + s, 1, c); dots(g, x - s, y + s, x + s, y + s, 1, c);
        }
        break;
      }
      case O_SWEAT: {
        float bob = 3 * sinf(now / 300.0f), x = rx + 26, y = cy - 20 + bob; uint16_t c = rgb(120, 200, 255);
        g->fillCircle((int16_t)x, (int16_t)(y + 5), 6, c);
        g->fillTriangle((int16_t)(x - 6), (int16_t)(y + 4), (int16_t)(x + 6), (int16_t)(y + 4), (int16_t)x, (int16_t)(y - 11), c);
        g->fillCircle((int16_t)(x - 2), (int16_t)(y + 6), 2, rgb(230, 245, 255));
        break;
      }
      case O_DROPLET: {
        float t = fmodf(now / 700.0f, 1.0f);
        g->fillCircle((int16_t)(W / 2 + 12 + t * 40), (int16_t)(cy + 50 + t * 16), 3, rgb(150, 210, 255));
        g->fillCircle((int16_t)(W / 2 + 18 + t * 50), (int16_t)(cy + 46 + t * 22), 2, rgb(150, 210, 255));
        break;
      }
      default: break;
    }
  }

  template <class G> void drawStarfield(G *g, uint32_t now) const {
    for (int i = 0; i < 46; i++) {
      uint32_t h = (uint32_t)(i * 2654435761u); int x = (int)(h % W), y = (int)((h >> 9) % H);
      float tw = 0.5f + 0.5f * sinf(now / 300.0f + i * 1.7f);
      int v = (int)(60 + 150 * tw); g->fillRect((int16_t)x, (int16_t)y, 1 + (i % 3 == 0), 1 + (i % 3 == 0), rgb(v, v, v + 20));
    }
  }
  template <class G> void drawGlitchBackground(G *g, uint32_t now) const {
    uint32_t f = now / 70;
    for (int y = 0; y < H; y += 3) {
      uint32_t h = (uint32_t)((y * 7919u) ^ (f * 2654435761u)); int v = 60 + (int)(h % 100);
      g->fillRect(0, (int16_t)y, W, 2, rgb(v, v / 3, 10));
    }
  }
  template <class G> void drawPowerBar(G *g, uint32_t t) const {   // the glowing line that becomes the eyes
    float k = clampf(t / 450.0f, 0.0f, 1.0f), e = 1.0f - (1.0f - k) * (1.0f - k);
    Pose p = cur; p.glow = e;
    float w = 4 + 104 * e, cy = H / 2 - 6, h = 5;
    if (k < 0.25f) { float a = k / 0.25f; g->fillCircle(W / 2, (int16_t)cy, (int16_t)(2 + 3 * a), scaled(p, 0.4f + 0.6f * a)); return; }
    glowRR(g, W / 2 - w / 2, cy - h / 2, w, h, 2.5f, p);
  }
  template <class G> void drawIntroTitle(G *g, uint32_t t) const {   // "TACHI" typed in with a pop and a sparkle per letter
    const char *txt = "TACHI"; int sc = 5, gw = 5 * sc, gapw = sc, n = 5;
    int total = n * gw + (n - 1) * gapw, x0 = (W - total) / 2, y0 = H / 2 + 44;
    Pose p = cur; p.glow = 1.0f;
    for (int i = 0; i < titleN && i < n; i++) {
      const uint8_t *gl = glyph(txt[i]); if (!gl) continue;
      int age = (int)t - (2250 + 150 * i);
      float pop = age < 140 ? (1.0f - age / 140.0f) : 0.0f;
      int dy = (int)(-10 * pop), lx0 = x0 + i * (gw + gapw);
      float flash = pop;   // new letters start white, settle into the eye colour
      for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (gl[r] & (0x10 >> c)) {
        int x = lx0 + c * sc, y = y0 + r * sc + dy;
        g->fillRect((int16_t)(x - 2), (int16_t)(y - 2), (int16_t)(sc + 3), (int16_t)(sc + 3), glowc(p, 0.22f * titleA));
      }
      for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (gl[r] & (0x10 >> c)) {
        int x = lx0 + c * sc, y = y0 + r * sc + dy;
        float k = titleA;
        g->fillRect((int16_t)x, (int16_t)y, (int16_t)(sc - 1), (int16_t)(sc - 1),
                    rgb((int)((p.cr + (255 - p.cr) * flash) * k), (int)((p.cg + (255 - p.cg) * flash) * k), (int)((p.cb + (255 - p.cb) * flash) * k)));
      }
      if (age >= 0 && age < 320) star4(g, (float)(lx0 + gw / 2), (float)(y0 - 8 + dy), 9.0f * (1.0f - age / 320.0f), rgb(255, 255, 255));
    }
  }
  template <class G> void drawTitle(G *g, uint32_t now) const {
    const char *txt = "TACHI"; int sc = 5, gw = 5 * sc, gapw = sc, n = 5;
    int total = n * gw + (n - 1) * gapw, x0 = (W - total) / 2, y0 = H / 2 - 7 * sc / 2;
    float pulse = 0.85f + 0.15f * sinf(now / 220.0f);
    Pose p = cur; p.glow = pulse;
    for (int pass = 0; pass < 2; pass++) {
      for (int i = 0; i < n; i++) {
        const uint8_t *gl = glyph(txt[i]); if (!gl) continue;
        for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (gl[r] & (0x10 >> c)) {
          int x = x0 + i * (gw + gapw) + c * sc, y = y0 + r * sc;
          if (pass == 0) g->fillRect((int16_t)(x - 2), (int16_t)(y - 2), (int16_t)(sc + 4), (int16_t)(sc + 4), scaled(p, 0.12f * pulse));
          else g->fillRect((int16_t)x, (int16_t)y, (int16_t)(sc - 1), (int16_t)(sc - 1), scaled(p, 1.0f));
        }
      }
    }
  }
};

}  // namespace mochi
