// Host test for src/gforce.h. Build: g++ -std=c++17 -O2 -I../../src gforce_test.cpp -o gforce_test && ./gforce_test
#include <cstdio>
#include <cmath>
#include "gforce.h"
using namespace gforce;
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b, tol) do { checks++; if (fabsf((float)(a) - (float)(b)) > (tol)) { printf("FAIL line %d: %s = %.3f, expected %.3f\n", __LINE__, #a, (double)(a), (double)(b)); fails++; } } while (0)

// A board mounted at any angle: R maps car axes (x right, y up, z forward... see below) to sensor axes.
struct Rot { float m[3][3]; Vec ap(Vec v) const { return {m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z, m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z, m[2][0]*v.x+m[2][1]*v.y+m[2][2]*v.z}; } };
static Rot rotX(float a) { return {{{1,0,0},{0,cosf(a),-sinf(a)},{0,sinf(a),cosf(a)}}}; }
static Rot rotY(float a) { return {{{cosf(a),0,sinf(a)},{0,1,0},{-sinf(a),0,cosf(a)}}}; }
static Rot rotZ(float a) { return {{{cosf(a),-sinf(a),0},{sinf(a),cosf(a),0},{0,0,1}}}; }
static Rot mul(const Rot &a, const Rot &b) { Rot r; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { r.m[i][j] = 0; for (int k = 0; k < 3; k++) r.m[i][j] += a.m[i][k] * b.m[k][j]; } return r; }

struct Sim {
  Meter m; Rot R; uint32_t t = 1000, stamp = 1000; float mphReal = 0; Vec bias{0, 0, 0}; bool noSpeed = false;
  explicit Sim(Rot r) : R(r) {}
  // car frame: x right, y up, z forward. Accel (g) felt by the sensor at rest: +1 g up.
  // yawDps: positive = counter-clockwise seen from above (a left turn); aLat right-positive g; dMph = mph per second
  void step(float aLat, float dMph, float yawDps, float dt = 0.033f) {
    Vec accCar = {aLat, 1.0f, 0};                                     // lateral push + gravity (forward G is NOT read from the accelerometer by the meter)
    mphReal += dMph * dt; if (mphReal < 0) mphReal = 0; t += (uint32_t)(dt * 1000 + 0.5f);
    if (t - stamp >= 100) stamp = t;                                  // the car reports speed every 100 ms, whole mph
    static float held = 0; if (stamp == t) held = floorf(mphReal + 0.5f);
    Vec gy = R.ap(Vec{0, yawDps, 0}) + bias;
    m.feed(R.ap(accCar), gy, noSpeed ? -1.0f : held, stamp, t);
  }
  void run(float sec, float aLat, float dMph, float yaw = 0) { for (int i = 0; i < (int)(sec / 0.033f); i++) step(aLat, dMph, yaw); }
};

int main() {
  // a spread of mountings: upright, tilted back, rotated about the vertical, on its side, upside down, odd angle
  Rot mounts[] = { rotX(0), rotX(0.4f), rotZ(1.2f), mul(rotX(1.3f), rotY(0.7f)), rotX(3.14159f), mul(rotZ(0.5f), mul(rotX(2.2f), rotY(-1.1f))), rotX(-1.5708f) };
  int mi = 0;
  for (Rot R : mounts) {
    mi++;
    { Sim s(R); s.run(4, 0, 0);                                       // at rest: nothing
      NEAR(s.m.lat(), 0, 0.01f); NEAR(s.m.lon(), 0, 0.01f); CHECK(s.m.ready()); }
    { Sim s(R); s.mphReal = 40; s.run(3, 0, 0);                       // steady 40 mph, straight: nothing
      NEAR(s.m.lat(), 0, 0.02f); NEAR(s.m.lon(), 0, 0.03f); }
    { Sim s(R); s.run(1, 0, 0); s.run(5, 0, 6.0f);                    // accelerating 6 mph/s = 0.27 g
      NEAR(s.m.lon(), 0.274f, 0.06f); NEAR(s.m.lat(), 0, 0.02f); }
    { Sim s(R); s.mphReal = 60; s.run(1, 0, 0); s.run(3, 0, -12.0f);  // hard braking 12 mph/s = 0.55 g
      NEAR(s.m.lon(), -0.547f, 0.07f); }
    { Sim s(R); s.mphReal = 30; s.run(1, 0, 0); s.run(3, 0, 0, -20.0f);   // right turn: clockwise 20 deg/s at 30 mph = v*w = 13.4*0.349/9.81 = 0.477 g to the right
      NEAR(s.m.lat(), 0.477f, 0.04f); NEAR(s.m.lon(), 0, 0.03f); }
    { Sim s(R); s.mphReal = 30; s.run(1, 0, 0); s.run(3, 0, 0, 20.0f);    // left turn
      NEAR(s.m.lat(), -0.477f, 0.04f); }
    { Sim s(R); s.bias = R.ap(Vec{0.4f, -0.7f, 0.3f}); s.run(5, 0, 0); // gyro offset while still is learned
      s.mphReal = 30; s.run(3, 0, 0); NEAR(s.m.lat(), 0, 0.02f); }
    (void)mi;
  }
  { Sim s(rotX(0)); s.noSpeed = true; s.run(3, 0, 0, -20.0f);          // no speed reading: shows zero, does not guess
    NEAR(s.m.lat(), 0, 0.001f); NEAR(s.m.lon(), 0, 0.001f); }
  { Sim s(rotX(0)); s.mphReal = 30; s.run(1, 0, 0); s.run(2, 0, 0, -20.0f);   // stale speed (reading stops): acceleration falls back to 0
    s.m.feed({0, 1, 0}, {0, 0, 0}, 30, s.stamp, s.t + 1500); NEAR(s.m.lon(), 0, 0.1f); }
  { Sim s(rotX(0.6f)); s.mphReal = 30; s.run(1, 0, 0); s.run(2, 0, 0, -20.0f);      // zero() takes the current gyro as the new offset
    s.bias = s.R.ap(Vec{0.5f, 0.5f, 0.5f}); s.m.zero(s.R.ap(Vec{0, 1, 0}), s.bias); s.m.resetPeak();
    s.run(2, 0, 0, 0); NEAR(s.m.lat(), 0, 0.02f); NEAR(s.m.peakTotal(), 0, 0.05f); }
  { Sim s(rotX(0)); s.mphReal = 30; s.run(1, 0, 0); s.run(2, 0, 0, -30.0f);       // trail and peak
    CHECK(s.m.trailCount() > 5 && s.m.trailCount() <= TRAIL); CHECK(s.m.peakTotal() > 0.5f); CHECK(s.m.peakLatAt() > 0.5f);
    float la, lo; s.m.trailAt(s.m.trailCount() - 1, &la, &lo); CHECK(la > 0.5f); s.m.resetPeak(); NEAR(s.m.peakTotal(), 0, 0.001f); }
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
