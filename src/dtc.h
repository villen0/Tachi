#pragma once
// OBD-II fault codes (DTCs): decoding the replies to modes 03 / 07 / 0A and the check-engine status from PID 0101. Pure C++ (no Arduino)
// so it can be unit-tested on a PC: tools/tests/dtc_test.cpp
//
// Replies reach this code as one string. Normal requests have all separators removed (see main.cpp onNotify), but while codes are
// being read the line breaks are kept as '|' so two modules' answers and multi-frame answers can be told apart:
//   single frame   "43020133022200"          43 = mode 03 answered, 02 = two codes (CAN adds this count), P0133, P0222, padding
//   no count byte  "43013302220000"          older protocols: the pairs follow 43 directly
//   multi frame    "008|0:430301330222|1:04200000000000"
//   two modules    "43010133000000|4300"
//   nothing        "NODATA" or "4300..."     treated as "no codes"
#include <stdint.h>
#include <string.h>
#include "obd_parse.h"

namespace dtc {

constexpr int MAX_CODES = 16;

struct Result {
  char codes[MAX_CODES][6] = {{0}};   // "P0301"
  int n = 0;
  bool answered = false;              // at least one module gave a valid answer (including "no codes")
};

inline char hexdigit(int v) { return "0123456789ABCDEF"[v & 15]; }

// Two bytes -> "P0301" style text (out needs 6 chars).
inline void decode(int a, int b, char *out) {
  static const char L[4] = {'P', 'C', 'B', 'U'};
  out[0] = L[(a >> 6) & 3]; out[1] = (char)('0' + ((a >> 4) & 3)); out[2] = hexdigit(a);
  out[3] = hexdigit(b >> 4); out[4] = hexdigit(b); out[5] = 0;
}

inline void add(Result &r, const char *code) {
  for (int i = 0; i < r.n; i++) if (strcmp(r.codes[i], code) == 0) return;      // the same code from two modules counts once
  if (r.n < MAX_CODES) { strcpy(r.codes[r.n], code); r.n++; }
}

// One complete answer (bytes after the service id) -> codes. Decides whether the first byte is a count.
inline void takeMessage(Result &r, const int *b, int n) {
  if (n < 0) return;
  auto nonzeroPairs = [&](int from) { int c = 0; for (int i = from; i + 1 < n; i += 2) if (b[i] || b[i + 1]) c++; return c; };
  int from = 0;
  if (n >= 1 && b[0] == nonzeroPairs(1)) from = 1;                                // "43 <count> pairs..."
  r.answered = true;
  for (int i = from; i + 1 < n; i += 2) if (b[i] || b[i + 1]) { char c[6]; decode(b[i], b[i + 1], c); add(r, c); }
}

// mode = 3, 7 or 10 (0x0A). reply = everything the adapter returned (with or without '|' line separators).
inline Result parseReply(const char *reply, int mode) {
  Result r;
  const int sid = 0x40 + mode;
  char sidTxt[3] = {hexdigit(sid >> 4), hexdigit(sid), 0};
  int len = (int)strlen(reply);
  int pos = 0;
  while (pos < len) {
    int end = pos; while (end < len && reply[end] != '|') end++;                 // one line is [pos, end)
    int n = end - pos;
    const char *L = reply + pos;
    int bytes[64]; int nb = 0;
    if (n >= 4 && L[0] == sidTxt[0] && L[1] == sidTxt[1]) {                       // single frame answer
      for (int i = 2; i + 1 < n && nb < 64; i += 2) { int v; if (!obd::byteAt(L, i, &v)) break; bytes[nb++] = v; }
      takeMessage(r, bytes, nb);
      pos = end + 1; continue;
    }
    if (n == 3 && obd::hexv(L[0]) >= 0 && obd::hexv(L[1]) >= 0 && obd::hexv(L[2]) >= 0) {   // "008": multi frame, total byte count
      int total = obd::hexv(L[0]) * 256 + obd::hexv(L[1]) * 16 + obd::hexv(L[2]);
      int p2 = end + 1; nb = 0;
      while (p2 < len) {                                                          // the "0:" "1:" ... frame lines that follow
        int e2 = p2; while (e2 < len && reply[e2] != '|') e2++;
        const char *F = reply + p2; int fn = e2 - p2;
        if (fn < 2 || F[1] != ':' || obd::hexv(F[0]) < 0) break;
        for (int i = 2; i + 1 < fn && nb < 64; i += 2) { int v; if (!obd::byteAt(F, i, &v)) break; bytes[nb++] = v; }
        p2 = e2 + 1;
      }
      if (nb > total) nb = total;
      if (nb >= 1 && bytes[0] == sid) takeMessage(r, bytes + 1, nb - 1);
      pos = p2; continue;
    }
    pos = end + 1;                                                                // NODATA, SEARCHING..., noise: skip
  }
  // Without separators (a reply that arrived as one string) a single answer still works: find the service id and read to the end.
  if (!r.answered && !strchr(reply, '|')) {
    const char *p = strstr(reply, sidTxt);
    if (p && (p == reply)) {
      int bytes[64], nb = 0;
      for (int i = 2; i + 1 < len && nb < 64; i += 2) { int v; if (!obd::byteAt(reply, i, &v)) break; bytes[nb++] = v; }
      takeMessage(r, bytes, nb);
    }
  }
  return r;
}

// Check-engine light and the number of stored codes, from the reply to 0101 ("4101" + 4 bytes: bit 7 of the first byte = light on).
struct Mil { bool valid = false, on = false; int count = 0; };
inline Mil parseMil(const char *reply) {
  Mil m; int b[4];
  if (!obd::dataBytes(reply, 0x01, 4, b)) return m;
  m.valid = true; m.on = (b[0] & 0x80) != 0; m.count = b[0] & 0x7F;
  return m;
}

}  // namespace dtc
