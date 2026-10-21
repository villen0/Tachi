// Host test for src/car_events.h.   g++ -std=c++17 -Wall -I../../src car_events_test.cpp -o car_events_test && ./car_events_test
#include "car_events.h"
#include <cstdio>

using namespace carevents;
static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// Convenience: fresh data, battery fine, fuel fine.
static Input mk(uint32_t now, int rpm, int mph) { return Input{now, true, rpm, true, mph, true, 14.0f, true, 60.0f}; }

int main() {
  // --- engine start ---
  { Detector d; uint32_t t = 100000;
    for (int i = 0; i < 40; i++) { CHECK(d.update(mk(t, 0, 0)) == EV_NONE); t += 100; }     // 4 s with the engine off
    CHECK(d.update(mk(t, 450, 0)) == EV_NONE);                                              // cranking, not yet at the start level
    CHECK(d.update(mk(t + 100, 800, 0)) == EV_ENGINE_START);
    CHECK(d.update(mk(t + 200, 800, 0)) == EV_NONE);                                        // only once
  }
  { Detector d; uint32_t t = 100000;                                                         // engine off less than 3 s: no start
    for (int i = 0; i < 10; i++) { d.update(mk(t, 0, 0)); t += 100; }
    CHECK(d.update(mk(t, 800, 0)) == EV_NONE);
  }
  { Detector d; uint32_t t = 100000;                                                         // data gap is not "engine was off"
    Input in = mk(t, 800, 0);
    for (int i = 0; i < 5; i++) { d.update(in); t += 100; in = mk(t, 800, 0); }
    in.rpmFresh = false; in.rpm = 0;
    for (int i = 0; i < 50; i++) { d.update(in); t += 100; in.now = t; }
    CHECK(d.update(mk(t, 800, 0)) == EV_NONE);
  }
  { Detector d; uint32_t t = 100000;                                                         // second start inside the cooldown is ignored
    for (int i = 0; i < 40; i++) { d.update(mk(t, 0, 0)); t += 100; }
    CHECK(d.update(mk(t, 800, 0)) == EV_ENGINE_START); t += 100;
    for (int i = 0; i < 40; i++) { d.update(mk(t, 0, 0)); t += 100; }
    CHECK(d.update(mk(t, 800, 0)) == EV_NONE);
  }
  // --- hard braking ---
  { Detector d; uint32_t t = 100000;
    CHECK(d.update(mk(t, 2000, 50)) == EV_NONE); t += 1000;                                  // seeds the previous speed
    CHECK(d.update(mk(t, 1500, 52)) == EV_NONE); t += 1000;                                  // gentle
    CHECK(d.update(mk(t, 900, 35)) == EV_HARD_BRAKE); t += 1000;                             // 17 mph drop in a second
    CHECK(d.update(mk(t, 900, 15)) == EV_NONE);                                              // still inside the 8 s cooldown
  }
  { Detector d; uint32_t t = 100000;                                                         // slow stop: below the minimum speed
    d.update(mk(t, 900, 12)); t += 1000;
    CHECK(d.update(mk(t, 800, 0)) == EV_NONE);
  }
  { Detector d; uint32_t t = 100000;                                                         // first sample after a gap never counts
    d.update(mk(t, 2000, 60)); t += 500;
    Input in = mk(t, 2000, 0); in.speedFresh = false;
    d.update(in); t += 5000;
    CHECK(d.update(mk(t, 900, 5)) == EV_NONE);                                               // seeds prevMph again, no event
  }
  { Detector d; uint32_t t = 100000;                                                         // calls stopped for a while (other page on screen)
    d.update(mk(t, 2000, 60)); t += 1000;
    d.update(mk(t, 2000, 60)); t += 60000;
    CHECK(d.update(mk(t, 900, 20)) == EV_NONE);
  }
  // --- battery and fuel ---
  { Detector d; uint32_t t = 100000;
    Input in = mk(t, 1200, 0); in.battV = 11.0f;
    CHECK(d.update(in) == EV_LOW_BATTERY);
    in.now = t + 1000; CHECK(d.update(in) == EV_NONE);                                       // cooldown
    in.now = t + 61000; CHECK(d.update(in) == EV_LOW_BATTERY);                               // again after 60 s
  }
  { Detector d; Input in = mk(100000, 0, 0); in.battV = 11.0f;                               // engine off: low voltage is normal-ish, no reaction
    CHECK(d.update(in) == EV_NONE);
    in = mk(100100, 1200, 0); in.battV = 11.0f; in.battFresh = false;                        // stale battery reading
    CHECK(d.update(in) == EV_NONE);
  }
  { Detector d; Input in = mk(100000, 1200, 30); in.fuelPct = 5.0f;
    CHECK(d.update(in) == EV_LOW_FUEL);
    in.now = 101000; CHECK(d.update(in) == EV_NONE);
    in = mk(300000, 1200, 30); in.fuelPct = 5.0f; CHECK(d.update(in) == EV_LOW_FUEL);        // after 120 s
    in = mk(500000, 1200, 30); in.fuelPct = 40.0f; CHECK(d.update(in) == EV_NONE);
    in = mk(700000, 1200, 30); in.fuelPct = 3.0f; in.fuelFresh = false; CHECK(d.update(in) == EV_NONE);   // stale
  }
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
