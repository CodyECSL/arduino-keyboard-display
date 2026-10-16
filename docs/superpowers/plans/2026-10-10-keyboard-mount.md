# K8-A3 Display Mount Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Print a clip, an arm and a cradle that hold the JC3248W535 display on the rear wall of a Keychron K8-A3, with an adjustable tilt.

**Architecture:** One parametric OpenSCAD file builds three parts: the clip (clamp, tabs, arm), the cradle (the reference rim, plus hinge ears) and a short fit-test piece. A shell script checks dimensions and clearance at each tilt angle. A second script exports the STL files.

**Tech Stack:** OpenSCAD 2021.01 (installed; it cannot import 3MF), Python 3 (no pip) for one conversion script, Bash.

**Spec:** `docs/superpowers/specs/2026-10-10-keyboard-mount-design.md`

## Update 2026-10-11 (issue #17): the v1 design replaces Tasks 4 to 6

This plan was written for `mount/keyboard_mount.scad` and `mount/tower_cradle.scad`. Both files were removed. The printed
and verified design is now built by **one** file, `mount/v1_printed/keyboard_mount_v1.scad`. Read Tasks 1 to 3 for the clamp, the
arm and the hinge teeth (they did not change). Where Tasks 4 to 6 disagree with this section, this section is right.

- **Cradle.** It reproduces `mount/v1_printed/Cradlev1.stl`, which the user made by hand and verified. It is the reference rim, with
  - a **bridge**: a plate 2 mm thick on the back face, 63 mm along the rim's short side and 23.9 mm along the hinge axis;
  - a **yoke**: two toothed fins, 6 mm thick, with a 11.9 mm gap between their toothed faces (teeth 0.7 mm high, point toward the gap);
  - a **gusset**: a 45 degree ridge between the fins, on the bridge;
  - a **slot** the user cut by hand in one long wall of the rim (17.6 wide, 3.8 deep from the front face).
  There are no ears on the rim's wall. The yoke is the same fork that `yoke_female.scad` makes, with the base replaced by the bridge.
- **Cradle frame** (the SCAD's): X along the hinge axis and the long edge, Y up the screen, Z toward the user. The rim front face is at Z = 5 and the back at Z = -7.4. The hinge axis is at Y = -16.5, Z = -26.074, so 18.7 mm behind the back face. The disc, r = 15, touches the rim edge at Y = -31.5.
- **Frame of `Cradlev1.stl`**: x = Y - 2.5, y = X + 7.5, z = 5 - Z. This is a proper rotation plus a shift, with no mirror.
- **Hardware.** M3 bolt and nut (`bolt_d = 3.4`). The nut trap is in the +X fin, the bolt head recess in the -X fin.
- **Scripts.** `build.sh` exports `fit_test`, `clamp`, `arm` and `cradle` from the v1 SCAD to `mount/stl/`. `check.sh` checks sizes, compares the cradle with `Cradlev1.stl`
  (`tools/compare_stl.py`) and compares the clamp and arm with the printed files by shape. `check_tower.sh` checks the cradle with the **printed** clamp and arm:
  the frozen files match commit `2c10556`, the control detects an overlap, the teeth mesh at 5 degree steps, and the tilt sweep.
- **Measured result.** Free tilt range 0 to 55 degrees in 5 degree steps. At 60 degrees the cradle hits the plain-box model of the keys. 50 and 55 degrees are free. Default `theta` is 55.
- **Needs** `mount/ref/rim.stl`. Make it with `tools/ref_3mf_to_stl.py` (see `mount/README.md`).


## Global Constraints

- Tool: OpenSCAD, one parametric `.scad` file (spec: "Tool").
- Printer: Bambu P1S, FDM, PETG, no supports (spec: "Printer").
- Wall thickness 2.81 mm, wall top 16.87 mm above desk, F5 keycap top 41.49 mm above desk.
- F4/F5 gap 10.12 mm. Tab width 8.5 mm, tab depth 4.0 mm. Gap floor is 4.4 mm below the wall top.
- F5 to F8 width, end to end: 75.29 mm.
- Hinge: one M3 bolt and nut, detent every 5 degrees, hinge height about 45 mm.
- Angle range 15 to 100 degrees from horizontal is an estimate. The clearance check gives the real range. 50 to 60 degrees must be inside it.
- No rear foot (the user said it is not needed). If the fit test shows lifting, add one as a follow-up.
- Keep USB-C and both microSD end openings clear.
- Work only on branch `CodyECSL/3d-model`. Never commit to `main`.
- Every commit message ends with: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`
- Do not commit the reference model or the rim files converted from it. It is licensed CC BY 4.0 (author nomadic.nico), so it may be shared with credit, but the build still loads it from `mount/ref/` (git-ignored). Credit is in `mount/ATTRIBUTION.md`.

## Coordinates

X runs along the keyboard rear edge. Y runs backward, away from the keys, with y = 0 on the outer face of the rear wall. Z runs up from the desk. The wall fills y from -2.81 to 0 and z from 0 to 16.87. The keys are at y < -2.81.

The cradle's own frame: local +y runs up the screen, local +z is the screen's face direction. The reference rim covers x -31.5 to 31.5, y -47.5 to 47.5, z -5 to 7.4. The cutout in its +y wall is the USB-C end, so the USB-C end points up and the cable leaves toward the top.

Tilt angle theta is the angle of the screen plane from horizontal. 90 is upright. Less than 90 leans back. The assembled cradle is: `translate([0, hinge_y, hinge_z]) rotate([theta, 0, 0]) translate([0, -axis_ly, -axis_lz]) cradle();`

## File Structure

- `mount/v1_printed/keyboard_mount_v1.scad`: all modules and parameters (this replaces `mount/keyboard_mount.scad`, which was removed). The variable `part` picks what to render.
- `mount/check_tower.sh`: checks the cradle against the printed v1 clamp and arm.
- `mount/tools/compare_stl.py`: compares two STL files (bounding box, volume, shared and different volume).
- `mount/check.sh`: dimension asserts and clearance check at each tilt angle.
- `mount/build.sh`: exports STL files to `mount/stl/`.
- `mount/tools/ref_3mf_to_stl.py`: converts the user's 3MF reference to `mount/ref/rim.stl`.
- `mount/README.md`: print settings, parts list, how to assemble.
- `.gitignore`: add `mount/ref/` and `mount/out/`.

In Tasks 1 to 6 below, read `keyboard_mount.scad` as `v1_printed/keyboard_mount_v1.scad`.

---

### Task 1: Scaffold, parameters and the reference rim

**Files:**
- Create: `mount/keyboard_mount.scad`, `mount/check.sh`, `mount/tools/ref_3mf_to_stl.py`
- Modify: `.gitignore`

**Interfaces:**
- Produces: parameters used by all later tasks (names below). Module `rim()` that imports the reference. Script `mount/check.sh` that exits non-zero on any failed check.

- [ ] **Step 1: Write the converter**

`mount/tools/ref_3mf_to_stl.py`:

```python
"""Convert the reference 3MF (Printables model 1442589) to an ASCII STL.

OpenSCAD 2021.01 cannot import 3MF. Usage:
    python3 -I mount/tools/ref_3mf_to_stl.py <file.3mf> mount/ref/rim.stl
"""
import re
import sys
import zipfile

src, dst = sys.argv[1], sys.argv[2]
xml = zipfile.ZipFile(src).read("3D/3dmodel.model").decode()
body = re.search(r'<object id="2".*?</object>', xml, re.S).group(0)
verts = [tuple(map(float, m)) for m in
         re.findall(r'<vertex x="([^"]+)" y="([^"]+)" z="([^"]+)"', body)]
tris = [tuple(map(int, m)) for m in
        re.findall(r'<triangle v1="(\d+)" v2="(\d+)" v3="(\d+)"', body)]
with open(dst, "w") as f:
    f.write("solid rim\n")
    for tri in tris:
        f.write("facet normal 0 0 0\nouter loop\n")
        for i in tri:
            f.write("vertex %f %f %f\n" % verts[i])
        f.write("endloop\nendfacet\n")
    f.write("endsolid rim\n")
print(len(verts), "vertices,", len(tris), "triangles")
```

- [ ] **Step 2: Convert and ignore the output**

```bash
mkdir -p mount/ref mount/tools
printf 'mount/ref/\nmount/out/\n' >> .gitignore
python3 -I mount/tools/ref_3mf_to_stl.py ~/Downloads/guition-jc3248w535-35-tft-support.3mf mount/ref/rim.stl
```
Expected: `1156 vertices, 2328 triangles`

- [ ] **Step 3: Write the check script with the first failing check**

`mount/check.sh`:

```bash
#!/usr/bin/env bash
# Checks for keyboard_mount.scad. Exits non-zero on the first failure.
set -u
cd "$(dirname "$0")"
OUT=out
mkdir -p "$OUT"
fail() { echo "FAIL: $*"; exit 1; }

# Render a part to STL and print its bounding box as "xmin xmax ymin ymax zmin zmax".
bbox() {
  openscad -o "$OUT/$1.stl" -D "part=\"$1\"" keyboard_mount.scad 2>"$OUT/$1.log" \
    || { cat "$OUT/$1.log"; fail "render $1"; }
  python3 -I - "$OUT/$1.stl" <<'PY'
import re, sys
v = [tuple(map(float, m)) for m in re.findall(r'vertex\s+(\S+)\s+(\S+)\s+(\S+)', open(sys.argv[1]).read())]
print(*[f"{f(p[i] for p in v):.2f}" for i in range(3) for f in (min, max)])
PY
}

near() { python3 -I -c "import sys; sys.exit(0 if abs($1-$2)<=${3:-0.05} else 1)"; }

# Task 1: the rim keeps the reference size.
read -r x0 x1 y0 y1 z0 z1 < <(bbox rim)
near "$x1-$x0" 63 || fail "rim width"
near "$y1-$y0" 95 || fail "rim length"
near "$z1-$z0" 12.4 || fail "rim height"
echo "PASS: rim size"
```

- [ ] **Step 4: Run it to verify it fails**

Run: `bash mount/check.sh`
Expected: FAIL (`keyboard_mount.scad` does not exist, so the render fails).

- [ ] **Step 5: Write the parameters and the rim**

`mount/keyboard_mount.scad`:

```openscad
// Keychron K8-A3 display mount. Units are mm. See docs/superpowers/specs/.
part = "assembly";   // rim | clip | cradle | fit_test | assembly
theta = 55;          // tilt from horizontal, assembly only
teeth = true;        // false for clearance checks
$fn = 48;

// Keyboard (measured by the user)
wall_t = 2.81;  wall_h = 16.87;  key_top_z = 41.49;
gap_w = 10.12;  gap_floor = 4.4; span = 75.29;
tab_pitch = span + gap_w;        // centre to centre of the two gaps

// Clip
tab_w = 8.5;  tab_depth = 4.0;  tab_t = 2.5;
bridge_t = 3; plate_t = 2.5;  plate_drop = 10;  grip = 0.2;
hinge_y = 20; hinge_z = 45;      // hinge axis, assembled frame

// Hinge
boss_w = 10;  ear_t = 4.5;  hinge_gap = 0.3;
teeth_n = 72; tooth_h = 0.7;  r_out = 15;  r_in = 8;   // 72 teeth = 5 degrees
bolt_d = 3.4; nut_af = 5.5;   nut_h = 2.6; head_d = 6.2; head_h = 3.2;

// Cradle (local frame of the reference rim)
rim_x = 63; rim_y = 95; rim_z0 = -5; rim_z1 = 7.4;
axis_ly = -39.5; axis_lz = -25;  // hinge axis in the cradle frame

assert(tab_w <= gap_w - 1, "tab too wide for the keycap gap");
assert(tab_depth < gap_floor, "tab deeper than the gap floor");

module rim() { import("ref/rim.stl"); }

if (part == "rim") rim();
```

- [ ] **Step 6: Run the check to verify it passes**

Run: `bash mount/check.sh`
Expected: `PASS: rim size`

- [ ] **Step 7: Commit**

```bash
git add .gitignore mount/keyboard_mount.scad mount/check.sh mount/tools/ref_3mf_to_stl.py
git commit -m "Add the mount scaffold and the reference rim import

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Clip clamp and fit-test piece

**Files:**
- Modify: `mount/keyboard_mount.scad`, `mount/check.sh`

**Interfaces:**
- Consumes: parameters from Task 1.
- Produces: `module clip_core()` (bridge, outer plate, two tabs, grip bump) and `part = "fit_test"`.

- [ ] **Step 1: Add the failing checks**

Append to `mount/check.sh`:

```bash
# Task 2: the fit-test piece. Bridge sits on the wall top; tabs reach down 4.0 mm.
read -r x0 x1 y0 y1 z0 z1 < <(bbox fit_test)
near "$z0" 12.87 || fail "fit_test lowest point $z0, want 12.87 (wall top minus tab depth)"
near "$z1" 19.87 || fail "fit_test top $z1, want 19.87"
near "$x1-$x0" 93.91 0.1 || fail "fit_test length"   # tab_pitch + tab_w = 85.41 + 8.5
echo "PASS: fit_test size"
```

- [ ] **Step 2: Run it to verify it fails**

Run: `bash mount/check.sh`
Expected: `PASS: rim size`, then FAIL on `fit_test` (the part does not exist).

- [ ] **Step 3: Write the clamp**

Insert before the `if (part ...)` lines in `mount/keyboard_mount.scad`:

```openscad
half_len = tab_pitch / 2 + tab_w / 2;
z_top = wall_h + bridge_t;

module clip_core() {
  // bridge over the wall top, only over the wall footprint
  translate([-half_len, -wall_t, wall_h]) cube([2 * half_len, wall_t + plate_t, bridge_t]);
  // outer plate down the back of the wall
  translate([-half_len, 0, wall_h - plate_drop]) cube([2 * half_len, plate_t, plate_drop + bridge_t]);
  // grip bump: 0.2 mm into the wall, on purpose
  translate([-half_len + 4, -grip, wall_h - plate_drop + 1]) cube([2 * half_len - 8, grip, 2]);
  // tabs in the two keycap gaps
  for (s = [-1, 1])
    translate([s * tab_pitch / 2 - tab_w / 2, -wall_t - tab_t, wall_h - tab_depth])
      cube([tab_w, tab_t, tab_depth + bridge_t]);
}

module fit_test() {
  difference() {
    clip_core();
    // 4 mm hole in the outer plate, to hang a test weight
    translate([0, -1, wall_h - plate_drop / 2]) rotate([-90, 0, 0]) cylinder(d = 4, h = plate_t + 2);
  }
}
```

Replace the last line (`if (part == "rim") rim();`) with:

```openscad
if (part == "rim") rim();
if (part == "fit_test") fit_test();
```

- [ ] **Step 4: Run to verify it passes**

Run: `bash mount/check.sh`
Expected: `PASS: rim size` and `PASS: fit_test size`

- [ ] **Step 5: Render a picture and look at it**

```bash
openscad -o mount/out/fit.png --camera=0,0,17,60,0,200,260 --imgsize=900,700 -D 'part="fit_test"' mount/keyboard_mount.scad
```
Read `mount/out/fit.png`. Expected: a U-shaped clamp with two tabs hanging on the inner side. Fix the geometry if the tabs hang on the wrong side.

- [ ] **Step 6: Commit**

```bash
git add mount/keyboard_mount.scad mount/check.sh
git commit -m "Add the clip clamp and the fit-test piece

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7 (hand-off to the user, before Task 3): print the fit-test piece**

Tell the user: print `mount/stl/fit_test.stl` first (run `bash mount/build.sh` after Task 6, or export it now with `openscad -o mount/out/fit_test.stl -D 'part="fit_test"' mount/keyboard_mount.scad`). PETG, 0.2 mm layers, print with the bridge face down on the bed, no supports. Clip it on the rear wall between the F4/F5 and F8/F9 gaps. Hang the weight of the display and mount (weigh them on a kitchen scale) from the hole with a string. Report: does it clip on, does it slide along the wall, does it lift. If the tabs do not fit, change `tab_w`, `tab_depth` or `grip` and repeat. Do not continue to Task 3 until the user reports the fit.

---

### Task 3: Arm and hinge boss

**Files:**
- Modify: `mount/keyboard_mount.scad`, `mount/check.sh`

**Interfaces:**
- Consumes: `clip_core()`, `hinge_y`, `hinge_z`, `boss_w`, tooth parameters.
- Produces: `module tooth_face(dir)` (teeth on a face at x = 0, pointing along `dir` = +1 or -1 on the x axis), `module clip()` (clamp + arm + boss + bolt hole), `part = "clip"`.

- [ ] **Step 1: Add the failing check**

Append to `mount/check.sh`:

```bash
# Task 3: the clip reaches the hinge height and is boss_w wide at the top.
read -r x0 x1 y0 y1 z0 z1 < <(bbox clip)
near "$z1" 60 1.5 || fail "clip top $z1, want about 60 (hinge_z 45 + r_out 15)"
echo "PASS: clip size"
```

- [ ] **Step 2: Run to verify it fails**

Run: `bash mount/check.sh`
Expected: FAIL at `clip` (not defined).

- [ ] **Step 3: Write the teeth, arm and boss**

Insert before the `if (part ...)` lines:

```openscad
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

module boss() {
  // disc of radius r_out, boss_w thick, with teeth on both faces
  rotate([0, 90, 0]) cylinder(r = r_out, h = boss_w, center = true);
  for (s = [-1, 1]) translate([s * boss_w / 2, 0, 0]) tooth_face(s, 0);
}

module clip() {
  difference() {
    union() {
      clip_core();
      // arm: a solid hull from the bridge top up to the boss disc
      hull() {
        translate([-boss_w / 2, -wall_t, z_top - 0.01]) cube([boss_w, wall_t + plate_t, 0.01]);
        translate([0, hinge_y, hinge_z]) rotate([0, 90, 0]) cylinder(r = r_out, h = boss_w, center = true);
      }
      translate([0, hinge_y, hinge_z]) for (s = [-1, 1]) translate([s * boss_w / 2, 0, 0]) tooth_face(s, 0);
    }
    translate([0, hinge_y, hinge_z]) rotate([0, 90, 0]) cylinder(d = bolt_d, h = boss_w + 2 * tooth_h + 2, center = true);
  }
}
```

Add `if (part == "clip") clip();` after the `fit_test` line.

- [ ] **Step 4: Run to verify it passes**

Run: `bash mount/check.sh`
Expected: three PASS lines. If `clip top` fails, print the measured value and fix `hinge_z` or the hull, not the check.

- [ ] **Step 5: Render and look**

```bash
openscad -o mount/out/clip.png --camera=0,10,35,70,0,30,320 --imgsize=900,900 -D 'part="clip"' mount/keyboard_mount.scad
```
Read `mount/out/clip.png`. Expected: clamp on the left, a sloped arm, a toothed disc on top.

- [ ] **Step 6: Commit**

```bash
git add mount/keyboard_mount.scad mount/check.sh
git commit -m "Add the clip arm and the toothed hinge boss

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Cradle with hinge ears (SUPERSEDED: see the update above. The cradle is now the bridge and yoke of `Cradlev1.stl`.)

**Files:**
- Modify: `mount/keyboard_mount.scad`, `mount/check.sh`

**Interfaces:**
- Consumes: `rim()`, `tooth_face()`, hinge parameters.
- Produces: `module cradle()` in the cradle frame (rim + two ears, bolt hole, nut trap, head recess) and `part = "cradle"`.

- [ ] **Step 1: Add the failing check**

Append to `mount/check.sh`:

```bash
# Task 4: the cradle is as wide as the rim and the ears reach the hinge axis.
read -r x0 x1 y0 y1 z0 z1 < <(bbox cradle)
near "$x1-$x0" 63 || fail "cradle width"
near "$z0" -40 1 || fail "cradle lowest point $z0, want about -40 (axis_lz -25 minus r_out 15)"
echo "PASS: cradle size"
```

- [ ] **Step 2: Run to verify it fails**

Run: `bash mount/check.sh`
Expected: FAIL at `cradle`.

- [ ] **Step 3: Write the cradle**

Insert before the `if (part ...)` lines:

```openscad
ear_x = boss_w / 2 + hinge_gap;     // inner face of each ear

module ear(s) {
  // hull from the disc at the hinge axis up to the back of the rim
  translate([s * (ear_x + ear_t / 2), 0, 0]) hull() {
    translate([0, axis_ly, axis_lz]) rotate([0, 90, 0]) cylinder(r = r_out, h = ear_t, center = true);
    translate([-ear_t / 2, rim_y / -2, rim_z0 - 0.5]) cube([ear_t, 18, 0.5]);
  }
}

module cradle() {
  difference() {
    union() {
      rim();
      for (s = [-1, 1]) ear(s);
      for (s = [-1, 1]) translate([s * ear_x, axis_ly, axis_lz]) tooth_face(-s, 2.5);
    }
    translate([0, axis_ly, axis_lz]) rotate([0, 90, 0]) cylinder(d = bolt_d, h = 40, center = true);
    // nut trap on the +x ear, bolt head recess on the -x ear
    translate([ear_x + ear_t - nut_h, axis_ly, axis_lz]) rotate([0, 90, 0]) cylinder(d = nut_af / cos(30) + 0.3, h = nut_h + 1, $fn = 6);
    translate([-(ear_x + ear_t) - 1, axis_ly, axis_lz]) rotate([0, 90, 0]) cylinder(d = head_d, h = head_h + 1);
  }
}
```

Add `if (part == "cradle") cradle();` after the `clip` line.

- [ ] **Step 4: Run to verify it passes**

Run: `bash mount/check.sh`
Expected: four PASS lines.

- [ ] **Step 5: Check which side of the rim the display seats from**

```bash
openscad -o mount/out/cradle.png --camera=0,0,-10,55,0,25,300 --imgsize=900,900 -D 'part="cradle"' mount/keyboard_mount.scad
```
Read the picture. The rim must seat the display with its screen facing local +z, and the USB-C cutout must be at the top (+y). If the rim's inner ledge says the display seats from the other side, add `mirror([0,0,1])` around `rim()` and change `rim_z0`/`rim_z1`/`axis_lz` to match. Tell the user which side you chose. This is the only place the plan depends on a guess about the reference.

- [ ] **Step 6: Commit**

```bash
git add mount/keyboard_mount.scad mount/check.sh
git commit -m "Add the cradle with hinge ears

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Assembly and clearance at each tilt angle

**Files:**
- Modify: `mount/keyboard_mount.scad`, `mount/check.sh`

**Interfaces:**
- Consumes: `clip()`, `cradle()`, `theta`, `teeth`.
- Produces: `module assembly()`, `module keyboard_proxy()` (wall and key block, used only for checks), `part = "assembly"`, `part = "hit_<name>"` for clearance checks. The measured tilt range, written to the README in Task 6.

- [ ] **Step 1: Write the assembly and the proxies**

Insert before the `if (part ...)` lines:

```openscad
module assembly() {
  clip();
  translate([0, hinge_y, hinge_z]) rotate([theta, 0, 0]) translate([0, -axis_ly, -axis_lz]) cradle();
}

// Wall and keys as plain blocks, for the clearance check only.
module keyboard_proxy() {
  translate([-60, -wall_t, 0]) cube([120, wall_t, wall_h]);                     // rear wall
  translate([-half_len, -40, 0]) cube([2 * half_len, 40 - wall_t - 0.5, key_top_z]); // keys, 0.5 mm behind the wall
}

// Test parts: the cradle at theta against the clip (teeth off) or against the keyboard.
module hit_clip()     { intersection() { clip(); translate([0, hinge_y, hinge_z]) rotate([theta, 0, 0]) translate([0, -axis_ly, -axis_lz]) cradle(); } }
module hit_keyboard() { intersection() { keyboard_proxy(); translate([0, hinge_y, hinge_z]) rotate([theta, 0, 0]) translate([0, -axis_ly, -axis_lz]) cradle(); } }
```

Add to the `if` lines:

```openscad
if (part == "assembly") assembly();
if (part == "hit_clip") hit_clip();
if (part == "hit_keyboard") hit_keyboard();
```

Note: the keys proxy is a block 40 mm deep. Real keycaps are narrower at the base and the gaps are open. A hit on the proxy is a warning, not proof of a collision. Report the first hit angle and look at it in a picture before trusting it.

- [ ] **Step 2: Add the clearance check (failing check first: the angles that must be free)**

Append to `mount/check.sh`:

```bash
# Task 5: clearance. A part is "free" when OpenSCAD reports an empty intersection.
is_free() {  # $1 = part, $2 = theta
  openscad -o "$OUT/hit.stl" -D "part=\"$1\"" -D "theta=$2" -D "teeth=false" keyboard_mount.scad 2>&1 | grep -q "empty"
}
FREE_MIN=""; FREE_MAX=""
for t in $(seq 0 5 120); do
  if is_free hit_clip "$t" && is_free hit_keyboard "$t"; then
    [ -z "$FREE_MIN" ] && FREE_MIN=$t
    FREE_MAX=$t
  fi
done
echo "Free tilt range: ${FREE_MIN:-none} to ${FREE_MAX:-none} degrees (5 degree steps)"
[ -n "$FREE_MIN" ] || fail "no free tilt angle"
for t in 50 55 60; do
  is_free hit_clip "$t" && is_free hit_keyboard "$t" || fail "tilt $t degrees collides"
done
echo "PASS: 50, 55 and 60 degrees are free"
```

- [ ] **Step 3: Run it**

Run: `bash mount/check.sh`
Expected: it prints the free range and `PASS: 50, 55 and 60 degrees are free`. If a hit appears at 50 to 60 degrees, change the geometry (for example `hinge_y`, `hinge_z` or `axis_lz`), not the check. Write the free range down for the README.

If the free range is narrower than about 30 to 100 degrees, report the first and last colliding part to the user and stop. The user decides whether to accept it or to move the hinge.

- [ ] **Step 4: Render the assembly at 55 degrees and look**

```bash
openscad -o mount/out/assembly55.png --camera=0,20,40,75,0,60,420 --imgsize=1000,900 -D 'part="assembly"' -D theta=55 mount/keyboard_mount.scad
```
Read the picture. Expected: the display leans back above the wall, clip on the wall.

- [ ] **Step 5: Commit**

```bash
git add mount/keyboard_mount.scad mount/check.sh
git commit -m "Add the assembly and the clearance check

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Export, README and wrap-up

**Files:**
- Create: `mount/build.sh`, `mount/README.md`, `mount/stl/*.stl`

**Interfaces:**
- Consumes: parts `clip`, `cradle`, `fit_test`.

- [ ] **Step 1: Write the build script**

`mount/build.sh`:

```bash
#!/usr/bin/env bash
# Export the printable parts to mount/stl/.
set -eu
cd "$(dirname "$0")"
mkdir -p stl
for p in fit_test clip cradle; do
  openscad -o "stl/$p.stl" -D "part=\"$p\"" keyboard_mount.scad
  echo "wrote stl/$p.stl"
done
```

- [ ] **Step 2: Run the build and the checks**

```bash
bash mount/build.sh && bash mount/check.sh
```
Expected: three `wrote` lines, then all PASS lines.

- [ ] **Step 3: Write the README**

`mount/README.md`, with these sections and facts (fill the measured values from Task 5):
- What it is, with the spec link.
- Parts: `fit_test.stl` (print first), `clip.stl`, `cradle.stl`. Hardware: one M3 x 20 bolt and one M3 nut.
- Print settings: PETG, 0.2 mm layers, 4 walls, 30 percent infill. Print `clip` upside down with the bridge face on the bed. Print `cradle` with the rim's open face on the bed (back face up). No supports. If the 72 teeth do not print cleanly, set `teeth_n = 36` (10 degree steps) in `keyboard_mount.scad`.
- Assemble: clip onto the rear wall between the F4/F5 and F8/F9 gaps. Slide the cradle ears over the boss. Put the bolt through the head recess side, add the nut. Tighten until the teeth hold. To change the angle, loosen the bolt a quarter turn, set the angle, tighten.
- The tilt range that the clearance check found, and the command `bash mount/check.sh`.
- Where the numbers come from (the measurement table in the spec).
- Known risk: no rear foot, as in the spec. The fit-test result goes here.

- [ ] **Step 4: Commit**

```bash
git add mount/build.sh mount/README.md mount/stl
git commit -m "Add the STL export and the README for the mount

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 5: Report to the user**

Tell the user: which parts are ready, the free tilt range, which side the display seats from (Task 4, step 5), and that the clip and cradle prints wait on their fit-test report. Do not open a PR. Ask whether to open one.

## Self-Review (done by the author of this plan)

- **Spec coverage:** clip method, tabs, grip (Task 2); hinge, 5 degree detents, M3 (Tasks 3 and 4); angle range and the 50 to 60 degree rule (Task 5); cutouts kept clear, because the reference rim is used as is (Task 4); fit-test first (Task 2); verification renders (Tasks 2 to 5); STL export (Task 6). The spec's "no foot" is in Global Constraints.
- **Open items from the spec:** cutout positions are inherited from the reference rim. The cable exit direction is handled by pointing the USB-C end up. The tilt limits come from Task 5.
- **Known weak points, not hidden:** the 72-tooth ratchet may be too fine for FDM (fallback given in the README step). The keyboard proxy is a plain block, so a hit in Task 5 needs a look at the picture. The side the display seats from is a guess until Task 4 step 5. None of the geometry has been rendered yet. Expect to adjust numbers in Tasks 3 to 5.
