// Keychron K8-A3 display mount. Units are mm. See docs/superpowers/specs/.
part = "assembly";   // rim | clamp | arm | cradle | fit_test | assembly | clip (clamp + arm)
theta = 55;          // tilt from horizontal, assembly only
teeth = true;        // false for clearance checks
$fn = 48;

// Keyboard (measured by the user)
wall_t = 2.81;  wall_h = 16.87;  key_top_z = 41.49;
gap_w = 10.12;  gap_floor = 4.4; span = 75.29;
tab_pitch = span + gap_w;        // centre to centre of the two gaps

// Clip
tab_w = 9.3;  tab_depth = 4.0;  tab_t = 2.5;   // 9.3 in a 10.12 gap: 0.4 per side (8.5 was loose)
bridge_t = 3; plate_t = 2.5;  plate_drop = 10;  grip = 0.35;  // 0.2 was loose
rail_a = 3.5; rail_b = 5.5; rail_d = 3.0; rail_c = 0.2;  // dovetail rail on the outer plate: half widths at the plate and at the tip, height, clearance
rail_z0 = wall_h - plate_drop + 1;   // lowest end of the rail
rail_stop = 2; rail_step = 1;        // widened stop section at the bottom, and its 45 degree ramp
foot_w = 16; foot_z0 = 10.7; foot_z1 = 22; foot_y1 = 14;   // arm foot, standing on the bed when printed
clamp_gap = 2.4;                 // outer plate face to tab face; wall is 2.81, so it pinches 0.41
hinge_y = 20; hinge_z = 45;      // hinge axis, assembled frame

// Hinge
boss_w = 10;  fin_t = 6;
teeth_n = 72; tooth_h = 0.7;  r_out = 15;  r_in = 8;   // 72 teeth = 5 degrees
hinge_gap = tooth_h + 0.25;      // ear face to boss face: one tooth height plus clearance
bolt_d = 3.4; nut_af = 5.5;   nut_h = 2.6; head_d = 6.2; head_h = 3.2;

// Cradle frame: X along the long edge (to the user's right), Y up the screen, Z toward the user.
// The screen faces +Z. The wall slot is on the long wall at Y = +31.5.
// This cradle reproduces mount/v1_printed/Cradlev1.stl, measured in the STL's own frame and
// converted: X = y_stl - 7.5, Y = x_stl + 2.5, Z = 5 - z_stl (a proper rotation plus a shift).
rim_w = 95; rim_h = 63; rim_zb = -7.4; rim_zf = 5;   // rim size, back face and front face
axis_ly = -16.5; axis_lz = -26.074;  // hinge axis: along X, 16.5 mm from the centre toward the bottom edge, 18.7 mm behind the back face
cb_t = 2; bridge_hx = 11.95;     // thin plate on the back face, over the full rim height: 2 mm thick, half width along X
// Fin profile in the (Y, Z) plane: the disc, a tangent line down to p1, and a tangent line down to the gusset peak p2 then a 45 degree slope to p3.
slot_x0 = -1.0; slot_w = 17.6; slot_h = 3.8; rim_wall = 2.4;   // the wall slot, see cradle()
p1 = [-13.92, -9.4];  p2 = [3.24, -15.24];  p3 = [9.08, -9.4];  g1 = [-2.6, -9.4];   // g1: the gusset's other foot, p2 is its peak

assert(axis_lz + r_out < rim_zb - 3, "hinge disc too close to the back of the rim");
assert(clamp_gap < wall_t, "clamp slot must be narrower than the wall to pinch it");
assert(tab_depth < gap_floor, "tab deeper than the gap floor");

// The reference rim is portrait. This turns it to landscape in the cradle frame, the way Cradlev1.stl has it:
// raw (x, y, z) -> (-y, x, z - 2.4). It is a proper rotation (det = +1). The raw +z face is the front (+Z),
// the raw -z face is the back, where the bridge and yoke are. The rim is symmetric in x and y.
module rim() { multmatrix([[0, 1, 0, 0], [-1, 0, 0, 0], [0, 0, 1, -2.4], [0, 0, 0, 1]]) import("ref/rim.stl"); }

half_len = tab_pitch / 2 + tab_w / 2;
z_top = wall_h + bridge_t;

module clip_core() {
  // bridge over the wall top, only over the wall footprint
  translate([-half_len, -clamp_gap, wall_h]) cube([2 * half_len, clamp_gap + plate_t, bridge_t]);
  // outer plate down the back of the wall
  translate([-half_len, 0, wall_h - plate_drop]) cube([2 * half_len, plate_t, plate_drop + bridge_t]);
  // grip bump: 0.2 mm into the wall, on purpose
  translate([-half_len + 4, -grip, wall_h - plate_drop + 1]) cube([2 * half_len - 8, grip, 2]);
  // tabs in the two keycap gaps
  for (s = [-1, 1])
    translate([s * tab_pitch / 2 - tab_w / 2, -clamp_gap - tab_t, wall_h - tab_depth])
      cube([tab_w, tab_t, tab_depth + bridge_t]);
}

module fit_test() {
  difference() {
    clip_core();
    // 4 mm hole in the outer plate, to hang a test weight
    translate([0, -1, wall_h - plate_drop / 2]) rotate([-90, 0, 0]) cylinder(d = 4, h = plate_t + 2);
  }
}

// One ratchet tooth along +x (radial), base on z = 0, apex line at z = h.
module tooth(r0, r1, pitch_deg, h) {
  w0 = r0 * pitch_deg * PI / 180;  w1 = r1 * pitch_deg * PI / 180;
  hull() {
    linear_extrude(0.01) polygon([[r0, -w0 / 2], [r1, -w1 / 2], [r1, w1 / 2], [r0, w0 / 2]]);
    translate([r0, -0.005, h - 0.01]) cube([r1 - r0, 0.01, 0.01]);
  }
}

// A ring of teeth on the plane x = 0. dir = +1 points the teeth toward +x.
// phase_deg turns the pattern. The axis of the ring is the x axis through the origin.
module tooth_face(dir = 1, phase_deg = 0) {
  if (teeth)
    rotate([0, dir > 0 ? 90 : -90, 0])
      for (i = [0 : teeth_n - 1])
        rotate([0, 0, phase_deg + i * 360 / teeth_n]) tooth(r_in, r_out, 360 / teeth_n, tooth_h);
}

// Dovetail rail on the outer plate. It ends flush with the bridge top so the clamp
// prints bridge-down with no supports. The bottom stop widens on a 45 degree ramp.
module dovetail(a, b) { polygon([[-a, plate_t - 0.01], [a, plate_t - 0.01], [b, plate_t + rail_d], [-b, plate_t + rail_d]]); }
module rail() {
  zr = z_top;   // top end of the rail
  translate([0, 0, rail_z0 + rail_stop + rail_step]) linear_extrude(zr - (rail_z0 + rail_stop + rail_step)) dovetail(rail_a, rail_b);
  hull() {
    translate([0, 0, rail_z0 + rail_stop + rail_step - 0.01]) linear_extrude(0.01) dovetail(rail_a, rail_b);
    translate([0, 0, rail_z0 + rail_stop]) linear_extrude(0.01) dovetail(rail_a + rail_step, rail_b + rail_step);
  }
  translate([0, 0, rail_z0]) linear_extrude(rail_stop) dovetail(rail_a + rail_step, rail_b + rail_step);
}

module clamp() { clip_core(); rail(); }

// Half width of the arm's channel at depth y: the rail plus clearance.
function hw(y) = rail_a + (rail_b - rail_a) * (y - plate_t) / rail_d + rail_c;

// Everything above a 45 degree ramp that starts at the far top corner of the foot.
// It keeps the far side of the arm printable without supports.
module above_ramp() {
  k = foot_z1 - foot_y1;   // ramp: z = y + k
  rotate([90, 0, 90]) linear_extrude(60, center = true)
    polygon([[-40, -40 + k], [100, 100 + k], [100, 400], [-40, 400]]);
}

module arm() {
  y0 = plate_t + 0.15;                 // inner face of the foot
  difference() {
    union() {
      // foot: stands on the bed
      translate([-foot_w / 2, y0, foot_z0]) cube([foot_w, foot_y1 - y0, foot_z1 - foot_z0]);
      // arm and hinge disc, cut by the 45 degree ramp on the far side
      intersection() {
        union() {
          hull() {
            translate([-boss_w / 2, y0, foot_z1 - 0.01]) cube([boss_w, foot_y1 - y0, 0.01]);   // same width as the disc, so the ears clear it
            translate([0, hinge_y, hinge_z]) rotate([0, 90, 0]) cylinder(r = r_out, h = boss_w, center = true);
          }
          translate([0, hinge_y, hinge_z]) for (s = [-1, 1]) translate([s * boss_w / 2, 0, 0]) tooth_face(s, 0);
        }
        above_ramp();
      }
    }
    // channel for the rail, open at the bottom and closed above the rail top
    translate([0, 0, foot_z0 - 1]) linear_extrude(z_top + 0.6 - (foot_z0 - 1))
      polygon([[-hw(plate_t + 0.05), plate_t + 0.05], [hw(plate_t + 0.05), plate_t + 0.05],
               [hw(plate_t + rail_d + rail_c), plate_t + rail_d + rail_c], [-hw(plate_t + rail_d + rail_c), plate_t + rail_d + rail_c]]);
    translate([0, hinge_y, hinge_z]) rotate([0, 90, 0]) cylinder(d = bolt_d, h = boss_w + 2 * tooth_h + 2, center = true);
  }
}

module clip() { clamp(); arm(); }   // assembled, for clearance checks

ear_x = boss_w / 2 + hinge_gap;     // inner face of each ear

// Fin outline in the (Y, Z) plane: the toothed disc, joined to the bridge by two straight sides.
module fin_profile() {
  hull() { translate([axis_ly, axis_lz]) circle(r = r_out); polygon([p1, p2, [p2[0], p1[1]]]); }
  polygon([p1, p3, p2]);
}

// 2D (Y, Z) shape extruded along X from x0 to x1.
module along_x(x0, x1) { translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0) children(); }

module cradle() {
  difference() {
    union() {
      rim();
      // bridge: a thin plate on the back face, over the full rim height
      translate([-bridge_hx, -rim_h / 2, rim_zb - cb_t]) cube([2 * bridge_hx, rim_h, cb_t]);
      // gusset between the fins: a 45 degree ridge on the bridge
      along_x(-bridge_hx, bridge_hx) polygon([g1, p3, p2]);
      // the two toothed fins
      for (s = [-1, 1]) along_x(s > 0 ? ear_x : -(ear_x + fin_t), s > 0 ? ear_x + fin_t : -ear_x) fin_profile();
      for (s = [-1, 1]) translate([s * ear_x, axis_ly, axis_lz]) tooth_face(-s, 2.5);
    }
    // slot through the wall at Y = +31.5, cut by hand in Cradlev1.stl: 17.6 wide along X, 3.8 deep from the front face
    translate([slot_x0, rim_h / 2 - rim_wall - 0.1, rim_zf - slot_h]) cube([slot_w, rim_wall + 0.2, slot_h + 0.1]);
    translate([0, axis_ly, axis_lz]) rotate([0, 90, 0]) cylinder(d = bolt_d, h = 40, center = true);
    // nut trap on the +x fin, bolt head recess on the -x fin
    translate([ear_x + fin_t - nut_h, axis_ly, axis_lz]) rotate([0, 90, 0]) cylinder(d = nut_af / cos(30) + 0.3, h = nut_h + 1, $fn = 6);
    translate([-(ear_x + fin_t) - 1, axis_ly, axis_lz]) rotate([0, 90, 0]) cylinder(d = head_d, h = head_h + 1);
  }
}

module assembly() {
  clip();
  translate([0, hinge_y, hinge_z]) rotate([theta, 0, 0]) translate([0, -axis_ly, -axis_lz]) cradle();
}

// Wall and keys as plain blocks, for the clearance check only.
module keyboard_proxy() {
  translate([-60, -wall_t, 0]) cube([120, wall_t, wall_h]);                          // rear wall
  translate([-half_len, -40, 0]) cube([2 * half_len, 40 - wall_t - 0.5, key_top_z]); // keys, 0.5 mm behind the wall
}

module placed_cradle() { translate([0, hinge_y, hinge_z]) rotate([theta, 0, 0]) translate([0, -axis_ly, -axis_lz]) cradle(); }

// Test parts: the cradle at theta against the clip or against the keyboard.
module hit_clip()     { intersection() { clip(); placed_cradle(); } }
module hit_keyboard() { intersection() { keyboard_proxy(); placed_cradle(); } }

if (part == "rim") rim();
if (part == "fit_test") fit_test();
if (part == "clamp") clamp();
if (part == "arm") arm();
if (part == "clip") clip();
if (part == "cradle") cradle();
if (part == "assembly") assembly();
if (part == "hit_clip") hit_clip();
if (part == "hit_keyboard") hit_keyboard();
