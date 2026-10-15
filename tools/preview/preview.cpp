// Host-side preview for src/mochi.h: renders every expression into PPM files (no hardware needed).
// Build:  g++ -O2 -std=c++17 -I../../src preview.cpp -o preview
// Run:    ./preview out_dir        then  python3 sheet.py out_dir sheet.png
#include "mochi.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>

struct MockGfx {
  int w, h; std::vector<uint16_t> px;
  MockGfx(int w_, int h_) : w(w_), h(h_), px(w_ * h_, 0) {}
  void put(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < w && y < h) px[y * w + x] = c; }
  void fillRect(int x, int y, int ww, int hh, uint16_t c) { for (int j = 0; j < hh; j++) for (int i = 0; i < ww; i++) put(x + i, y + j, c); }
  void fillCircle(int cx, int cy, int r, uint16_t c) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) put(cx + i, cy + j, c); }
  void fillEllipse(int cx, int cy, int rx, int ry, uint16_t c) {
    if (rx < 1 || ry < 1) return;
    for (int j = -ry; j <= ry; j++) for (int i = -rx; i <= rx; i++) if ((double)i * i / ((double)rx * rx) + (double)j * j / ((double)ry * ry) <= 1.0) put(cx + i, cy + j, c);
  }
  void fillRoundRect(int x, int y, int ww, int hh, int r, uint16_t c) {
    if (ww <= 0 || hh <= 0) return; if (r > ww / 2) r = ww / 2; if (r > hh / 2) r = hh / 2;
    for (int j = 0; j < hh; j++) for (int i = 0; i < ww; i++) {
      int dx = 0, dy = 0;
      if (i < r) dx = r - i - 1; else if (i >= ww - r) dx = i - (ww - r);
      if (j < r) dy = r - j - 1; else if (j >= hh - r) dy = j - (hh - r);
      if (dx * dx + dy * dy <= r * r || (dx == 0 || dy == 0)) put(x + i, y + j, c);
    }
  }
  static int edge(int ax, int ay, int bx, int by, int px_, int py_) { return (bx - ax) * (py_ - ay) - (by - ay) * (px_ - ax); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2}), miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    int area = edge(x0, y0, x1, y1, x2, y2); if (area == 0) return;
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
      int w0 = edge(x1, y1, x2, y2, x, y), w1 = edge(x2, y2, x0, y0, x, y), w2 = edge(x0, y0, x1, y1, x, y);
      if (area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0)) put(x, y, c);
    }
  }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { put(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
  }
};

static void savePPM(const MockGfx &g, const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", g.w, g.h);
  for (uint16_t c : g.px) { unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}

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
