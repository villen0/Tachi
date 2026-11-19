// Host test for src/panel_crop.h. Build: g++ -std=c++17 -O2 -I../../src panel_crop_test.cpp -o panel_crop_test && ./panel_crop_test
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "panel_crop.h"
using namespace panelcrop;
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
  const int FB = 480, PW = 466, CR = 7;
  // every span the firmware could send: every start x and width in a row, rows inside and outside the visible area
  long spans = 0, drawn = 0;
  for (int x = 0; x < FB; x++) for (int w = 1; x + w <= FB; w += (w < 6 ? 1 : 7)) for (int y : {0, 6, 7, 8, 100, 239, 472, 473, 474, 479}) {
    Span s; bool ok = cropSpan(x, y, w, CR, CR, PW, PW, &s); spans++;
    // the rows 7..472 are visible (y - 7 in 0..465)
    bool rowVisible = y - CR >= 0 && y - CR < PW;
    // visible columns of the request: frame-buffer x..x+w-1 intersected with CR..CR+PW-1
    int vx0 = x > CR ? x : CR, vx1 = (x + w - 1) < (CR + PW - 1) ? (x + w - 1) : (CR + PW - 1);
    bool anyVisible = rowVisible && vx0 <= vx1;
    CHECK(ok == anyVisible);
    if (!ok) continue;
    drawn++;
    CHECK(s.py == y - CR); CHECK(s.px >= 0 && s.w > 0 && s.px + s.w <= PW);                 // inside the panel
    CHECK((s.px & 1) == 0 && (s.w & 1) == 0);                                              // even start and width
    CHECK(s.px <= vx0 - CR && s.px + s.w - 1 >= vx1 - CR);                                // covers everything that was visible
    // the source pixel for panel column c is frame-buffer column c + CR (as the pointer is advanced by skip): it must stay inside the buffer
    int srcFirst = x + s.skip, srcLast = srcFirst + s.w - 1;                               // frame-buffer columns read
    CHECK(srcFirst >= 0 && srcLast < FB);
    CHECK(srcFirst - x == s.px - (x - CR));                                                // pixel (px) comes from frame-buffer column px + CR
  }
  for (int y = 0; y < 466; y++) { int a = pairRow(y); CHECK((a & 1) == 0 && (y == a || y == a + 1) && a + 1 < 466); }   // every panel row belongs to a pair that fits in the panel
  printf("%ld spans tried, %ld visible\n", spans, drawn);
  // touch mapping
  int x, y;
  mapTouch(0, 0, false, false, false, PW, PW, CR, CR, &x, &y); CHECK(x == 7 && y == 7);
  mapTouch(465, 465, false, false, false, PW, PW, CR, CR, &x, &y); CHECK(x == 472 && y == 472);
  mapTouch(10, 100, false, true, false, PW, PW, CR, CR, &x, &y); CHECK(x == 465 - 10 + 7 && y == 107);
  mapTouch(10, 100, false, false, true, PW, PW, CR, CR, &x, &y); CHECK(x == 17 && y == 465 - 100 + 7);
  mapTouch(10, 100, true, false, false, PW, PW, CR, CR, &x, &y); CHECK(x == 107 && y == 17);
  mapTouch(10, 100, true, true, true, PW, PW, CR, CR, &x, &y); CHECK(x == 465 - 100 + 7 && y == 465 - 10 + 7);
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
