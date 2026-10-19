#pragma once
// Draws mochi.h's 172x180 face art at an integer scale: offers the six calls mochi.h needs and multiplies every
// coordinate. Used by the round board (2x, straight into the 480x480 canvas) and by tools/preview/round_preview.cpp.
#include <math.h>
#include <stdint.h>

template <class G> struct ScaledGfx {
  G *g; int s, ox, oy;
  int X(float v) const { return ox + (int)lroundf(v * s); }
  int Y(float v) const { return oy + (int)lroundf(v * s); }
  int S(float v) const { return (int)lroundf(v * s); }
  void fillRect(float x, float y, float w, float h, uint16_t c) { int x0 = X(x), y0 = Y(y); g->fillRect(x0, y0, X(x + w) - x0, Y(y + h) - y0, c); }
  void fillRoundRect(float x, float y, float w, float h, float r, uint16_t c) { int x0 = X(x), y0 = Y(y); g->fillRoundRect(x0, y0, X(x + w) - x0, Y(y + h) - y0, S(r), c); }
  void fillCircle(float x, float y, float r, uint16_t c) { g->fillCircle(X(x), Y(y), S(r), c); }
  void fillEllipse(float x, float y, float rx, float ry, uint16_t c) { g->fillEllipse(X(x), Y(y), S(rx), S(ry), c); }
  void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) { g->fillTriangle(X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), c); }
  void drawLine(float x0, float y0, float x1, float y1, uint16_t c) {   // a 1 px line becomes s px thick
    for (int k = 0; k < s; k++) g->drawLine(X(x0) + k, Y(y0), X(x1) + k, Y(y1), c);
    for (int k = 1; k < s; k++) g->drawLine(X(x0), Y(y0) + k, X(x1), Y(y1) + k, c);
  }
};
