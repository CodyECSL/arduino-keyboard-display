# Keychron K8-A3 Display Mount Design

Date: 2026-10-10
Branch: `CodyECSL/3d-model`

## Goal

A 3D-printed mount that holds the JC3248W535 macropad display on the rear edge
of a Keychron K8-A3 keyboard, behind the F5-F8 keys. The display sits higher
than the keycaps and leans back toward the user. The user can change the angle
in steps.

## Measurements (taken by the user with a caliper, 2026-10-10)

| Item | Value |
|---|---|
| Rear wall thickness | 2.81 mm |
| Rear wall top, height above desk | 16.87 mm |
| F5 keycap top, height above desk | 41.49 mm |
| Gap between F4 and F5 keycaps | 10.12 mm |
| Depth from wall top down to the floor of that gap | 4.4 mm |
| Width F5 to F8 keycaps, end to end | 75.29 mm |
| Reference cradle (from the 3MF) | 63 x 95 mm, 12.4 mm deep |

The F8/F9 gap equals the F4/F5 gap (confirmed by the user after the first fit test).
The user said to ignore the 17.72 mm reading.

## Decisions

| Topic | Decision |
|---|---|
| Clip position | On the rear wall, between the F4/F5 gap and the F8/F9 gap |
| Clip method | Two inner tabs, one in each gap, joined by a bridge over the wall top. An outer face runs down the back of the wall with a 0.35 mm interference bump (0.2 mm was loose). The slot from the plate face to the tab face is 2.4 mm, narrower than the 2.81 mm wall, so the clamp pinches the wall by 0.41 mm. |
| Tabs | 9.3 mm wide, 4.0 mm deep. About 0.4 mm side clearance and 0.4 mm depth clearance. The first print (8.5 mm tabs, 0.2 mm grip) clipped on but was loose and slid along the wall. |
| Rear foot | None. The user said it is not needed. See "Risk". |
| Hinge | One M3 bolt (about 25 mm) and one M3 nut. Two-sided: the arm's disc has teeth on both faces. It sits between the two toothed fins of the cradle's yoke, so both faces mesh. Ratchet teeth give a detent every 5 degrees. (Changed 2026-10-11 to match the printed v1 files; see issue #17.) |
| Hinge height | 45 mm above the desk, 3.5 mm above the keycap tops (the user's choice) |
| Angle range | Measured by `mount/check_tower.sh` with the v1 cradle: 0 to 55 degrees from horizontal in 5 degree steps. The cradle hits the plain-box keys at 60. 50 and 55 degrees are inside. Hinge height stays at 45 mm (the user's choice). |
| Cradle | The design in `mount/v1_printed/Cradlev1.stl`, made by the user and verified. The reference rim (landscape, 95 wide, 63 tall, screen facing the user) with a 2 mm bridge plate across its back, a yoke of two 6 mm toothed fins on the bridge, and a 45 degree gusset between the fins. The hinge axis is along the long edge, 16.5 mm from the centre toward the bottom edge and 18.7 mm behind the back face. The user cut a 17.6 x 3.8 mm slot in one long wall by hand. It prints screen-face down with no supports. `keyboard_mount_v1.scad` reproduces it; the comparison is in `mount/README.md`. |
| Tool | OpenSCAD, one parametric `.scad` file (`mount/v1_printed/keyboard_mount_v1.scad`) |
| Printer | Bambu P1S, FDM, PETG, no supports |

## Parts

1. **Clamp.** Tabs, bridge, outer plate and a dovetail rail near its left end. Prints bridge-down with no supports.
2. **Arm.** Dovetail foot, arm and a hinge disc with teeth on both faces. Slides down the rail from above. Prints standing, with a 45 degree ramp on the far side, with no supports.
3. **Cradle.** Holds the display. The rim, a bridge and a yoke with two toothed fins. Prints screen-face down.
4. **Fit-test piece.** The clamp only, short. It was printed first.

The arm is a separate part because the arm rises from the bridge top. The 94 mm bridge must lie on the bed to print without supports, and the arm would then be below the bed.

## Risk

The display hangs outside the wall. Its weight tries to lift the inner tabs out
of the gaps. Only the 4.0 mm tab depth and the friction grip on the wall stop
this. The user believes the weight keeps the clip on. This is unverified.
The fit-test piece, loaded with a weight near the display weight, checks it.
If the clip lifts, add a small rear foot or tighten the grip.

## Open items

- Cutout positions in the reference 3MF are not read yet.
- Cable exit direction and desk space behind the keyboard are not answered.
  The default is a straight exit to the rear.
- The exact angle limits come from the clearance check.

## Verification

- Render the model at 15, 35, 55, 75 and 100 degrees and check for collisions
  with the wall, the keys and the clip.
- Compare each dimension in the model with the measurement table above.
- Print the fit-test piece and check that it clips on and holds the weight.
