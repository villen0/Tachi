// Tachi enclosure for the Waveshare ESP32-S3-Touch-LCD-2.8C (round 480x480 touch display).
// Two parts joined by four M3 screws through side lugs, with a lap joint: a FRONT bezel (display window) and a BACK shell
// (cavity for the circuit board, cut-outs for USB-C ports, buttons and the TF slot).
//
// SIZES MARKED "VERIFY" ARE ESTIMATES. The disc and display sizes come from Waveshare's drawing (95.86 mm and 70.13 mm).
// Board thickness and port positions were estimated from product photos: measure the real board with calipers and
// print the "fit_test" part first, then adjust the parameters below.
//
// Which part to render:  openscad -D 'part="front"' -o front.stl round28_case.scad
//   part = "front" | "back" | "fit_test" | "assembly" (preview, back and front pulled apart)
// "front" prints face down, "back_print" prints with the back wall on the bed. Material: PETG (a hot dashboard softens PLA).

part = "assembly";

// ---- board (VERIFY with calipers) ----
disc_d    = 95.86;   // glass disc diameter (Waveshare drawing)
active_d  = 70.13;   // visible display diameter (Waveshare drawing)
disc_h    = 3.6;     // VERIFY: thickness of the glass + LCD disc
pcb_h     = 8.0;     // VERIFY: height from the back of the disc to the tallest part on the back (buzzer, TF slot, connectors)
fit_gap   = 0.45;    // clearance around the disc (radius); 0.3 is snug, 0.6 is loose

// ---- case ----
wall      = 2.4;     // side wall
face_t    = 2.0;     // front plate thickness
back_t    = 2.4;     // back plate thickness
foam_gap  = 0.6;     // spare room behind the parts; fill with 1 mm foam tape so the board cannot rattle
win_margin = 1.2;    // window is this much bigger (radius) than the visible display
lap       = 2.0;     // depth of the overlap joint between front and back
lug_d     = 9;       // screw lug diameter
lug_angles = [20, 160, 200, 340];   // degrees clockwise from the top, kept away from the port cut-outs
screw_clear = 3.4;   // M3 clearance hole (back half)
screw_pilot = 2.6;   // M3 self-tapping pilot hole (front half)
screw_head_d = 6.2;  // screw head pocket
screw_head_h = 2.6;

// Port cut-outs in the side wall: [angle clockwise from top (deg), width (mm), height (mm), centre height behind the disc (mm), label]
// VERIFY: angles are estimated from the Waveshare back-view photo (two USB-C at the lower sides, TF slot upper right,
// BOOT/RST/power switch on the left, I2C and UART headers on the right).
ports = [
  [225, 11,  6.0, 4.4, "USB-C (USB)"],
  [135, 11,  6.0, 4.4, "USB-C (UART)"],
  [ 45, 16,  3.5, 3.4, "TF card"],
  [270, 30,  7.0, 3.8, "BOOT / RST / power switch"],
  [ 90, 22,  6.0, 4.2, "I2C + UART headers"],
];

// ---- derived ----
ri      = disc_d / 2 + fit_gap;          // inner radius of the pocket
ro      = ri + wall;                     // outer radius
z_split = face_t + disc_h + 0.3;         // plane between front and back parts (z = 0 is the front face)
cav_h   = pcb_h + foam_gap;
z_end   = z_split + cav_h + back_t;
lug_r   = ro + 2.2;                      // lug centre radius
$fn = 96;

module outline2d() { union() { circle(r = ro); for (a = lug_angles) rotate(-a) translate([0, lug_r]) circle(d = lug_d, $fn = 48); } }
module lugs(z0, z1) { for (a = lug_angles) rotate(-a) translate([0, lug_r, z0]) cylinder(d = lug_d, h = z1 - z0, $fn = 48); }

module front_part() {
  difference() {
    union() {
      cylinder(r = ro, h = z_split + lap);
      lugs(0, z_split);
    }
    // pocket for the disc, then the lap step: remove the inner half of the wall above the split so the back shell can slide in
    translate([0, 0, face_t]) cylinder(r = ri, h = z_split + lap);
    translate([0, 0, z_split]) cylinder(r = ri + wall / 2 + 0.2, h = lap + 0.1);
    // display window with a 0.8 mm chamfer on the visible side
    translate([0, 0, -0.01]) cylinder(r = active_d / 2 + win_margin, h = face_t + 0.02);
    translate([0, 0, -0.01]) cylinder(r1 = active_d / 2 + win_margin + 0.8, r2 = active_d / 2 + win_margin, h = 0.81);
    // screw pilot holes
    for (a = lug_angles) rotate(-a) translate([0, lug_r, 1.5]) cylinder(d = screw_pilot, h = z_split, $fn = 24);
  }
}

module back_part() {
  difference() {
    union() {
      translate([0, 0, z_split]) cylinder(r = ro, h = cav_h + back_t);
      lugs(z_split, z_end);
    }
    // cavity
    translate([0, 0, z_split - 0.01]) cylinder(r = ri, h = cav_h + 0.01);
    // lap notch: remove the outer half of the wall at the joint (the front part's rim goes here)
    translate([0, 0, z_split - 0.01]) difference() { cylinder(r = ro + 5, h = lap + 0.01); cylinder(r = ri + wall / 2, h = lap + 0.01); }
    // port cut-outs
    for (p = ports) rotate(-p[0]) translate([-p[1] / 2, ri - 1.5, z_split + p[3] - p[2] / 2]) cube([p[1], wall + 3, p[2]]);
    // screw clearance holes with a head pocket on the back
    for (a = lug_angles) rotate(-a) translate([0, lug_r, z_split - 1]) {
      cylinder(d = screw_clear, h = z_end, $fn = 24);
      translate([0, 0, cav_h + back_t - screw_head_h + 1]) cylinder(d = screw_head_d, h = screw_head_h + 1, $fn = 24);
    }
  }
}

if (part == "front") front_part();
else if (part == "back") back_part();
else if (part == "back_print") translate([0, 0, z_end]) rotate([180, 0, 0]) back_part();
else if (part == "fit_test") intersection() { front_part(); cylinder(r = ro + lug_d, h = face_t + disc_h + 0.5); }   // front bezel only, to check the disc fits
else { front_part(); translate([0, 0, 18]) back_part(); }
