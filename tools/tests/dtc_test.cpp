// Host test for src/dtc.h and src/dtc_text.h.   g++ -std=c++17 -Wall -I../../src dtc_test.cpp -o dtc_test && ./dtc_test
#include "dtc.h"
#include "dtc_text.h"
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
  char c[6];
  // --- decoding the two bytes ---
  dtc::decode(0x01, 0x33, c); CHECK(strcmp(c, "P0133") == 0);
  dtc::decode(0x03, 0x01, c); CHECK(strcmp(c, "P0301") == 0);
  dtc::decode(0x04, 0x20, c); CHECK(strcmp(c, "P0420") == 0);
  dtc::decode(0x13, 0x45, c); CHECK(strcmp(c, "P1345") == 0);
  dtc::decode(0x21, 0x96, c); CHECK(strcmp(c, "P2196") == 0);
  dtc::decode(0x41, 0x23, c); CHECK(strcmp(c, "C0123") == 0);
  dtc::decode(0x81, 0x23, c); CHECK(strcmp(c, "B0123") == 0);
  dtc::decode(0xC1, 0x00, c); CHECK(strcmp(c, "U0100") == 0);
  dtc::decode(0xFF, 0xFF, c); CHECK(strcmp(c, "U3FFF") == 0);
  // --- single frame with the count byte (CAN) ---
  { dtc::Result r = dtc::parseReply("43020133022200", 3); CHECK(r.answered && r.n == 2 && !strcmp(r.codes[0], "P0133") && !strcmp(r.codes[1], "P0222")); }
  { dtc::Result r = dtc::parseReply("43010301000000", 3); CHECK(r.answered && r.n == 1 && !strcmp(r.codes[0], "P0301")); }   // count 1, P0301
  // --- single frame without a count byte (older protocols) ---
  { dtc::Result r = dtc::parseReply("43013302220000", 3); CHECK(r.answered && r.n == 2 && !strcmp(r.codes[0], "P0133") && !strcmp(r.codes[1], "P0222")); }
  { dtc::Result r = dtc::parseReply("43030100000000", 3); CHECK(r.answered && r.n == 1 && !strcmp(r.codes[0], "P0301")); }   // P0301 then padding, no count
  // --- no codes ---
  { dtc::Result r = dtc::parseReply("43000000000000", 3); CHECK(r.answered && r.n == 0); }
  { dtc::Result r = dtc::parseReply("4300", 3); CHECK(r.answered && r.n == 0); }
  // --- multi frame (three codes with the count) ---
  { dtc::Result r = dtc::parseReply("008|0:430301330222|1:04200000000000", 3); CHECK(r.answered && r.n == 3 && !strcmp(r.codes[2], "P0420")); }
  // --- two modules: the union, duplicates counted once ---
  { dtc::Result r = dtc::parseReply("43010133000000|4300", 3); CHECK(r.n == 1 && !strcmp(r.codes[0], "P0133")); }
  { dtc::Result r = dtc::parseReply("43010301000000|43020301042000", 3); CHECK(r.n == 2 && !strcmp(r.codes[0], "P0301") && !strcmp(r.codes[1], "P0420")); }
  // --- pending (07) and permanent (0A) use their own service ids ---
  { dtc::Result r = dtc::parseReply("47010171000000", 7); CHECK(r.answered && r.n == 1 && !strcmp(r.codes[0], "P0171")); }
  { dtc::Result r = dtc::parseReply("4A01017100000", 10); CHECK(r.answered && r.n == 1 && !strcmp(r.codes[0], "P0171")); }
  { dtc::Result r = dtc::parseReply("43010301000000", 7); CHECK(!r.answered && r.n == 0); }          // a mode 03 answer is not a mode 07 answer
  // --- nothing usable ---
  { dtc::Result r = dtc::parseReply("NODATA", 3); CHECK(!r.answered && r.n == 0); }
  { dtc::Result r = dtc::parseReply("", 3); CHECK(!r.answered && r.n == 0); }
  { dtc::Result r = dtc::parseReply("?", 3); CHECK(!r.answered && r.n == 0); }
  { dtc::Result r = dtc::parseReply("SEARCHING...|UNABLETOCONNECT", 3); CHECK(!r.answered && r.n == 0); }
  { dtc::Result r = dtc::parseReply("43ZZ", 3); CHECK(r.n == 0); }
  { dtc::Result r = dtc::parseReply("4301", 3); CHECK(r.n == 0); }                                         // cut short
  { dtc::Result r = dtc::parseReply("008|0:4303", 3); CHECK(r.n == 0); }                                   // multi frame cut short: no invented codes
  // --- more codes than the table holds are capped ---
  { char big[200] = "43"; for (int i = 0; i < 20; i++) strcat(big, "0301"); dtc::Result r = dtc::parseReply(big, 3); CHECK(r.n == 1); }   // the same code repeated is still one
  { char big[200] = "43"; for (int i = 0; i < 20; i++) { char t[5]; snprintf(t, sizeof t, "01%02X", i + 1); strcat(big, t); } dtc::Result r = dtc::parseReply(big, 3); CHECK(r.n <= dtc::MAX_CODES); }
  // --- check engine status from 0101 ---
  { dtc::Mil m = dtc::parseMil("410183070000"); CHECK(m.valid && m.on && m.count == 3); }
  { dtc::Mil m = dtc::parseMil("41010007E100"); CHECK(m.valid && !m.on && m.count == 0); }
  { dtc::Mil m = dtc::parseMil("41018100"); CHECK(!m.valid); }                                              // too short
  { dtc::Mil m = dtc::parseMil("NODATA"); CHECK(!m.valid); }
  { dtc::Mil m = dtc::parseMil("410183070000410100000000"); CHECK(m.valid && m.on && m.count == 3); }       // two modules: the first answer
  // --- descriptions ---
  CHECK(dtc::describe("P0301") && strstr(dtc::describe("P0301"), "cylinder 1"));
  CHECK(dtc::describe("P0420") && strstr(dtc::describe("P0420"), "Catalyst"));
  CHECK(dtc::describe("P1234") == nullptr);
  CHECK(dtc::describe("B0123") == nullptr);
  for (const dtc::Text &t : dtc::TEXTS) { CHECK(strlen(t.code) == 5); CHECK(strlen(t.text) <= 28); }       // all fit the screen
  // all table codes are unique
  for (const dtc::Text &a : dtc::TEXTS) { int same = 0; for (const dtc::Text &b : dtc::TEXTS) if (!strcmp(a.code, b.code)) same++; CHECK(same == 1); }
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
