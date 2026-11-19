#pragma once
// Geometry for a panel that is smaller than the 480x480 frame buffer (the AMOLED board: 466x466 shown from the middle of the buffer),
// and for mapping the touch controller's raw point to frame-buffer pixels. Pure C++ so it is unit-tested on a PC: tools/tests/panel_crop_test.cpp
namespace panelcrop {

struct Span { int px, py, w, skip; };   // where to draw on the panel, how wide, and the offset (in pixels, may be -1) from the span's first source pixel

// One row span of the frame buffer (x, y, w in frame-buffer pixels) -> the panel span to send. Returns false when nothing of it is visible.
// The visible part is cropped; the CO5300 likes even column starts and widths, so the span is widened to even (never past the panel).
inline bool cropSpan(int x, int y, int w, int cropX, int cropY, int panelW, int panelH, Span *s) {
  int px = x - cropX, py = y - cropY, skip = 0;
  if (py < 0 || py >= panelH || w <= 0) return false;
  if (px < 0) { skip = -px; w += px; px = 0; }
  if (px + w > panelW) w = panelW - px;
  if (w <= 0) return false;
  if (px & 1) { px--; skip--; w++; }
  if (w & 1) w++;
  if (px + w > panelW) { w = panelW - px; if (w & 1) w--; }
  if (w <= 0) return false;
  *s = {px, py, w, skip};
  return true;
}

// The CO5300 (like most QSPI AMOLEDs) wants drawing windows that start on an even row and are an even number of rows tall: a one-row span is
// sent together with its partner row (an even row pairs with the one below it, an odd row with the one above). Returns the pair's first row.
inline int pairRow(int py) { return py & ~1; }

// The touch controller's raw point -> frame-buffer pixels (the shown panel is the middle of the frame buffer).
inline void mapTouch(int rx, int ry, bool swapXY, bool flipX, bool flipY, int panelW, int panelH, int cropX, int cropY, int *x, int *y) {
  int a = rx, b = ry;
  if (swapXY) { int t = a; a = b; b = t; }
  if (flipX) a = panelW - 1 - a;
  if (flipY) b = panelH - 1 - b;
  *x = a + cropX; *y = b + cropY;
}

}  // namespace panelcrop
