// Host test for src/gforce.h. Build: g++ -std=c++17 -O2 -I../../src gforce_test.cpp -o gforce_test && ./gforce_test
#include <cstdio>
#include <cmath>
#include "gforce.h"
using namespace gforce;
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b, tol) do { checks++; if (fabsf((float)(a) - (float)(b)) > (tol)) { printf("FAIL line %d: %s = %.3f, expected %.3f\n", __LINE__, #a, (double)(a), (double)(b)); fails++; } } while (0)

struct Sim {                       // a board whose sensor axes relate to the car in a chosen way
  Meter m; uint32_t t = 1000; float mph = 0;
  float tilt = 0;                  // board tipped back by this many radians (rotation about the sensor x axis)
  float lonSign = -1;              // sensor z points to the driver: forward = -z
  float latSign = 1;               // sensor x points to the car's right
  Vec up() const { return {0, cosf(tilt), sinf(tilt)}; }
  // one step: lateral accel (g, right positive), longitudinal (g, forward positive), speed change (mph per second) for dt seconds
  void step(float aLat, float aFwd, float dMphPerS, float yawDps, float dt = 0.033f) {
    Vec u = up(), fwdAxis = {0, sinf(tilt), -cosf(tilt)};            // forward in the sensor frame (the horizontal part of -z)
    Vec acc = u + scale(fwdAxis, aFwd * (lonSign < 0 ? 1.0f : -1.0f)) + Vec{aLat * latSign, 0, 0};
    mph += dMphPerS * dt; if (mph < 0) mph = 0; t += (uint32_t)(dt * 1000);
    Vec gy = scale(u, yawDps);                                       // rotation about the vertical axis (positive: counter-clockwise from above)
    m.feed(acc, gy, mph, t);
  }
  void run(float sec, float aLat, float aFwd, float dMph, float yaw) { for (int i = 0; i < (int)(sec / 0.033f); i++) step(aLat, aFwd, dMph, yaw); }
};

int main() {
  { Sim s; s.run(4, 0, 0, 0, 0);                                      // at rest: nothing
    NEAR(s.m.lat(), 0, 0.01f); NEAR(s.m.lon(), 0, 0.01f); NEAR(s.m.total(), 0, 0.02f); CHECK(s.m.ready()); }
  { Sim s; s.run(1, 0, 0, 0, 0);                                      // accelerate (3 mph/s = 0.137 g): the forward sign is learned from the speed
    s.run(8, 0, 0.137f, 3.0f, 0);
    NEAR(s.m.lon(), 0.137f, 0.03f); CHECK(s.m.signLon() == -1); }
  { Sim s; s.run(1, 0, 0, 0, 0); s.run(8, 0, 0.137f, 3.0f, 0);       // then brake hard: the dot goes the other way
    s.run(0.2f, 0, 0, 0, 0); s.mph = 45;
    s.run(2, 0, -0.45f, -9.9f, 0);
    NEAR(s.m.lon(), -0.45f, 0.05f); CHECK(s.m.peakTotal() >= 0.4f); }
  { Sim s; s.lonSign = 1;                                              // a board mounted the other way round: learned the other way
    s.run(1, 0, 0, 0, 0); s.run(8, 0, 0.137f, 3.0f, 0);
    NEAR(s.m.lon(), 0.137f, 0.03f); CHECK(s.m.signLon() == 1); }
  { Sim s; s.run(1, 0, 0, 0, 0); s.mph = 33.5f;                      // right turn at 15 m/s with 0.5 g: yaw rate = 0.5*9.81/15 rad/s clockwise = -18.7 deg/s
    s.run(4, 0.5f, 0, 0, -18.7f);
    NEAR(s.m.lat(), 0.5f, 0.06f); CHECK(s.m.signLat() == 1); }
  { Sim s; s.latSign = -1; s.run(1, 0, 0, 0, 0); s.mph = 33.5f;      // sensor x pointing left: learned, the dot still goes right in a right turn
    s.run(4, 0.5f, 0, 0, -18.7f);
    NEAR(s.m.lat(), 0.5f, 0.06f); CHECK(s.m.signLat() == -1); }
  { Sim s; s.run(1, 0, 0, 0, 0); s.mph = 33.5f;                      // left turn: negative lateral
    s.run(4, -0.4f, 0, 0, 14.9f);
    NEAR(s.m.lat(), -0.4f, 0.06f); }
  { Sim s; s.tilt = 0.44f;                                             // board tipped back 25 degrees: flattening still gives the right answer
    s.run(1, 0, 0, 0, 0); s.run(8, 0, 0.2f, 4.4f, 0);
    NEAR(s.m.lon(), 0.2f, 0.04f); NEAR(s.m.lat(), 0, 0.03f); }
  { Sim s; s.run(2, 0, 0, 0, 0); s.tilt = 0.3f; s.run(1, 0, 0, 0, 0); // tilted while still: zero() takes the new rest position
    s.m.zero(s.up()); s.run(1, 0, 0, 0, 0);
    NEAR(s.m.lat(), 0, 0.02f); NEAR(s.m.lon(), 0, 0.02f); CHECK(s.m.peakTotal() < 0.05f); }
  { Sim s; s.run(1, 0, 0, 0, 0); s.tilt = 0.1f; s.run(30, 0, 0, 0, 0);   // the reference follows a slow change while the car is still
    NEAR(s.m.lon(), 0, 0.05f); }
  { Sim s; s.run(1, 0, 0, 0, 0); s.run(3, 0.3f, 0, 0, 0);            // trail and peak
    CHECK(s.m.trailCount() > 5 && s.m.trailCount() <= TRAIL); float la, lo; s.m.trailAt(s.m.trailCount() - 1, &la, &lo); NEAR(la, 0.3f, 0.06f);
    float p = s.m.peakTotal(); CHECK(p > 0.2f); s.m.resetPeak(); CHECK(s.m.peakTotal() == 0); }
  { Meter m; m.feed({0, 1, 0}, {0, 0, 0}, -1, 1000); m.feed({0, 1, 0}, {0, 0, 0}, -1, 1033);   // unknown speed (no car link): still works, signs stay at their defaults
    CHECK(m.signLat() == 1 && m.signLon() == 1); NEAR(m.total(), 0, 0.01f); }
  printf("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
