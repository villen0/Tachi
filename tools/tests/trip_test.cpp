// Host test for src/trip.h.   g++ -std=c++17 -Wall -I../../src trip_test.cpp -o trip_test && ./trip_test
#include "trip.h"
#include <cstdio>
#include <cstring>

using namespace trip;
static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

struct Sim {                                // feeds the tracker one sample per second
  Tracker t; uint32_t now = 100000; Tracker::Event last = Tracker::T_NONE;
  Sim() {}
  Tracker::Event step(int rpm, int mph, int coolC = 90, float batt = 14.0f, float boost = -10.0f, float fuel = 60.0f, bool fresh = true) {
    Sample s; s.now = now += 1000;
    s.rpmFresh = fresh; s.rpm = rpm; s.speedFresh = fresh; s.mph = mph; s.coolFresh = fresh; s.coolC = coolC; s.intakeFresh = fresh; s.intakeC = 30;
    s.battFresh = fresh; s.battV = batt; s.boostFresh = fresh; s.boostPsi = boost; s.fuelFresh = fresh; s.fuelPct = fuel;
    last = t.update(s); return last;
  }
  void idleSeconds(int n, int coolC = 90) { for (int i = 0; i < n; i++) step(800, 0, coolC); }
  void cruise(int n, int mph, int rpm = 2000) { for (int i = 0; i < n; i++) step(rpm, mph); }
  // engine off: fresh data with RPM 0, then silence, until the tracker ends the trip
  Tracker::Event park() { Tracker::Event e = Tracker::T_NONE; for (int i = 0; i < 40 && e != Tracker::T_ENDED && e != Tracker::T_DISCARDED; i++) e = step(0, 0, 90, 12.4f, -1000, 60, false); return e; }
};

int main() {
  // --- a normal trip: start, drive 10 min at 45 mph, park ---
  {
    Sim s;
    CHECK(s.step(800, 0) == Tracker::T_STARTED);
    CHECK(s.t.active());
    s.cruise(600, 45);
    Record live = s.t.live();
    CHECK(live.topMph == 45);
    CHECK(live.distCentiMi >= 740 && live.distCentiMi <= 760);                 // 10 min at 45 mph = 7.5 miles
    CHECK(s.park() == Tracker::T_ENDED);
    Record r; CHECK(s.t.takeFinished(&r));
    CHECK(!s.t.active());
    CHECK(r.durationS >= 600 && r.durationS <= 603);
    CHECK(r.distCentiMi >= 740 && r.distCentiMi <= 760);
    CHECK(r.avgMph >= 44 && r.avgMph <= 46);
    CHECK(r.maxRpm == 2000);
    CHECK(r.maxCoolC == 90 && r.maxIntakeC == 30);
    CHECK(r.peakBoostDeciPsi == -100);                                          // -10.0 psi, vacuum
    CHECK(r.minBattDeciV == 140 && r.maxBattDeciV == 140);
    CHECK(r.fuelUsedPct == 0);
    CHECK(r.hardBrakes == 0 && r.hardLaunches == 0);
    CHECK(r.rating == 100 && gradeOf(r.rating) == 'A');
    CHECK(!s.t.takeFinished(&r));                                               // only once
    CHECK(s.t.nextSeq() == 2);
  }
  // --- a quick start-stop is discarded, nothing stored ---
  {
    Sim s; s.step(800, 0); s.idleSeconds(30);
    CHECK(s.park() == Tracker::T_DISCARDED);
    Record r; CHECK(!s.t.takeFinished(&r));
    CHECK(s.t.nextSeq() == 1);
  }
  // --- a long idle with no distance is discarded too ---
  { Sim s; s.step(800, 0); s.idleSeconds(300); CHECK(s.park() == Tracker::T_DISCARDED); }
  // --- peaks and ranges ---
  {
    Sim s; s.step(800, 0);
    for (int i = 0; i < 120; i++) s.step(2500, 40, 90 + i / 10, 13.8f + (i % 3) * 0.1f, -5.0f + i * 0.1f, 60.0f - i * 0.05f);
    s.step(6200, 80, 112, 12.9f, 4.5f, 54.0f);
    for (int i = 0; i < 60; i++) s.step(2000, 40, 90, 14.0f, -10.0f, 54.0f);
    CHECK(s.park() == Tracker::T_ENDED);
    Record r; CHECK(s.t.takeFinished(&r));
    CHECK(r.topMph == 80 && r.maxRpm == 6200);
    CHECK(r.maxCoolC == 112);
    CHECK(r.peakBoostDeciPsi == 69);                                            // the ramp reached 6.9 psi before the 4.5 psi sample
    CHECK(r.minBattDeciV == 129 && r.maxBattDeciV == 140);
    CHECK(r.fuelUsedPct >= 5 && r.fuelUsedPct <= 7);
    CHECK((r.flags & 3) == 3);                                                  // reached the "worried" coolant level
    CHECK(r.rating <= 80);                                                      // 20 points for overheating
  }
  // --- hard braking and hard launches are counted, the rating drops ---
  {
    Sim s; s.step(800, 0); s.idleSeconds(5);
    for (int k = 0; k < 4; k++) {
      s.step(3500, 5); s.step(4200, 14);                                        // launch: from 5 mph +9 in one second at high revs
      s.cruise(20, 50);
      s.step(1500, 50); s.step(1000, 30); s.step(900, 12);                      // hard stop: -20 mph in one second
      s.cruise(20, 10);
    }
    s.cruise(60, 30);
    CHECK(s.park() == Tracker::T_ENDED);
    Record r; CHECK(s.t.takeFinished(&r));
    CHECK(r.hardBrakes >= 3 && r.hardBrakes <= 4);
    CHECK(r.hardLaunches >= 3 && r.hardLaunches <= 4);
    CHECK(r.rating < 90);                                                       // visibly worse than the calm trip
  }
  // --- gentle trip: a slow stop does not count as hard braking ---
  {
    Sim s; s.step(800, 0);
    s.cruise(120, 50); for (int m = 50; m >= 0; m -= 5) s.step(1200, m); s.cruise(5, 0);
    for (int i = 0; i < 60; i++) s.step(2000, 35);
    CHECK(s.park() == Tracker::T_ENDED);
    Record r; CHECK(s.t.takeFinished(&r));
    CHECK(r.hardBrakes == 0);
  }
  // --- a gap in the polling (30 s with no samples) is not counted as driving ---
  {
    Sim s; s.step(800, 0); s.cruise(120, 60);
    uint32_t before = s.t.live().distCentiMi;
    s.now += 30000; s.step(2000, 60);                                           // one sample 30 s later
    uint32_t after = s.t.live().distCentiMi;
    CHECK(after - before <= 10);                                                // at most 5 s worth (60 mph = 0.0167 mi/s)
  }
  // --- engine restarted within the end window: still the same trip ---
  {
    Sim s; s.step(800, 0); s.cruise(200, 40);
    for (int i = 0; i < 10; i++) s.step(0, 0, 90, 12.4f, -1000, 60, false);     // 10 s off (a stall or a start-stop)
    CHECK(s.step(800, 0) == Tracker::T_NONE);
    CHECK(s.t.active());
  }
  // --- unknown values stay unknown ---
  {
    Sim s; { Sample x; x.now = s.now += 1000; x.rpmFresh = true; x.rpm = 800; s.t.update(x); }   // the car answers RPM and speed only
    for (int i = 0; i < 200; i++) { Sample x; x.now = s.now += 1000; x.rpmFresh = true; x.rpm = 2000; x.speedFresh = true; x.mph = 40; s.t.update(x); }
    s.park();
    Record r; CHECK(s.t.takeFinished(&r));
    CHECK(r.maxCoolC == UNKNOWN16 && r.maxIntakeC == UNKNOWN16 && r.peakBoostDeciPsi == UNKNOWN16);
    CHECK(r.minBattDeciV == 0 && r.maxBattDeciV == 0 && r.fuelUsedPct == -1);
  }
  // --- new fault codes are listed once, up to four, and counted beyond that ---
  {
    Sim s; s.step(800, 0); s.cruise(150, 40);
    s.t.addNewCode("P0301"); s.t.addNewCode("P0301"); s.t.addNewCode("P0420"); s.t.addNewCode("P0171"); s.t.addNewCode("P0172"); s.t.addNewCode("P0300");
    s.cruise(30, 40); s.park();
    Record r; CHECK(s.t.takeFinished(&r));
    CHECK(r.newCodeCount == 5);                                                 // 5 distinct, only the first 4 are named
    CHECK(strcmp(r.newCodes[0], "P0301") == 0 && strcmp(r.newCodes[1], "P0420") == 0 && strcmp(r.newCodes[3], "P0172") == 0);
  }
  // --- rating examples ---
  {
    Record r; r.durationS = 1200; r.distCentiMi = 1000;
    CHECK(computeRating(r) == 100);
    r.hardBrakes = 2; CHECK(computeRating(r) == 88);                            // 6 * (2 / 10 miles) * 10 = 12
    r.hardLaunches = 2; CHECK(computeRating(r) == 82);
    r.secHighRpm = 120; CHECK(computeRating(r) == 78);                          // 2 minutes above 5500 = 4
    r.flags = 1; CHECK(computeRating(r) == 68);
    r.flags = 3; CHECK(computeRating(r) == 58);
    r.idleS = 400; CHECK(computeRating(r) == 53);                               // idled a third of the trip: 5 more
    r.hardBrakes = 200; CHECK(computeRating(r) == 0);                           // never below 0
    CHECK(gradeOf(95) == 'A' && gradeOf(85) == 'B' && gradeOf(75) == 'C' && gradeOf(65) == 'D' && gradeOf(10) == 'F');
  }
  // --- the history ring ---
  {
    History h; CHECK(h.valid()); CHECK(h.get(0) == nullptr);
    for (int i = 1; i <= 7; i++) { Record r; r.seq = (uint16_t)i; r.topMph = (uint8_t)(10 * i); h.push(r); }
    CHECK(h.count == HISTORY);
    CHECK(h.get(0)->seq == 7 && h.get(1)->seq == 6 && h.get(4)->seq == 3);       // newest first, the two oldest were overwritten
    CHECK(h.get(5) == nullptr && h.get(-1) == nullptr);
    CHECK(h.nextSeq == 8);
    // save and load as raw bytes (what main.cpp does with Preferences)
    uint8_t buf[sizeof(History)]; memcpy(buf, &h, sizeof(h));
    History g; memcpy(&g, buf, sizeof(g));
    CHECK(g.valid() && g.get(0)->seq == 7 && g.get(0)->topMph == 70);
    // corrupted / old data is rejected
    History bad = h; bad.magic = 0; CHECK(!bad.valid());
    bad = h; bad.recSize = 3; CHECK(!bad.valid());
    bad = h; bad.head = 9; CHECK(!bad.valid());
    bad = h; bad.version = 2; CHECK(!bad.valid());
  }
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
