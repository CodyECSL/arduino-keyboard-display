// The "female" half of the v1 arm's toothed disc: a fork with two toothed fins, one on each side of
// the arm's disc, so BOTH toothed faces of the printed v1 arm mesh. New file. It does not change any v1 file.
//
// Frame: the hinge axis is the X axis through the origin (the bolt runs along X). The arm's disc sits between
// the fins at x = -5..5. Teeth point toward the disc. The flat mounting face of the base is the plane
// y = +6, covering z = +21.6 to +34.0 and x = -11.95 to +11.95. Glue or merge that face to your frame.
include <keyboard_mount_v1.scad>
part = "none";           // keeps the v1 parts from rendering
fpart = "yoke_female";

fin_t = 6;                                   // fin thickness along X
fin_x = boss_w / 2 + hinge_gap;              // 5.95: inner (toothed) face of each fin, same as the v1 ears
base_y0 = -6;  base_y1 = 6.3;                // base block, 12.3 deep along Y (0.3 mm is overlap for fusing to a frame)
base_z0 = 21.6;  base_z1 = 34.0;             // 12.4 mm: the rim wall depth in the cradle design

module base() { translate([-(fin_x + fin_t), base_y0, base_z0]) cube([2 * (fin_x + fin_t), base_y1 - base_y0, base_z1 - base_z0]); }
module fin(s) {
  translate([s > 0 ? fin_x : -(fin_x + fin_t), 0, 0]) hull() {
    translate([0, 0, 0]) rotate([0, 90, 0]) cylinder(r = r_out, h = fin_t);
    translate([0, base_y0, base_z0 - 0.01]) cube([fin_t, base_y1 - base_y0, 0.01]);
  }
}

module yoke_female() {
  difference() {
    union() {
      base();
      for (s = [-1, 1]) fin(s);
      for (s = [-1, 1]) translate([s * fin_x, 0, 0]) tooth_face(-s, 2.5);   // same phase as the v1 ears
    }
    rotate([0, 90, 0]) cylinder(d = bolt_d, h = 40, center = true);                                          // M3 bolt hole through both fins
    translate([fin_x + fin_t - nut_h, 0, 0]) rotate([0, 90, 0]) cylinder(d = nut_af / cos(30) + 0.3, h = nut_h + 1, $fn = 6);   // nut pocket, +x fin
    translate([-(fin_x + fin_t) - 1, 0, 0]) rotate([0, 90, 0]) cylinder(d = head_d, h = head_h + 1);         // head recess, -x fin
  }
}

// Test: the female turned by theta about the hinge axis, in the arm's place at the hinge.
module placed(side = 0) {
  intersection() {
    translate([0, hinge_y, hinge_z]) rotate([theta, 0, 0]) yoke_female();
    if (side != 0) translate([side > 0 ? 0 : -100, -100, -100]) cube([100, 300, 300]);
  }
}
if (fpart == "yoke_female") yoke_female();
if (fpart == "hit_arm") intersection() { arm(); placed(0); }
if (fpart == "hit_arm_left") intersection() { arm(); placed(-1); }
if (fpart == "hit_arm_right") intersection() { arm(); placed(1); }
