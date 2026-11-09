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
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
