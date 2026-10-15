#pragma once
// OBD-II / ELM327 reply parsing, pure C++ (no Arduino) so it can be unit-tested on a PC: tools/tests/obd_parse_test.cpp
//
// Replies arrive with spaces/newlines/prompt already stripped, e.g. "410D00" (speed), "410C0BB3" (RPM).
// This car has two modules answering every request, so a reply often contains the same answer twice
// ("410D00410D00"); we always use the first one.
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

namespace obd {

inline int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Reads two hex digits at r[pos]; false if they are missing or not hex (e.g. the reply is cut short).
inline bool byteAt(const char *r, int pos, int *out) {
  int len = (int)strlen(r);
  if (pos < 0 || pos + 2 > len) return false;
  int a = hexv(r[pos]), b = hexv(r[pos + 1]);
  if (a < 0 || b < 0) return false;
  *out = a * 16 + b;
  return true;
}

// Finds the first "41<pid>" answer and copies n data bytes after it. out must hold n ints.
inline bool dataBytes(const char *r, int pid, int n, int *out) {
  char tag[5] = {'4', '1', "0123456789ABCDEF"[(pid >> 4) & 15], "0123456789ABCDEF"[pid & 15], 0};
  const char *p = strstr(r, tag);
  if (!p) return false;
  int base = (int)(p - r) + 4;
  for (int k = 0; k < n; k++) if (!byteAt(r, base + 2 * k, &out[k])) return false;
  return true;
}

// ---- supported PIDs ---------------------------------------------------------------------------------
// Requests 0100, 0120, 0140, 0160 each return a 32 bit mask for the next 32 PIDs. The most significant
// bit is the first PID of the block (0x01 for the answer to 0100).
struct SupportSet {
  uint32_t mask[4] = {0, 0, 0, 0};   // blocks 00, 20, 40, 60
  bool have[4] = {false, false, false, false};
  void clear() { for (int i = 0; i < 4; i++) { mask[i] = 0; have[i] = false; } }
  void set(int base, uint32_t m) { int i = base / 0x20; if (i >= 0 && i < 4) { mask[i] = m; have[i] = true; } }
  bool supported(int pid) const {
    if (pid < 1 || pid > 0x80) return false;
    int i = (pid - 1) / 0x20, bit = (pid - 1) % 0x20;
    return have[i] && ((mask[i] >> (31 - bit)) & 1);
  }
  int count() const { int n = 0; for (int p = 1; p <= 0x80; p++) if (supported(p)) n++; return n; }
};

// Reply to request 01<base> (base = 0x00, 0x20, 0x40, 0x60): "41" + base + 4 mask bytes.
inline bool parseBitmap(const char *r, int base, uint32_t *mask) {
  int b[4];
  if (!dataBytes(r, base, 4, b)) return false;
  *mask = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
  return true;
}

// ---- adapter voltage ("ATRV") ------------------------------------------------------------------------
// The adapter answers like "12.6V" (some clones omit the V, some add text). Takes the first number.
inline bool parseAtrv(const char *r, float *volts) {
  const char *p = r;
  while (*p && !(*p >= '0' && *p <= '9')) p++;
  if (!*p) return false;
  char *end = nullptr;
  float v = strtof(p, &end);
  if (end == p) return false;
  if (v < 3.0f || v > 40.0f) return false;     // not a plausible battery voltage
  *volts = v;
  return true;
}

// ---- value formulas (mode 01) -------------------------------------------------------------------------
inline int   tempC(int a) { return a - 40; }                       // 0105, 010F, 0146
inline float percent(int a) { return a * 100.0f / 255.0f; }        // 0104, 0111, 012F
inline float moduleVolts(int a, int b) { return (a * 256 + b) / 1000.0f; }   // 0142
inline int   rpmOf(int a, int b) { return (a * 256 + b) / 4; }     // 010C
inline int   mphOf(int a) { return (int)(a * 0.621f + 0.5f); }     // 010D (km/h -> mph)

}  // namespace obd
