# Attribution

This project uses and adapts a 3D model made by someone else. It is shared under a Creative Commons licence, and this page gives the credit that the licence requires.

## The original work

| | |
|---|---|
| **Title** | Guition JC3248W535 3.5 Surface Mount Case (the file used is named "Guition JC3248W535 3.5 TFT Support") |
| **Author** | nomadic.nico (Printables: @nomadicnico_177416) |
| **Source** | https://www.printables.com/model/1442589-guition-jc3248w535-35-surface-mount-case |
| **Licence** | Creative Commons Attribution 4.0 International (CC BY 4.0): https://creativecommons.org/licenses/by/4.0/ |

The title is taken from the page address and from the model's file name. The page itself was not readable when this was written, so the exact title wording on Printables may differ slightly.

## What was changed

The original is a rectangular rim, 63 x 95 x 12.4 mm, that holds the Guition JC3248W535 display. Changes made in this repository:

- **`mount/v1_printed/Cradlev1.stl`** contains the original rim with a hinge yoke and a thin bridge across its back added, so that it can tilt on the arm. This was modelled by the repository owner (CodyECSL). It is a derivative of the original.
- **`mount/v1_printed/keyboard_mount_v1.scad`** loads the original rim as a separate file at build time and moves it. It turns it from portrait to landscape and adds the bridge, the yoke and a wall slot, so that the result reproduces `Cradlev1.stl`. The cradle it builds is a derivative of the original in the same way. The original rim file itself is **not** stored in this repository: `mount/ref/` and the generated cradle files that contain the rim are listed in `.gitignore`. The script `mount/tools/ref_3mf_to_stl.py` only converts the downloaded file from 3MF to STL.

## No endorsement

The author did not make, check or approve this project or the changes above. Their name is here only as credit.

## Everything else

The clamp, the arm, the hinge teeth and the rest of the mount in this repository were made for this project and are not part of the original work.
