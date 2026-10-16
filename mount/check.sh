#!/usr/bin/env bash
# Checks for v1_printed/keyboard_mount_v1.scad. Exits non-zero on the first failure.
set -u
cd "$(dirname "$0")"
OUT=out
SCAD=v1_printed/keyboard_mount_v1.scad
[ -f ref/rim.stl ] || { echo "ref/rim.stl is missing. See build.sh." >&2; exit 1; }
mkdir -p "$OUT"
fail() { echo "FAIL: $*" >&2; exit 1; }

# Render a part to STL and print its bounding box as "xmin xmax ymin ymax zmin zmax".
bbox() {
  openscad -o "$OUT/$1.stl" -D "part=\"$1\"" $SCAD 2>"$OUT/$1.log" \
    || { cat "$OUT/$1.log" >&2; fail "render $1"; }
  python3 -I - "$OUT/$1.stl" <<'PY'
import re, sys
v = [tuple(map(float, m)) for m in re.findall(r'vertex\s+(\S+)\s+(\S+)\s+(\S+)', open(sys.argv[1]).read())]
print(*[f"{f(p[i] for p in v):.2f}" for i in range(3) for f in (min, max)])
PY
}

# Load the bounding box of a part into x0 x1 y0 y1 z0 z1. Stops the script if the render fails.
box() {
  local out
  out=$(bbox "$1") || exit 1
  read -r x0 x1 y0 y1 z0 z1 <<<"$out"
}

near() { python3 -I -c "import sys; sys.exit(0 if abs($1-$2)<=${3:-0.05} else 1)"; }

# Task 1: the rim import works.
box rim
near "$z1-$z0" 12.4 || fail "rim depth"
echo "PASS: rim depth"

# Task 2: the fit-test piece. Bridge sits on the wall top; the outer plate reaches down 10 mm.
box fit_test
near "$z0" 6.87 || fail "fit_test lowest point $z0, want 6.87 (wall top minus plate_drop)"
near "$z1" 19.87 || fail "fit_test top $z1, want 19.87"
near "$x1-$x0" 94.71 0.1 || fail "fit_test length"   # tab_pitch + tab_w = 85.41 + 9.3
near "$y1-$y0" 7.4 0.05 || fail "fit_test y range, want 7.4 (tab 2.5 + clamp_gap 2.4 + plate 2.5)"
echo "PASS: fit_test size"

# Task 3 (split): the clamp carries the rail; the arm carries the hinge disc.
box clamp
near "$z0" 6.87 || fail "clamp lowest point $z0, want 6.87"
near "$z1" 19.87 || fail "clamp top $z1, want 19.87 (rail must stop at the bridge top so it can lie on the bed)"
near "$y1" 5.5 || fail "clamp y max $y1, want 5.5 (plate 2.5 + rail 3.0)"
echo "PASS: clamp size"
box arm
near "$z0" 10.7 || fail "arm lowest point $z0, want 10.7 (the foot must stand on the bed)"
near "$z1" 60 1.5 || fail "arm top $z1, want about 60 (hinge_z 45 + r_out 15)"
near "$x1-$x0" 16 0.1 || fail "arm width , want 16 (foot)"
echo "PASS: arm size"

# Task 4: the cradle. Frame: X along the long edge, Y up the screen, Z toward the user. Front face at Z = 5, back at -7.4.
# The hinge disc (axis_lz -26.074, r 15) is the lowest part. The rim is 95 x 63. The fins stand 11.95 from the middle.
box cradle
near "$x0" -47.5 0.05 || fail "cradle left edge $x0, want -47.5 (rim width 95)"
near "$x1" 47.5 0.05 || fail "cradle right edge $x1, want 47.5"
near "$y0" -31.5 0.05 || fail "cradle bottom edge $y0, want -31.5 (rim height 63)"
near "$y1" 31.5 0.05 || fail "cradle top edge $y1, want 31.5"
near "$z1" 5 || fail "cradle front face at $z1, want 5 (screen faces the user)"
near "$z0" -41.07 0.05 || fail "cradle lowest point $z0, want -41.07 (axis_lz -26.074 minus r_out 15)"
echo "PASS: cradle size"

# Task 4b: the cradle matches Cradlev1.stl, the cradle the user made and verified. The tool turns the SCAD frame into the
# STL frame (x = Y - 2.5, y = X + 7.5, z = 5 - Z), then compares the bounding boxes and the volume that is in one and not the other.
python3 -I tools/compare_stl.py v1_printed/Cradlev1.stl "$OUT/cradle.stl" --matrix 0 1 0 1 0 0 0 0 -1 -2.5 7.5 5 \
  --max-box 0.1 --max-only 0.06 > "$OUT/cradle_vs_v1.txt" || { cat "$OUT/cradle_vs_v1.txt" >&2; fail "cradle differs from Cradlev1.stl"; }
echo "PASS: cradle matches Cradlev1.stl ($(grep 'ray sample' "$OUT/cradle_vs_v1.txt" | sed 's/.*pitch [0-9.]*: //'))"

# Task 4c: the clamp and the arm still build the same shapes as the printed files. The STL export is not byte for byte
# repeatable (triangle order changes between runs), so this compares the shapes.
for f in clamp arm; do
  python3 -I tools/compare_stl.py "v1_printed/$f.stl" "$OUT/$f.stl" --max-box 0.01 --max-only 0.02 > "$OUT/${f}_vs_v1.txt" \
    || { cat "$OUT/${f}_vs_v1.txt" >&2; fail "$f no longer builds the printed shape"; }
done
echo "PASS: clamp and arm builds match the printed files"
# Task 5: clearance. A part is "free" when OpenSCAD reports an empty intersection.
is_free() {  # $1 = part, $2 = theta
  openscad -o "$OUT/hit.stl" -D "part=\"$1\"" -D "theta=$2" -D "teeth=false" $SCAD 2>&1 | grep -q "empty"
}
FREE_MIN=""; FREE_MAX=""
for t in $(seq 0 5 120); do
  if is_free hit_clip "$t" && is_free hit_keyboard "$t"; then
    [ -z "$FREE_MIN" ] && FREE_MIN=$t
    FREE_MAX=$t
  else
    echo "  hit at $t degrees: clip=$(is_free hit_clip "$t" && echo free || echo HIT) keyboard=$(is_free hit_keyboard "$t" && echo free || echo HIT)"
  fi
done
echo "Free tilt range: ${FREE_MIN:-none} to ${FREE_MAX:-none} degrees (5 degree steps)"
[ -n "$FREE_MIN" ] || fail "no free tilt angle"
for t in 50 55; do
  is_free hit_clip "$t" && is_free hit_keyboard "$t" || fail "tilt $t degrees collides"
done
echo "PASS: 50 and 55 degrees are free (60 hits the keys with this cradle, see README)"
