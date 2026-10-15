// Host test for src/obd_parse.h.   g++ -std=c++17 -Wall -I../../src obd_parse_test.cpp -o obd_parse_test && ./obd_parse_test
#include "obd_parse.h"
#include <cstdio>
#include <cmath>

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
  int b[4];
  // --- real replies read off the user's Debug page photos (spaces already removed) ---
  CHECK(obd::dataBytes("410C0BB3410C0BAF", 0x0C, 2, b) && obd::rpmOf(b[0], b[1]) == 748);   // first of two answers
  CHECK(obd::dataBytes("410C0BC4410C0BC6", 0x0C, 2, b) && obd::rpmOf(b[0], b[1]) == 753);
  CHECK(obd::dataBytes("410D00410D00", 0x0D, 1, b) && obd::mphOf(b[0]) == 0);
  CHECK(obd::dataBytes("410574410574", 0x05, 1, b) && obd::tempC(b[0]) == 76);               // 169 F on the screen
  // speed: 0x36 = 54 km/h -> 34 mph
  CHECK(obd::dataBytes("410D36410D36", 0x0D, 1, b) && obd::mphOf(b[0]) == 34);
  // --- bad / partial replies must not parse ---
  CHECK(!obd::dataBytes("NODATA", 0x0D, 1, b));
  CHECK(!obd::dataBytes("", 0x0D, 1, b));
  CHECK(!obd::dataBytes("?", 0x0D, 1, b));
  CHECK(!obd::dataBytes("410C0B", 0x0C, 2, b));          // cut short
  CHECK(!obd::dataBytes("410D", 0x0D, 1, b));
  CHECK(!obd::dataBytes("410C0BB3", 0x0D, 1, b));        // other PID
  CHECK(!obd::dataBytes("410CZZZZ", 0x0C, 2, b));        // not hex
  CHECK(obd::dataBytes("SEARCHING...410D10", 0x0D, 1, b) && b[0] == 0x10);   // adapter text in front
  // --- supported-PID bitmaps ---
  obd::SupportSet s;
  uint32_t m;
  CHECK(obd::parseBitmap("4100BE3FA813", 0x00, &m) && m == 0xBE3FA813u);
  s.set(0x00, m);
  CHECK(s.supported(0x01));      // 0xBE = 1011 1110: PID 01 yes
  CHECK(!s.supported(0x02));     // PID 02 no
  CHECK(s.supported(0x03));
  CHECK(s.supported(0x05));      // coolant
  CHECK(s.supported(0x0C));      // RPM
  CHECK(s.supported(0x0D));      // speed
  CHECK(s.supported(0x20));      // block 0x20 advertised
  CHECK(!s.supported(0x21));     // block not read yet
  CHECK(obd::parseBitmap("4100BE3FA8134100BE3FA813", 0x00, &m) && m == 0xBE3FA813u);   // duplicated answer
  CHECK(!obd::parseBitmap("4100BE3F", 0x00, &m));        // truncated
  CHECK(!obd::parseBitmap("NODATA", 0x00, &m));
  CHECK(obd::parseBitmap("4120A0000001", 0x20, &m) && m == 0xA0000001u);
  s.set(0x20, m);
  CHECK(s.supported(0x21) && !s.supported(0x22) && s.supported(0x23) && s.supported(0x40));
  s.clear();
  CHECK(!s.supported(0x05) && s.count() == 0);
  CHECK(!s.supported(0) && !s.supported(0x81));
  // --- ATRV ---
  float v = 0;
  CHECK(obd::parseAtrv("12.6V", &v) && fabsf(v - 12.6f) < 0.01f);
  CHECK(obd::parseAtrv("14.2", &v) && fabsf(v - 14.2f) < 0.01f);
  CHECK(obd::parseAtrv("ATRV12.1V", &v) && fabsf(v - 12.1f) < 0.01f);
  CHECK(obd::parseAtrv("13V", &v) && fabsf(v - 13.0f) < 0.01f);
  CHECK(!obd::parseAtrv("?", &v));
  CHECK(!obd::parseAtrv("OK", &v));
  CHECK(!obd::parseAtrv("", &v));
  CHECK(!obd::parseAtrv("0.0V", &v));                    // not a plausible voltage
  // --- other formulas ---
  CHECK(obd::dataBytes("41423A98", 0x42, 2, b) && fabsf(obd::moduleVolts(b[0], b[1]) - 15.0f) < 0.001f);
  CHECK(obd::dataBytes("412F80", 0x2F, 1, b) && fabsf(obd::percent(b[0]) - 50.2f) < 0.1f);
  CHECK(obd::dataBytes("410F3C", 0x0F, 1, b) && obd::tempC(b[0]) == 20);
  CHECK(obd::dataBytes("414652", 0x46, 1, b) && obd::tempC(b[0]) == 42);
  CHECK(obd::dataBytes("4111FF", 0x11, 1, b) && fabsf(obd::percent(b[0]) - 100.0f) < 0.01f);
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
