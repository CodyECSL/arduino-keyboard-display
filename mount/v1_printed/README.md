# v1: the clamp and arm that were printed

FROZEN. Do not edit or overwrite these files. They are copied from git commit `2c10556`
(the last commit before the cradle design changed again).

| File | What it is |
|---|---|
| `clamp.stl` | Clamp with the dovetail rail at the center |
| `arm.stl` | Arm with a dovetail foot and a toothed disc on both faces (matches the v1 ears) |
| `keyboard_mount_v1.scad` | The source that made them (it imports `ref/rim.stl` through the `ref` link). Since issue #17 its cradle also builds the design in `Cradlev1.stl`. The clamp and arm parts did not change |

`mount/build.sh` writes new builds to `mount/stl/`, a separate folder that is not stored in git.

## Added later: the female interface (new files only, nothing above was changed)

| File | What it is |
|---|---|
| `yoke_female.stl` | The mating half of the arm's toothed disc: a fork with two toothed fins, one on each side of the disc, so both toothed faces mesh |
| `yoke_female.scad` | Its source. The hinge axis is the X axis through the origin. The flat mounting face is the plane y = +6, covering z = +21.6 to +34.0 and x = -11.95 to +11.95 |

Hardware: one M3 bolt, about 25 mm, and one M3 nut. The nut sits in the +x fin, and the bolt head sits in the -x fin.
Checked against `arm.stl` in this folder: both faces mesh at 5 degree steps and clash between them.

## Added: the cradle (made and verified by the user, not generated here)

| File | What it is |
|---|---|
| `Cradlev1.stl` | The cradle that goes with `clamp.stl` and `arm.stl`. Made by the user, who verified that it works. Added unchanged, byte for byte, from `Downloads/Cradlev1.stl`. Binary STL, 7326 triangles, 63 x 95 x 46.1 mm, 24.9 cm3 |

This is now the v1 cradle. The STL itself was not generated here and is unchanged. `keyboard_mount_v1.scad` now reproduces it (`part = "cradle"`); the comparison is in `../README.md`.

## Attribution

`Cradlev1.stl` is a derivative of "Guition JC3248W535 3.5 Surface Mount Case" by nomadic.nico (Printables @nomadicnico_177416),
https://www.printables.com/model/1442589-guition-jc3248w535-35-surface-mount-case ,
licensed CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). The rim was changed: a hinge yoke and a thin back bridge were added.
See [../ATTRIBUTION.md](../ATTRIBUTION.md).
