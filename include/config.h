#pragma once
// Tachi settings for the Waveshare ESP32-S3-Touch-LCD-2.8C (round 480x480 touch display). Pins and panel timings: board_round28.h.
// ===== User settings =====
#define FW_TAG "round"   // short feature tag shown with the build time on the Debug page (proves which firmware is flashed)
// Part of the BLE adapter's name (check with the nRF Connect app or the scan log).
#define ADAPTER_NAME_SUBSTR "CodeReader"   // seen in nRF Connect as "CodeReader31B"

// BLE ELM327 UUIDs, verified from a phone screenshot of the adapter.
#define SERVICE_UUID_STR "FFF0"
#define RX_UUID_STR      "FFF1"   // adapter -> board (notify)
#define TX_UUID_STR      "FFF2"   // board -> adapter (write)

// Temperature display unit: 1 = deg F, 0 = deg C. (Mood thresholds below stay in deg C.)
#define TEMP_UNIT_F 1

// Mood thresholds (coolant, deg C)
#define TEMP_UNEASY_C   100
#define TEMP_WORRIED_C  108

// Excited: revs at/above RPM_EXCITED_ON (until they drop below _OFF), or speed rising this fast (mph per second).
#define RPM_EXCITED_ON         3500
#define RPM_EXCITED_OFF        3000
#define ACCEL_EXCITED_MPH_PER_S   8
// Calm: relaxed cruising at highway speed with low revs.
#define CRUISE_MPH_ON   55
#define CRUISE_MPH_OFF  45
#define CRUISE_RPM_MAX  2600

// Demo mode: fake RPM / speed / coolant, no Bluetooth. Lets you test the face at a desk.
// DEMO_MODE_DEFAULT is the state at boot. Touch and hold the screen ~1.5 s (or hold the BOOT button) to toggle at runtime.
#define DEMO_MODE_DEFAULT  0
#define LONG_PRESS_MS      1500

// Character (Mochi-style animated eyes on the Face page, see docs/mochi-plan.md)
#define MOCHI_INTRO  1        // play the welcome animation at power-on
#define INTRO_FORCE  0        // welcome type: 0 = auto (full on fresh power-up, short after a restart), 1 = always full, 2 = always short
#define UI_FPS       30       // target frame rate of the Face page

// Pages (swipe left / right, or tap, to change page). Set 0 to hide.
#define SHOW_PAGE_CLUSTER  0   // sport cluster: RPM ring, big speed, coolant / fuel / battery chips
#define SHOW_PAGE_DIALS    1   // analog tachometer and analog speedometer
#define SHOW_PAGE_VITALS   0   // the four vitals rings (hidden, set 1 to bring it back)
#define SHOW_PAGE_BATTERY  1   // battery page: one value with a bar, same style as TEMPS (comes right after it)
#define SHOW_PAGE_CUSTOM   0   // custom boost page: the arc with three readouts (needs SHOW_PAGE_STYLES; hidden, the Arc page keeps the boost gauge)
#define SHOW_PAGE_GEAR     0   // estimated gear page (needs SHOW_PAGE_STYLES; hidden for now, set 1 to bring it back)
#define SHOW_PAGE_STYLES   1   // arc (boost), custom, quad, dual and gear pages (the two boost pages appear only if the car answers MAP)
#define SHOW_PAGE_TRIP     1   // drive report: live trip, report after the engine stops, last 5 trips kept in flash
#define SHOW_PAGE_CODES    1   // fault codes: read them, clear them with a 3 second hold (engine off only)
#define DRIVE_REPORT_AUTO_OPEN 1   // when a trip ends, open the drive report by itself
#define TRIP_END_MS        30000   // engine off or no car data this long ends the trip
#define TRIP_MIN_S         90      // shorter trips are thrown away
#define TRIP_MIN_MILES     0.2f
#define CODE_CLEAR_HOLD_MS 3000    // how long CLEAR has to be held
#define CODE_CHECK_MS      30000   // how often the check-engine status (0101) is checked while connected
#define BOOST_UNIT_PSI     1   // boost in psi (1) or bar (0)
// What the multi-value pages show. Pick from: RD_COOL RD_INTAKE RD_BATT RD_FUEL RD_LOAD RD_THROTTLE RD_AMBIENT RD_BOOST RD_TORQUE RD_OIL
#define RD_COOL 0
#define RD_INTAKE 1
#define RD_BATT 2
#define RD_FUEL 3
#define RD_LOAD 4
#define RD_THROTTLE 5
#define RD_AMBIENT 6
#define RD_BOOST 7
#define RD_TORQUE 8   // engine torque (needs the car to answer PID 62, ideally 63 too for Nm / lb-ft; otherwise shows percent of the reference torque)
#define RD_OIL 9      // engine oil temperature (PID 015C; many cars do not answer it, then it shows --)
#define CUSTOM_ROWS {RD_COOL, RD_INTAKE, RD_BATT}            // the three readouts under the boost number
#define QUAD_CELLS  {RD_TORQUE, RD_INTAKE, RD_FUEL, RD_LOAD}   // the four values of the quad page
#define DUAL_ROWS   {RD_COOL, RD_OIL}                        // the one or two values with bars (one is centred)
#define DIAL_FPS          30   // target frame rate of the dial pages
#define SWIPE_NEXT_ON_RIGHT 0  // 1 = swipe right goes to the next page, 0 = swipe left goes to the next page (like a phone)
// One-off test at first connect: listens ~45 s for the Mazda gear selector CAN message (ID 0x01F) and reports on the Debug page and Serial. Set 0 when done.
#define FACE_TIMING_LOG    0   // 1 = print the Face page / welcome frame rate on the UART port (the one with the USB-serial chip) every 2 s
#define GEAR_SNIFF_TEST    0
#define GEAR_SNIFF_SEC     45
#define RPM_REDLINE        6500   // shift lights and the red zone of the sweep

// One-off reactions of the face on the Face page (logic in src/car_events.h): engine start (sparkle), hard braking (startle), redline (glitch),
// hard launch (laugh), smooth stop (content), engine off (sleepy), engine warmed up (happy), low battery / low fuel (crying).
#define CAR_EVENTS            1
#define EVENT_ENGINE_OFF_MS   3000   // engine must have been off (RPM under 400, data fresh) this long before a start counts
#define EVENT_BRAKE_MIN_MPH   15     // hard braking: speed before the stop ...
#define EVENT_BRAKE_DROP_MPH  10     // ... and the drop within one second
#define EVENT_FUEL_LOW_PCT    7      // low fuel reaction below this percent
#define EVENT_LAUNCH_MIN_RPM  3000   // hard launch (laugh): revs needed, with speed jumping 6+ mph in a second from below 25 mph
#define EVENT_WARM_C          80     // engine warmed up (happy look, once per power-up): coolant in deg C, after a cold reading first
// The redline reaction (glitch flash) uses RPM_REDLINE below.

// Battery colours: engine off (RPM ~0) yellow below BATT_LOW_OFF_V; engine running yellow below BATT_LOW_RUN_V; red below BATT_CRIT_V
#define BATT_LOW_OFF_V   12.0f
#define BATT_LOW_RUN_V   13.2f
#define BATT_CRIT_V      11.5f
#define FUEL_LOW_PCT     15
#define GAUGE_FRESH_MS   40000   // an extra gauge older than this is shown as --

// Timing
#define SLOW_EVERY         4      // one background poll (coolant / oil / extra gauges) every N speed+RPM polls
#define STALE_AFTER_MS     5000   // no valid data this long -> sleepy
#define SCREEN_OFF_AFTER_MS 120000 // no car data this long (engine off) -> backlight off; 0 = never turn it off. Data or a touch wakes it
#define SCREEN_WAKE_MS      30000  // a touch keeps the screen on this long when there is still no car data
#define FACE_SETTLE_MS     10000  // after the welcome the face stays default this long (no mood change, no idle reaction)
#define BOOT_SLEEP_GRACE_MS 25000 // after power-up the face stays awake this long while waiting for the first data
#define RECONNECT_WAIT_MS  3000
#define CMD_TIMEOUT_MS     1000   // wait for one OBD reply (the UI keeps animating meanwhile)
#define INIT_TIMEOUT_MS    3000   // adapter setup commands (ATZ is slow)
#define SCAN_TIME_MS       5000

// ===== Board (pins, panel timings, layout) =====
#include "board_round28.h"
