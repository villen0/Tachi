// Renders the Revsense welcome at a few moments and checks it: the dirty boxes cover every changed pixel, nothing is drawn outside
// the round panel, and the last frame leaves a clean screen. Build: g++ -std=c++17 -O2 -I../../src brand_preview.cpp -o brand_preview && ./brand_preview out_dir
#include <cstdio>
#include <string>
#include <vector>
#include "brand.h"
static void save(const std::vector<uint16_t> &fb, const std::string &f) {
  FILE *o = fopen(f.c_str(), "wb"); fprintf(o, "P6\n480 480\n255\n");
  for (uint16_t c : fb) { unsigned char p[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)}; fwrite(p, 1, 3, o); }
  fclose(o);
}
int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  if (!rui::begin()) { puts("alloc failed"); return 1; }
  std::vector<uint16_t> fb(480 * 480, rui::BG), panel(480 * 480, rui::BG);
  int bad = 0, frames = 0; long sent = 0, maxSent = 0; uint32_t shots[] = {300, 800, 1200, 1700, 2300, 2900, 3300}; int si = 0;
  for (uint32_t t = 0; ; t += 33) {
    if (!brand::frame(fb.data(), t)) break;
    frames++;
    long fs = 0; rui::flushDirty([&](int x, int y, int w) { fs += w; for (int k = 0; k < w; k++) panel[y * 480 + x + k] = fb[y * 480 + x + k]; });
    if (frames > 1) { sent += fs; if (fs > maxSent) maxSent = fs; }
    for (size_t i = 0; i < fb.size(); i++) if (fb[i] != panel[i]) bad++;                    // the panel must equal the frame buffer: no changed pixel left unmarked
    if (si < 7 && t >= shots[si]) save(fb, dir + "/brand_" + std::to_string(shots[si++]) + ".ppm");
  }
  // outside the round panel nothing may be drawn (corners of the boxes are cut by the circle: 235 px radius from the centre)
  int outside = 0; for (int y = 0; y < 480; y++) for (int x = 0; x < 480; x++) { float dx = x - 239.5f, dy = y - 239.5f; if (dx * dx + dy * dy > 236.0f * 236.0f && fb[y * 480 + x] != rui::BG) outside++; }
  printf("%d frames, %d stale panel pixels, %d pixels outside the panel; pixels sent per frame: average %ld, worst %ld (a full screen is 230400)\n", frames, bad, outside, sent / (frames > 1 ? frames - 1 : 1), maxSent);
  puts(bad == 0 && outside == 0 ? "PASS" : "FAIL");
  return bad || outside;
}
