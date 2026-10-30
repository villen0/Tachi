// Host self-test for src/round_ui.h: after many incremental frames the picture must equal a from-scratch render of the same state,
// and every changed pixel must lie inside a span that flushDirty() reports (otherwise the panel would keep stale pixels).
// Build: g++ -std=c++17 -O2 -I../../src round_ui_selftest.cpp -o round_ui_selftest && ./round_ui_selftest
#include <cstdio>
#include <vector>
#include "round_ui.h"
int main() {
  if (!rui::begin()) return 1;
  int bad = 0;
  for (int pg = 0; pg < rui::PG_COUNT; pg++) {
    std::vector<uint16_t> fb(rui::W * rui::H, 0), panel(rui::W * rui::H, 0), ref(rui::W * rui::H, 0);
    rui::State &S = rui::st(); S.page = -1; S.lastT = 0;
    rui::Data d; d.haveCool = d.haveFuel = d.haveBatt = d.haveLoad = true; d.dotN = 6; d.dotCur = pg + 1; d.coolShown = 203; d.battV = 14.0f;
    uint32_t now = 1000;
    for (int i = 0; i < 400; i++) {
      float ph = i * 0.07f;
      d.rpm = 800 + 3000 * (0.5f + 0.5f * sinf(ph)) + (i % 97 == 0 ? 3200 : 0); d.mph = 45 * (0.5f + 0.5f * sinf(ph * 0.6f));
      d.coolC = 80 + (i / 8) % 40; d.coolShown = (int)(d.coolC * 1.8f + 32); d.fuelPct = 100 - (i / 5) % 100; d.battV = 11.0f + (i % 50) * 0.08f; d.loadPct = (i * 3) % 100;
      now += 33;
      rui::frame(fb.data(), pg, d, now);
      rui::flushDirty([&](int x, int y, int w) { for (int k = 0; k < w; k++) panel[y * rui::W + x + k] = fb[y * rui::W + x + k]; });
    }
    // 1) the panel (only dirty spans copied) must equal the frame buffer
    int stale = 0; for (size_t i = 0; i < fb.size(); i++) if (fb[i] != panel[i]) stale++;
    // 2) the incremental frame buffer must equal a full redraw of the same state
    rui::frame(ref.data(), pg, d, now, true);                       // same eased values (dt = 0), full rebuild into a fresh buffer
    int diff = 0; for (size_t i = 0; i < fb.size(); i++) if (fb[i] != ref[i]) diff++;
    printf("page %d: stale-on-panel %d px, differs-from-full-redraw %d px\n", pg, stale, diff);
    if (diff || stale) {
      int x0 = 999, y0 = 999, x1 = -1, y1 = -1;
      for (int i = 0; i < rui::W * rui::H; i++) if (fb[i] != ref[i] || fb[i] != panel[i]) { int x = i % rui::W, y = i / rui::W; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
      printf("   bad pixels lie in x %d..%d, y %d..%d\n", x0, x1, y0, y1);
    }
    bad += stale + diff;
  }
  puts(bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
