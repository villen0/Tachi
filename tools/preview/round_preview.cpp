// Host-side preview of the Face page on the round 480x480 board: mochi.h drawn 2x through ScaledGfx (src/scaled_gfx.h).
// Build:  g++ -O2 -std=c++17 -I../../src round_preview.cpp -o round_preview
// Run:    ./round_preview out.ppm     (writes a 3x2 contact sheet; pixels outside the round panel are shown dark grey)
#include "mochi.h"
#include "scaled_gfx.h"
#include "mockgfx.h"

static void run(mochi::Mochi &m, uint32_t &t, uint32_t ms) { uint32_t end = t + ms; while (t < end) { t += 16; m.update(t); } }

int main(int argc, char **argv) {
  const int N = 480, S = 2;
  MockGfx panel(N, N);
  ScaledGfx<MockGfx> sg{&panel, S, (N - mochi::W * S) / 2, (N - mochi::H * S) / 2};
  MockGfx sheet(N * 3, N * 2);
  mochi::Expr shots[] = {mochi::E_DEFAULT, mochi::E_TURBO, mochi::E_SWEAT, mochi::E_ANGRY, mochi::E_SLEEPY, mochi::E_COUNT /* welcome */};
  for (int i = 0; i < 6; i++) {
    mochi::Mochi m; m.begin(7); uint32_t t = 1000; m.update(t);
    if (shots[i] == mochi::E_COUNT) { m.startIntro(t, false); run(m, t, 2600); }
    else { run(m, t, 500); m.react(shots[i], 0, t); run(m, t, 1400); }
    panel.fillRect(0, 0, N, N, 0);
    m.render(&sg, t);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
      int dx = x - N / 2, dy = y - N / 2;
      uint16_t c = dx * dx + dy * dy <= (N / 2) * (N / 2) ? panel.px[y * N + x] : 0x2104;
      sheet.put((i % 3) * N + x, (i / 3) * N + y, c);
    }
  }
  savePPM(sheet, argc > 1 ? argv[1] : "round.ppm");
  printf("ok\n");
  return 0;
}
