// Host-side preview for src/mochi.h: renders every expression into PPM files (no hardware needed).
// Build:  g++ -O2 -std=c++17 -I../../src preview.cpp -o preview
// Run:    ./preview out_dir        then  python3 sheet.py out_dir sheet.png
#include "mochi.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>

#include "mockgfx.h"

// Run the engine for `ms` of simulated time at ~60 fps and return the final frame.
static void run(mochi::Mochi &m, uint32_t &t, uint32_t ms) { uint32_t end = t + ms; while (t < end) { t += 16; m.update(t); } }

int main(int argc, char **argv) {
  std::string out = argc > 1 ? argv[1] : "out";
  system(("mkdir -p " + out).c_str());
  MockGfx g(mochi::W, mochi::H);
  // 1. every expression, settled for 1.4 s (animated overlays are captured mid-animation)
  for (int e = 0; e < mochi::E_COUNT; e++) {
    mochi::Mochi m; m.begin(7); uint32_t t = 1000; m.update(t);
    run(m, t, 500);
    m.react((mochi::Expr)e, 0, t);
    run(m, t, 1400);
    m.render(&g, t);
    char name[400]; snprintf(name, sizeof name, "%s/e%02d_%s.ppm", out.c_str(), e, mochi::exprName((mochi::Expr)e)); savePPM(g, name);
  }
  // 2. transition strips: Default -> Love, Default -> Angry, Sleepy -> Wake (frames at 0,50,100,150,250,400 ms)
  struct Tr { mochi::Expr from, to; const char *tag; } trs[] = {{mochi::E_DEFAULT, mochi::E_LOVE, "love"}, {mochi::E_DEFAULT, mochi::E_ANGRY, "angry"}, {mochi::E_SLEEPY, mochi::E_WAKE, "wake"}};
  for (auto &tr : trs) {
    mochi::Mochi m; m.begin(3); uint32_t t = 1000; m.update(t);
    m.react(tr.from, 0, t); run(m, t, 1500);
    m.react(tr.to, 0, t);
    uint32_t marks[] = {0, 50, 100, 150, 250, 400}; uint32_t t0 = t;
    for (int i = 0; i < 6; i++) { run(m, t, marks[i] > (t - t0) ? marks[i] - (t - t0) : 0); m.render(&g, t); char name[400]; snprintf(name, sizeof name, "%s/t_%s_%d.ppm", out.c_str(), tr.tag, i); savePPM(g, name); }
  }
  // 3. a blink
  { mochi::Mochi m; m.begin(1); uint32_t t = 1000; m.update(t); for (int i = 0; i < 6; i++) { run(m, t, 40); m.render(&g, t); char n[400]; snprintf(n, sizeof n, "%s/b_%d.ppm", out.c_str(), i); savePPM(g, n); } }
  // 4. the boot welcome (headlights drive-in): full and short, sampled over time, plus a 4x title zoom
  for (int variant = 0; variant < 2; variant++) {
    mochi::Mochi m; m.begin(5); uint32_t t = 1000; m.update(t); m.startIntro(t, variant == 1); uint32_t t0 = t;
    std::vector<uint32_t> full = {300, 600, 900, 1200, 1500, 1750, 1950, 2100, 2200, 2350, 2500, 2650, 2800, 2950, 3250, 3600, 4000, 4400, 4900, 5200, 5400, 5700};
    std::vector<uint32_t> shrt = {40, 150, 280, 400, 520, 640, 760, 880, 1000};
    int i = 0;
    for (uint32_t mk : (variant ? shrt : full)) {
      while (t - t0 < mk) { t += 16; m.update(t); }
      m.render(&g, t); uint8_t lr = 0, lg = 0, lb = 0; bool led = m.introLed(&lr, &lg, &lb);
      char n[400]; snprintf(n, sizeof n, "%s/%c_%02d_%04u%s.ppm", out.c_str(), variant ? 's' : 'i', i++, (unsigned)(t - t0), led ? "_led" : ""); savePPM(g, n);
      if (!variant && mk == 4400) { snprintf(n, sizeof n, "%s/z_title.ppm", out.c_str()); savePPM(g, n); }
    }
    printf("%s welcome still active at the end: %d\n", variant ? "short" : "full", (int)m.introActive());
  }
  printf("done\n");
  return 0;
}
