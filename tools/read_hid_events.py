#!/usr/bin/env python3
"""Print key events from the macropad's HID device. Needs read access to /dev/input (use sudo)."""
import argparse
import select
import struct
import time

KEYS = {
    113: "MUTE", 114: "VOLUMEDOWN", 115: "VOLUMEUP", 163: "NEXTSONG", 164: "PLAYPAUSE", 165: "PREVIOUSSONG",
    # Keyboard keys sent by the Apps and AI Mode pages (Linux input event codes).
    28: "ENTER", 15: "TAB", 57: "SPACE", 103: "UP", 99: "SYSRQ(PRINTSCREEN)",
    29: "LEFTCTRL", 42: "LEFTSHIFT", 56: "LEFTALT", 125: "LEFTMETA",
    5: "4", 20: "T", 44: "Z", 46: "C", 47: "V",
}
EV_KEY = 1
EVENT = struct.Struct("llHHi")  # 64-bit Linux struct input_event


def find_event_files(name_part):
    found = []
    block = {}
    with open("/proc/bus/input/devices") as f:
        for line in list(f) + ["\n"]:
            if line.strip() == "":
                if name_part in block.get("N", "").lower():
                    for tok in block.get("H", "").split():
                        if tok.startswith("event"):
                            found.append("/dev/input/" + tok)
                block = {}
            elif line[0] in "NH":
                block[line[0]] = line
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--name", default="macropad")
    args = ap.parse_args()

    if not find_event_files(args.name.lower()):
        raise SystemExit(f"no input device with '{args.name}' in its name")
    # The device disappears when the board resets and comes back as a new event file.
    # Keep one open file per path and rescan every loop.
    files = {}
    end = time.time() + args.seconds
    while time.time() < end:
        for path in find_event_files(args.name.lower()):
            if path not in files:
                try:
                    files[path] = open(path, "rb", buffering=0)
                    print("watching", path, flush=True)
                except OSError:
                    pass
        if not files:
            time.sleep(0.2)
            continue
        ready, _, _ = select.select(list(files.values()), [], [], 0.2)
        for f in ready:
            try:
                data = f.read(EVENT.size)
            except OSError:  # ENODEV: device unplugged or reset
                path = next(p for p, g in files.items() if g is f)
                print("lost", path, flush=True)
                f.close()
                del files[path]
                continue
            if len(data) < EVENT.size:
                continue
            _, _, etype, code, value = EVENT.unpack(data)
            if etype == EV_KEY:
                state = {0: "up", 1: "down", 2: "repeat"}.get(value, str(value))
                print(f"KEY_{KEYS.get(code, code)} {state}", flush=True)


main()
