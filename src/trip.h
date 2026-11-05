#pragma once
// Trip tracking and the drive rating, pure C++ (no Arduino) so it can be unit-tested on a PC: tools/tests/trip_test.cpp
//
// main.cpp feeds the tracker about once a second with the current readings. A trip starts when the engine runs, ends when it has
// been off (or silent) for a while, and is then scored and kept in a small ring of the last 5 trips (saved in flash by main.cpp).
// Peaks that need a reading the car does not give (boost, fuel, intake air) stay "unknown" instead of 0.
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace trip {

constexpr int16_t UNKNOWN16 = -32768;
constexpr int HISTORY = 5;

struct Config {
  uint32_t endMs = 30000;                 // engine off / no car data this long -> the trip is over
  uint32_t minSeconds = 90;               // shorter trips are thrown away (a quick start-stop)
  float minMiles = 0.2f;
  int startRpm = 500, runRpm = 400;       // starts at startRpm, counts as running above runRpm
  int brakeMinMph = 15, brakeDropMph = 10, launchMaxMph = 25, launchRiseMph = 6, launchRpm = 3000;
  uint32_t brakeGapMs = 5000, launchGapMs = 8000;   // one event per this much time at most
  int highRpm = 5500;                     // time above this costs rating points
  int uneasyC = 100, worriedC = 108;      // coolant thresholds (same as the face moods)
};

struct Sample {
  uint32_t now = 0;
  bool rpmFresh = false; int rpm = 0;
  bool speedFresh = false; int mph = 0;
  bool coolFresh = false; int coolC = 0;
  bool intakeFresh = false; int intakeC = 0;
  bool battFresh = false; float battV = 0;
  bool boostFresh = false; float boostPsi = 0;
  bool fuelFresh = false; float fuelPct = 0;
};

#pragma pack(push, 1)
struct Record {                           // one finished (or live) trip, fixed size so it can be stored as raw bytes
  uint16_t seq = 0;                       // trip number since the history was created
  uint16_t durationS = 0, idleS = 0, distCentiMi = 0;
  uint8_t topMph = 0, avgMph = 0;
  uint16_t maxRpm = 0;
  int16_t maxCoolC = UNKNOWN16, maxIntakeC = UNKNOWN16;
  int16_t peakBoostDeciPsi = UNKNOWN16;   // 0.1 psi; negative = vacuum (a non-turbo engine never goes above 0)
  uint8_t minBattDeciV = 0, maxBattDeciV = 0;   // 0.1 V, 0 = unknown
  int8_t fuelUsedPct = -1;                // -1 = unknown
  uint8_t hardBrakes = 0, hardLaunches = 0;
  uint16_t secHighRpm = 0;
  uint8_t rating = 0;                     // 0..100
  uint8_t newCodeCount = 0;               // fault codes that appeared during the trip
  char newCodes[4][6] = {{0}, {0}, {0}, {0}};
  uint8_t flags = 0;                      // bit 0: coolant reached "uneasy", bit 1: "worried"
};
#pragma pack(pop)

inline char gradeOf(int rating) { return rating >= 90 ? 'A' : rating >= 80 ? 'B' : rating >= 70 ? 'C' : rating >= 60 ? 'D' : 'F'; }

// Tachi's own rating, documented in the README: 100 minus penalties, never below 0.
inline int computeRating(const Record &r, const Config &c = Config()) {
  float miles = r.distCentiMi / 100.0f; if (miles < 1.0f) miles = 1.0f;       // short trips are judged as if 1 mile long
  float p = 0;
  p += 6.0f * (r.hardBrakes / miles) * 10.0f;                                  // 6 per hard brake per 10 miles
  p += 3.0f * r.hardLaunches;
  p += 2.0f * (r.secHighRpm / 60.0f);                                          // 2 per minute above the high-RPM line
  if (r.flags & 2) p += 20.0f; else if (r.flags & 1) p += 10.0f;               // overheating
  if (r.durationS > 0 && r.idleS * 4 > r.durationS) p += 5.0f;                 // idling more than a quarter of the trip
  (void)c;
  int v = (int)lroundf(100.0f - p);
  return v < 0 ? 0 : (v > 100 ? 100 : v);
}

class Tracker {
 public:
  enum Event : uint8_t { T_NONE, T_STARTED, T_ENDED, T_DISCARDED };
  explicit Tracker(const Config &c = Config()) : cfg(c) {}

  // Call about once a second. Returns T_STARTED / T_ENDED (a finished trip is waiting in takeFinished()) / T_DISCARDED.
  Event update(const Sample &s) {
    bool running = s.rpmFresh && s.rpm >= (on ? cfg.runRpm : cfg.startRpm);
    if (!on) {
      if (!running) return T_NONE;
      begin(s);
      return T_STARTED;
    }
    uint32_t dtMs = s.now - lastNow; lastNow = s.now;
    if (dtMs > 5000) dtMs = 5000;                                              // a gap in the polling must not count as driving time
    float dt = dtMs / 1000.0f;
    if (running) {
      lastRunMs = s.now;
      accumulate(s, dt);
      return T_NONE;
    }
    if (s.now - lastRunMs >= cfg.endMs) return finish();
    return T_NONE;
  }

  bool active() const { return on; }
  uint16_t nextSeq() const { return seq; }
  void setNextSeq(uint16_t n) { seq = n; }

  // The trip so far (valid while active()).
  Record live() const { return snapshot(lastRunMs - startMs); }

  bool takeFinished(Record *out) { if (!haveDone) return false; *out = done; haveDone = false; return true; }

  void addNewCode(const char *code) {                                          // a fault code that appeared during this trip
    if (!on || !code || !code[0]) return;
    for (int i = 0; i < nCodes && i < 4; i++) if (strncmp(codes[i], code, 5) == 0) return;
    if (nCodes < 4) { strncpy(codes[nCodes], code, 5); codes[nCodes][5] = 0; }
    nCodes++;
  }

 private:
  Config cfg;
  bool on = false, haveDone = false;
  uint16_t seq = 1;
  uint32_t startMs = 0, lastNow = 0, lastRunMs = 0;
  float distMi = 0, idleS = 0, highS = 0;
  int topMph = 0, maxRpm = 0, maxCool = UNKNOWN16, maxIntake = UNKNOWN16;
  float peakBoost = -1000; bool haveBoost = false;
  float minBatt = 1000, maxBatt = 0;
  float fuelStart = -1, fuelEnd = -1;
  int brakes = 0, launches = 0;
  int prevMph = 0; bool prevValid = false; uint32_t prevT = 0, brakeAt = 0, launchAt = 0;
  uint8_t flags = 0;
  int nCodes = 0; char codes[4][6] = {{0}, {0}, {0}, {0}};
  Record done;

  void begin(const Sample &s) {
    on = true; startMs = lastNow = lastRunMs = s.now;
    distMi = idleS = highS = 0; topMph = 0; maxRpm = 0; maxCool = maxIntake = UNKNOWN16; haveBoost = false; peakBoost = -1000;
    minBatt = 1000; maxBatt = 0; fuelStart = fuelEnd = -1; brakes = launches = 0; prevValid = false; brakeAt = launchAt = 0; flags = 0;
    nCodes = 0; memset(codes, 0, sizeof(codes));
    accumulate(s, 0);
  }

  void accumulate(const Sample &s, float dt) {
    if (s.rpm > maxRpm) maxRpm = s.rpm;
    if (s.rpm >= cfg.highRpm) highS += dt;
    if (s.speedFresh) {
      distMi += s.mph * dt / 3600.0f;
      if (s.mph > topMph) topMph = s.mph;
      if (s.mph == 0) idleS += dt;
      // hard braking / launch: between two samples about a second apart, so a reconnect never looks like a crash stop
      if (prevValid && s.now - prevT >= 900 && s.now - prevT <= 3000) {
        if (prevMph >= cfg.brakeMinMph && prevMph - s.mph >= cfg.brakeDropMph && s.now >= brakeAt) { brakes++; brakeAt = s.now + cfg.brakeGapMs; }
        if (prevMph <= cfg.launchMaxMph && s.mph - prevMph >= cfg.launchRiseMph && s.rpm >= cfg.launchRpm && s.now >= launchAt) { launches++; launchAt = s.now + cfg.launchGapMs; }
      }
      if (!prevValid || s.now - prevT >= 900) { prevMph = s.mph; prevT = s.now; prevValid = true; }
    } else prevValid = false;
    if (s.coolFresh) {
      if (maxCool == UNKNOWN16 || s.coolC > maxCool) maxCool = (int16_t)s.coolC;
      if (s.coolC >= cfg.worriedC) flags |= 3; else if (s.coolC >= cfg.uneasyC) flags |= 1;
    }
    if (s.intakeFresh && (maxIntake == UNKNOWN16 || s.intakeC > maxIntake)) maxIntake = (int16_t)s.intakeC;
    if (s.boostFresh && (!haveBoost || s.boostPsi > peakBoost)) { peakBoost = s.boostPsi; haveBoost = true; }
    if (s.battFresh && s.battV > 5.0f && s.battV < 20.0f) { if (s.battV < minBatt) minBatt = s.battV; if (s.battV > maxBatt) maxBatt = s.battV; }
    if (s.fuelFresh && s.fuelPct >= 0 && s.fuelPct <= 100) { if (fuelStart < 0) fuelStart = s.fuelPct; fuelEnd = s.fuelPct; }
  }

  Record snapshot(uint32_t durMs) const {
    Record r;
    r.seq = seq;
    uint32_t sec = durMs / 1000; r.durationS = (uint16_t)(sec > 65535 ? 65535 : sec);
    r.idleS = (uint16_t)(idleS > 65535 ? 65535 : idleS);
    float cm = distMi * 100.0f; r.distCentiMi = (uint16_t)(cm > 65535 ? 65535 : (cm < 0 ? 0 : cm + 0.5f));
    r.topMph = (uint8_t)(topMph > 255 ? 255 : topMph);
    r.avgMph = durMs > 0 ? (uint8_t)fminf(255.0f, distMi / (durMs / 3600000.0f) + 0.5f) : 0;
    r.maxRpm = (uint16_t)(maxRpm > 65535 ? 65535 : maxRpm);
    r.maxCoolC = (int16_t)maxCool; r.maxIntakeC = (int16_t)maxIntake;
    r.peakBoostDeciPsi = haveBoost ? (int16_t)lroundf(peakBoost * 10.0f) : UNKNOWN16;
    r.minBattDeciV = maxBatt > 0 ? (uint8_t)lroundf(minBatt * 10.0f) : 0; r.maxBattDeciV = maxBatt > 0 ? (uint8_t)lroundf(maxBatt * 10.0f) : 0;
    r.fuelUsedPct = (fuelStart >= 0 && fuelEnd >= 0) ? (int8_t)lroundf(fminf(100.0f, fmaxf(0.0f, fuelStart - fuelEnd))) : -1;
    r.hardBrakes = (uint8_t)(brakes > 255 ? 255 : brakes); r.hardLaunches = (uint8_t)(launches > 255 ? 255 : launches);
    r.secHighRpm = (uint16_t)(highS > 65535 ? 65535 : highS);
    r.newCodeCount = (uint8_t)(nCodes > 255 ? 255 : nCodes); memcpy(r.newCodes, codes, sizeof(codes));
    r.flags = flags;
    r.rating = (uint8_t)computeRating(r, cfg);
    return r;
  }

  Event finish() {
    on = false;
    Record r = snapshot(lastRunMs - startMs);
    if (r.durationS < cfg.minSeconds || r.distCentiMi < (uint16_t)(cfg.minMiles * 100.0f)) return T_DISCARDED;
    done = r; haveDone = true; seq++;
    return T_ENDED;
  }
};

// The last HISTORY trips, newest first, as a plain struct that can be written to flash byte for byte.
struct History {
  uint32_t magic = 0x54524950;            // "TRIP"
  uint16_t version = 1, recSize = sizeof(Record);
  uint16_t nextSeq = 1;
  uint8_t count = 0, head = 0;            // head = slot the next trip is written to
  Record rec[HISTORY];

  void push(const Record &r) { rec[head] = r; head = (head + 1) % HISTORY; if (count < HISTORY) count++; nextSeq = r.seq + 1; }
  const Record *get(int i) const {        // 0 = newest
    if (i < 0 || i >= count) return nullptr;
    return &rec[(head + HISTORY - 1 - i) % HISTORY];
  }
  bool valid() const { return magic == 0x54524950 && version == 1 && recSize == sizeof(Record) && count <= HISTORY && head < HISTORY; }
};

}  // namespace trip
