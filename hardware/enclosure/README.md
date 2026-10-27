# Enclosure for the round 2.8C board

`round28_case.scad` is a parametric two-part case (front bezel + back shell, four M3 screws) for the Waveshare ESP32-S3-Touch-LCD-2.8C.
Open it in OpenSCAD (free, openscad.org), or export from the command line:

    openscad -D 'part="front"'      -o front.stl      round28_case.scad   # prints face down
    openscad -D 'part="back_print"' -o back.stl       round28_case.scad   # prints with the back wall on the bed
    openscad -D 'part="fit_test"'   -o fit_test.stl   round28_case.scad   # 6 mm test ring: print this first
    openscad -D 'part="assembly"'   -o assembly.stl   round28_case.scad   # preview only

Size as designed: 101.6 mm across (108.6 mm with the screw lugs), 16.9 mm thick. Fits the A1 mini bed (180 mm) easily.

## Before printing the real thing
1. Print `fit_test` and check the glass disc drops in (95.86 mm plus 0.45 mm clearance per side; change `fit_gap` if too tight or loose).
2. Measure with calipers and update `disc_h` (glass thickness) and `pcb_h` (back of the disc to the tallest part). Both are estimates.
3. Check the port cut-outs (`ports` list: angle, width, height, centre height) against the real board. Positions were estimated from product photos.

## Print settings
PETG (PLA softens in a hot car), 0.2 mm layers, 3 walls, 20 % infill, no supports. Screws: M3 x 10, self-tapping into the front lugs.
Add a 1 mm strip of foam tape behind the board so it cannot rattle.

Not included yet: dash stand or vent clip, vent slots.
