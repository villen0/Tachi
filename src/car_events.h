#pragma once
// One-off car events (engine start, hard braking, low battery, low fuel), pure C++ (no Arduino) so it can be
// unit-tested on a PC: tools/tests/car_events_test.cpp. main.cpp feeds it the current readings every Face frame and
// plays a short face reaction for whatever it returns; the thresholds come from config.h.
#include <stdint.h>

namespace carevents {

enum Event : uint8_t { EV_NONE, EV_ENGINE_START, EV_HARD_BRAKE, EV_LOW_BATTERY, EV_LOW_FUEL };

struct Config {
  int engineOffRpm = 400, engineStartRpm = 500, runningRpm = 600;
  uint32_t engineOffMs = 3000;            // engine must have been off (data fresh, RPM low) this long before a start counts
  int brakeMinMph = 15, brakeDropMph = 10;  // speed before the stop, and the drop between two 1 s samples
  float battCritV = 11.5f, fuelLowPct = 7.0f;
  uint32_t startCooldownMs = 20000, brakeCooldownMs = 8000, battCooldownMs = 60000, fuelCooldownMs = 120000;
};

struct Input {
  uint32_t now;
  bool rpmFresh; int rpm;
  bool speedFresh; int mph;
  bool battFresh; float battV;
  bool fuelFresh; float fuelPct;
};

class Detector {
 public:
  explicit Detector(const Config &c = Config()) : cfg(c) {}

  // Call often (every frame is fine). Returns at most one event per call, highest priority first:
  // hard brake, engine start, low battery, low fuel.
  Event update(const Input &in) {
    // The Face page is not always on screen, so calls can stop for a while. After a gap nothing from before it may count.
    if (called && in.now - lastCall > 3000) { offKnown = false; prevValid = false; }
    called = true; lastCall = in.now;

    bool start = false, brake = false;

    // Engine start: RPM was low for a while with fresh data, then reaches the start level.
    if (in.rpmFresh) {
      if (in.rpm < cfg.engineOffRpm) { if (!offKnown) { offKnown = true; offSince = in.now; } }
      else if (in.rpm >= cfg.engineStartRpm) {
        start = offKnown && in.now - offSince >= cfg.engineOffMs;
        offKnown = false;
      }
    } else offKnown = false;                    // unknown: do not count a gap as "engine was off"

    // Hard braking: judged only between two consecutive fresh speed samples about a second apart,
    // so the first reading after boot / a reconnect / a gap just seeds prevMph.
    if (!in.speedFresh) prevValid = false;
    else if (in.now - lastSample >= 1000) {
      brake = prevValid && prevMph >= cfg.brakeMinMph && prevMph - in.mph >= cfg.brakeDropMph;
      prevMph = in.mph; prevValid = true; lastSample = in.now;
    }

    Event ev = EV_NONE;
    if (brake && ready(brakeAt, in.now)) { ev = EV_HARD_BRAKE; brakeAt = in.now + cfg.brakeCooldownMs; }
    else if (start && ready(startAt, in.now)) { ev = EV_ENGINE_START; startAt = in.now + cfg.startCooldownMs; }
    else if (in.battFresh && in.rpmFresh && in.rpm > cfg.runningRpm && in.battV > 5.0f && in.battV < cfg.battCritV && ready(battAt, in.now)) {
      ev = EV_LOW_BATTERY; battAt = in.now + cfg.battCooldownMs;
    } else if (in.fuelFresh && in.fuelPct >= 0 && in.fuelPct < cfg.fuelLowPct && ready(fuelAt, in.now)) {
      ev = EV_LOW_FUEL; fuelAt = in.now + cfg.fuelCooldownMs;
    }
    return ev;
  }

 private:
  Config cfg;
  bool called = false; uint32_t lastCall = 0;
  bool offKnown = false; uint32_t offSince = 0;
  bool prevValid = false; int prevMph = 0; uint32_t lastSample = 0;
  uint32_t startAt = 0, brakeAt = 0, battAt = 0, fuelAt = 0;   // earliest time the event may fire again
  bool ready(uint32_t at, uint32_t now) const { return now >= at; }
};

}  // namespace carevents
