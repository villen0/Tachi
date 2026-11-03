#pragma once
// Gear estimate for the Gear page. Standard OBD has no gear value, so the gear is worked out from RPM and speed.
// Pure C++ (no Arduino), shared by the firmware and the host tools.
#include <math.h>

namespace gear {

// Ratios are RPM per mph for each gear. These are a guess for the Mazda 3; tune them from a drive log.
struct GearTable { float rpmPerMph[6]; };
const GearTable DEFAULT_GEARS = {{167, 95, 68, 47, 33, 28}};

// 'N' stopped, '1'..'6', '-' unknown (engine off, slipping the clutch, between gears).
inline char estimateGear(int rpm, int mph, const GearTable &t = DEFAULT_GEARS) {
  if (mph < 2) return 'N';
  if (rpm < 500) return '-';
  float r = (float)rpm / (float)mph, best = 1e9f; int bi = -1;
  for (int i = 0; i < 6; i++) { float e = fabsf(logf(r / t.rpmPerMph[i])); if (e < best) { best = e; bi = i; } }
  return best < 0.16f ? (char)('1' + bi) : '-';   // within about 16 % of a known ratio, else slipping / between gears
}

}  // namespace gear
