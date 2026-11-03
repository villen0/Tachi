#pragma once
// One-off car events, pure C++ (no Arduino) so it can be unit-tested on a PC: tools/tests/car_events_test.cpp.
// main.cpp feeds it the current readings every Face frame and plays a short face reaction for whatever it returns;
// the thresholds come from config.h.
//
//   engine start   RPM was low for a while, now above the start level
//   hard braking   speed drops fast between two 1 s samples
//   redline        RPM reaches the redline (edge: once per visit)
//   hard launch    speed jumps up from a near-stop with high revs
//   smooth stop    the car comes to a stop without hard braking
//   engine off     RPM falls to zero after the engine was running
//   engine warm    coolant reaches working temperature after a cold start (once per power-up)
//   low battery / low fuel   level checks with long cooldowns
#include <stdint.h>

namespace carevents {

enum Event : uint8_t { EV_NONE, EV_ENGINE_START, EV_HARD_BRAKE, EV_LOW_BATTERY, EV_LOW_FUEL,
                       EV_REDLINE, EV_HARD_LAUNCH, EV_SMOOTH_STOP, EV_ENGINE_OFF, EV_ENGINE_WARM };

struct Config {
  int engineOffRpm = 400, engineStartRpm = 500, runningRpm = 600, engineStoppedRpm = 100;
  uint32_t engineOffMs = 3000;            // engine must have been off (data fresh, RPM low) this long before a start counts
  int brakeMinMph = 15, brakeDropMph = 10;  // speed before the stop, and the drop between two 1 s samples
  float battCritV = 11.5f, fuelLowPct = 7.0f;
  int redlineRpm = 6500, redlineMarginRpm = 250;           // fires at redline - margin, re-arms 800 rpm below the redline
  int launchMaxMph = 25, launchRiseMph = 6, launchMinRpm = 3000;   // hard launch: from below launchMaxMph, +launchRiseMph in 1 s, with these revs
  int stopMinMph = 8; uint32_t stopWindowMs = 10000;       // smooth stop: was above stopMinMph within this window, now 0, no hard brake in the window
  int warmC = 80;                                          // coolant that counts as warmed up (deg C); needs a cold reading first
  uint32_t startCooldownMs = 20000, brakeCooldownMs = 8000, battCooldownMs = 60000, fuelCooldownMs = 120000,
           redlineCooldownMs = 8000, launchCooldownMs = 15000, stopCooldownMs = 30000, offCooldownMs = 30000;
};

struct Input {
  uint32_t now;
  bool rpmFresh; int rpm;
  bool speedFresh; int mph;
  bool battFresh; float battV;
  bool fuelFresh; float fuelPct;
  bool coolFresh = false; int coolC = 0;                   // coolant in deg C
};

class Detector {
 public:
  explicit Detector(const Config &c = Config()) : cfg(c) {}

  // Call often (every frame is fine). Returns at most one event per call. Priority: hard brake, redline, hard launch,
  // engine start, smooth stop, engine off, engine warm, low battery, low fuel.
  Event update(const Input &in) {
    // The Face page is not always on screen, so calls can stop for a while. After a gap nothing from before it may count.
    if (called && in.now - lastCall > 3000) { offKnown = false; prevValid = false; haveFast = false; inRedline = false; }
    called = true; lastCall = in.now;

    bool start = false, brake = false, launch = false, stop = false, off = false, warm = false, redline = false;

    // Engine start: RPM was low for a while with fresh data, then reaches the start level.
    if (in.rpmFresh) {
      if (in.rpm < cfg.engineOffRpm) { if (!offKnown) { offKnown = true; offSince = in.now; } }
      else if (in.rpm >= cfg.engineStartRpm) {
        start = offKnown && in.now - offSince >= cfg.engineOffMs;
        offKnown = false;
      }
    } else offKnown = false;                    // unknown: do not count a gap as "engine was off"

    // Redline: edge-triggered, so holding the revs at the limit gives one reaction.
    if (in.rpmFresh) {
      if (!inRedline && in.rpm >= cfg.redlineRpm - cfg.redlineMarginRpm) { inRedline = true; redline = true; }
      else if (inRedline && in.rpm < cfg.redlineRpm - 800) inRedline = false;
    } else inRedline = false;

    // Engine off: the engine was running and the revs now read (nearly) zero with fresh data.
    if (in.rpmFresh) {
      if (in.rpm >= cfg.runningRpm) wasRunning = true;
      else if (in.rpm <= cfg.engineStoppedRpm && wasRunning) { off = true; wasRunning = false; }
    }

    // Speed events are judged only between two consecutive fresh speed samples about a second apart,
    // so the first reading after boot / a reconnect / a gap just seeds prevMph.
    if (!in.speedFresh) { prevValid = false; haveFast = false; }
    else if (in.now - lastSample >= 1000) {
      if (prevValid) {
        brake = prevMph >= cfg.brakeMinMph && prevMph - in.mph >= cfg.brakeDropMph;
        if (brake) lastBrakeMs = in.now, haveBrake = true;
        launch = prevMph <= cfg.launchMaxMph && in.mph - prevMph >= cfg.launchRiseMph && in.rpmFresh && in.rpm >= cfg.launchMinRpm;
        bool hardRecently = haveBrake && in.now - lastBrakeMs < cfg.stopWindowMs;
        stop = in.mph == 0 && prevMph > 0 && haveFast && in.now - lastFastMs <= cfg.stopWindowMs && !hardRecently;
      }
      if (in.mph >= cfg.stopMinMph) { lastFastMs = in.now; haveFast = true; }
      prevMph = in.mph; prevValid = true; lastSample = in.now;
    }

    // Engine warm: once per power-up, and only if the engine was seen cold first (not at every restart of a warm car).
    if (in.coolFresh && in.coolC > -30 && in.coolC < 150) {
      if (in.coolC < cfg.warmC - 8) sawCold = true;
      if (sawCold && !warmFired && in.coolC >= cfg.warmC) { warm = true; warmFired = true; }
    }

    Event ev = EV_NONE;
    if (brake && ready(brakeAt, in.now)) { ev = EV_HARD_BRAKE; brakeAt = in.now + cfg.brakeCooldownMs; }
    else if (redline && ready(redlineAt, in.now)) { ev = EV_REDLINE; redlineAt = in.now + cfg.redlineCooldownMs; }
    else if (launch && ready(launchAt, in.now)) { ev = EV_HARD_LAUNCH; launchAt = in.now + cfg.launchCooldownMs; }
    else if (start && ready(startAt, in.now)) { ev = EV_ENGINE_START; startAt = in.now + cfg.startCooldownMs; }
    else if (stop && ready(stopAt, in.now)) { ev = EV_SMOOTH_STOP; stopAt = in.now + cfg.stopCooldownMs; }
    else if (off && ready(offAt, in.now)) { ev = EV_ENGINE_OFF; offAt = in.now + cfg.offCooldownMs; }
    else if (warm) ev = EV_ENGINE_WARM;
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
  bool inRedline = false, wasRunning = false, sawCold = false, warmFired = false;
  bool haveFast = false, haveBrake = false; uint32_t lastFastMs = 0, lastBrakeMs = 0;
  uint32_t startAt = 0, brakeAt = 0, battAt = 0, fuelAt = 0, redlineAt = 0, launchAt = 0, stopAt = 0, offAt = 0;   // earliest time the event may fire again
  bool ready(uint32_t at, uint32_t now) const { return now >= at; }
};

}  // namespace carevents
