// Host test for src/report_view.h.   g++ -std=c++17 -Wall -I../../src report_view_test.cpp -o report_view_test && ./report_view_test
#include "report_view.h"
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define STREQ(a, b) do { checks++; if (strcmp((a), (b)) != 0) { fails++; printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, (a), (b)); } } while (0)

int main() {
  report::Units us;                                     // mph, F, psi
  trip::Record r; r.seq = 7; r.durationS = 2520; r.idleS = 200; r.distCentiMi = 1840; r.topMph = 74; r.avgMph = 26; r.maxRpm = 4850;
  r.maxCoolC = 95; r.maxIntakeC = 40; r.peakBoostDeciPsi = -24; r.minBattDeciV = 131; r.maxBattDeciV = 142; r.hardBrakes = 1; r.hardLaunches = 3; r.rating = 91;
  { rui::TripView v; report::tripView(v, &r, false, 0, 3, us);
    CHECK(v.have && v.grade == 'A' && v.rating == 91 && v.sel == 0 && v.nSaved == 3 && !v.live);
    STREQ(v.title, "DRIVE REPORT"); STREQ(v.t[0].label, "TOP MPH"); STREQ(v.t[0].value, "74"); STREQ(v.t[1].value, "4850");
    STREQ(v.t[2].label, "MAX F"); STREQ(v.t[2].value, "203"); STREQ(v.t[3].label, "PEAK PSI"); STREQ(v.t[3].value, "-2.4");
    STREQ(v.t[4].value, "13.1-14.2"); STREQ(v.t[5].value, "0"); STREQ(v.foot1, "42 MIN  18.4 MI"); STREQ(v.foot2, "1 HARD BRAKE  3 LAUNCHES");
    CHECK(v.t[5].col == rui::WHITE && v.t[2].col == rui::WHITE); }
  // metric units
  { report::Units m; m.mph = false; m.tempF = false; m.psi = false; rui::TripView v; report::tripView(v, &r, false, 0, 0, m);
    STREQ(v.t[0].label, "TOP KMH"); STREQ(v.t[0].value, "119"); STREQ(v.t[2].label, "MAX C"); STREQ(v.t[2].value, "95");
    STREQ(v.t[3].label, "PEAK BAR"); STREQ(v.t[3].value, "-0.17"); STREQ(v.foot1, "42 MIN  29.6 KM"); }
  // live, an earlier trip, long trip, warnings
  { rui::TripView v; report::tripView(v, &r, true, 0, 2, us); STREQ(v.title, "LIVE TRIP"); CHECK(v.live); }
  { rui::TripView v; report::tripView(v, &r, false, 2, 3, us); STREQ(v.title, "TRIP 7"); }
  { trip::Record q = r; q.durationS = 5400; q.flags = 3; q.newCodeCount = 2; q.minBattDeciV = 112; q.secHighRpm = 90; q.rating = 55;
    rui::TripView v; report::tripView(v, &q, false, 0, 1, us);
    STREQ(v.foot1, "1H 30M  18.4 MI"); CHECK(v.grade == 'F'); CHECK(v.t[2].col == rui::RED && v.t[5].col == rui::RED && v.t[4].col == rui::RED && v.t[1].col == rui::ORANGE); STREQ(v.t[5].value, "2"); }
  // unknown values show "--"
  { trip::Record q; q.durationS = 600; q.distCentiMi = 500; rui::TripView v; report::tripView(v, &q, false, 0, 0, us);
    STREQ(v.t[2].value, "--"); STREQ(v.t[3].value, "--"); STREQ(v.t[4].value, "--"); STREQ(v.foot1, "10 MIN  5.0 MI"); }
  // no trip
  { rui::TripView v; report::tripView(v, nullptr, false, 0, 0, us); CHECK(!v.have); STREQ(v.title, "DRIVE REPORT"); STREQ(v.t[0].label, "TOP MPH"); }
  // negative temperatures convert correctly
  CHECK(report::cToShown(-10, us) == 14); CHECK(report::cToShown(0, us) == 32); CHECK(report::cToShown(100, us) == 212); CHECK(report::cToShown(91, us) == 196);
  // --- codes ---
  { report::CodeList c; rui::CodesView v; uint32_t v0 = report::codesView(v, c);
    CHECK(v.nRows == 0 && v.total == 0 && !v.answered);
    c.answered = true; uint32_t v1 = report::codesView(v, c); CHECK(v1 != v0 && v.answered); }
  { report::CodeList c; c.answered = true; c.mil = true;
    c.add("P0171", 0); c.add("P0301", 0); c.add("P0420", 1); c.add("P1234", 0); c.add("P0300", 0); c.add("P0301", 0);   // duplicate ignored
    c.isNew[c.find("P0301")] = true;
    rui::CodesView v; report::codesView(v, c);
    CHECK(v.total == 5 && v.nRows == 4 && v.mil);
    STREQ(v.row[0].code, "P0301"); STREQ(v.row[0].tag, "NEW"); CHECK(v.row[0].col == rui::ORANGE); CHECK(strstr(v.row[0].desc, "cylinder 1"));   // new first
    STREQ(v.row[1].code, "P0171"); STREQ(v.row[1].tag, "STORED");                                                                                       // then stored, in order
    STREQ(v.row[2].code, "P1234"); STREQ(v.row[2].desc, "");                                                                                            // no description
    STREQ(v.row[3].code, "P0300");
    // a pending code shows when there is room
    report::CodeList d; d.answered = true; d.add("P0420", 1); d.add("P0171", 0);
    report::codesView(v, d); STREQ(v.row[0].code, "P0171"); STREQ(v.row[1].code, "P0420"); STREQ(v.row[1].tag, "PENDING"); CHECK(v.row[1].col == rui::YELLOW); }
  // the page version changes with content and stays put when only unrelated things change
  { report::CodeList a; a.answered = true; a.add("P0301", 0); rui::CodesView v; uint32_t h1 = report::codesView(v, a), h2 = report::codesView(v, a);
    CHECK(h1 == h2); a.isNew[0] = true; CHECK(report::codesView(v, a) != h1); a.isNew[0] = false; a.mil = true; CHECK(report::codesView(v, a) != h1); a.mil = false; a.add("P0420", 0); CHECK(report::codesView(v, a) != h1); }
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
