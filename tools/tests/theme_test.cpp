// Host test for src/theme.h. Build: g++ -std=c++17 -O2 -I../../src theme_test.cpp -o theme_test && ./theme_test
#include <cstdio>
#include <cstring>
#include "theme.h"
#include "settings.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
  CHECK(theme::THEMES == settings::THEMES && theme::MODES == settings::MODES);   // the settings blob and the themes agree on the counts
  const theme::Look &c = theme::look(0);
  CHECK(!strcmp(c.name, "CLASSIC") && c.k[0].r == 0 && c.k[0].g == 190 && c.k[0].b == 255 && c.k[3].r == 255 && c.k[3].g == 110);   // CLASSIC keeps the original colours
  for (int t = 0; t < theme::THEMES; t++) {
    const theme::Look &l = theme::look(t);
    CHECK(l.name && strlen(l.name) >= 4 && strlen(l.name) <= 12);
    for (int u = t + 1; u < theme::THEMES; u++) CHECK(strcmp(l.name, theme::look(u).name) != 0);
    theme::Look m0 = theme::make(t, 0); CHECK(!memcmp(&m0.accent, &l.accent, 3) && !memcmp(&m0.k, &l.k, sizeof(l.k)));
    for (int m = 1; m < theme::MODES; m++) { theme::Look x = theme::make(t, m); theme::RGB a = theme::modeColor(m); CHECK(x.accent.r == a.r && x.k[0].g == a.g && x.k[1].r == l.k[1].r); CHECK(strlen(theme::modeName(m)) <= 8); }
  }
  CHECK(theme::look(-3).name == theme::look(0).name && theme::look(99).name == theme::look(0).name);
  CHECK(!strcmp(theme::modeName(0), "THEME") && !strcmp(theme::modeName(6), "VIOLET"));
  printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
  return fails != 0;
}
