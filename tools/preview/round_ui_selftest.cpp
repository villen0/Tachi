// Host self-test for src/round_ui.h: after many incremental frames the picture must equal a from-scratch render of the same state,
// and every changed pixel must lie inside a span that flushDirty() reports (otherwise the panel would keep stale pixels).
// Build: g++ -std=c++17 -O2 -I../../src round_ui_selftest.cpp -o round_ui_selftest && ./round_ui_selftest
#include <cstdio>
#include <vector>
#include <cstring>
#include "round_ui.h"
int main() {
  if (!rui::begin()) return 1;
  int bad = 0;
  for (int pg = 0; pg < rui::PG_COUNT; pg++) {
    std::vector<uint16_t> fb(rui::W * rui::H, 0), panel(rui::W * rui::H, 0), ref(rui::W * rui::H, 0);
    rui::State &S = rui::st(); S.page = -1; S.lastT = 0;
    rui::Data d; d.haveCool = d.haveFuel = d.haveBatt = d.haveLoad = true; d.dotN = 6; d.dotCur = pg + 1; d.coolShown = 203; d.battV = 14.0f;
    // multi-value pages: static labels first (they are read when the page is built), live values inside the loop
    strcpy(d.arcLabel, "BOOST"); strcpy(d.arcUnit, "BAR"); strcpy(d.title, "ENGINE"); d.arcMin = -1; d.arcMax = 2; d.arcMajor = 0.5f; d.arcMinor = 0.1f; d.arcRed = 1.6f; d.arcDec = 1;
    const char *lab[4] = {"COOLANT", "INTAKE", "BATTERY", "LOAD"}, *unit[4] = {"F", "F", "V", "%"};
    for (int k = 0; k < 4; k++) { strcpy(d.rd[k].label, lab[k]); strcpy(d.rd[k].unit, unit[k]); d.rd[k].icon = k + 1; }
    uint32_t now = 1000;
    for (int i = 0; i < 400; i++) {
      d.haveArc = (i % 61) != 5; d.arcVal = -1 + 3 * (0.5f + 0.5f * sinf(i * 0.05f)); snprintf(d.arcText, sizeof(d.arcText), "%.2f", d.arcVal);
      for (int k = 0; k < 4; k++) { d.rd[k].have = (i % 53) != k; snprintf(d.rd[k].text, sizeof(d.rd[k].text), "%d", 60 + (i * (k + 3)) % 140); d.rd[k].frac = ((i * (k + 3)) % 140) / 140.0f; d.rd[k].col = (i % 40) < 20 ? rui::CYAN : rui::ORANGE; }
      d.gear = "N123456-"[(i / 7) % 8];
      { rui::GForceView &G = d.g; G.have = (i % 77) != 3; float a = i * 0.11f; G.x = 1.2f * sinf(a); G.y = 1.0f * cosf(a * 0.7f); G.total = sqrtf(G.x * G.x + G.y * G.y); G.peak = 0.5f + 0.4f * sinf(a * 0.1f); G.peakX = 0.5f * sinf(a * 0.1f); G.peakY = -0.4f;
        G.n = (i / 5) % 13; for (int k = 0; k < G.n; k++) { G.tx[k] = 0.9f * sinf(a - (G.n - k) * 0.15f); G.ty[k] = 0.9f * cosf((a - (G.n - k) * 0.15f) * 0.7f); } }
      // drive report: live values every frame, the chip selection and the number of saved trips change now and then (ver rebuilds the page)
      { rui::TripView &T = d.trip; T.have = (i % 90) != 7; T.live = (i / 100) % 2 == 0; T.sel = (i / 50) % 4; T.nSaved = 1 + (i / 70) % 5;
        strcpy(T.title, T.live ? "LIVE TRIP" : "DRIVE REPORT");
        const char *tl[6] = {"TOP MPH", "MAX RPM", "MAX F", "PEAK PSI", "VOLTS", "CODES"};
        for (int k = 0; k < 6; k++) { strcpy(T.t[k].label, tl[k]); snprintf(T.t[k].value, sizeof(T.t[k].value), k == 4 ? "%d.%d-%d.%d" : "%d", 10 + (i * (k + 3)) % 90, i % 10, 14, (i * 3) % 10); T.t[k].col = (i % 40) < 20 ? rui::WHITE : rui::ORANGE; }
        snprintf(T.foot1, sizeof(T.foot1), "%d MIN  %d.%d MI", 5 + i % 60, i % 40, i % 10); snprintf(T.foot2, sizeof(T.foot2), "%d HARD BRAKES  %d LAUNCHES", i % 5, i % 3); }
      d.ver = (i / 50) + (i / 70) * 8;
      // fault codes: the rows change every 60 frames (ver), the status and the clear progress every frame
      { rui::CodesView &C = d.codes; C.answered = (i % 120) > 10; C.mil = (i / 60) % 2 == 0; C.nRows = (i / 60) % 5; C.total = C.nRows + ((i / 60) % 3 == 0 ? 1 : 0);
        for (int k = 0; k < C.nRows; k++) { snprintf(C.row[k].code, sizeof(C.row[k].code), "P0%d%d1", 3 + k % 2, k); snprintf(C.row[k].desc, sizeof(C.row[k].desc), k == 2 ? "" : "Misfire cylinder %d", k + 1); strcpy(C.row[k].tag, k == 0 ? "NEW" : "STORED"); C.row[k].col = k == 0 ? rui::ORANGE : rui::WHITE; }
        snprintf(C.status, sizeof(C.status), (i % 2) ? "HOLD 3 SECONDS TO CLEAR" : "READING..."); C.statusCol = rui::DIM; C.clearProg = (i % 30) / 30.0f; }
      d.ver += (i / 60) * 100 + (d.codes.answered ? 1000 : 0) + (d.trip.live ? 2000 : 0);   // every static field must be reflected in ver
      { rui::SettingsView &V = d.settings; V.n = 2 + (i / 90) % 4; for (int k = 0; k < V.n; k++) { snprintf(V.row[k].label, sizeof(V.row[k].label), "ROW %d", k); snprintf(V.row[k].value, sizeof(V.row[k].value), "%d %%", (i / 45 + k) % 100); V.row[k].icon = (uint8_t)(rui::IC_SUN + (k & 1)); } d.ver += V.n * 100000 + (i / 45) * 7; }
      { rui::OptionsView &O = d.opts; O.n = 1 + (i / 80) % 5; O.swatch = (i / 160) % 2; for (int k = 0; k < O.n; k++) { snprintf(O.row[k].label, sizeof(O.row[k].label), "OPT %d", k); snprintf(O.row[k].value, sizeof(O.row[k].value), "%d", (i / 33 + k) % 90); O.row[k].kind = (uint8_t)(k & 1); } d.ver += O.n * 1000000 + (i / 33) * 13; }
      rui::applyTheme((i / 110) % theme::THEMES, (i / 170) % theme::MODES); d.theme = rui::themeGen; d.kmh = (i / 130) % 2; d.gauge = (i / 55) % 3;   // theme / unit changes rebuild the page
      { rui::HistoryView &Hv = d.hist; Hv.n = (i / 70) % 7; for (int k = 0; k < Hv.n; k++) { snprintf(Hv.row[k].tag, sizeof(Hv.row[k].tag), "#%d", 5 + k); snprintf(Hv.row[k].mid, sizeof(Hv.row[k].mid), "%d MIN %d.%d MI", 5 + (i / 70) + k, k, i / 70); Hv.row[k].col = k & 1 ? rui::MINT : rui::YELLOW; } d.ver += Hv.n * 20000000 + (i / 70) * 31; }
      float ph = i * 0.07f;
      d.rpm = 800 + 3000 * (0.5f + 0.5f * sinf(ph)) + (i % 97 == 0 ? 3200 : 0); d.mph = 45 * (0.5f + 0.5f * sinf(ph * 0.6f));
      d.coolC = 80 + (i / 8) % 40; d.coolShown = (int)(d.coolC * 1.8f + 32); d.fuelPct = 100 - (i / 5) % 100; d.battV = 11.0f + (i % 50) * 0.08f; d.loadPct = (i * 3) % 100;
      now += 33;
      rui::frame(fb.data(), pg, d, now);
      rui::flushDirty([&](int x, int y, int w) { for (int k = 0; k < w; k++) panel[y * rui::W + x + k] = fb[y * rui::W + x + k]; });
    }
    // 1) the panel (only dirty spans copied) must equal the frame buffer
    int stale = 0; for (size_t i = 0; i < fb.size(); i++) if (fb[i] != panel[i]) stale++;
    // 2) the incremental frame buffer must equal a full redraw of the same state
    rui::frame(ref.data(), pg, d, now, true);                       // same eased values (dt = 0), full rebuild into a fresh buffer
    int diff = 0; for (size_t i = 0; i < fb.size(); i++) if (fb[i] != ref[i]) diff++;
    printf("page %d: stale-on-panel %d px, differs-from-full-redraw %d px\n", pg, stale, diff);
    if (diff || stale) {
      int x0 = 999, y0 = 999, x1 = -1, y1 = -1;
      for (int i = 0; i < rui::W * rui::H; i++) if (fb[i] != ref[i] || fb[i] != panel[i]) { int x = i % rui::W, y = i / rui::W; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
      printf("   bad pixels lie in x %d..%d, y %d..%d\n", x0, x1, y0, y1);
    }
    bad += stale + diff;
  }
  // status screens (scanning, connecting, not found, link lost): the animation and the waiting-time line are drawn incrementally; the result must equal a full redraw
  for (int kind = 0; kind <= 4; kind++) {
    std::vector<uint16_t> fb(rui::W * rui::H, 0), panel(rui::W * rui::H, 0), ref(rui::W * rui::H, 0);
    rui::State &S = rui::st(); S.page = -1;
    uint32_t now = 1000; int sec = kind == 4 ? -1 : 0;
    rui::statusPage(fb.data(), kind, rui::CYAN, "TITLE", "SUBTITLE", now, true, sec);
    rui::flushDirty([&](int x, int y, int w) { for (int k = 0; k < w; k++) panel[y * rui::W + x + k] = fb[y * rui::W + x + k]; });
    for (int i = 0; i < 300; i++) {
      now += 33; if (kind != 4 && i % 30 == 29) sec++;                       // the second counter ticks every ~1 s, going past the 15 s "tap to skip" text
      if (i == 150 && kind != 4) sec = 14;
      rui::statusPage(fb.data(), kind, rui::CYAN, "TITLE", "SUBTITLE", now, false, sec);
      rui::flushDirty([&](int x, int y, int w) { for (int k = 0; k < w; k++) panel[y * rui::W + x + k] = fb[y * rui::W + x + k]; });
    }
    int stale = 0; for (size_t i = 0; i < fb.size(); i++) if (fb[i] != panel[i]) stale++;
    rui::statusPage(ref.data(), kind, rui::CYAN, "TITLE", "SUBTITLE", now, true, sec);
    int diff = 0; for (size_t i = 0; i < fb.size(); i++) if (fb[i] != ref[i]) diff++;
    printf("status %d: stale-on-panel %d px, differs-from-full-redraw %d px\n", kind, stale, diff);
    bad += stale + diff;
  }
  puts(bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
