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
