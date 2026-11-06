// Host preview of the round dial pages: renders src/round_ui.h to PPM files (same code as the board).
// Build: g++ -std=c++17 -O2 -I../../src round_ui_preview.cpp -o round_ui_preview && ./round_ui_preview out_dir
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include "round_ui.h"

static void save(const std::vector<uint16_t> &fb, const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", rui::W, rui::H);
  for (int y = 0; y < rui::H; y++) for (int x = 0; x < rui::W; x++) {
    float dx = x + 0.5f - rui::CX, dy = y + 0.5f - rui::CY; bool inside = dx * dx + dy * dy <= 240.0f * 240.0f;   // the panel is round
    uint16_t c = inside ? fb[y * rui::W + x] : 0;
    unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  if (!rui::begin()) { puts("alloc failed"); return 1; }
  std::vector<uint16_t> fb(rui::W * rui::H, 0);
  const char *names[] = {"sport", "tach", "speedo", "vitals", "arc", "custom", "quad", "dual", "gear", "trip", "history", "codes", "settings", "options"};
  struct Case { float rpm, mph; const char *tag; } cases[] = {{3763, 63, "mid"}, {6900, 112, "red"}, {0, 0, "idle"}};
  for (int pg = 0; pg < rui::PG_COUNT; pg++) for (auto &c : cases) {
    rui::State &S = rui::st(); S.page = -1; S.lastT = 0;           // restart each picture from scratch
    rui::Data d; d.rpm = c.rpm; d.mph = c.mph; d.haveCool = d.haveFuel = d.haveBatt = d.haveLoad = true;
    d.coolC = c.rpm > 6000 ? 109 : 95; d.coolShown = (int)(d.coolC * 1.8f + 32.5f); d.fuelPct = c.rpm > 6000 ? 12 : 61; d.battV = 14.0f; d.loadPct = c.rpm > 6000 ? 92 : 72;
    d.dotN = 7; d.dotCur = pg + 1;
    // multi-value pages (boost shown in psi)
    strcpy(d.arcLabel, "BOOST"); strcpy(d.arcUnit, "PSI"); strcpy(d.title, pg == rui::PG_QUAD ? "ENGINE" : "TEMPS");
    d.arcMin = -20; d.arcMax = 30; d.arcMajor = 10; d.arcMinor = 2; d.arcRed = 24; d.arcDec = 0;
    float boost = c.rpm > 6000 ? 26.0f : (c.rpm > 0 ? 9.5f : -11.0f); d.haveArc = true; d.arcVal = boost; snprintf(d.arcText, sizeof(d.arcText), "%.1f", boost);
    const char *lab[4] = {"TORQUE", "INTAKE", "FUEL", "LOAD"}, *unit[4] = {"LB-FT", "F", "%", "%"}, *txt[4] = {"195", "88", "13.6", "42"}; float fr[4] = {0.62f, 0.3f, 0.7f, 0.42f};
    for (int k = 0; k < 4; k++) { strcpy(d.rd[k].label, lab[k]); strcpy(d.rd[k].unit, unit[k]); strcpy(d.rd[k].text, txt[k]); d.rd[k].icon = k == 0 ? rui::IC_THERM : k == 1 ? rui::IC_AIR : k == 2 ? rui::IC_BATT : rui::IC_LOAD; d.rd[k].have = true; d.rd[k].frac = fr[k]; d.rd[k].col = k == 1 ? rui::ORANGE : rui::WHITE; }
    if (pg == rui::PG_DUAL) { strcpy(d.rd[1].label, "OIL TEMP"); d.rd[1].icon = rui::IC_BOOST; }
    d.gear = c.rpm > 6000 ? '5' : (c.rpm > 0 ? '3' : 'N');
    { d.settings.n = 2; strcpy(d.settings.row[0].label, "BRIGHTNESS"); strcpy(d.settings.row[0].value, "85 %"); d.settings.row[0].icon = rui::IC_SUN; strcpy(d.settings.row[1].label, "DEBUG"); strcpy(d.settings.row[1].value, "cluster2"); d.settings.row[1].icon = rui::IC_BUG; }
    { rui::TripView &T = d.trip; T.have = c.rpm > 0; T.live = false; T.rating = c.rpm > 6000 ? 58 : 91; T.grade = c.rpm > 6000 ? 'F' : 'A'; T.sel = 1; T.nSaved = 3;
      const char *tl[6] = {"TOP MPH", "MAX RPM", "MAX F", "PEAK PSI", "VOLTS", "CODES"}; const char *tv[6] = {"74", "4850", "203", "-2.4", "13.1-14.2", c.rpm > 6000 ? "2" : "0"};
      for (int k = 0; k < 6; k++) { strcpy(T.t[k].label, tl[k]); strcpy(T.t[k].value, tv[k]); T.t[k].col = (k == 5 && c.rpm > 6000) ? rui::RED : rui::WHITE; }
      strcpy(T.foot1, "42 MIN  18.4 MI"); strcpy(T.foot2, c.rpm > 6000 ? "5 HARD BRAKES  3 LAUNCHES" : "0 HARD BRAKES  0 LAUNCHES"); }
    { rui::CodesView &C = d.codes; C.answered = true; C.mil = c.rpm > 0; C.nRows = c.rpm > 6000 ? 4 : (c.rpm > 0 ? 2 : 0); C.total = c.rpm > 6000 ? 5 : C.nRows;
      const char *cc[4] = {"P0301", "P0420", "P1234", "P0171"}, *cd[4] = {"Misfire cylinder 1", "Catalyst weak B1", "", "Mixture too lean B1"}, *ct[4] = {"NEW", "STORED", "PENDING", "STORED"};
      for (int k = 0; k < 4; k++) { strcpy(C.row[k].code, cc[k]); strcpy(C.row[k].desc, cd[k]); strcpy(C.row[k].tag, ct[k]); C.row[k].col = k == 0 ? rui::ORANGE : (k == 2 ? rui::YELLOW : rui::WHITE); }
      strcpy(C.status, c.rpm > 6000 ? "CLEARING... KEEP HOLDING" : "HOLD 3 SECONDS TO CLEAR"); C.statusCol = rui::DIM; C.clearProg = c.rpm > 6000 ? 0.6f : 0; }
    { rui::OptionsView &O = d.opts; O = rui::OptionsView(); strcpy(O.title, "WARNINGS"); O.n = 5; O.swatch = false;
      const char *ol[5] = {"COOL WARM", "COOL HOT", "BATT LOW", "FUEL LOW", "REDLINE"}, *ov[5] = {"212 F", "226 F", "12.0 V", "15 %", "6500"};
      for (int k = 0; k < 5; k++) { strcpy(O.row[k].label, ol[k]); strcpy(O.row[k].value, ov[k]); O.row[k].kind = 1; } }
    { rui::HistoryView &Hv = d.hist; Hv = rui::HistoryView(); Hv.n = c.rpm > 0 ? 6 : 0; const char *tg[6] = {"LIVE", "#12", "#11", "#10", "#9", "#8"}, *md[6] = {"12 MIN 4.2 MI", "42 MIN 18.4 MI", "1H05M 31.0 MI", "9 MIN 2.1 MI", "27 MIN 11.6 MI", "55 MIN 24.8 MI"}, *rt[6] = {"B 84", "A 91", "C 72", "F 48", "A 95", "D 63"};
      for (int k = 0; k < Hv.n; k++) { strcpy(Hv.row[k].tag, tg[k]); strcpy(Hv.row[k].mid, md[k]); strcpy(Hv.row[k].right, rt[k]); Hv.row[k].col = k == 3 ? rui::RED : (k == 2 || k == 5 ? rui::YELLOW : rui::MINT); } }
    rui::frame(fb.data(), pg, d, 1000); rui::frame(fb.data(), pg, d, 1100);
    d.rpm = c.rpm; rui::frame(fb.data(), pg, d, 2000);
    save(fb, dir + "/" + names[pg] + "_" + c.tag + ".ppm");
  }
  puts("done");
    // themes (sport, speedo in km/h, vitals) and the theme chooser
  for (int t = 0; t < theme::THEMES; t++) for (int pg : {rui::PG_SPORT, rui::PG_SPEEDO, rui::PG_VITALS, rui::PG_OPTIONS}) {
    rui::applyTheme(t, 0);
    rui::State &S = rui::st(); S.page = -1; S.lastT = 0;
    rui::Data d; d.theme = rui::themeGen; d.rpm = 5200; d.mph = 71; d.kmh = pg == rui::PG_SPEEDO; d.haveCool = d.haveFuel = d.haveBatt = d.haveLoad = true;
    d.coolC = 92; d.coolShown = 198; d.fuelPct = 64; d.battV = 14.0f; d.loadPct = 68; d.dotN = 7; d.dotCur = 2;
    { rui::OptionsView &O = d.opts; strcpy(O.title, "THEME"); O.n = 2; O.swatch = true; strcpy(O.row[0].label, "LOOK"); strcpy(O.row[0].value, theme::look(t).name); strcpy(O.row[1].label, "COLOUR"); strcpy(O.row[1].value, "THEME"); }
    rui::frame(fb.data(), pg, d, 1000); rui::frame(fb.data(), pg, d, 1100);
    save(fb, dir + "/theme" + std::to_string(t) + "_" + names[pg] + ".ppm");
  }
  rui::applyTheme(0, 0);
  return 0;
}
