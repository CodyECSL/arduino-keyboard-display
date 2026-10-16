#!/usr/bin/env bash
# Does the v1 cradle (the yoke and bridge that Cradlev1.stl has) work with the PRINTED v1 clamp and arm?
# One SCAD now holds all three: v1_printed/keyboard_mount_v1.scad.
set -u
cd "$(dirname "$0")"
OUT=out; mkdir -p "$OUT"
SCAD=v1_printed/keyboard_mount_v1.scad
[ -f ref/rim.stl ] || { echo "ref/rim.stl is missing. See build.sh." >&2; exit 1; }
fail() { echo "FAIL: $*" >&2; exit 1; }

# The frozen v1 files must be unchanged.
for f in clamp arm; do
  cmp -s v1_printed/$f.stl <(git show 2c10556:mount/stl/$f.stl) || fail "v1_printed/$f.stl differs from commit 2c10556"
done
echo "PASS: v1 files are byte-identical to commit 2c10556"

# overlap <tpart> <theta> [extra -D args]  ->  prints "free", "HIT" or "ERROR"
overlap() {
  local out; rm -f "$OUT/o.stl"
  out=$(openscad -o "$OUT/o.stl" -D part="\"$1\"" -D theta="$2" "${@:3}" $SCAD 2>&1)
  if echo "$out" | grep -q "object is empty"; then echo free
  elif [ -f "$OUT/o.stl" ]; then echo HIT
  else echo ERROR; fi
}

# Control: a deliberately overlapping case must be reported as a hit.
[ "$(overlap hit_clip 55 -D hinge_gap=-1.0)" = HIT ] || fail "control: the test cannot detect an overlap"
echo "PASS: control detects an overlap"

# Teeth: the arm's teeth and the cradle's teeth mesh at 5 degree steps and clash between them.
mesh() { overlap hit_clip "$1" -D teeth=true; }
for t in 20 50 55 60; do [ "$(mesh $t)" = free ] || fail "teeth clash at $t degrees (should mesh)"; done
for t in 52.5 57.5; do [ "$(mesh $t)" = HIT ] || fail "teeth do not clash at $t degrees (should)"; done
echo "PASS: teeth mesh at 20, 50, 55, 60 and clash at 52.5, 57.5 degrees"

# Clearance sweep, teeth off: the cradle against the v1 clamp and arm, and against the keys.
FREE_MIN=""; FREE_MAX=""
for t in $(seq 0 5 120); do
  c=$(overlap hit_clip "$t" -D teeth=false); k=$(overlap hit_keyboard "$t" -D teeth=false)
  [ "$c" = ERROR ] || [ "$k" = ERROR ] && fail "render error at $t degrees"
  if [ "$c" = free ] && [ "$k" = free ]; then [ -z "$FREE_MIN" ] && FREE_MIN=$t; FREE_MAX=$t
  else echo "  hit at $t degrees: v1 clamp+arm=$c keys=$k"; fi
done
echo "Free tilt range: ${FREE_MIN:-none} to ${FREE_MAX:-none} degrees (5 degree steps)"
for t in 50 55; do
  [ "$(overlap hit_clip $t -D teeth=false)" = free ] && [ "$(overlap hit_keyboard $t -D teeth=false)" = free ] || fail "tilt $t degrees collides"
done
echo "PASS: 50 and 55 degrees are free (60 hits the keys with this cradle, see README)"
