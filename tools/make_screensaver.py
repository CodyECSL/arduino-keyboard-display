#!/usr/bin/env python3
"""Transcode a video into the macropad screensaver file.

Usage: tools/make_screensaver.py INPUT.mp4 OUTPUT.mjpeg [--fps 30] [--quality 2] [--speed 1.0]

Copy OUTPUT.mjpeg to the SD card as /mjpeg/SAVER.mjpeg (FAT32).

The output is a raw stream of baseline 4:2:0 JPEG frames, 480x320, back to back. It is the same
format as the mjpeg files that came with the device. The stream has no timing, so --fps must match
TARGET_FPS in src/screensaver.cpp.
"""
import argparse
import subprocess

WIDTH, HEIGHT = 480, 320


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--quality", type=int, default=2, help="ffmpeg -q:v, 2 (best) to 31 (worst)")
    ap.add_argument("--speed", type=float, default=1.0, help="playback speed factor, 1.5 = 1.5x faster motion")
    args = ap.parse_args()

    # Crop to fill: scale so the short side fits, then cut the long side.
    vf = (f"setpts=PTS/{args.speed},fps={args.fps},scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=increase,"
          f"crop={WIDTH}:{HEIGHT}")
    subprocess.run(
        ["ffmpeg", "-loglevel", "error", "-y", "-i", args.input, "-vf", vf, "-q:v", str(args.quality),
         "-pix_fmt", "yuvj420p", "-c:v", "mjpeg", "-f", "mjpeg", args.output],
        check=True)


if __name__ == "__main__":
    main()
