#pragma once
// Gauge looks (themes) and accent colour modes. Pure C++ (no Arduino), unit-tested: tools/tests/theme_test.cpp
// A theme is a handful of colours: the four stops of the rev / level gradient (cool -> hot), the cold and speedo ring tints, the accent
// colour (titles, needles, icons) and the bezel / panel tones. A colour mode replaces the accent and the cool end of the gradient.
// Names are our own; the moods are loosely a clean technical blue, a sharp red, a cool silver and a warm track orange.
#include <stdint.h>
#include <string.h>

namespace theme {

struct RGB { uint8_t r, g, b; };
struct Look {
  const char *name;
  RGB k[4];                 // gradient stops: cool, mid, warm, hot
  RGB cold, spd1, spd2;     // below-zero arc tint; speedo ring stops after the cool end
  RGB accent;               // titles, needle, icons
  RGB bezel, panel, panelEdge;
};

constexpr int THEMES = 5, MODES = 7;

inline const Look &look(int t) {
  static const Look L[THEMES] = {
    {"CLASSIC",     {{0, 190, 255}, {110, 240, 190}, {255, 208, 50}, {255, 110, 40}}, {60, 120, 255}, {70, 110, 255}, {200, 70, 255}, {0, 200, 255},   {30, 37, 54}, {22, 30, 50}, {52, 66, 98}},
    {"SPORT RED",   {{255, 236, 230}, {255, 170, 70}, {255, 90, 50}, {255, 40, 50}},   {255, 150, 120}, {255, 120, 90}, {255, 60, 90},  {255, 70, 60},   {46, 28, 32}, {38, 22, 28}, {104, 56, 60}},
    {"ICE SILVER",  {{120, 170, 255}, {200, 225, 255}, {255, 230, 140}, {255, 120, 80}}, {90, 130, 220}, {150, 190, 255}, {220, 230, 255}, {190, 215, 255}, {40, 46, 60}, {26, 32, 46}, {86, 98, 126}},
    {"TRACK ORANGE",{{255, 170, 40}, {255, 120, 30}, {255, 70, 30}, {255, 30, 40}},    {255, 190, 90}, {255, 140, 40}, {255, 60, 60},  {255, 150, 30},  {46, 34, 24}, {38, 28, 20}, {104, 76, 50}},
    {"REVSENSE",    {{240, 110, 80}, {231, 70, 42}, {255, 160, 50}, {255, 45, 60}},    {120, 120, 135}, {231, 70, 42}, {255, 160, 50}, {231, 70, 42}, {38, 38, 42}, {24, 24, 28}, {72, 72, 78}},   // the logo red on neutral dark greys
  };
  return L[t >= 0 && t < THEMES ? t : 0];
}

// accent colours for the modes 1..6 (mode 0 keeps the theme's own)
inline RGB modeColor(int m) {
  static const RGB M[MODES - 1] = {{0, 200, 255}, {110, 240, 130}, {255, 208, 50}, {255, 120, 40}, {255, 70, 90}, {200, 100, 255}};
  return M[(m >= 1 && m < MODES ? m : 1) - 1];
}
inline const char *modeName(int m) {
  static const char *N[MODES] = {"THEME", "CYAN", "GREEN", "AMBER", "ORANGE", "RED", "VIOLET"};
  return N[m >= 0 && m < MODES ? m : 0];
}

// the look to draw with: theme t, colour mode m
inline Look make(int t, int m) {
  Look l = look(t);
  if (m >= 1 && m < MODES) { RGB c = modeColor(m); l.accent = c; l.k[0] = c; }
  return l;
}

}  // namespace theme
