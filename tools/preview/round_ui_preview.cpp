// Host preview of the round dial pages: renders src/round_ui.h to PPM files (same code as the board).
// Build: g++ -std=c++17 -O2 -I../../src round_ui_preview.cpp -o round_ui_preview && ./round_ui_preview out_dir
#include <cstdio>
#include <vector>
#include <string>
#include "round_ui.h"

static void save(const std::vector<uint16_t> &fb, const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", rui::W, rui::H);
  for (int y = 0; y < rui::H; y++) for (int x = 0; x < rui::W; x++) {
    float dx = x + 0.5f - rui::CX, dy = y + 0.5f - rui::CY; bool inside = dx * dx + dy * dy <= 240.0f * 240.0f;   // the panel is round
    uint16_t c = inside ? fb[y * rui::W + x] : 0;
    unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  if (!rui::begin()) { puts("alloc failed"); return 1; }
  std::vector<uint16_t> fb(rui::W * rui::H, 0);
  const char *names[] = {"sport", "tach", "speedo", "vitals"};
  struct Case { float rpm, mph; const char *tag; } cases[] = {{3763, 63, "mid"}, {6900, 112, "red"}, {0, 0, "idle"}};
  for (int pg = 0; pg < rui::PG_COUNT; pg++) for (auto &c : cases) {
    rui::State &S = rui::st(); S.page = -1; S.lastT = 0;           // restart each picture from scratch
    rui::Data d; d.rpm = c.rpm; d.mph = c.mph; d.haveCool = d.haveFuel = d.haveBatt = d.haveLoad = true;
    d.coolC = c.rpm > 6000 ? 109 : 95; d.coolShown = (int)(d.coolC * 1.8f + 32.5f); d.fuelPct = c.rpm > 6000 ? 12 : 61; d.battV = 14.0f; d.loadPct = c.rpm > 6000 ? 92 : 72;
    d.dotN = 7; d.dotCur = pg + 1;
    rui::frame(fb.data(), pg, d, 1000); rui::frame(fb.data(), pg, d, 1100);
    d.rpm = c.rpm; rui::frame(fb.data(), pg, d, 2000);
    save(fb, dir + "/" + names[pg] + "_" + c.tag + ".ppm");
  }
  puts("done");
  return 0;
}
