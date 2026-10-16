"""Compare two STL files: bounding box, volume, and the volume that is in one but not the other.

Usage:
    python3 -I mount/tools/compare_stl.py REF.stl TEST.stl [--matrix a b c d e f g h i tx ty tz] [--pitch 0.2]
                                           [--max-box MM] [--max-only CM3]

--matrix moves TEST into the frame of REF: p' = M p + t, M row by row. For the SCAD cradle against
Cradlev1.stl use the cradle-to-STL frame change:  x = Y - 2.5, y = X + 7.5, z = 5 - Z, that is
    --matrix 0 1 0  1 0 0  0 0 -1  -2.5 7.5 5
--max-box and --max-only make it exit 1 when a bounding box edge moves more than MM, or when either
"only in" volume is more than CM3 (this is what check.sh uses).
The difference is found by shooting rays along z on a grid and comparing the solid intervals.
It is a sampled measure with an error of about the pitch on every face, so read small numbers as zero.
"""
import argparse
import re
import numpy as np


def load(path):
    data = open(path, "rb").read()
    if data[:5] == b"solid" and b"facet" in data[:400]:
        v = np.array(re.findall(rb"vertex\s+(\S+)\s+(\S+)\s+(\S+)", data), dtype=float)
        return v.reshape(-1, 3, 3)
    n = int(np.frombuffer(data[80:84], "<u4")[0])
    rec = np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")])
    return np.frombuffer(data[84:], dtype=rec, count=n)["v"].reshape(-1, 3, 3).astype(float)


def volume(t):
    return float(np.einsum("ij,ij->i", t[:, 0], np.cross(t[:, 1], t[:, 2])).sum() / 6)


def columns(t, xs, ys):
    """For each grid column, the sorted z where the ray crosses a triangle."""
    cols = {}
    for tri in t:
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = tri
        d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(d) < 1e-12:
            continue
        i0, i1 = np.searchsorted(xs, min(x0, x1, x2)), np.searchsorted(xs, max(x0, x1, x2), "right")
        j0, j1 = np.searchsorted(ys, min(y0, y1, y2)), np.searchsorted(ys, max(y0, y1, y2), "right")
        if i0 >= i1 or j0 >= j1:
            continue
        gx, gy = np.meshgrid(xs[i0:i1], ys[j0:j1], indexing="ij")
        a = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / d
        b = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / d
        c = 1 - a - b
        m = (a >= 0) & (b >= 0) & (c >= 0)
        for i, j in zip(*np.nonzero(m)):
            cols.setdefault((i0 + i, j0 + j), []).append(a[i, j] * z0 + b[i, j] * z1 + c[i, j] * z2)
    return cols


def intervals(zs):
    zs = sorted(zs)
    return list(zip(zs[0::2], zs[1::2]))


def overlap(a, b):
    total, i, j = 0.0, 0, 0
    while i < len(a) and j < len(b):
        lo, hi = max(a[i][0], b[j][0]), min(a[i][1], b[j][1])
        total += max(0.0, hi - lo)
        if a[i][1] < b[j][1]:
            i += 1
        else:
            j += 1
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("test")
    ap.add_argument("--matrix", nargs=12, type=float)
    ap.add_argument("--pitch", type=float, default=0.2)
    ap.add_argument("--max-box", type=float)
    ap.add_argument("--max-only", type=float)
    args = ap.parse_args()
    r, t = load(args.ref), load(args.test)
    if args.matrix:
        m = np.array(args.matrix)
        M, off = m[:9].reshape(3, 3), m[9:]
        t = t @ M.T + off
        if np.linalg.det(M) < 0:
            t = t[:, [0, 2, 1]]   # a mirror turns the triangles inside out; swap two corners to fix the sign
    ok = True
    box_move = np.abs(np.concatenate([r.reshape(-1, 3).min(0) - t.reshape(-1, 3).min(0), r.reshape(-1, 3).max(0) - t.reshape(-1, 3).max(0)])).max()
    print(f"largest bounding box edge difference: {box_move:.3f} mm")
    if args.max_box is not None and box_move > args.max_box:
        ok = False
    for name, tri in (("ref ", r), ("test", t)):
        p = tri.reshape(-1, 3)
        print(f"{name}: {len(tri)} triangles, box {np.round(p.min(0), 2).tolist()} .. {np.round(p.max(0), 2).tolist()}, volume {volume(tri) / 1000:.3f} cm3")
    p = np.vstack([r.reshape(-1, 3), t.reshape(-1, 3)])
    xs = np.arange(p[:, 0].min(), p[:, 0].max(), args.pitch) + 0.0137 * args.pitch
    ys = np.arange(p[:, 1].min(), p[:, 1].max(), args.pitch) + 0.0291 * args.pitch
    cr, ct = columns(r, xs, ys), columns(t, xs, ys)
    only_r = only_t = both = 0.0
    worst = []
    for k in set(cr) | set(ct):
        a, b = intervals(cr.get(k, [])), intervals(ct.get(k, []))
        la, lb = sum(h - l for l, h in a), sum(h - l for l, h in b)
        o = overlap(a, b)
        both += o
        only_r += la - o
        only_t += lb - o
        if la - o > 0.5 or lb - o > 0.5:
            worst.append((xs[k[0]], ys[k[1]], la - o, lb - o))
    cell = args.pitch ** 2
    print(f"ray sample, pitch {args.pitch}: shared {both * cell / 1000:.3f} cm3, only in ref {only_r * cell / 1000:.3f} cm3, only in test {only_t * cell / 1000:.3f} cm3")
    if args.max_only is not None and max(only_r, only_t) * cell / 1000 > args.max_only:
        ok = False
    if worst:
        w = np.array(worst)
        print(f"columns that differ by more than 0.5 mm: {len(w)}; x {w[:, 0].min():.1f}..{w[:, 0].max():.1f}, y {w[:, 1].min():.1f}..{w[:, 1].max():.1f}")
    raise SystemExit(0 if ok else 1)


main()
