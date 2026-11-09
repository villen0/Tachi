#pragma once
// G-force meter for the round Revsense dash. Pure C++ (no Arduino), unit-tested on a PC: tools/tests/gforce_test.cpp
//
// It does NOT depend on how the board is mounted (tilt, rotation, even upside down), because it never reads the accelerometer as a
// "forward" or "sideways" axis:
//  - longitudinal G (accelerate / brake) is how fast the car's speed changes (OBD speed, a straight-line fit over the last ~0.8 s);
//  - lateral G (cornering) is speed x yaw rate (centripetal acceleration a = v * w); the yaw rate is the gyro's rotation about the
//    vertical axis, and "vertical" is the direction of gravity learned from the accelerometer while the car is still. The left / right
//    sign follows from physics (a clockwise turn seen from above is a right turn), so nothing has to be learned on the road.
//  - the gyro's resting offset is learned while the car is still, so a lazy stop does not leave a ghost turn.
// Limits: speed arrives a few times a second, so accelerate / brake lags by about a third of a second; no speed = shows zero.
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

constexpr int TRAIL = 12;
constexpr int SPD_N = 12;                              // speed samples kept for the slope

class Meter {
 public:
  // accelerometer in g (+1 g up at rest; only its direction is used), gyro in deg/s, speed in mph (negative = unknown),
  // spdStamp = when that speed was measured (ms; changes when a new reading arrived), ms = now
  void feed(Vec acc, Vec gyroDps, float mph, uint32_t spdStamp, uint32_t ms) {
    float dt = lastMs ? (ms - lastMs) / 1000.0f : 0.03f; if (dt <= 0 || dt > 1) dt = 0.03f; lastMs = ms;
    bool known = mph >= 0;
    bool still = known && mph < 0.5f;
    float mag = len(acc);
    // gravity reference: first sample, then follow it slowly while the car is still and the reading is calm (~1 g)
    if (!haveBase) { base = acc; haveBase = true; }
    else if (still && fabsf(mag - 1.0f) < 0.06f && len(acc - base) < 0.15f) base = base + scale(acc - base, 0.02f);
    up = unit(base);
    // gyro resting offset: follow it while the car is still
    if (!haveBias) { bias = gyroDps; haveBias = true; }
    else if (still && len(gyroDps - bias) < 20.0f) bias = bias + scale(gyroDps - bias, 0.02f);
    float yaw = dot(gyroDps - bias, up) * 0.0174533f;    // rad/s about the vertical axis, positive = counter-clockwise seen from above = a left turn

    float lat = 0, lon = 0;
    if (known) {
      float v = mph * 0.44704f;
      lat = -v * yaw / 9.80665f;                         // right turn: clockwise, negative yaw, pushed to the right = positive
      lon = speedSlopeG(mph, spdStamp, ms);
    } else { nSpd = 0; }
    float kLat = 1.0f - expf(-dt / 0.08f), kLon = 1.0f - expf(-dt / 0.15f);
    fLat += (lat - fLat) * kLat; fLon += (lon - fLon) * kLon;
    float tot = sqrtf(fLat * fLat + fLon * fLon);
    if (tot > peak) { peak = tot; peakLat = fLat; peakLon = fLon; }
    if (ms - trailMs >= 80) { trailMs = ms; trail[trailHead][0] = fLat; trail[trailHead][1] = fLon; trailHead = (trailHead + 1) % TRAIL; if (trailN < TRAIL) trailN++; }
  }
  void zero(Vec acc, Vec gyroDps) { base = acc; haveBase = true; bias = gyroDps; haveBias = true; fLat = fLon = 0; trailN = 0; resetPeak(); }
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
  Vec upVec() const { return up; }

 private:
  // acceleration in g from the speed readings of the last ~0.8 s (least-squares line, so the 1 mph steps average out)
  float speedSlopeG(float mph, uint32_t stamp, uint32_t ms) {
    if (nSpd == 0 || stamp != sT[(sHead + SPD_N - 1) % SPD_N]) {   // a new reading
      sT[sHead] = stamp; sV[sHead] = mph; sHead = (sHead + 1) % SPD_N; if (nSpd < SPD_N) nSpd++;
    }
    if ((uint32_t)(ms - stamp) > 800) return 0;                     // no fresh speed: no acceleration to show
    uint32_t t1 = sT[(sHead + SPD_N - 1) % SPD_N];
    int n = 0; double st = 0, sv = 0;
    for (int i = 0; i < nSpd; i++) { int k = (sHead + SPD_N - 1 - i + SPD_N) % SPD_N; if ((uint32_t)(t1 - sT[k]) > 800) break; n++; st += (double)(int32_t)(sT[k] - t1); sv += sV[k]; }
    if (n < 2) return 0;
    double mt = st / n, mv = sv / n, num = 0, den = 0;
    for (int i = 0; i < n; i++) { int k = (sHead + SPD_N - 1 - i + SPD_N) % SPD_N; double x = (double)(int32_t)(sT[k] - t1) - mt; num += x * (sV[k] - mv); den += x * x; }
    if (den < 40000.0) return 0;                                    // the readings span less than about 0.3 s: too short to trust
    return (float)(num / den * 1000.0 * 0.0456);                    // mph per ms -> mph per second -> g (1 mph/s = 0.0456 g)
  }
  Vec base, bias, up{0, 1, 0};
  bool haveBase = false, haveBias = false;
  uint32_t lastMs = 0, trailMs = 0;
  uint32_t sT[SPD_N] = {}; float sV[SPD_N] = {}; int sHead = 0, nSpd = 0;
  float fLat = 0, fLon = 0, peak = 0, peakLat = 0, peakLon = 0;
  float trail[TRAIL][2] = {}; int trailHead = 0, trailN = 0;
};

}  // namespace gforce
