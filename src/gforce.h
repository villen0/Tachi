#pragma once
// G-force meter: turns the board's accelerometer (+ gyro) readings into lateral / longitudinal G in the car's frame. Pure C++ (no Arduino),
// unit-tested on a PC: tools/tests/gforce_test.cpp
//
// The board can sit in the car at any tilt, so nothing is assumed about the mounting:
//  - "up" is learned from the accelerometer while the car is still (and re-learned slowly, or on zero());
//  - lateral and longitudinal are the sensor's x and z axes flattened onto the horizontal plane;
//  - which way is forward / right is learned while driving: the sign of the longitudinal axis from how the speed changes, the sign of
//    the lateral axis from the gyro (right turn: the yaw rate and the speed say which way the sideways push must be).
// Until it has learned, the defaults are "z points to the driver, x points right" (a board on the dash facing the driver).
#include <math.h>
#include <stdint.h>

namespace gforce {

struct Vec { float x = 0, y = 0, z = 0; };
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float len(Vec a) { return sqrtf(dot(a, a)); }
inline Vec scale(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline Vec unit(Vec a) { float l = len(a); return l > 1e-6f ? scale(a, 1.0f / l) : Vec{0, 0, 0}; }
inline Vec flatten(Vec axis, Vec up) { return unit(axis - scale(up, dot(axis, up))); }   // the axis with its vertical part removed

constexpr int TRAIL = 12;

class Meter {
 public:
  // raw accelerometer in g (the reading at rest is +1 g pointing up), gyro in deg/s, speed in mph (negative = unknown), time in ms
  void feed(Vec acc, Vec gyroDps, float mph, uint32_t ms) {
    float dt = lastMs ? (ms - lastMs) / 1000.0f : 0.03f; if (dt <= 0 || dt > 1) dt = 0.03f; lastMs = ms;
    bool still = mph >= 0 && mph < 0.5f;
    float mag = len(acc);
    // gravity reference: first sample, then follow it slowly while the car is still and the reading is calm (~1 g)
    if (!haveBase) { base = acc; haveBase = true; }
    else if (still && fabsf(mag - 1.0f) < 0.06f && len(acc - base) < 0.15f) base = base + scale(acc - base, 0.02f);
    up = unit(base);
    Vec ex = flatten({1, 0, 0}, up), ez = flatten({0, 0, 1}, up);
    Vec dyn = acc - base;
    float rawLat = dot(dyn, ex), rawLon = dot(dyn, ez);
    // learn the signs
    float v = mph >= 0 ? mph * 0.44704f : 0;
    if (mph >= 0) learnLon(rawLon, mph, ms);
    float yaw = dot(gyroDps, up) * 0.0174533f;                                    // rad/s about the vertical axis (positive: counter-clockwise seen from above)
    if (v > 5.0f && fabsf(yaw) > 0.05f && fabsf(rawLat) > 0.05f) {               // right turn = clockwise = negative yaw, and the car is pushed to the right
      float expect = -v * yaw;
      scoreLat += ((expect > 0) == (rawLat > 0)) ? 1 : -1; if (scoreLat > 8) scoreLat = 8; if (scoreLat < -8) scoreLat = -8;
    }
    float lat = (scoreLat < 0 ? -1.0f : 1.0f) * rawLat, lon = (scoreLon < 0 ? -1.0f : 1.0f) * rawLon;
    // smooth (about 80 ms) so the dot is steady but still quick
    float k = 1.0f - expf(-dt / 0.08f);
    fLat += (lat - fLat) * k; fLon += (lon - fLon) * k;
    float tot = sqrtf(fLat * fLat + fLon * fLon);
    if (tot > peak) { peak = tot; peakLat = fLat; peakLon = fLon; }
    if (ms - trailMs >= 80) { trailMs = ms; trail[trailHead][0] = fLat; trail[trailHead][1] = fLon; trailHead = (trailHead + 1) % TRAIL; if (trailN < TRAIL) trailN++; }
  }
  void zero(Vec acc) { base = acc; haveBase = true; fLat = fLon = 0; trailN = 0; resetPeak(); }
  void resetPeak() { peak = 0; peakLat = peakLon = 0; }

  float lat() const { return fLat; }                 // positive: to the right
  float lon() const { return fLon; }                 // positive: accelerating, negative: braking
  float total() const { return sqrtf(fLat * fLat + fLon * fLon); }
  float peakTotal() const { return peak; }
  float peakLatAt() const { return peakLat; }
  float peakLonAt() const { return peakLon; }
  int trailCount() const { return trailN; }
  void trailAt(int i, float *la, float *lo) const { int k = (trailHead - trailN + i + 2 * TRAIL) % TRAIL; *la = trail[k][0]; *lo = trail[k][1]; }   // i = 0 oldest
  bool ready() const { return haveBase; }
  void setSigns(int lon, int lat) { scoreLon = lon < 0 ? -4 : 4; scoreLat = lat < 0 ? -4 : 4; }   // restore what was learned on an earlier drive
  bool learned() const { return scoreLon >= 3 || scoreLon <= -3; }                                  // the forward direction has been confirmed by driving
  int signLat() const { return scoreLat < 0 ? -1 : 1; }
  int signLon() const { return scoreLon < 0 ? -1 : 1; }
  Vec upVec() const { return up; }

 private:
  void learnLon(float rawLon, float mph, uint32_t ms) {                            // the longitudinal sign from how the speed changes
    if (!haveSpd) { spdMph = mph; spdMs = ms; sumLon = 0; nLon = 0; haveSpd = true; return; }
    sumLon += rawLon; nLon++;
    if (ms - spdMs >= 700) {
      float accG = (mph - spdMph) / ((ms - spdMs) / 1000.0f) * 0.0456f;            // 1 mph per second = 0.0456 g
      float avg = nLon ? sumLon / nLon : 0;
      if (fabsf(accG) > 0.06f && fabsf(avg) > 0.03f) { scoreLon += ((accG > 0) == (avg > 0)) ? 1 : -1; if (scoreLon > 8) scoreLon = 8; if (scoreLon < -8) scoreLon = -8; }
      spdMph = mph; spdMs = ms; sumLon = 0; nLon = 0;
    }
  }
  Vec base, up{0, 1, 0};
  bool haveBase = false, haveSpd = false;
  uint32_t lastMs = 0, spdMs = 0, trailMs = 0;
  float spdMph = 0, sumLon = 0; int nLon = 0;
  int scoreLon = 0, scoreLat = 0;
  float fLat = 0, fLon = 0, peak = 0, peakLat = 0, peakLon = 0;
  float trail[TRAIL][2] = {}; int trailHead = 0, trailN = 0;
};

}  // namespace gforce
