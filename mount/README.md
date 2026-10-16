# Keychron K8-A3 display mount

A clamp, an arm and a cradle that hold the JC3248W535 display on the rear wall
of a Keychron K8-A3, behind the F5-F8 keys. The display is landscape. The tilt
changes in 5 degree steps. Spec: `docs/superpowers/specs/2026-10-10-keyboard-mount-design.md`.

## Parts

The source is `v1_printed/keyboard_mount_v1.scad`. The `stl/` folder is created by `bash mount/build.sh` and is not stored in git.
The files that were printed and verified are in `v1_printed/` and must not change.

| File | What it is |
|---|---|
| `v1_printed/clamp.stl`, `stl/clamp.stl` | The clamp, with a dovetail rail near its left end |
| `v1_printed/arm.stl`, `stl/arm.stl` | Arm with a dovetail foot and a hinge disc with teeth on both faces |
| `v1_printed/Cradlev1.stl` | The cradle the user made by hand and verified (the reference rim, a bridge and a yoke) |
| `stl/cradle.stl` (built locally) | The same cradle, made by the SCAD. It matches `Cradlev1.stl` (see "Cradle compared with Cradlev1.stl") |

Hardware: one **M3 bolt**, about 25 mm, and one **M3 nut**. The nut sits in the +X fin and the bolt head in the -X fin.

`stl/cradle.stl` is not in git because it contains the reference rim (Printables
model 1442589 by nomadic.nico, licensed CC BY 4.0). `Cradlev1.stl` is in git, and it is a derivative of that model.
See [ATTRIBUTION.md](ATTRIBUTION.md) for the credit and for what was changed.

## How the hinge works

A bridge, 2 mm thick, lies across the back of the rim. A yoke stands on it: two fins, 6 mm thick, with a 11.9 mm gap between them.
Each fin has ratchet teeth on its inner face. The arm's disc goes into the gap, and its two toothed faces mesh with the fins.
One bolt through both fins and the disc holds them. A 45 degree gusset between the fins joins them to the bridge.

## Build the files

1. Get the reference: <https://www.printables.com/model/1442589-guition-jc3248w535-35-surface-mount-case/files>
2. `mkdir -p mount/ref && python3 -I mount/tools/ref_3mf_to_stl.py <the 3mf file> mount/ref/rim.stl`
3. `bash mount/build.sh`
4. `bash mount/check.sh` and `bash mount/check_tower.sh` (a few minutes each)

OpenSCAD 2021.01 cannot read 3MF, which is why step 2 converts it.
To compare any two STL files, use `python3 -I mount/tools/compare_stl.py REF.stl TEST.stl` (the header of the file explains the options).

## Cradle compared with Cradlev1.stl

Result of `bash mount/check.sh`, task 4b. The SCAD cradle is turned into the frame of `Cradlev1.stl` first (x = Y - 2.5, y = X + 7.5, z = 5 - Z).

| | `Cradlev1.stl` | SCAD cradle |
|---|---|---|
| Bounding box (mm) | -34.0 .. 29.0, -40.0 .. 55.0, 0.0 .. 46.07 | -34.01 .. 29.0, -40.0 .. 55.0, 0.0 .. 46.09 |
| Volume | 24.865 cm3 | 24.881 cm3 (0.06 percent more) |
| Volume in one and not the other, rays 0.2 mm apart | 0.023 cm3 only in the STL | 0.035 cm3 only in the SCAD |

The numbers measured in `Cradlev1.stl`: bridge 2.0 mm thick; fins 6.0 mm thick, toothed faces 11.9 mm apart, teeth 0.7 mm high; disc r = 15 mm,
axis at x = -19.0, z = 31.07 in the STL frame (the disc touches the rim edge at x = -34); nut recess 2.6 mm deep, bolt head recess 3.2 mm deep.
Differences that remain, all small: the sizes of the nut pocket and the bolt head recess differ by about 2 mm3 each (the hexagon has the same turn as the STL, checked; a 30 degree turn was worse),
and the fin sides were read from sections of the STL, so they can differ by a few tenths of a mm.
Also:

- The rim in `Cradlev1.stl` has a slot through one long wall (17.6 mm wide, 3.8 mm deep from the front face). The reference rim does not have it, so the SCAD cuts it.
- The turn of the rim in the SCAD (`rim()`) was changed so that it matches `Cradlev1.stl`. The STL has the yoke on the raw rim's -z face. The old SCAD comment said the screen faces the raw -z face. Both cannot be true, because the bridge would cover the screen. I took the STL as correct and put the yoke on the back (-Z). I did not check which face of the rim holds the screen.
- The check is by sampled rays and by bounding box. It did not compare a slicer result or a print.

## Print settings

PETG, 0.2 mm layers, 4 walls, no supports.
- `clamp`: bridge face on the bed (the rail then ends on the bed too).
- `arm`: stand it on its foot, the way it sits on the keyboard. The far side is a 45 degree ramp.
- `cradle`: screen face on the bed. The bridge and the yoke rise from the back of the rim. The fin sides and the gusset are 45 degrees or steeper.

Each part is designed to print with no supports. This is checked by geometry only, not by a slicer run.

## Assemble

1. Press the clamp onto the rear wall so one tab sits in the F4/F5 gap and the other in the F8/F9 gap. The rail is at the left end.
2. Slide the arm down the rail on the clamp's outer plate, from above, until it stops.
3. Put the nut in the hexagonal pocket in the +X fin of the cradle.
4. Put the arm's disc between the two fins and mesh the teeth. Put the bolt through the head recess in the -X fin, the disc and the +X fin, then screw it into the nut.
5. To change the angle, loosen the bolt a quarter turn, set the angle and tighten.

## Measured facts

- Free tilt range with this cradle: **0 to 55 degrees** from horizontal, in 5 degree steps (printed clamp and arm, and the keys as a plain box). At 60 degrees the cradle hits the keys. 50 and 55 degrees are free. Default `theta` is 55.
  This is a result of `check_tower.sh`. The old limit of 95 degrees was for an earlier cradle design.
- The hinge teeth mesh at every 5 degrees and clash at the half steps (checked at 20, 50, 52.5, 55, 57.5 and 60 degrees).
- The clamp and arm built from the SCAD have the same shape as the printed `clamp.stl` and `arm.stl` (volume and bounding box). The STL export of the arm is not byte for byte repeatable, so the check compares shapes. The printed files are checked byte for byte against git commit `2c10556`.
- Fit test (2 prints): the first print (8.5 mm tabs, 0.2 mm bump, 2.81 mm slot) was loose.
  The second (9.3 mm tabs, 0.35 mm bump, 2.4 mm slot) is tight, with no sliding.
- No rear foot. The fit test did not lift out under weight.

## Not verified

- The strength of the hinge. It is held by one M3 bolt and the tooth faces. Nobody has tested it with a load.
- That the teeth print cleanly. At the inner radius (8 mm) a tooth is only about 0.7 mm wide.
- The keys block in the check is a plain box. Real keycaps slope, so the true limit may be slightly wider than 55 degrees.
- That 55 degrees is enough for the user. The old design reached 95 degrees. If more tilt is needed, the yoke must move or the keys block must be measured better.
- Which way the display faces in the SCAD frame was fixed from `Cradlev1.stl` (the yoke is on the back). The display fit in the rim was not checked again.
