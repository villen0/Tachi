# Tachi plans (Mochi character, navigation, extra car-data pages)

Full working plan, latest version. See ROADMAP.md for status.

# Plan: Dasai Mochi-style animated character for Tachi

## Context
The Tachi pet (ESP32-S3, 172x320 ST7789, repo `villen0/Tachi`, `src/main.cpp` ~660 lines) currently shows a
simple static face: shapes are redrawn on mood changes and it only blinks. The user wants it to behave like a
real Dasai Mochi desk robot "exactly, with the same reactions". They supplied a 2m42s screen recording of
the real device cycling through its built-in animations (captions in the video name each one). I extracted
frames and catalogued all of them. This plan recreates that look and behaviour from scratch (own drawing code,
own logo wording: no copying of Dasai's artwork or name) and ties the reactions to car data.

Decisions made with the user:
- **Placement:** replaces the Face page. MPH / RPM / Coolant / Oil / Debug pages stay.
- **Specials wanted:** Revs gauge, Turbo / Headlights / Police eyes, Pong. (Music is skipped: no microphone on the board. The Dasai! logo splash is not requested; only a simple "Tachi" boot title.)
- **BOOT button:** short press = next page, double press = random reaction ("touch"), long press = demo toggle (unchanged).
- **Behaviour:** mix of car-driven reactions and random idle pet behaviour.

## What the reference device looks like (from the video)
- Black screen, two tall rounded-rectangle eyes in glowing cool white/blue, a tiny smile "u"-shaped mouth between/below. Small on the screen like a face inside a helmet.
- Expressions are the eye shapes morphing smoothly, not pictures swapping.
- Catalogue seen: Power On/Title ("もち" wordmark), Default, Shrink, Look Right/Left/Down, Raspberry (`> <` eyes + tongue), Revs (tachometer), Music (spectrum), Turbo (angry eyes -> turbine-fan eyes), Love (pink hearts), Angry (scribbled storm face), Crying, Stars (starry bg + eyes), Smirk (angled lids), Sleepy (closed lines, "z"), Sneeze, Headlights (beam shapes), Glitch (orange static, jagged grin), Laugh (`^ ^` + open mouth), Squint, Police (red/blue siren eyes), Distracted (eyes wander), UwU, Cookie, Pong, Upside Down, Dasai! logo, Rainbow (colour-cycling eyes).

## Approach (recommended)
**Drawn, parametric animation, not video.** Each eye is described by a few numbers (width, height, corner radius,
position, tilt/lid angles, colour) plus an optional overlay (heart, tear, star, sweat drop, tongue, zzz...). Moving
between expressions = easing those numbers over ~150-300 ms, so everything morphs smoothly like the real thing.
Uses almost no flash. The new 16 GB SD card is not needed for this (kept free for logging; could hold extra
data or clips later).

Render to an off-screen **canvas** (`Arduino_Canvas`, in GFX Library 1.6.9, already a dependency; supports
`fillRoundRect`, `fillCircle`, `fillTriangle`, `fillEllipse`, `fillArc`, `drawArc`) and push it to the panel in one
transfer per frame. Canvas is only the face region (about 172 x 160 px, ~55 KB, fits internal RAM; PSRAM is not
required) so a flush takes roughly 6-11 ms: 30 fps is realistic. This also removes the flicker seen today.

**Decouple rendering from Bluetooth polling.** Today `uiTick()` runs inside the OBD wait loops. Move the character
to its own FreeRTOS task with a small shared-state struct (mood inputs, speed/RPM/coolant, trigger events) so
animation never delays OBD requests and vice versa. The Debug page gets an `fps` figure next to `req/s` so any
regression in the 5-6 cycles/s polling rate is visible immediately.

## Work breakdown

### Phase 0: look-and-feel preview on this machine (no hardware)
1. Write the expression engine so its drawing calls use only the Arduino_GFX method names listed above.
2. Build a small host-side preview tool (outside the firmware, e.g. `tools/preview/`) that implements those
   methods on a plain pixel buffer and writes PNG contact sheets of every expression and a few transition frames.
3. Show the sheets to the user; adjust shapes/colours/timing until it looks right **before touching the board**.

### Phase 1: character engine + idle personality
- New files `src/mochi.h` / `src/mochi.cpp`: eye/mouth model, easing (ease-out/spring), blink, look-around,
  overlays, expression table, idle scheduler (random pick every ~8-20 s from: look L/R/down, smirk, squint,
  distracted, UwU, cookie, sneeze, raspberry, yawn; long idle -> sleepy).
- `main.cpp`: replace `drawEyes()` / `drawMouthFor()` and the `PAGE_FACE` branch of `uiTick()` with calls into
  `mochi`; keep the old face behind a compile flag (`MOCHI_FACE` in `config.h`) as a fallback.
- Reuse: `computeMood()` (health priority: sleepy > worried > uneasy stays authoritative), `fresh()`, `setLedMood()`,
  `drawDataPage`, `handleButton`/ISR (`main.cpp`), existing demo mode (extend `demoStep()` so every reaction can be
  seen at a desk).

### Phase 2: car-driven reactions
| Car event (existing signals) | Reaction |
|---|---|
| Boot | "Tachi" wordmark title, then Default |
| Data returns after sleepy / engine starts | Wake-up (Power On style) |
| Idle, parked | random idle pool above; Pong, Headlights, Police, Glitch only here |
| Moving from standstill | Headlights (brief) |
| Hard acceleration or RPM >= 3500 | Turbo eyes (replaces current "excited") |
| Highway cruise (>=55 mph, low RPM) | Squint / UwU (replaces current "calm") |
| Hard braking (speed dropping fast) | Stars / wide-eye startle |
| Coolant uneasy (>=212 F) | Crying/sweat-drop eyes (yellow LED) |
| Coolant worried (>=226 F) | Angry eyes (red LED) |
| No data 5 s | Sleepy (existing rule) |

Safety rule for the design: while the car is moving, only gentle animations (no flashing red/blue Police lights,
no Glitch, no Pong), so it never distracts the driver or resembles emergency lights. Thresholds go in `config.h`
like the existing ones.

### Phase 3: special screens
- **Revs gauge:** the RPM page becomes a tachometer (arc ticks, needle driven by real RPM, number in the middle),
  using `drawArc` / `fillTriangle`; option to keep the plain number via a `config.h` flag.
- **Pong:** parked easter egg, auto-playing two-paddle Pong drawn on the canvas, exits when the car moves.
- Turbo / Headlights / Police are expression variants from Phase 1's table, triggered as above.

### Phase 4: button gestures
- Extend the existing ISR/`handleButton()` to detect a double press (~300 ms window).
- On the Face page a single press waits ~300 ms to see if a second tap follows (second tap = random reaction,
  otherwise next page). On every other page a single press still changes page immediately (no added delay).
- Long press still toggles demo mode.

## Critical files
- `src/main.cpp`: remove old face drawing, call the new module, extend button handling, FreeRTOS task setup.
- `src/mochi.cpp`, `src/mochi.h` (new): engine, expressions, idle scheduler, specials.
- `include/config.h`: `MOCHI_FACE`, frame rate, reaction thresholds, double-press window.
- `README.md`: new behaviour, reaction table, button gestures.
- `tools/preview/` (new, host-only): PNG preview harness; not part of the PlatformIO build.

## Verification
1. **Host preview:** contact sheets of every expression and transitions reviewed with the user (Phase 0).
2. **Build:** `pio run` must pass (done in a scratch copy because the sandbox blocks PlatformIO's registry; GitHub-hosted libs work, as before).
3. **Desk test on the board in demo mode** (user): every expression and the Revs gauge appear; BOOT short/double/long behave as above; check the Debug page `fps` (target >= 20) and `req/s` (must stay about 5-6).
4. **Car test** (user): reactions follow driving; photo of the Debug page to confirm polling rate did not drop; confirm no UI stalls.
5. **Rollback:** set `MOCHI_FACE 0` to return to the previous face.

## Risks / open points
- Frame rate and BLE polling share the CPU: mitigated by the separate render task and the dirty-region canvas;
  measured via `fps` + `req/s` on the Debug page. Fallback: lower fps (e.g. 20) or the old face.
- The earlier unexplained slowdown (needed a reconnect) is not understood; the timing line on the Debug page will help diagnose if it recurs.
- Pins/ST7789 offsets remain as verified by the photos (display works).
- "Exactly" the same: this is a faithful recreation of the style and reaction set, not a copy of Dasai's artwork; the Japanese "もち" title needs a CJK font, so the boot title uses "Tachi" in the default font instead.
- Can't play the video format of the original; I worked from still frames only, so motion/timing details (how fast the eyes squash, transitions) will be tuned from the user's feedback on the previews.

---

# Feature B (separate track): Navigation on Tachi, "Android Auto-like", phone -> board

## Context
User asked for something "like Android Auto" and picked **navigation** as the priority. A real Android Auto head
unit is not possible on this board (proprietary protocol, car-side host). The realistic version: a small
**Android companion app** reads Google Maps' turn-by-turn notification on the user's Samsung phone and sends the
next turn to Tachi over Bluetooth LE; Tachi shows a **Nav page** (arrow, distance, street, ETA) in the page cycle.

## Decisions made with the user
- **Install route:** APK built by a GitHub Actions workflow in `villen0/Tachi`, downloaded and sideloaded on the Samsung phone (no Android Studio).
- **Order:** Mochi character first (Phase 0 preview sheets), navigation after.
- **Nav page content (v1):** turn arrow + distance only. Street name, ETA and speed are optional later additions (kept in Phase 4).

## Research findings (read-only)
- Google Maps' navigation notification uses a custom view (RemoteViews), so plain notification text is not
  enough (b4x forum threads 99433 / 83898). Parsing it is possible: `github.com/3v1n0/GMapsParser` is a Kotlin
  library that parses these notifications into structured data (updates via callbacks), and
  `github.com/appleshaman/CarPlayBLE` is an Android app + ESP32 (BLE GATT server) project that already shows Maps
  navigation info on a small ESP32 display. Fields/licences NOT yet checked: read both before choosing to reuse code vs
  write our own parser (check the licence; do not copy code that is incompatible).
- The Hackster "read phone notifications using ESP" project confirms the pattern: Android foreground service
  keeps a BLE link alive and writes notification data to a writable characteristic on the ESP32.
- Sandbox: Java 21 + Gradle 8.14 exist, but there is no Android SDK (no `sdkmanager`), so the APK cannot be built
  or run here. Plan: **build the APK with GitHub Actions** in `villen0/Tachi` (workflow file in the repo); the
  user downloads the APK artifact on the phone and installs it (allow "install unknown apps"). No Android Studio needed.
  Workflow runs/outcome verified via the GitHub MCP tools (`actions_run_trigger`, `actions_get`, `get_job_logs`).

## Design
**Board (firmware, `src/nav.cpp`, new; hooks in `main.cpp`):**
- Add a BLE **GATT server** (NimBLE, same stack already used as central for the OBD adapter) advertising as
  "Tachi". One write characteristic `NAV` (compact fixed-size packet: maneuver code, distance in metres, street
  name <= 24 chars, ETA minutes, flags) and one notify characteristic for status/ack. NimBLE supports multiple roles; config
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` must allow >= 2 (adapter + phone).
- New page **Nav** in the cycle (`Face > MPH > RPM > Coolant > Oil > Nav > Debug`): big arrow drawn with polygons
  (left/right/slight/sharp/straight/U-turn/roundabout/arrive; no image assets), distance in feet/miles (units
  consistent with mph), street name, ETA. When no navigation is active it shows "No route" and the page is skipped
  automatically (so page cycling is unchanged when you are not navigating).
- Nav simulator in demo mode (fake turns) so the page can be checked at a desk without the phone.
- Reuse: page enum / `pageInfo`-style dispatch and `drawnPage` redraw logic in `main.cpp`, `handleButton()` page
  cycling, `tempOut`-style unit helper pattern for mph/feet, Debug page for link/status lines.

**Phone (Android app, Kotlin, new folder `android/`):**
- `NotificationListenerService` (user grants Notification access) filtered to Google Maps' package, parsing
  direction, distance, street, ETA (approach from GMapsParser / CarPlayBLE after licence check).
- Foreground service holding a BLE central connection to Tachi (scan by name "Tachi"), reconnect on drop,
  Android 12+ `BLUETOOTH_CONNECT/SCAN` permissions, ask user to exempt the app from battery optimisation (Samsung kills background apps).
- Sends a packet only when the instruction changes or distance changes enough (<= 1 Hz) to keep BLE load small.
- Minimal UI: permission setup, connection status, "send test turn" button.

## Phases
1. **Board side first, no phone needed:** GATT server + Nav page + demo simulator; test with the free nRF Connect
   app writing hand-made packets to the NAV characteristic (confirm arrow/distance render and page skipping).
2. **Coexistence check:** with the phone connected (nRF Connect) confirm Debug-page `req/s` still ~5-6 and `fps`
   unaffected. If it drops: lower nav update rate / connection interval, or disconnect nav while the OBD link is busy.
3. **Android app + CI:** write the app and a GitHub Actions workflow that builds a debug APK; user installs it,
   grants permissions, starts Google Maps navigation; iterate from photos / screenshots of the Nav page.
4. Polish: roundabout exit number, lane hint, night-friendly colours, optional ETA/arrival message.

## Risks / open points
- Maps notification format is not an official API and can change with Maps updates; parser may need updates. Mitigate by a
  "raw text" fallback mode (show whatever text was found).
- Samsung battery management may kill the foreground service; mitigations above, plus documentation.
- Two BLE connections + advertising on one radio could slow OBD polling (earlier slowdown cause is still not understood) -> Phase 2 measurement is a gate.
- Cannot build/test the APK in this sandbox: relies on CI build + user testing on the phone, so expect a few iteration rounds.
- Safety/UX: keep the Nav page glanceable (big arrow + distance); it is an extra page, not an automatic takeover.
- Scope: navigation only for now; music and call/message alerts are possible later through the same app and link (new characteristics / pages).

## Suggested order across both tracks
Mochi Phase 0 (preview) -> Mochi Phase 1 -> Nav Phase 1 (board side) -> Nav Phase 2 gate -> Nav Phase 3 (app + CI) -> remaining Mochi phases. User can reorder.

---

# PROJECT TRACKER (single place for everything; keep updated each session)
Last updated after Phase 1 was pushed (commit 03f6dfc) and the user confirmed "demo mode: everything working fine".
Mirror this into the repo as `docs/ROADMAP.md` when plan mode ends (the repo copy of `docs/mochi-plan.md` is outdated).

## DONE (pushed to villen0/Tachi main)
- OBD over BLE (CodeReader31B): speed, RPM, coolant, oil attempt; back-to-back polling ~5-6 cycles/s; stale values shown as `--`.
- Pages: Face, MPH, RPM, Coolant, Oil temp, Debug; BOOT short press cycles (interrupt-driven), long press = demo mode.
- Temperatures in deg F (`TEMP_UNIT_F`), mood thresholds in deg C; microSD CSV logging (one file per boot).
- Debug page: link state, ages, cmd/write/gap timing, raw replies, face fps / frame ms / car req/s.
- Mochi character Phase 0 (preview tool + contact sheets in `docs/preview/`) and Phase 1 (animated face on its own UI task,
  welcome animation, idle personality, mood->expression mapping, demo showcase, `MOCHI_FACE 0` rollback).
- User verified on hardware: welcome animation, expressions, demo mode all work.

## WAITING ON USER (test in the car)
- Phase 1 in the car: leave the Face page open ~1 min, photograph Debug page: `face` fps (target >= 20) and `req:` (must stay ~5-6).
- Check moods against real driving (sleepy -> wake, sweat/angry at heat, turbo at high RPM, squint at highway).
- Confirm no blank/noisy screen at 40 MHz SPI (`LCD_SPI_HZ`); lower if so.

## APPROVED, NOT STARTED (Mochi)
- Phase 2: car-driven reactions (hard braking -> startle/stars, pull-away -> headlights, speed >= threshold -> gentle warning, etc.; no flashing lights while moving).
- Phase 3: Revs gauge on the RPM page (needle = real RPM) and parked Pong easter egg.
- Phase 4: double-press BOOT = random reaction (single press still = next page; 300 ms wait only on the Face page).

## EXTRA FEATURES MENTIONED EARLIER, NEED A DECISION (the user asked to review these before the next phase)
1. **Supported-values check ("PID discovery")**: read-only diagnostic that asks the engine computer which standard values it supports
   (0100/0120/0140/0160 bitmaps) and shows the list on the Debug page; tells us exactly which extra pages are possible. Recommended first step.
2. **More car-data pages** (each is one more page in the cycle; only those the car answers will be offered):
   - **Battery / system voltage** (adapter reading `ATRV`, plus control-module voltage `0142`): ~12.5 V engine off, ~14 V charging; useful health indicator and likely to work even when car PIDs do not.
   - Intake air temperature (`010F`), ambient air temperature (`0146`), fuel level (`012F`), throttle position (`0111`), engine load (`0104`), fuel trims (`0106/0107`), MAF (`0110`).
   - Could also feed the face (e.g. low battery -> worried, cold ambient -> shiver).
3. **Oil temperature**: car answers `NODATA` to the standard request (see Debug `oil:` line). Only Mazda-specific requests could work; unknown, guesswork. User chose to skip for now.
4. **Tire pressure**: standard OBD has none. Options: (a) cheap BLE TPMS valve-cap sensors read directly by the board (need the exact model for the decoder; page + low-pressure mood), (b) car-proprietary requests (unknown, may be warning-only). Needs a decision + sensor model.
5. **Navigation on Tachi** ("Android Auto-like"): Android companion app (reads the Google Maps navigation notification, sends turn arrow + distance over BLE) + GATT server and Nav page on the board; APK built by GitHub Actions. Full design above (Feature B). Parked until after Mochi work; user chose turn arrow + distance for v1.
6. **Music and call/message alerts** from the phone: possible later through the same app/link (new characteristics + pages). Needs Feature B first.
7. **Temperature unit toggle by button** (F/C at runtime) instead of the compile-time `TEMP_UNIT_F`.
8. **Sound**: board has no speaker/microphone; would need an added buzzer or I2S amp (Mochi "Music" spectrum screen also needs a mic; skipped).
9. **Tuning**: mood thresholds (`config.h`), idle reaction frequency, eye colours/sizes after seeing them on the real screen.
10. Tidy: update repo docs (`docs/mochi-plan.md` outdated; add `docs/ROADMAP.md`); decide whether to keep demo showcase as is.

## KNOWN ISSUES / OPEN QUESTIONS
- Earlier unexplained slowdown (1-2 req/s, 2-6 s old values) that disappeared after reconnecting; cause unknown. Debug page timing line (cmd/wr/gap/sd) and `req:` exist to catch it. Now also a risk factor for the new UI task (measure!).
- Two modules answer every PID request (duplicate replies); first reply is used; speed matched the dash in the car test.
- Combined `010C0D`, `ATCRA7E8` filter and the reply-count suffix were tried and made things worse/unsupported: kept off (`USE_*` flags).
- SD pins and BOOT/GPIO0 button are from a third-party pin map; both appear to work (SD logging showed `sd:on`).
- Sandbox limits: PlatformIO registry blocked (workaround: GitHub-hosted libs in a scratch copy), no Android SDK, cannot run the board.

## DECISIONS (latest)
- **Order now:** (1) Feature C below (battery voltage page + other car-data pages), then (2) remaining Mochi phases 2-4, then (3) navigation (Feature B).
- **Tire pressure: dropped (ignore).** Oil temp: skipped. Music/call alerts: later, after navigation.
- Phase 1 car test is still pending from the user (Debug page `face` fps and `req:`); do not treat Phase 1 as finished until seen.

---

# Feature C: battery voltage + other car-data pages (approved direction, plan below)

## Context
The Tachi pages show only speed, RPM and coolant (oil returns NO DATA). The user wants a battery voltage page and "other car-data pages".
Which standard values the car answers is unknown, so the firmware first asks the car (read-only) and offers only pages it answers.

## Values and how to read them (standard OBD-II mode 01 unless noted)
| Page | Request | Value |
|---|---|---|
| Battery | adapter command `ATRV` (replies like `12.6V`), fallback `0142` (control module voltage) | volts, 1 decimal |
| Intake air temp | `010F` | A-40 deg C -> shown in deg F |
| Outside air temp | `0146` | A-40 deg C -> deg F |
| Fuel level | `012F` | A*100/255 % |
| Throttle | `0111` | A*100/255 % |
| Engine load | `0104` | A*100/255 % |
| (optional) fuel trims `0106/0107`, MAF `0110`, fuel rate `015E` | | candidates for later |
Support is read from the car's bitmaps: requests `0100`, `0120`, `0140`, `0160` return 32-bit masks (bit set = PID supported;
first byte MSB = PID 01). A page is shown only if its PID is supported (battery: if `ATRV` answers).

## Design
- **New `src/obd_parse.h`** (pure C++, no Arduino): `parseSupportedBitmap`, `isSupported(pid)`, `parseAtrv`, value formulas, and the
  "first of duplicate replies" rule (two modules answer every request, e.g. `410D00410D00`). Reused by firmware and by a host test.
- **Host test `tools/tests/obd_parse_test.cpp`** (built with g++ here): feeds real replies seen in the user's Debug photos
  (`410C0BB3410C0BAF`, `410D00410D00`, `41057441057 4` without spaces) plus synthetic bitmap/ATRV/edge cases (NO DATA, `?`, short/garbled).
- **Discovery at connect** (after `0100`): read bitmaps, store `supported[]`, print to serial and show a compact list on the Debug page.
- **Gauge table** in `main.cpp` replacing the hard-coded pages: `{page id, title, unit, request, decode, decimals, fresh window}`.
  `pageInfo()` / `drawDataPage()` generalised to a formatted string (so `12.6` and `45` both work); colours: battery < 12.0 V engine
  off / < 13.2 V running = yellow, < 11.5 V red (thresholds in `config.h`); fuel < 15 % yellow.
- **Dynamic page list:** `visiblePages` = Face, MPH, RPM, Coolant, then every supported extra page, Oil only if it ever answered, Debug always.
  BOOT cycles through visible pages only; page dots count visible pages (`handleButton`, `drawPageDots`, `drawDataPage`, `drawDebugPage`).
  `config.h` switches (`SHOW_PAGE_BATTERY`, ...) let the user hide any page.
- **Polling:** keep speed+RPM every cycle; slow gauges round-robin every `SLOW_EVERY` cycles, but the gauge for the page currently on
  screen is polled every other cycle so throttle/load feel live; `ATRV` polled about every 5 s. A gauge that fails 5 times in a row is
  retried only every ~8 s (same pattern as oil today) and its page hides until it answers.
- **SD log:** fixed header with all columns (`ms,rpm,mph,coolant,oil,batt_v,intake,ambient,fuel_pct,throttle_pct,load_pct`), blank when unknown.
- **Mood hook (small):** low battery while running can trigger the worried/sad expression later in Mochi Phase 2 (not in this feature).

## Critical files
- `src/obd_parse.h` (new), `tools/tests/obd_parse_test.cpp` (new), `src/main.cpp` (gauge table, discovery, polling, page list, Debug lines),
  `include/config.h` (page switches, battery/fuel thresholds), `README.md`, repo tracker `docs/ROADMAP.md` (new).

## Verification
1. Host test passes (`g++ tools/tests/obd_parse_test.cpp && ./a.out`), covering the real replies from the user's photos.
2. `pio run` builds in the scratch copy (same workaround as before); `MOCHI_FACE 0` still builds.
3. User in the car: Debug page shows `sup:` list and the battery value; new pages appear only for supported values; cycling skips the hidden ones;
   `req:` stays ~5-6 with the extra polling (photo of Debug page); battery reads ~12-13 V engine off and ~13.5-14.5 V running.
4. If a value looks wrong, the raw reply is already on the Debug page ring; compare with the dash where possible (fuel level, outside temp).

## Risks
- Adapter may not support `ATRV` (clone) -> falls back to `0142`; if neither, no battery page (shown on Debug).
- More slow requests could lower the update rate of RPM/speed: mitigated by round-robin pacing; `req:` is the check.
- Some PIDs are advertised but answer NO DATA at times: handled by the retry/hide logic.
- A long page cycle: mitigated by hiding unsupported pages and the config switches.
