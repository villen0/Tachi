#pragma once
// Pixel-buffer stand-in for the Arduino_GFX calls used by mochi.h / cluster.h (host preview only).
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <stdint.h>

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

