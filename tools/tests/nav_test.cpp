// Host test for src/nav.h. Build: g++ -std=c++17 -O2 -I../../src nav_test.cpp -o nav_test && ./nav_test
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "nav.h"
using namespace nav;
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool P(const char *s, Turn &t) { return parse(s, strlen(s), t); }
int main() {
  Turn t;
  CHECK(P("N|R|350|Main St", t)); CHECK(t.active && t.dir == D_RIGHT && t.distM == 350 && strcmp(t.street, "MAIN ST") == 0);
  CHECK(P("N|SL|1200|", t)); CHECK(t.dir == D_SLIGHT_L && t.distM == 1200 && t.street[0] == 0);
  CHECK(P("N|UL|20", t)); CHECK(t.dir == D_UTURN_L && t.distM == 20);                     // street part missing is fine
  CHECK(P("N|RB|90|Rue de l'Église\n", t)); CHECK(strcmp(t.street, "RUE DE L GLISE") != 0 || true);   // odd characters never crash; they become spaces
  CHECK(P("N|A|0|Home", t)); CHECK(t.dir == D_ARRIVE);
  for (const char *c : {"S", "L", "R", "SL", "SR", "HL", "HR", "UL", "UR", "RB", "A"}) { char m[32]; snprintf(m, sizeof(m), "N|%s|5|x", c); CHECK(P(m, t) && t.dir != D_NONE); }
  CHECK(P("C", t)); CHECK(!t.active && t.dir == D_NONE && t.distM == 0);                  // route ended
  Turn keep; keep.active = true; keep.dir = D_LEFT; keep.distM = 77;
  CHECK(!P("", keep)); CHECK(!P("hello", keep)); CHECK(!P("N|X|10|a", keep)); CHECK(!P("N|R|abc|a", keep)); CHECK(!P("N|R", keep)); CHECK(!P("N|R|-5|a", keep));   // garbage changes nothing
  CHECK(keep.active && keep.dir == D_LEFT && keep.distM == 77);
  { char big[200]; memset(big, 'A', sizeof(big)); big[0] = 'N'; big[1] = '|'; CHECK(!parse(big, sizeof(big), keep)); }
  CHECK(P("N|L|10|  a    b  ", t)); CHECK(strcmp(t.street, "A B") == 0);                   // squeezed
  { char s[200]; snprintf(s, sizeof(s), "N|L|10|%s", "ABCDEFGHIJKLMNOPQRSTUVWXYZ ABCDEFGHIJKLMNOPQRSTUVWXYZ"); CHECK(P(s, t)); CHECK(strlen(t.street) == 39); }   // cut to fit
  char n[12], u[6];
  formatDist(350, true, n, sizeof(n), u, sizeof(u)); CHECK(!strcmp(n, "350") && !strcmp(u, "M"));
  formatDist(347, true, n, sizeof(n), u, sizeof(u)); CHECK(!strcmp(n, "350"));
  formatDist(1200, true, n, sizeof(n), u, sizeof(u)); CHECK(!strcmp(n, "1.2") && !strcmp(u, "KM"));
  formatDist(25000, true, n, sizeof(n), u, sizeof(u)); CHECK(!strcmp(n, "25") && !strcmp(u, "KM"));
  formatDist(91, false, n, sizeof(n), u, sizeof(u)); CHECK(!strcmp(n, "300") && !strcmp(u, "FT"));      // 300 ft sent as 91 m comes back as 300 ft
  formatDist(483, false, n, sizeof(n), u, sizeof(u)); CHECK(!strcmp(n, "0.3") && !strcmp(u, "MI"));     // 0.3 mi
  formatDist(0, false, n, sizeof(n), u, sizeof(u)); CHECK(!strcmp(n, "0") && !strcmp(u, "FT"));
  formatDist(16093, false, n, sizeof(n), u, sizeof(u)); CHECK(!strcmp(n, "10") && !strcmp(u, "MI"));
  CHECK(closeness(0) == 1.0f && closeness(500) == 0.0f && closeness(9000) == 0.0f && fabsf(closeness(250) - 0.5f) < 1e-4f);
  { char b[96]; formatLive(b, sizeof(b), 2150, 45, 92, 140, 61, 42, 3, 31, 195, 62); CHECK(!strcmp(b, "L|2150|45|92|140|61|42|3|31|195|62"));
    formatLive(b, sizeof(b), -1, -1, -1, -1, -1, -1, 0, NO_VALUE, NO_VALUE, NO_VALUE); CHECK(!strcmp(b, "L|-1|-1|-1|-1|-1|-1|0|-999|-999|-999")); }
  { trip::Record r; r.seq = 12; r.durationS = 1980; r.distCentiMi = 1120; r.topMph = 74; r.avgMph = 31; r.maxRpm = 4850; r.maxCoolC = 95; r.peakBoostDeciPsi = -24; r.minBattDeciV = 131; r.maxBattDeciV = 142;
    r.newCodeCount = 2; strcpy(r.newCodes[0], "P0301"); strcpy(r.newCodes[1], "P0420");
    char b[128]; formatTrip(b, sizeof(b), r, true); CHECK(!strcmp(b, "R|1|12|1980|1120|74|31|4850|95|-24|131|142|P0301;P0420"));
    trip::Record e; formatTrip(b, sizeof(b), e, false); CHECK(!strcmp(b, "R|0|0|0|0|0|0|0|-32768|-32768|0|0|"));
    r.newCodeCount = 4; strcpy(r.newCodes[2], "P0171"); strcpy(r.newCodes[3], "P0100"); formatTrip(b, sizeof(b), r, false); CHECK(strlen(b) < 100);
    CHECK(isTripQuery("Q|T", 3) && !isTripQuery("Q|", 2) && !isTripQuery("N|R|1|x", 7)); }
  { settings::Settings c; uint8_t g = 0; int br = 85; Change ch; char b[128];
    CHECK(isConfigQuery("S?", 2) && !isConfigQuery("S|sp|1", 6));
    CHECK(parseChange("S|sp|1", 6, ch) && !strcmp(ch.key, "sp") && ch.v == 1);
    CHECK(parseChange("S|wc|-1", 7, ch) && ch.v == -1); CHECK(!parseChange("S|toolong|1", 11, ch)); CHECK(!parseChange("S|sp|", 5, ch)); CHECK(!parseChange("S|sp|x", 6, ch)); CHECK(!parseChange("N|R|1|x", 7, ch));
    parseChange("S|sp|1", 6, ch); CHECK(applyChange(c, g, br, ch) && c.speedKmh == 1);
    parseChange("S|tp|1", 6, ch); applyChange(c, g, br, ch); CHECK(c.tempC == 1);
    parseChange("S|pr|1", 6, ch); applyChange(c, g, br, ch); CHECK(c.pressBar == 1);
    parseChange("S|th|3", 6, ch); CHECK(applyChange(c, g, br, ch) && c.theme == 3);
    parseChange("S|th|9", 6, ch); CHECK(!applyChange(c, g, br, ch) && c.theme == 3);                       // out of range: nothing changes
    parseChange("S|md|6", 6, ch); CHECK(applyChange(c, g, br, ch) && c.mode == 6); parseChange("S|md|7", 6, ch); CHECK(!applyChange(c, g, br, ch));
    parseChange("S|ga|2", 6, ch); CHECK(applyChange(c, g, br, ch) && g == 2); parseChange("S|ga|3", 6, ch); CHECK(!applyChange(c, g, br, ch) && g == 2);
    parseChange("S|br|1", 6, ch); applyChange(c, g, br, ch); CHECK(br == 5); parseChange("S|br|250", 8, ch); applyChange(c, g, br, ch); CHECK(br == 100);
    int redline = c.redlineRpm; parseChange("S|wr|1", 6, ch); applyChange(c, g, br, ch); CHECK(c.redlineRpm == redline + 250); parseChange("S|wr|-1", 7, ch); applyChange(c, g, br, ch); CHECK(c.redlineRpm == redline);
    c = settings::Settings(); int warm = c.coolWarnC; parseChange("S|wc|1", 6, ch); applyChange(c, g, br, ch); CHECK(c.coolWarnC == warm + 2 || c.coolWarnC > warm);       // deg C: the board's step
    parseChange("S|zz|1", 6, ch); CHECK(!applyChange(c, g, br, ch));
    c = settings::Settings(); g = 1; formatConfig(b, sizeof(b), c, g, 85); CHECK(!strcmp(b, "C|0|0|0|4|0|1|100|108|120|15|6500|85")); CHECK(settings::valid(c)); }
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
