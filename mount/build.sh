#!/usr/bin/env bash
# Export the printable parts of v1_printed/keyboard_mount_v1.scad to mount/stl/ (ignored by git).
# The printed v1 files in v1_printed/ are never touched. This needs ref/rim.stl; see ATTRIBUTION.md.
set -eu
cd "$(dirname "$0")"
[ -f ref/rim.stl ] || { echo "ref/rim.stl is missing. Make it: python3 -I tools/ref_3mf_to_stl.py <the 3MF> ref/rim.stl" >&2; exit 1; }
mkdir -p stl
for p in fit_test clamp arm cradle; do
  openscad -o "stl/$p.stl" -D "part=\"$p\"" v1_printed/keyboard_mount_v1.scad
  echo "wrote stl/$p.stl"
done
