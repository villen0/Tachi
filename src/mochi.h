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
  static const uint8_t R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
  static const uint8_t P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
  static const uint8_t M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
  static const uint8_t D[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
  if (c >= '0' && c <= '9') return D[c - '0'];
  switch (c) { case 'T': return T; case 'A': return A; case 'C': return C; case 'H': return Hh; case 'I': return I;
               case 'R': return R; case 'P': return P; case 'M': return M; default: return nullptr; }
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
  void startIntro(uint32_t now, bool shortVersion = false) {
    intro = true; introShort = shortVersion; introT0 = now; clockNow = now;
    reaction = E_COUNT; blinks = false; blinkStart = 0;
    cur = tgt = def(E_DEFAULT).pose; expr = E_DEFAULT; style = ES_RECT; mouth = M_SMILE; ovl = O_NONE;
  }
  bool introActive() const { return intro; }
  // Scripted welcome (headlights drive-in): lamps appear far away, a car drives in and brakes, the lamps flash and become the
  // eyes, the eyes turn into happy arches while a glowing "TACHI" writes itself on, then the normal face returns.
  // Full about 5.4 s; the short version (after a quick restart) plays only the drive-in part, about 0.9 s.
  // LED colour that follows the headlights during the drive-in; false afterwards (the mood colour takes over).
  bool introLed(uint8_t *r, uint8_t *g, uint8_t *b) const {
    if (!intro) return false;
    float T = welcomeClock(clockNow - introT0);
    if (T >= 2500.0f) return false;
    float on = smoothf((T - 300.0f) / 170.0f), fl = flashAt(T);
    float k = on * (1.0f - 0.7f * smootherf((T - 2080.0f) / 420.0f));
    *r = (uint8_t)(clampf(40 * k + 100 * fl, 0, 140)); *g = (uint8_t)(clampf(70 * k + 90 * fl, 0, 160)); *b = (uint8_t)(clampf(100 * k + 70 * fl, 0, 170));
    return true;
  }
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
    clockNow = now;
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
    if (intro) { drawWelcome(g, now - introT0); return; }
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
  }

 private:
  Rng rng;
  Expr base = E_DEFAULT, expr = E_DEFAULT, reaction = E_COUNT;
  Pose cur{}, tgt{};
  EyeStyle style = ES_RECT; Mouth mouth = M_SMILE; Overlay ovl = O_NONE; bool blinks = true, flip = false;
  uint32_t reactionEnd = 0, last = 0, nextBlink = 0, blinkStart = 0, nextIdle = 0, wanderAt = 0;
  float blink = 1.0f;
  // boot welcome
  bool intro = false, introShort = false; uint32_t introT0 = 0, clockNow = 0;

  // ---- welcome timeline (ms on the "full" clock; the short version plays the first 2500 ms about 2.8x faster) ----
  float welcomeClock(uint32_t t) const { return introShort ? (float)t * (2500.0f / 900.0f) : (float)t; }
  uint32_t introEnd() const { return introShort ? 900 : 5450; }
  static float smoothf(float x) { x = clampf(x, 0, 1); return x * x * (3 - 2 * x); }
  static float smootherf(float x) { x = clampf(x, 0, 1); return x * x * x * (x * (6 * x - 15) + 10); }
  static float easeOut3(float x) { x = clampf(x, 0, 1); float q = 1 - x; return 1 - q * q * q; }
  static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
  static float flashAt(float T) {   // two quick flashes while the lamps turn into eyes
    float f = 0;
    for (int i = 0; i < 2; i++) { float c = 2080.0f + 150.0f * i + 45.0f; float v = smoothf(1.0f - fabsf(T - c) / 80.0f); if (v > f) f = v; }
    return f;
  }
  void runIntro(uint32_t now) {
    if (now - introT0 >= introEnd()) {       // hand over to the live face in the very pose the welcome ended in
      intro = false; blinks = true; blinkStart = 0; nextBlink = now + 1500;
      cur = def(E_DEFAULT).pose; expr = E_DEFAULT; apply(base, now);
    }
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
  template <class G> static void text5x7(G *g, const char *txt, int cx, int cy, int sc, uint16_t col) {   // centered pixel text
    int n = (int)strlen(txt), total = n * 5 * sc + (n - 1) * sc, x0 = cx - total / 2, y0 = cy - 7 * sc / 2;
    for (int i = 0; i < n; i++) {
      const uint8_t *gl = glyph(txt[i]); if (!gl) continue;
      for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (gl[r] & (0x10 >> c))
        g->fillRect((int16_t)(x0 + i * 6 * sc + c * sc), (int16_t)(y0 + r * sc), (int16_t)sc, (int16_t)sc, col);
    }
  }
  // Tachometer: 270 degree sweep from lower-left, value01 = needle position, alpha = overall brightness.
  // Reusable for the RPM page (Mochi Phase 3).
  template <class G> void drawTach(G *g, int cx, int cy, int R, float value01, float glow, float alpha) const {
    const float START = 135.0f, SWEEP = 270.0f, D2R = 0.0174533f;
    auto col = [&](int r, int gr, int b, float k) { return rgb((int)(r * k * alpha), (int)(gr * k * alpha), (int)(b * k * alpha)); };
    for (int k = 0; k <= 16; k++) {                       // ticks: 8 majors (0..8) with one minor between
      float v = k / 16.0f, a = (START + v * SWEEP) * D2R;
      bool major = (k % 2 == 0), red = v >= 0.74f, lit = v <= value01 + 0.001f;
      float r0 = R - (major ? 11.0f : 6.0f), r1 = (float)R;
      float br = lit ? 1.0f : 0.30f;
      uint16_t c = red ? col(255, 70, 60, br) : col(205, 235, 255, br);
      dots(g, cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r1, cy + sinf(a) * r1, major ? 1 : 0, c);
      if (major) {                                         // digit 0..8
        char d[2] = {(char)('0' + k / 2), 0};
        float rd = R - 24.0f;
        text5x7(g, d, (int)(cx + cosf(a) * rd), (int)(cy + sinf(a) * rd), 2, red ? col(255, 90, 80, br) : col(205, 235, 255, br * 0.9f));
      }
    }
    int steps = (int)(value01 * 54);                       // glowing trail behind the needle along the rim
    for (int i = 0; i < steps; i++) {
      float v = i / 54.0f, a = (START + v * SWEEP) * D2R, f = 0.25f + 0.75f * (float)i / (steps > 0 ? steps : 1);
      g->fillCircle((int16_t)(cx + cosf(a) * (R + 4)), (int16_t)(cy + sinf(a) * (R + 4)), 1, v >= 0.74f ? col(255, 70, 60, f * glow) : col(50, 140, 255, f * glow));
    }
    float na = (START + clampf(value01, 0, 1) * SWEEP) * D2R;
    float tx = cx + cosf(na) * (R - 14), ty = cy + sinf(na) * (R - 14);
    dots(g, cx - cosf(na) * 8, cy - sinf(na) * 8, tx, ty, 1, value01 >= 0.74f ? col(255, 110, 90, 1) : col(255, 255, 255, 1));
    g->fillCircle((int16_t)cx, (int16_t)cy, 7, col(40, 90, 180, 1));
    g->fillCircle((int16_t)cx, (int16_t)cy, 4, col(205, 235, 255, 1));
    text5x7(g, "RPM", cx, cy + (int)(R * 0.62f), 1, col(120, 170, 230, 1));
  }
  // ---- vector "TACHI" (strokes on a 0..1 grid) -------------------------------------------------------
  static int letterStrokes(char c) { switch (c) { case 'T': return 2; case 'A': return 2; case 'C': return 1; case 'H': return 3; case 'I': return 3; default: return 0; } }
  static int letterStroke(char c, int s, float (*p)[2]) {   // fills p with the polyline of stroke s, returns the point count
    auto set = [&](int i, float x, float y) { p[i][0] = x; p[i][1] = y; };
    switch (c) {
      case 'T': if (s == 0) { set(0, 0, 0); set(1, 1, 0); return 2; } set(0, 0.5f, 0); set(1, 0.5f, 1); return 2;
      case 'A': if (s == 0) { set(0, 0, 1); set(1, 0.5f, 0); set(2, 1, 1); return 3; } set(0, 0.2f, 0.68f); set(1, 0.8f, 0.68f); return 2;
      case 'C': for (int i = 0; i <= 14; i++) { float a = (-42.0f - 276.0f * i / 14.0f) * 0.0174533f; set(i, 0.55f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a)); } return 15;
      case 'H': if (s == 0) { set(0, 0, 0); set(1, 0, 1); } else if (s == 1) { set(0, 1, 0); set(1, 1, 1); } else { set(0, 0, 0.5f); set(1, 1, 0.5f); } return 2;
      case 'I': if (s == 0) { set(0, 0.15f, 0); set(1, 0.85f, 0); } else if (s == 1) { set(0, 0.5f, 0); set(1, 0.5f, 1); } else { set(0, 0.15f, 1); set(1, 0.85f, 1); } return 2;
    }
    return 0;
  }
  static float polyLen(float (*p)[2], int n) { float L = 0; for (int i = 0; i + 1 < n; i++) L += hypotf(p[i + 1][0] - p[i][0], p[i + 1][1] - p[i][1]); return L; }
  static int polyUpTo(float (*p)[2], int n, float len, float (*out)[2]) {   // copies the polyline up to a length (out holds n+1)
    int m = 1; out[0][0] = p[0][0]; out[0][1] = p[0][1];
    for (int i = 0; i + 1 < n; i++) {
      float d = hypotf(p[i + 1][0] - p[i][0], p[i + 1][1] - p[i][1]);
      if (len >= d) { out[m][0] = p[i + 1][0]; out[m][1] = p[i + 1][1]; m++; len -= d; }
      else { float t = d > 0 ? len / d : 0; out[m][0] = lerpf(p[i][0], p[i + 1][0], t); out[m][1] = lerpf(p[i][1], p[i + 1][1], t); m++; break; }
    }
    return m;
  }
  // Visits every point of the hand-written "TACHI" that has been written so far (about 1 point per pixel of stroke).
  // f(x, y, letter, writingNow) is called per point; writingNow is true for the letter that is still being drawn.
  template <class F> void titleWalk(float T, float t0, int y0, F f) const {
    const char *word = "TACHI"; const int lw = 22, lh = 30, gap = 8, n = 5;
    const int x0 = (W - (n * lw + (n - 1) * gap)) / 2;
    for (int i = 0; i < n; i++) {
      float prog = smoothf((T - (t0 + 190.0f * i)) / 260.0f);
      if (prog <= 0) continue;
      float pts[16][2], part[17][2], totalLen = 0; int ns = letterStrokes(word[i]);
      for (int s = 0; s < ns; s++) { int c = letterStroke(word[i], s, pts); totalLen += polyLen(pts, c); }
      float want = prog * totalLen;
      for (int s = 0; s < ns; s++) {
        int c = letterStroke(word[i], s, pts); float L = polyLen(pts, c), take = want < L ? want : L; want -= take;
        if (take <= 0) continue;
        int m = polyUpTo(pts, c, take, part);
        for (int k = 0; k + 1 < m; k++) {
          float ax = x0 + i * (lw + gap) + part[k][0] * lw, ay = y0 + part[k][1] * lh;
          float bx = x0 + i * (lw + gap) + part[k + 1][0] * lw, by = y0 + part[k + 1][1] * lh;
          int steps = (int)(hypotf(bx - ax, by - ay) * 1.2f) + 1;
          for (int q = 0; q <= steps; q++) f(lerpf(ax, bx, (float)q / steps), lerpf(ay, by, (float)q / steps), i, prog > 0.04f && prog < 1.0f);
        }
      }
    }
  }
  // Glowing "TACHI" written on with a bright tip. alpha 0..1 fades it, shim 0..1 moves a bright band across the finished word.
  template <class G> void drawVectorTitle(G *g, float T, float t0, float alpha, float shim, int y0) const {
    const int n = 5, total = n * 22 + (n - 1) * 8, x0 = (W - total) / 2;
    auto glow = [&](float k) { return rgb((int)(50 * k * alpha), (int)(130 * k * alpha), (int)(255 * k * alpha)); };
    uint16_t wide = glow(26.0f / 255.0f), mid = glow(70.0f / 255.0f);
    titleWalk(T, t0, y0, [&](float x, float y, int, bool) { g->fillCircle((int16_t)x, (int16_t)y, 6, wide); });
    titleWalk(T, t0, y0, [&](float x, float y, int, bool) { g->fillCircle((int16_t)x, (int16_t)y, 4, mid); });
    float tx = 0, ty = 0; bool tip = false;
    titleWalk(T, t0, y0, [&](float x, float y, int, bool writing) {
      float sh = shim > 0 ? clampf(1.0f - fabsf(x - (x0 - 20 + (total + 40) * shim)) / 14.0f, 0, 1) * 0.75f : 0.0f;
      g->fillCircle((int16_t)x, (int16_t)y, 2, rgb((int)((205 + 50 * sh) * alpha), (int)((235 + 20 * sh) * alpha), (int)(255 * alpha)));
      if (writing) { tx = x; ty = y; tip = true; }
    });
    if (tip) {
      g->fillCircle((int16_t)tx, (int16_t)ty, 8, rgb((int)(120 * 90 / 255.0f * alpha), (int)(190 * 90 / 255.0f * alpha), (int)(255 * 90 / 255.0f * alpha)));
      g->fillCircle((int16_t)tx, (int16_t)ty, 3, rgb((int)(255 * alpha), (int)(255 * alpha), (int)(255 * alpha)));
    }
  }

  // ---- the headlights drive-in welcome ---------------------------------------------------------------
  template <class G> void drawWelcome(G *g, uint32_t tms) const {
    const float T = welcomeClock(tms);
    const int cx = W / 2;
    auto fc = [&](int r, int gr, int b, float a255) { float k = a255 / 255.0f; return rgb((int)(r * k), (int)(gr * k), (int)(b * k)); };
    float on = smoothf((T - 300.0f) / 170.0f);
    float p = easeOut3((T - 300.0f) / 1150.0f);
    float half = lerpf(7, 33, p), r = lerpf(3, 17, p), ly = lerpf(82, 100, p);
    float carA = smootherf((T - 1250.0f) / 380.0f) * (1.0f - smootherf((T - 2080.0f) / 420.0f));
    float morph = smootherf((T - 2080.0f) / 420.0f);
    float scene = on * (1.0f - smootherf((T - 2050.0f) / 380.0f));
    float flash = flashAt(T);
    float sq = 1.0f;
    if (T >= 1750.0f && T < 2080.0f) { float x = (T - 1750.0f) / 330.0f; sq = 1.0f - 0.10f * sinf(x * 3.14159f) * (1.0f - 0.25f * x); }

    if (scene > 0.01f) {
      // road lines streaming outward from the vanishing point
      const float vpx = (float)cx, vpy = 70.0f; static const float angs[6] = {-1.2f, -0.65f, -0.3f, 0.3f, 0.65f, 1.2f};
      for (int a = 0; a < 6; a++) for (int k = 0; k < 6; k++) {
        float ph = fmodf(T / 420.0f + k / 6.0f, 1.0f), d0 = powf(ph, 1.7f), d1 = fminf(1.0f, d0 + 0.1f);
        float al = 140.0f * scene * fminf(1.0f, d0 * 3.0f) * (1.0f - 0.55f * p);
        uint16_t col = fc(150, 190, 255, al);
        float x0 = vpx + sinf(angs[a]) * 120 * d0, y0 = vpy + cosf(angs[a]) * 110 * d0, x1 = vpx + sinf(angs[a]) * 120 * d1, y1 = vpy + cosf(angs[a]) * 110 * d1;
        g->drawLine((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, col);
        if (d0 > 0.5f) g->drawLine((int16_t)x0 + 1, (int16_t)y0, (int16_t)x1 + 1, (int16_t)y1, col);
      }
      // pool of light on the road (widest and dimmest first)
      static const int prx[3] = {70, 56, 42}, pry[3] = {22, 17, 12}; static const float pa[3] = {20, 46, 76};
      for (int i = 0; i < 3; i++) g->fillEllipse(cx, H - 16, prx[i], pry[i], fc(60, 120, 255, pa[i] * scene * p));
      // layered light cones = soft gradient
      float spread = lerpf(14, 78, p); static const float sp[6] = {1.0f, 0.86f, 0.72f, 0.58f, 0.44f, 0.30f}, ca[6] = {22, 44, 66, 90, 114, 140};
      for (int layer = 0; layer < 6; layer++) for (int side = -1; side <= 1; side += 2) {
        float x = cx + side * half, s = spread * sp[layer];
        float ax = x - side * 2, ay = ly - 2, bx = x - side * 2, by = ly + 4, cx2 = x + side * s * 1.05f, cy2 = ly + s * 1.35f, dx = x + side * s * 0.15f, dy = ly + s * 1.5f;
        uint16_t col = fc(190, 225, 255, ca[layer] * scene);
        g->fillTriangle((int16_t)ax, (int16_t)ay, (int16_t)bx, (int16_t)by, (int16_t)cx2, (int16_t)cy2, col);
        g->fillTriangle((int16_t)ax, (int16_t)ay, (int16_t)cx2, (int16_t)cy2, (int16_t)dx, (int16_t)dy, col);
      }
    }

    if (carA > 0.01f) {                                           // car front: cabin + body as one silhouette, grille smile, plate "TACHI"
      float bw = 118, bh = 56 * sq, by = ly + 6 + (56 - bh);
      int bx0 = (int)(cx - bw / 2), bx1 = (int)(cx + bw / 2), byt = (int)(by - bh / 2 - 6), byb = (int)(by + bh / 2 + 6);
      int cb0 = cx - 40, cb1 = cx + 40, cbt = (int)(by - bh / 2 - 26), cbb = (int)(by - bh / 2 + 8);
      uint16_t outl = fc(70, 95, 140, 255 * carA), fill = fc(24, 28, 40, 255 * carA);
      g->fillRoundRect(cb0 - 2, cbt - 2, cb1 - cb0 + 4, cbb - cbt + 4, 16, outl);
      g->fillRoundRect(bx0 - 2, byt - 2, bx1 - bx0 + 4, byb - byt + 4, 20, outl);
      g->fillRoundRect(cb0, cbt, cb1 - cb0, cbb - cbt, 14, fill);
      g->fillRoundRect(bx0, byt, bx1 - bx0, byb - byt, 18, fill);
      g->fillRoundRect(cx - 30, (int)(by - bh / 2 - 21), 60, 23, 9, fc(14, 18, 28, 255 * carA));        // windshield
      g->fillRoundRect(cx - 26, (int)by + 4, 52, 5, 2, fc(58, 70, 98, 255 * carA));                      // grille bar
      for (int k = -2; k <= 2; k++) g->drawLine(cx + k * 9, (int)by + 4, cx + k * 9, (int)by + 9, fill);
      int py = (int)(by + bh / 2 - 1);
      g->fillRoundRect(cx - 29, py - 1, 58, 19, 4, fc(120, 130, 150, 255 * carA));
      g->fillRoundRect(cx - 28, py, 56, 17, 3, fc(228, 233, 243, 255 * carA));
      uint16_t ink = fc(30, 45, 90, 255 * carA);
      for (int i = 0; i < 5; i++) {                                // tiny vector TACHI on the plate
        const char *w = "TACHI"; float pts[16][2];
        for (int s = 0; s < letterStrokes(w[i]); s++) {
          int c = letterStroke(w[i], s, pts);
          for (int k = 0; k + 1 < c; k++)
            g->drawLine((int16_t)(cx - 22 + i * 9.5f + pts[k][0] * 7), (int16_t)(py + 4 + pts[k][1] * 9), (int16_t)(cx - 22 + i * 9.5f + pts[k + 1][0] * 7), (int16_t)(py + 4 + pts[k + 1][1] * 9), ink);
        }
      }
    }

    // lamps -> eyes -> thin line -> happy arches -> eyes again, face moving up for the title and back down
    float up = smootherf((T - 2500.0f) / 450.0f) * (1.0f - smootherf((T - 4850.0f) / 500.0f));
    float squash = smootherf((T - 2480.0f) / 230.0f) * (1.0f - smootherf((T - 5100.0f) / 260.0f));
    float arch = smootherf((T - 2700.0f) / 260.0f) * (1.0f - smootherf((T - 4850.0f) / 260.0f));
    float eyesY = lerpf(ly, 84.0f, morph) - 20.0f * up;
    if (on > 0.01f) {
      Pose pw = basePose();
      pw.cr = lerpf(pw.cr, 255, flash) * on; pw.cg = lerpf(pw.cg, 255, flash) * on; pw.cb = lerpf(pw.cb, 255, flash) * on;
      pw.gr = lerpf(pw.gr, 255, flash * 0.7f) * on; pw.gg = lerpf(pw.gg, 255, flash * 0.7f) * on; pw.gb = lerpf(pw.gb, 255, flash * 0.7f) * on;
      pw.glow = 1.0f + flash;
      for (int side = -1; side <= 1; side += 2) {
        float x = cx + side * half, w = 2 * r, h = lerpf(2 * r, 54, morph), rad = lerpf(r, 14, morph);
        h = lerpf(h, 9, squash); rad = lerpf(rad, 4.5f, squash);
        if (arch <= 0.001f) {
          glowRR(g, x - w / 2, eyesY - h / 2, w, h, rad, pw);
        } else {                                                   // ^ arch (grows out of the thin line)
          float amp = 11.0f * arch; float px[17], py[17];
          for (int i = 0; i <= 16; i++) { float u = -1 + 2.0f * i / 16; px[i] = x + u * w / 2; py[i] = eyesY + 2 - amp * (1 - u * u) + amp * 0.35f; }
          for (int i = 0; i <= 16; i++) g->fillCircle((int16_t)px[i], (int16_t)py[i], 6, glowc(pw, 0.10f * pw.glow));
          for (int i = 0; i <= 16; i++) g->fillCircle((int16_t)px[i], (int16_t)py[i], 5, glowc(pw, 0.25f * pw.glow));
          for (int i = 0; i <= 16; i++) g->fillCircle((int16_t)px[i], (int16_t)py[i], 3, scaled(pw, 1.0f));
        }
      }
      if (morph > 0.35f) {                                         // smile: grows out of the grille smile, same place as the live face's smile
        float a = smootherf((morph - 0.35f) / 0.65f), wd = lerpf(8, 12, up), my = 84.0f + 22.0f - 20.0f * up;
        uint16_t col = fc(205, 235, 255, 255 * a);
        for (int i = 0; i <= 24; i++) { float u = -1 + 2.0f * i / 24; g->fillCircle((int16_t)(cx + u * wd), (int16_t)(my + 5 * (1 - u * u)), 1, col); }
      }
    }
    float ta = smootherf((T - 2850.0f) / 100.0f) * (1.0f - smootherf((T - 4800.0f) / 350.0f));
    if (T >= 2850.0f && ta > 0.01f) drawVectorTitle(g, T, 2900.0f, ta, (T >= 4050.0f && T <= 4750.0f) ? clampf((T - 4050.0f) / 700.0f, 0, 1) : 0.0f, 118);
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
