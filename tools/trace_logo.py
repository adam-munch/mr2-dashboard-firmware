#!/usr/bin/env python3
"""
Trace logo artwork into the outline table in src/app/logo.c.

The artwork must be light shapes on a dark or transparent background,
made of straight edges, forming one outline without holes (logo_draw()
fills a single closed outline). The script follows the outline, finds the
corners, fits each edge to the anti-aliased pixels for sub-pixel accuracy,
and prints the C table plus LOGO_WIDTH / LOGO_HEIGHT in source pixels.

Usage:
    tools/trace_logo.py logo.png

Needs Pillow and NumPy (pip install pillow numpy).
"""
import sys

import numpy as np
from PIL import Image, ImageDraw

CORNER_EPS = 2.0     # px: how far the outline may stray from a straight edge
MIN_TURN_DEG = 6.0   # smaller bends are aliasing on one edge, not corners


def coverage(path):
    """Per-pixel coverage of the light artwork, 0..1."""
    im = np.asarray(Image.open(path).convert("RGBA")).astype(float) / 255
    return im[..., :3].mean(axis=2) * im[..., 3]


def trace_loops(mask):
    """Closed outlines along pixel edges, as lists of corner points."""
    h, w = mask.shape
    pad = np.pad(mask, 1)
    nxt = {}
    for y, x in zip(*np.nonzero(pad[:-1, 1:-1] != pad[1:, 1:-1])):  # horizontal edges
        if pad[y + 1, x + 1]:
            nxt[(x, y)] = (x + 1, y)
        else:
            nxt[(x + 1, y)] = (x, y)
    for y, x in zip(*np.nonzero(pad[1:-1, :-1] != pad[1:-1, 1:])):  # vertical edges
        if pad[y + 1, x]:
            nxt[(x, y)] = (x, y + 1)
        else:
            nxt[(x, y + 1)] = (x, y)
    loops, seen = [], set()
    for start in sorted(nxt):
        if start in seen:
            continue
        loop, p = [], start
        while p not in seen:
            seen.add(p)
            loop.append(p)
            p = nxt[p]
        loops.append(np.array(loop, float))
    return loops


def rdp(pts, eps):
    """Indices of the Ramer-Douglas-Peucker simplification of an open path."""
    a, b = pts[0], pts[-1]
    d = b - a
    norm = np.hypot(*d)
    if norm == 0:
        dist = np.hypot(*(pts - a).T)
    else:
        dist = np.abs(d[0] * (pts[:, 1] - a[1]) - d[1] * (pts[:, 0] - a[0])) / norm
    i = int(np.argmax(dist))
    if len(pts) < 3 or dist[i] <= eps:
        return [0, len(pts) - 1]
    left, right = rdp(pts[: i + 1], eps), rdp(pts[i:], eps)
    return left[:-1] + [j + i for j in right]


def fit_corners(loop):
    """Corners of a traced outline, from line fits to each edge."""
    # Start far from the centroid so the first corner is a real one.
    loop = np.roll(loop, -int(np.argmax(np.hypot(*(loop - loop.mean(axis=0)).T))), axis=0)
    pts = np.vstack([loop, loop[:1]])
    idx = rdp(pts, CORNER_EPS)[:-1]

    def fit_edges():
        edges = []
        for j, i0 in enumerate(idx):
            i1 = idx[j + 1] if j + 1 < len(idx) else len(pts) - 1
            seg = pts[i0:i1 + 1]
            trim = max(1, len(seg) // 8)  # corners are rounded by the pixels
            core = seg[trim:-trim] if len(seg) > 2 * trim + 2 else seg
            centre = core.mean(axis=0)
            direction = np.linalg.svd(core - centre)[2][0]
            if np.dot(direction, seg[-1] - seg[0]) < 0:
                direction = -direction
            edges.append((centre, direction))
        return edges

    while True:
        edges = fit_edges()
        turns = [np.degrees(np.arccos(np.clip(np.dot(edges[j - 1][1], edges[j][1]), -1, 1)))
                 for j in range(len(idx))]
        j = int(np.argmin(turns))
        if turns[j] >= MIN_TURN_DEG:
            break
        del idx[j]

    corners = []
    for j in range(len(idx)):
        (p1, d1), (p2, d2) = edges[j - 1], edges[j]
        t = np.linalg.solve(np.array([d1, -d2]).T, p2 - p1)
        corners.append(p1 + t[0] * d1)
    return np.array(corners)


def fidelity(points, cov, ss=4):
    """Mean and worst coverage error of the filled outline against the artwork."""
    h, w = cov.shape
    img = Image.new("L", (w * ss, h * ss), 0)
    ImageDraw.Draw(img).polygon([(x * ss, y * ss) for x, y in points], fill=255)
    err = np.abs(np.asarray(img.resize((w, h), Image.BOX)) / 255 - cov)
    return err.mean(), err.max()


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 1
    cov = coverage(argv[1])
    loops = [lp for lp in trace_loops(cov >= 0.5) if len(lp) >= 16]
    if len(loops) != 1:
        print(f"expected one outline without holes, found {len(loops)}", file=sys.stderr)
        return 1
    corners = fit_corners(loops[0])
    corners = np.clip(corners, 0, [cov.shape[1], cov.shape[0]])
    points = np.round(corners).astype(int)
    mean, worst = fidelity(points, cov)

    origin = points.min(axis=0)
    points -= origin
    size = points.max(axis=0)
    print(f"/* {len(points)} points; mean coverage error {mean * 100:.2f} % of a pixel, "
          f"worst {worst:.2f} px */")
    print(f"#define LOGO_WIDTH  {size[0]}")
    print(f"#define LOGO_HEIGHT {size[1]}")
    print("const logo_point_t logo_outline[] = {")
    row = []
    for x, y in points:
        row.append(f"{{{x}, {y}}},")
        if len(" ".join(row)) > 88:
            print("    " + " ".join(row[:-1]))
            row = row[-1:]
    print("    " + " ".join(row))
    print("};")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
