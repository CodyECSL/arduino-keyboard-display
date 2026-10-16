#!/bin/sh
# Usage: tools/snap_display.sh OUTPUT.jpg
# Captures one frame from the Dell WB7022 webcam.
set -e
out="$1"
[ -n "$out" ] || { echo "usage: $0 OUTPUT.jpg" >&2; exit 2; }
dev=""
for d in /sys/class/video4linux/video*; do
  if grep -q "Dell Webcam WB7022" "$d/name" && [ "$(cat "$d/index")" = "0" ]; then
    dev="/dev/$(basename "$d")"
    break
  fi
done
[ -n "$dev" ] || { echo "Dell WB7022 capture node not found" >&2; exit 1; }
ffmpeg -loglevel error -y -f v4l2 -i "$dev" -frames:v 20 -update 1 "$out"
