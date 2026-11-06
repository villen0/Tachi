// Layout checker for the round pages (src/round_ui.h): renders pages with worst-case content and reports
//   - text or pills that leave the round panel (8 px margin from the bezel, corners included),
//   - text that overlaps other text, pills / chips / buttons that overlap each other,
//   - text wider than the pill, chip or button it sits in (6 px padding), text crossing a container edge,
//   - fonts outside the page's type scale (the new pages use only label, small, title and medium text).
// The Drive report, Fault codes and Settings pages must pass (exit code 1 otherwise); the other pages are only listed for information.
// Build: g++ -std=c++17 -O2 -I../../src round_ui_layout_check.cpp -o round_ui_layout_check && ./round_ui_layout_check
#define RUI_LAYOUT_CHECK
#include <cstdio>
#include <cstring>
#include <vector>
#include "round_ui.h"
#include "dtc_text.h"

using rui::LayoutRec;
static const float CIRCLE_C = 239.5f, TEXT_R = 230.0f, BOX_R = 233.0f;
static const int PAD = 6;

static bool hit(const LayoutRec &a, const LayoutRec &b) { return a.x0 <= b.x1 + 1 && b.x0 <= a.x1 + 1 && a.y0 <= b.y1 + 1 && b.y0 <= a.y1 + 1; }
static bool contains(const LayoutRec &a, const LayoutRec &b) { return a.x0 <= b.x0 && a.y0 <= b.y0 && a.x1 >= b.x1 && a.y1 >= b.y1; }   // b sits inside a (a chip on a row)
static float cornerR(int x, int y) { float dx = x - CIRCLE_C, dy = y - CIRCLE_C; return sqrtf(dx * dx + dy * dy); }
static bool inside(const LayoutRec &r, float maxR) { return cornerR(r.x0, r.y0) <= maxR && cornerR(r.x1, r.y0) <= maxR && cornerR(r.x0, r.y1) <= maxR && cornerR(r.x1, r.y1) <= maxR; }

static int check(const char *name, const std::vector<LayoutRec> &log, const int *allowedFonts, int nAllowed, bool verbose) {
  int bad = 0;
  auto fail = [&](const char *what, const LayoutRec &a, const LayoutRec *b) {
    bad++; if (!verbose) return;
    printf("  %s: %s '%s' [%d,%d..%d,%d]", name, what, a.s, a.x0, a.y0, a.x1, a.y1);
    if (b) printf("  with '%s' [%d,%d..%d,%d]", b->s, b->x0, b->y0, b->x1, b->y1);
    printf("\n");
  };
  for (const LayoutRec &r : log) {
    if (!inside(r, r.kind == 't' ? TEXT_R : BOX_R)) fail("outside the panel:", r, nullptr);
    if (r.kind == 't' && nAllowed) { bool ok = false; for (int i = 0; i < nAllowed; i++) if (r.fontH == allowedFonts[i]) ok = true; if (!ok) fail("font outside the type scale (height off):", r, nullptr); }
  }
  for (size_t i = 0; i < log.size(); i++) for (size_t j = i + 1; j < log.size(); j++) {
    const LayoutRec &a = log[i], &b = log[j];
    if (a.kind == 't' && b.kind == 't' && hit(a, b)) fail("text overlaps text:", a, &b);
    if (a.kind == 'c' && b.kind == 'c' && hit(a, b) && !contains(a, b) && !contains(b, a)) fail("boxes overlap", a, &b);
  }
  for (const LayoutRec &t : log) if (t.kind == 't') for (const LayoutRec &c : log) if (c.kind == 'c') {
    int cx = (t.x0 + t.x1) / 2, cy = (t.y0 + t.y1) / 2;
    bool centreIn = cx >= c.x0 && cx <= c.x1 && cy >= c.y0 && cy <= c.y1;
    if (centreIn) { if (t.x0 < c.x0 + PAD || t.x1 > c.x1 - PAD || t.y0 < c.y0 + 2 || t.y1 > c.y1 - 2) fail("text does not fit its box:", t, &c); }
    else if (hit(t, c)) fail("text crosses a box edge:", t, &c);
  }
  return bad;
}

struct Page { int pg; const char *name; bool target; };

int main() {
  if (!rui::begin()) { puts("alloc failed"); return 1; }
  std::vector<uint16_t> fb(rui::W * rui::H, 0);
  const int scaleNew[] = {13, 11, 16, 21};               // F_LBL, F_SML, F_TTL, F_MED
  int totalBad = 0;

  // ---- Drive report: worst-case content ----
  struct TripCase { const char *name; bool have, live; int sel, saved; };
  TripCase tcs[] = {{"trip-worst", true, false, 2, 5}, {"trip-live", true, true, 0, 5}, {"trip-empty", false, false, 0, 0}};
  for (auto &tc : tcs) {
    rui::Data d; d.dotN = 13; d.dotCur = 10; rui::TripView &T = d.trip;
    T.have = tc.have; T.live = tc.live; T.sel = tc.sel; T.nSaved = tc.saved; T.rating = 100; T.grade = 'D';
    snprintf(T.title, sizeof(T.title), tc.live ? "LIVE TRIP" : "TRIP 65535");
    const char *lab[6] = {"TOP KMH", "MAX RPM", "MAX F", "PEAK PSI", "VOLTS", "CODES"}, *val[6] = {"155", "8000", "260", "-14.7", "10.5-15.5", "99"};
    for (int i = 0; i < 6; i++) { snprintf(T.t[i].label, sizeof(T.t[i].label), "%s", lab[i]); snprintf(T.t[i].value, sizeof(T.t[i].value), "%s", val[i]); }
    snprintf(T.foot1, sizeof(T.foot1), "10H 59M  999.9 MI"); snprintf(T.foot2, sizeof(T.foot2), "99 HARD BRAKES  99 LAUNCHES");
    rui::layoutLog().clear(); rui::State &S = rui::st(); S.page = -1; S.lastT = 0;
    rui::frame(fb.data(), rui::PG_TRIP, d, 1000, true);
    int bad = check(tc.name, rui::layoutLog(), scaleNew, 4, true); totalBad += bad;
    printf("%-14s %3d records, %d problems\n", tc.name, (int)rui::layoutLog().size(), bad);
  }

  // ---- Fault codes: the widest description, all tags, the longest status lines, 0..4 rows ----
  const char *widest = ""; int ww = 0; for (const dtc::Text &t : dtc::TEXTS) { int w = rui::textW(rui::F_SML, t.text); if (w > ww) { ww = w; widest = t.text; } }
  printf("widest code description: '%s' (%d px)\n", widest, ww);
  const int ROW_W = 280;                                  // the code rows are 280 px wide (the divider line): every description has to fit it
  for (const dtc::Text &t : dtc::TEXTS) { int w = rui::textW(rui::F_SML, t.text); if (w > ROW_W - 8) { printf("  description too wide for a row: %s '%s' (%d px, max %d)\n", t.code, t.text, w, ROW_W - 8); totalBad++; } }
  const char *stat[] = {"HOLD CLEAR 3 SECONDS TO ERASE", "COUNT SAYS 12, READ 12", "NOT CONNECTED TO THE CAR", "KEEP HOLDING TO CLEAR", "TURN THE ENGINE OFF FIRST"};
  for (int rows = 0; rows <= 4; rows++) for (int answered = 0; answered <= 1; answered++) for (int mil = 0; mil <= 1; mil++) for (int sIdx = 0; sIdx < 5; sIdx++) {
    if (rows > 0 && !answered) continue;
    rui::Data d; d.dotN = 13; d.dotCur = 11; rui::CodesView &C = d.codes;
    C.answered = answered; C.mil = mil; C.nRows = rows; C.total = rows == 0 ? 0 : 16;
    const char *tag[4] = {"PENDING", "STORED", "NEW", "PENDING"}; const char *code[4] = {"P0301", "P0420", "U0121", "P2097"};
    for (int i = 0; i < rows; i++) { snprintf(C.row[i].code, sizeof(C.row[i].code), "%s", code[i]); snprintf(C.row[i].desc, sizeof(C.row[i].desc), "%s", widest); snprintf(C.row[i].tag, sizeof(C.row[i].tag), "%s", tag[i]); }
    snprintf(C.status, sizeof(C.status), "%s", stat[sIdx]); C.clearProg = 0.5f;
    rui::layoutLog().clear(); rui::State &S = rui::st(); S.page = -1; S.lastT = 0;
    rui::frame(fb.data(), rui::PG_CODES, d, 1000, true);
    char nm[48]; snprintf(nm, sizeof(nm), "codes r%d a%d m%d s%d", rows, answered, mil, sIdx);
    int bad = check(nm, rui::layoutLog(), scaleNew, 4, true); totalBad += bad;
  }
  printf("codes pages checked (all row counts, states and status lines)\n");

  // ---- Trip history: six rows with the longest plausible text ----
  {
    for (int n : {0, 6}) {
      rui::Data d; d.dotN = 13; d.dotCur = 10; d.hist.n = n;
      for (int k = 0; k < n; k++) { snprintf(d.hist.row[k].tag, sizeof(d.hist.row[k].tag), k ? "#%d" : "LIVE", 988 + k); snprintf(d.hist.row[k].mid, sizeof(d.hist.row[k].mid), "%dH%02dM 123.4 MI", 10 + k, 5 + k); strcpy(d.hist.row[k].right, "A 99"); }
      rui::layoutLog().clear(); rui::State &S = rui::st(); S.page = -1; S.lastT = 0;
      rui::frame(fb.data(), rui::PG_HISTORY, d, 1000, true);
      char nm[24]; snprintf(nm, sizeof(nm), "history-%d", n);
      int bad = check(nm, rui::layoutLog(), scaleNew, 4, true); totalBad += bad;
      printf("%-14s %3d records, %d problems\n", nm, (int)rui::layoutLog().size(), bad);
    }
    for (int i = 0; i < 6; i++) { int y = rui::HI_Y0 + (rui::HI_H + rui::HI_GAP) * i + rui::HI_H / 2; if (rui::histRowAt(rui::CX, y) != i) { printf("  history row %d hit test fails\n", i); totalBad++; } }
    if (rui::HI_Y0 + (rui::HI_H + rui::HI_GAP) * 6 > 400) { puts("  history rows run too low"); totalBad++; }
  }

  // ---- Settings hub: five rows with the longest plausible values, and the BACK chip shared by the sub-screens ----
  {
    rui::Data d; d.dotN = 13; d.dotCur = 12;
    const char *lab[5] = {"BRIGHTNESS", "UNITS", "WARNINGS", "THEME", "DEBUG"}, *val[5] = {"100 %", "KMH C BAR", "4 CHANGED", "SPORT RED", "cluster2"};
    d.settings.n = 5;
    for (int i = 0; i < 5; i++) { snprintf(d.settings.row[i].label, sizeof(d.settings.row[i].label), "%s", lab[i]); snprintf(d.settings.row[i].value, sizeof(d.settings.row[i].value), "%s", val[i]); d.settings.row[i].icon = (uint8_t)(rui::IC_SUN + (i & 1)); }
    rui::layoutLog().clear(); rui::State &S = rui::st(); S.page = -1; S.lastT = 0;
    rui::frame(fb.data(), rui::PG_SETTINGS, d, 1000, true);
    int bad = check("settings-hub", rui::layoutLog(), scaleNew, 4, true); totalBad += bad;
    printf("%-14s %3d records, %d problems\n", "settings-hub", (int)rui::layoutLog().size(), bad);
    rui::layoutLog().clear(); rui::backChip(fb.data());
    bad = check("back-chip", rui::layoutLog(), scaleNew, 4, true); totalBad += bad;
    printf("%-14s %3d records, %d problems\n", "back-chip", (int)rui::layoutLog().size(), bad);
    // options screens: worst-case labels and values, both row kinds
    for (int kind = 0; kind < 2; kind++) {
      rui::Data o; o.dotN = 13; o.dotCur = 12; strcpy(o.opts.title, "WARNINGS"); o.opts.n = 5; o.opts.swatch = kind == 0;
      const char *ol[5] = {"COOL WARM", "COOL HOT", "BATT LOW", "FUEL LOW", "REDLINE"}, *ov[5] = {"226 F", "226 F", "14.0 V", "50 %", "10000"};
      const char *cl[5] = {"SPEED", "TEMPERATURE", "PRESSURE", "LOOK", "COLOUR"}, *cv[5] = {"MPH", "F", "PSI", "TRACK ORANGE", "VIOLET"};
      for (int k = 0; k < 5; k++) { snprintf(o.opts.row[k].label, sizeof(o.opts.row[k].label), "%s", kind ? ol[k] : cl[k]); snprintf(o.opts.row[k].value, sizeof(o.opts.row[k].value), "%s", kind ? ov[k] : cv[k]); o.opts.row[k].kind = (uint8_t)kind; }
      if (kind == 0) o.opts.n = 5;
      rui::layoutLog().clear(); rui::State &S2 = rui::st(); S2.page = -1; S2.lastT = 0;
      rui::frame(fb.data(), rui::PG_OPTIONS, o, 1000, true);
      char nm[24]; snprintf(nm, sizeof(nm), "options-%s", kind ? "steppers" : "cycle");
      int bad2 = check(nm, rui::layoutLog(), scaleNew, 4, true); totalBad += bad2;
      printf("%-14s %3d records, %d problems\n", nm, (int)rui::layoutLog().size(), bad2);
    }
    // the stepper chips must not overlap each other
    if (rui::OPT_MINUS_X + rui::OPT_BTN_W >= rui::OPT_PLUS_X || rui::optionPart(1, rui::OPT_PLUS_X + 5) != 2 || rui::optionPart(1, rui::OPT_MINUS_X + 5) != 1 || rui::optionPart(1, rui::ST_ROW_X0 + 30) != -1) { puts("  stepper hit zones wrong"); totalBad++; }
    // the hub rows, the BACK chip and the brightness presets must not touch each other's touch zones
    for (int i = 0; i < 5; i++) { int y = rui::ST_ROW_Y0 + (rui::ST_ROW_H + rui::ST_ROW_GAP) * i + rui::ST_ROW_H / 2; if (rui::settingsRowAt(rui::CX, y) != i) { printf("  settings row %d hit test fails\n", i); totalBad++; } }
    if (rui::backAt(rui::CX, rui::BACK_Y + rui::BACK_H / 2) != 1 || rui::backAt(rui::CX, rui::BR_PRESET_Y1 - 4) == 1) { puts("  BACK hit zone overlaps the brightness presets or misses the chip"); totalBad++; }
    if (rui::BR_PRESET_Y1 + 6 > rui::BACK_Y - 4) { puts("  brightness presets too close to the BACK chip"); totalBad++; }
  }

  // ---- the other pages, for information only (not part of the clean-up scope) ----
  puts("\nother pages (information only):");
  Page others[] = {{rui::PG_SPORT, "sport"}, {rui::PG_TACH, "tach"}, {rui::PG_SPEEDO, "speedo"}, {rui::PG_VITALS, "vitals"}, {rui::PG_ARC, "arc"}, {rui::PG_CUSTOM, "custom"}, {rui::PG_QUAD, "quad"}, {rui::PG_DUAL, "dual"}, {rui::PG_BATTERY, "battery", true}, {rui::PG_GEAR, "gear"}};
  for (auto &o : others) {
    rui::Data d; d.dotN = 13; d.dotCur = 5; d.rpm = 6500; d.mph = 120; d.haveCool = d.haveFuel = d.haveBatt = d.haveLoad = true; d.coolShown = 260; d.fuelPct = 100; d.battV = 14.8f; d.loadPct = 100;
    d.haveArc = true; d.arcVal = -14.7f; strcpy(d.arcText, "-14.7"); strcpy(d.arcUnit, "PSI"); strcpy(d.arcLabel, "BOOST"); d.arcMin = -20; d.arcMax = 30; d.arcMajor = 10; d.arcMinor = 2; d.arcRed = 24; d.arcDec = 0; strcpy(d.title, "ENGINE");
    const char *lb[4] = {"COOLANT", "INTAKE", "BATTERY", "LOAD"}, *un[4] = {"F", "F", "V", "%"}, *tx[4] = {"260", "104", "14.8", "100"};
    for (int i = 0; i < 4; i++) { strcpy(d.rd[i].label, lb[i]); strcpy(d.rd[i].unit, un[i]); strcpy(d.rd[i].text, tx[i]); d.rd[i].have = true; d.rd[i].icon = i + 1; }
    d.gear = '6';
    rui::layoutLog().clear(); rui::State &S = rui::st(); S.page = -1; S.lastT = 0;
    rui::frame(fb.data(), o.pg, d, 1000, true);
    int bad = check(o.name, rui::layoutLog(), nullptr, 0, true);
    printf("%-8s %3d records, %d problems\n", o.name, (int)rui::layoutLog().size(), bad);
  }

  printf("\n%s (%d problems on the Drive report, Fault codes and Settings pages)\n", totalBad ? "FAIL" : "PASS", totalBad);
  return totalBad ? 1 : 0;
}
