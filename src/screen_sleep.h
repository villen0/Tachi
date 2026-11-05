#pragma once
// When the screen should be lit. Pure C++ (no Arduino) so it can be unit-tested on a PC: tools/tests/screen_sleep_test.cpp
//
// The car's USB port stays powered after the engine is off, so the board keeps running. This decides when the backlight can go dark:
// after the car has been silent for a while, and not just after power-up (the first connection can take a while). A touch or the
// BOOT button wakes it for a short time, fresh car data wakes it at once. All times are milliseconds since power-up.
#include <stdint.h>

namespace screensleep {

struct Config {
  uint32_t staleMs = 5000;          // car data older than this counts as "silent"
  uint32_t offAfterMs = 120000;     // silent this long -> screen off (0 = never turn it off)
  uint32_t wakeMs = 30000;          // a touch keeps the screen on this long
  uint32_t bootGraceMs = 25000;     // after power-up, never off while still waiting for the very first data
};

class ScreenSleep {
 public:
  explicit ScreenSleep(const Config &c = Config()) : cfg(c) {}

  // lastValidMs: when car data (not the adapter's own battery reading) last arrived, 0 if never. lastTouchMs: last finger-down or
  // BOOT press, 0 if never. Returns true when the screen should be lit.
  bool update(uint32_t now, uint32_t lastValidMs, bool everData, uint32_t lastTouchMs, bool demo) const {
    if (cfg.offAfterMs == 0 || demo) return true;
    if (lastValidMs && now - lastValidMs <= cfg.staleMs) return true;     // car data is flowing
    if (lastTouchMs && now - lastTouchMs < cfg.wakeMs) return true;       // somebody touched it a moment ago
    if (!everData && now < cfg.bootGraceMs) return true;                  // still waiting for the first data after power-up
    return now - lastValidMs < cfg.offAfterMs;                            // quiet since the last data (or since power-up when lastValidMs is 0)
  }

 private:
  Config cfg;
};

}  // namespace screensleep
