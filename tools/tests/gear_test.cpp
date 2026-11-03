// Host test for src/gear.h.   g++ -std=c++17 -Wall -I../../src gear_test.cpp -o gear_test && ./gear_test
#include "gear.h"
#include <cstdio>
static int bad = 0;
static void chk(int rpm, int mph, char want) { char g = gear::estimateGear(rpm, mph); if (g != want) { bad++; printf("rpm %d mph %d: got %c want %c\n", rpm, mph, g, want); } }
int main() {
  chk(0, 0, 'N'); chk(800, 1, 'N'); chk(300, 40, '-');                 // stopped, engine off
  chk(167 * 10, 10, '1'); chk(95 * 20, 20, '2'); chk(68 * 30, 30, '3'); chk(47 * 50, 50, '4'); chk(33 * 60, 60, '5'); chk(28 * 70, 70, '6');
  chk(2000, 50, '-');                                                  // ratio 40: between 4th and 5th
  printf(bad ? "FAIL %d\n" : "gear PASS\n", bad); return bad;
}
