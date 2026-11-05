#pragma once
// Short plain-English meanings of the common generic OBD-II powertrain codes (SAE J2012), about 26 characters so they fit a round screen.
// Manufacturer specific codes (most P1xxx, P2xxx above the generic range, B, C, U) are not listed: describe() returns nullptr for them.
#include <string.h>

namespace dtc {

struct Text { const char *code; const char *text; };
static const Text TEXTS[] = {
  {"P0011", "Intake cam timing advanced"}, {"P0012", "Intake cam timing retarded"}, {"P0013", "Exhaust cam circuit"},
  {"P0014", "Exhaust cam advanced"}, {"P0016", "Crank / cam sync fault"}, {"P0017", "Crank / exhaust cam sync"},
  {"P0030", "O2 heater B1S1 circuit"}, {"P0050", "O2 heater B2S1 circuit"},
  {"P0100", "Air flow sensor circuit"}, {"P0101", "Air flow sensor range"}, {"P0102", "Air flow sensor low"}, {"P0103", "Air flow sensor high"},
  {"P0106", "MAP sensor range"}, {"P0107", "MAP sensor low"}, {"P0108", "MAP sensor high"},
  {"P0111", "Intake temp sensor range"}, {"P0112", "Intake temp sensor low"}, {"P0113", "Intake temp sensor high"},
  {"P0116", "Coolant sensor range"}, {"P0117", "Coolant sensor low"}, {"P0118", "Coolant sensor high"},
  {"P0121", "Throttle sensor range"}, {"P0122", "Throttle sensor low"}, {"P0123", "Throttle sensor high"},
  {"P0125", "Closed loop too slow"}, {"P0128", "Thermostat: too cool"},
  {"P0130", "O2 sensor B1S1 circuit"}, {"P0131", "O2 sensor B1S1 low"}, {"P0132", "O2 sensor B1S1 high"},
  {"P0133", "O2 sensor B1S1 slow"}, {"P0134", "O2 sensor B1S1 no signal"}, {"P0135", "O2 heater B1S1 fault"},
  {"P0137", "O2 sensor B1S2 low"}, {"P0138", "O2 sensor B1S2 high"}, {"P0141", "O2 heater B1S2 fault"},
  {"P0171", "Mixture too lean B1"}, {"P0172", "Mixture too rich B1"},
  {"P0174", "Mixture too lean B2"}, {"P0175", "Mixture too rich B2"},
  {"P0190", "Fuel rail pressure sensor"}, {"P0191", "Fuel rail pressure range"}, {"P0192", "Fuel rail pressure low"}, {"P0193", "Fuel rail pressure high"},
  {"P0200", "Injector circuit"}, {"P0201", "Injector cyl 1 circuit"}, {"P0202", "Injector cyl 2 circuit"},
  {"P0203", "Injector cyl 3 circuit"}, {"P0204", "Injector cyl 4 circuit"},
  {"P0217", "Engine overtemperature"}, {"P0230", "Fuel pump primary circuit"}, {"P0234", "Turbo overboost"}, {"P0299", "Turbo underboost"},
  {"P0300", "Random misfire detected"}, {"P0301", "Misfire cylinder 1"}, {"P0302", "Misfire cylinder 2"}, {"P0303", "Misfire cylinder 3"},
  {"P0304", "Misfire cylinder 4"}, {"P0305", "Misfire cylinder 5"}, {"P0306", "Misfire cylinder 6"},
  {"P0315", "Crank position variation"}, {"P0325", "Knock sensor 1 circuit"}, {"P0327", "Knock sensor 1 low"}, {"P0328", "Knock sensor 1 high"},
  {"P0335", "Crank sensor circuit"}, {"P0336", "Crank sensor range"}, {"P0340", "Cam sensor circuit"}, {"P0341", "Cam sensor range"},
  {"P0351", "Ignition coil A circuit"}, {"P0352", "Ignition coil B circuit"}, {"P0353", "Ignition coil C circuit"}, {"P0354", "Ignition coil D circuit"},
  {"P0401", "EGR flow too low"}, {"P0402", "EGR flow too high"}, {"P0403", "EGR circuit"},
  {"P0420", "Catalyst weak B1"}, {"P0430", "Catalyst weak B2"},
  {"P0440", "EVAP system fault"}, {"P0441", "EVAP purge flow wrong"}, {"P0442", "EVAP small leak"}, {"P0443", "EVAP purge valve circuit"},
  {"P0446", "EVAP vent circuit"}, {"P0449", "EVAP vent valve circuit"}, {"P0451", "EVAP pressure range"},
  {"P0452", "EVAP pressure low"}, {"P0453", "EVAP pressure high"}, {"P0455", "EVAP large leak"},
  {"P0456", "EVAP very small leak"}, {"P0457", "Fuel cap loose / EVAP leak"},
  {"P0461", "Fuel level sensor range"}, {"P0462", "Fuel level sensor low"}, {"P0463", "Fuel level sensor high"},
  {"P0480", "Cooling fan 1 control"}, {"P0481", "Cooling fan 2 control"},
  {"P0500", "Vehicle speed sensor"}, {"P0501", "Vehicle speed range"}, {"P0505", "Idle control system"},
  {"P0506", "Idle speed lower than set"}, {"P0507", "Idle speed higher than set"},
  {"P0520", "Oil pressure sensor circuit"}, {"P0524", "Oil pressure too low"},
  {"P0562", "System voltage low"}, {"P0563", "System voltage high"}, {"P0571", "Brake switch circuit"},
  {"P0600", "Serial comm link"}, {"P0601", "Engine computer memory"}, {"P0606", "Engine computer processor"},
  {"P0620", "Generator control circuit"}, {"P0627", "Fuel pump control open"}, {"P0641", "Sensor ref voltage A"},
  {"P0700", "Transmission fault"}, {"P0715", "Input speed sensor"}, {"P0720", "Output speed sensor"},
  {"P0730", "Wrong gear ratio"}, {"P0741", "Torque converter clutch"}, {"P0750", "Shift solenoid A"}, {"P0755", "Shift solenoid B"},
  {"P2096", "Post-cat mixture lean B1"}, {"P2097", "Post-cat mixture rich B1"},
  {"U0100", "Lost comm: engine computer"}, {"U0101", "Lost comm: transmission"}, {"U0121", "Lost comm: ABS"},
};

// nullptr when the code is not in the table (the screen then says to check a service manual).
inline const char *describe(const char *code) {
  for (const Text &t : TEXTS) if (strncmp(t.code, code, 5) == 0) return t.text;
  return nullptr;
}

}  // namespace dtc
