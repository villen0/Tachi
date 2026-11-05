// Host test for src/screen_sleep.h.   g++ -std=c++17 -Wall -I../../src screen_sleep_test.cpp -o screen_sleep_test && ./screen_sleep_test
#include "screen_sleep.h"
#include <cstdio>

using namespace screensleep;
static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
  const ScreenSleep s;                                   // defaults: stale 5 s, off after 120 s, wake 30 s, boot grace 25 s
  // car data flowing: always lit
  CHECK(s.update(500000, 499000, true, 0, false));
  CHECK(s.update(500000, 495000, true, 0, false));       // exactly 5 s old still counts as fresh
  // engine stopped at t = 500 s: stays lit until 120 s of silence
  CHECK(s.update(500000 + 60000, 500000, true, 0, false));
  CHECK(s.update(500000 + 119999, 500000, true, 0, false));
  CHECK(!s.update(500000 + 120000, 500000, true, 0, false));
  CHECK(!s.update(500000 + 3600000, 500000, true, 0, false));          // an hour later
  // data returns: on at once
  CHECK(s.update(500000 + 400000, 500000 + 399000, true, 0, false));
  // touch wakes for 30 s, then off again (data still silent)
  { uint32_t t0 = 900000; CHECK(s.update(t0 + 1000, 500000, true, t0, false)); CHECK(s.update(t0 + 29999, 500000, true, t0, false)); CHECK(!s.update(t0 + 30000, 500000, true, t0, false)); }
  // a touch held for a long time keeps refreshing lastTouchMs, so it stays lit
  CHECK(s.update(900000 + 60000, 500000, true, 900000 + 59000, false));
  // power-up with no car (desk): lit for the first 120 s, off afterwards, a touch wakes it
  CHECK(s.update(1000, 0, false, 0, false));
  CHECK(s.update(30000, 0, false, 0, false));
  CHECK(s.update(119999, 0, false, 0, false));
  CHECK(!s.update(120000, 0, false, 0, false));
  CHECK(s.update(125000, 0, false, 124000, false));
  // never off inside the boot grace even with a tiny timeout
  { Config c; c.offAfterMs = 5000; ScreenSleep q(c); CHECK(q.update(20000, 0, false, 0, false)); CHECK(!q.update(26000, 0, false, 0, false)); }
  // demo mode: never off
  CHECK(s.update(9000000, 0, false, 0, true));
  CHECK(s.update(9000000, 500000, true, 0, true));
  // feature disabled: never off
  { Config c; c.offAfterMs = 0; ScreenSleep q(c); CHECK(q.update(9000000, 500000, true, 0, false)); }
  // leaving demo mode zeroes lastValidMs, the touch that did it keeps the screen on for 30 s
  CHECK(s.update(700000, 0, true, 699500, false));
  CHECK(!s.update(731000, 0, true, 699500, false));
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
