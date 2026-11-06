#pragma once
// Turns the trip records and the fault-code list into what the Drive report and Fault codes pages show (text, units, colours, rows).
// Pure C++ (no Arduino) so the formatting is unit-tested on a PC: tools/tests/report_view_test.cpp
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "round_ui.h"
#include "trip.h"
#include "dtc.h"
#include "dtc_text.h"

namespace report {

struct Units { bool mph = true, tempF = true, psi = true; };

inline int cToShown(int c, const Units &u) { return u.tempF ? (c * 9 + (c >= 0 ? 2 : -2)) / 5 + 32 : c; }

inline void tripView(rui::TripView &v, const trip::Record *r, bool live, int sel, int nSaved, const Units &u) {
  v = rui::TripView();
  v.live = live; v.sel = sel; v.nSaved = nSaved;
  const char *tl[6] = {u.mph ? "TOP MPH" : "TOP KMH", "MAX RPM", u.tempF ? "MAX F" : "MAX C", u.psi ? "PEAK PSI" : "PEAK BAR", "VOLTS", "CODES"};
  for (int i = 0; i < 6; i++) { snprintf(v.t[i].label, sizeof(v.t[i].label), "%s", tl[i]); v.t[i].col = rui::WHITE; }
  if (!r) { snprintf(v.title, sizeof(v.title), live ? "LIVE TRIP" : "DRIVE REPORT"); return; }
  v.have = true;
  if (live) snprintf(v.title, sizeof(v.title), "LIVE TRIP");
  else if (sel == 0) snprintf(v.title, sizeof(v.title), "DRIVE REPORT");
  else snprintf(v.title, sizeof(v.title), "TRIP %u", (unsigned)r->seq);
  snprintf(v.t[0].value, sizeof(v.t[0].value), "%d", u.mph ? (int)r->topMph : (int)lroundf(r->topMph * 1.609f));
  snprintf(v.t[1].value, sizeof(v.t[1].value), "%u", (unsigned)r->maxRpm);
  if (r->secHighRpm >= 5) v.t[1].col = rui::ORANGE;
  if (r->maxCoolC != trip::UNKNOWN16) {
    snprintf(v.t[2].value, sizeof(v.t[2].value), "%d", cToShown(r->maxCoolC, u));
    if (r->flags & 2) v.t[2].col = rui::RED; else if (r->flags & 1) v.t[2].col = rui::YELLOW;
  }
  if (r->peakBoostDeciPsi != trip::UNKNOWN16) {
    float psi = r->peakBoostDeciPsi / 10.0f;
    if (u.psi) snprintf(v.t[3].value, sizeof(v.t[3].value), "%.1f", psi); else snprintf(v.t[3].value, sizeof(v.t[3].value), "%.2f", psi * 0.0689476f);
  }
  if (r->maxBattDeciV > 0) {
    snprintf(v.t[4].value, sizeof(v.t[4].value), "%.1f-%.1f", r->minBattDeciV / 10.0f, r->maxBattDeciV / 10.0f);
    if (r->minBattDeciV < 115) v.t[4].col = rui::RED; else if (r->minBattDeciV < 124) v.t[4].col = rui::YELLOW;
  }
  auto cap = [](int n) { return n > 99 ? 99 : n; };
  snprintf(v.t[5].value, sizeof(v.t[5].value), "%d", cap(r->newCodeCount));
  if (r->newCodeCount > 0) v.t[5].col = rui::RED;
  int min = (int)((r->durationS + 30) / 60);
  float dist = r->distCentiMi / 100.0f * (u.mph ? 1.0f : 1.609f);
  if (min >= 60) snprintf(v.foot1, sizeof(v.foot1), "%dH %02dM  %.1f %s", min / 60, min % 60, dist, u.mph ? "MI" : "KM");
  else snprintf(v.foot1, sizeof(v.foot1), "%d MIN  %.1f %s", min < 1 ? 1 : min, dist, u.mph ? "MI" : "KM");
  snprintf(v.foot2, sizeof(v.foot2), "%d HARD BRAKE%s  %d LAUNCH%s", cap(r->hardBrakes), r->hardBrakes == 1 ? "" : "S", cap(r->hardLaunches), r->hardLaunches == 1 ? "" : "ES");
}

// One row of the trip history: "#12", "42 MIN 18.4 MI".
inline void histRow(rui::HistRow &h, const trip::Record &r, bool live, const Units &u) {
  h = rui::HistRow();
  if (live) snprintf(h.tag, sizeof(h.tag), "LIVE"); else snprintf(h.tag, sizeof(h.tag), "#%u", (unsigned)(r.seq % 1000));
  int min = (int)((r.durationS + 30) / 60); float dist = r.distCentiMi / 100.0f * (u.mph ? 1.0f : 1.609f);
  if (min >= 60) snprintf(h.mid, sizeof(h.mid), "%dH%02dM %.1f %s", min / 60, min % 60, dist, u.mph ? "MI" : "KM");
  else snprintf(h.mid, sizeof(h.mid), "%d MIN %.1f %s", min < 1 ? 1 : min, dist, u.mph ? "MI" : "KM");
  h.col = rui::WHITE;   // no grade or rating any more: the row is the number, time and distance
}

// The fault-code list shared between the main task (reads the codes) and the UI task (shows them).
struct CodeList {
  bool answered = false;              // the car has answered a code request at least once
  bool mil = false; int milCount = -1; // check-engine light and the stored count from 0101 (-1 = not read)
  int n = 0;
  char code[dtc::MAX_CODES][6] = {{0}};
  uint8_t kind[dtc::MAX_CODES] = {0};  // 0 stored, 1 pending
  bool isNew[dtc::MAX_CODES] = {false};
  void clear() { n = 0; memset(code, 0, sizeof(code)); memset(kind, 0, sizeof(kind)); memset(isNew, 0, sizeof(isNew)); }
  int find(const char *c) const { for (int i = 0; i < n; i++) if (strncmp(code[i], c, 5) == 0) return i; return -1; }
  void add(const char *c, uint8_t k) { if (find(c) >= 0 || n >= dtc::MAX_CODES) return; strncpy(code[n], c, 5); code[n][5] = 0; kind[n] = k; n++; }
};

inline uint32_t hashStep(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

// Fills the rows (new codes first, then stored, then pending) and returns the page version (changes when any static content changes).
inline uint32_t codesView(rui::CodesView &v, const CodeList &c) {
  v = rui::CodesView();
  v.answered = c.answered; v.mil = c.mil; v.total = c.n;
  static const uint8_t order[3][2] = {{1, 0}, {0, 0}, {0, 1}};          // {isNew wanted, kind wanted}: new, stored, pending
  int rows = 0;
  for (int pass = 0; pass < 3 && rows < 4; pass++)
    for (int i = 0; i < c.n && rows < 4; i++) {
      bool wantNew = order[pass][0] != 0; uint8_t wantKind = order[pass][1];
      bool match = wantNew ? c.isNew[i] : (!c.isNew[i] && c.kind[i] == wantKind);
      if (!match) continue;
      rui::CodeRow &r = v.row[rows++];
      snprintf(r.code, sizeof(r.code), "%s", c.code[i]);
      const char *d = dtc::describe(c.code[i]); snprintf(r.desc, sizeof(r.desc), "%s", d ? d : "");
      if (c.isNew[i]) { snprintf(r.tag, sizeof(r.tag), "NEW"); r.col = rui::ORANGE; }
      else if (c.kind[i] == 1) { snprintf(r.tag, sizeof(r.tag), "PENDING"); r.col = rui::YELLOW; }
      else { snprintf(r.tag, sizeof(r.tag), "STORED"); r.col = rui::WHITE; }
    }
  v.nRows = rows;
  uint32_t h = 2166136261u; h = hashStep(h, c.answered); h = hashStep(h, c.mil); h = hashStep(h, c.n); h = hashStep(h, rows);
  for (int i = 0; i < rows; i++) { for (const char *q = v.row[i].code; *q; q++) h = hashStep(h, (uint8_t)*q); for (const char *q = v.row[i].tag; *q; q++) h = hashStep(h, (uint8_t)*q); }
  return h & 0x7FFFFFFF;
}

}  // namespace report
