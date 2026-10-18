# Tachi roadmap and tracker

(Kept in sync with the working plan; update every session.)

## Tracker (single place for everything; keep updated each session)
Last updated after Feature C (battery + extra car-data pages) was built. Phase 1 demo mode verified by the user; car tests pending.
Order agreed: Feature C -> Mochi Phases 2-4 -> navigation. Tire pressure dropped, oil temp skipped.

## DONE (pushed to villen0/Tachi main)
- OBD over BLE (CodeReader31B): speed, RPM, coolant, oil attempt; back-to-back polling ~5-6 cycles/s; stale values shown as `--`.
- Pages: Face, MPH, RPM, Coolant, Oil temp, Debug; BOOT short press cycles (interrupt-driven), long press = demo mode.
- Temperatures in deg F (`TEMP_UNIT_F`), mood thresholds in deg C; microSD CSV logging (one file per boot).
- Debug page: link state, ages, cmd/write/gap timing, raw replies, face fps / frame ms / car req/s.
- Mochi character Phase 0 (preview tool + contact sheets in `docs/preview/`) and Phase 1 (animated face on its own UI task,
  welcome animation, idle personality, mood->expression mapping, demo showcase, `MOCHI_FACE 0` rollback).
- User verified on hardware: welcome animation, expressions, demo mode all work.

## BUILT, WAITING FOR THE CAR TEST: Feature C (extra car-data pages)
- `src/obd_parse.h` + host test `tools/tests/obd_parse_test.cpp` (42 checks pass, includes real replies from the Debug photos).
- At connect the firmware reads the car's supported-PID bitmaps, shows them on the Debug page (`sup:` lines) and offers pages only for supported values:
  battery (ATRV, fallback 0142), intake air, outside air, fuel, throttle, engine load. Oil page only shows if the car ever answers.
- Pages cycle through visible pages only; page dots count visible pages; `SHOW_PAGE_*` switches in `config.h`.
- Background polling: coolant every other background tick, other gauges in turn, the gauge on screen every other cycle.
- SD log now has all columns. Demo mode simulates every gauge.
- To check in the car: Debug `sup:` list, battery ~12-13 V off / ~13.5-14.5 V running, `req:` still ~5-6, new pages appear, values plausible vs dash.

## BUILT: welcome = headlights drive-in (replaces the gauge-sweep welcome; waiting for desk/car check)
- Full (~5.4 s): lamps appear, car drives in and brakes, lamps flash into the eyes, arches + hand-drawn glowing TACHI writing on, back to the normal face; LED follows the headlights.
- Short (~0.9 s) after quick restarts (reset reason != power-on, or the RAM cookie survived). `INTRO_FORCE` 0/1/2 in `config.h`. Debug page shows `boot:`.
- Gauge-sweep welcome removed; `drawTach()` + digit glyphs kept in `mochi.h` for the Revs gauge page (Mochi Phase 3).
- Previews rendered by the real engine: `docs/preview/welcome_drivein_*.png`.
- To check on the board: long-press (demo) replays the full welcome; frame rate on the Debug page during the welcome (heaviest scene, target >= 20 fps).
- Flashing from the PC counts as a restart (short welcome); use `INTRO_FORCE 1` to always see the full one.

## WAITING ON USER (test in the car)
- Phase 1 in the car: leave the Face page open ~1 min, photograph Debug page: `face` fps (target >= 20) and `req:` (must stay ~5-6).
- Check moods against real driving (sleepy -> wake, sweat/angry at heat, turbo at high RPM, squint at highway).
- Confirm no blank/noisy screen at 40 MHz SPI (`LCD_SPI_HZ`); lower if so.

## APPROVED, NOT STARTED (Mochi)
- Phase 2: car-driven reactions (hard braking -> startle/stars, pull-away -> headlights, speed >= threshold -> gentle warning, etc.; no flashing lights while moving).
- Rally cluster page (landscape, built, waiting for the user's desk test): gear estimate, sweep, tiles; see README.
- Phase 3: Revs gauge on the RPM page (needle = real RPM) and parked Pong easter egg.
- Phase 4: double-press BOOT = random reaction (single press still = next page; 300 ms wait only on the Face page).

## EXTRA FEATURES MENTIONED EARLIER (decided: 1, 2 built; 3 skipped; 4 dropped; 5-6 after Mochi; 7-10 open)
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
