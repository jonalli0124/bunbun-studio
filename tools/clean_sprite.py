#!/usr/bin/env python3
"""
clean_sprite.py — normalise PixelLab mannequin output for the bunbun pak.

Two passes, in this order:

  1. QUANTISE   collapse near-identical colours into one representative value.
                PixelLab returns e.g. #f9e5c2 / #f8e5c0 / #f7e5c5 / #f6e4c1 —
                four values within a couple of units. These are quantisation
                noise, not shading, and they must become ONE value or the
                palette swap has nothing to key on.

  2. DEOUTLINE  remove dark pixels that touch transparency. PixelLab adds an
                outline regardless of the "Lineless" setting; the bunbun style
                is defined by colour against transparency, not by a contour.

Both are deterministic and idempotent. Run on a folder of PNGs; every frame of
every part goes through the same path, so parts stay in the same palette space.

Usage:
    python clean_sprite.py IN_DIR OUT_DIR [--dist 12] [--dark 150]
                           [--keep-outline] [--pad 96] [--report]
"""

import argparse
import json
import os
import sys
from collections import Counter

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow required:  pip install pillow")


# ---------------------------------------------------------------- quantise

def _dist2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def build_palette(counter, max_dist):
    """Greedy merge: most common colour wins, absorbs everything within
    max_dist. Deterministic because we walk in descending count order."""
    thresh = max_dist ** 2
    reps, mapping = [], {}
    for col, _n in counter.most_common():
        for rep in reps:
            if _dist2(col, rep) <= thresh:
                mapping[col] = rep
                break
        else:
            reps.append(col)
            mapping[col] = col
    return mapping, reps


def collect_colours(paths):
    """One palette across the whole set, so every frame and every part share
    the same values. Per-frame palettes would drift between rotations."""
    c = Counter()
    for p in paths:
        im = Image.open(p).convert("RGBA")
        px = im.load()
        w, h = im.size
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a > 127:
                    c[(r, g, b)] += 1
    return c


# --------------------------------------------------------------- deoutline

NEIGH8 = [(-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)]


def strip_outline(im, dark_sum):
    """Erase dark pixels adjacent to transparency, repeatedly, until no more
    qualify. Iterating matters: a 2px outline needs two passes, and stopping
    after one leaves a half-ring that looks worse than the original."""
    px = im.load()
    w, h = im.size
    removed = 0
    while True:
        doomed = []
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a <= 127 or (r + g + b) >= dark_sum:
                    continue
                for dx, dy in NEIGH8:
                    nx, ny = x + dx, y + dy
                    if not (0 <= nx < w and 0 <= ny < h) or px[nx, ny][3] <= 127:
                        doomed.append((x, y))
                        break
        if not doomed:
            break
        for x, y in doomed:
            px[x, y] = (0, 0, 0, 0)
        removed += len(doomed)
    return removed


# ------------------------------------------------------------------ helpers

def pad_to(im, size):
    """Centre into a square canvas. The firmware's spriteBlit centres the
    untrimmed canvas on the feet point, so every part must share one canvas
    size or the offsets mean different things."""
    if im.size == (size, size):
        return im
    if im.width > size or im.height > size:
        raise ValueError(f"{im.size} does not fit in {size}x{size}")
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(im, ((size - im.width) // 2, (size - im.height) // 2))
    return out


def opaque_px(im):
    """Count of pixels the pak will actually draw. Used to prove a normalise
    didn't quietly shave the character against the canvas edge."""
    return sum(im.getchannel("A").histogram()[128:])


def feet_line(im):
    """y of the last opaque row — the ground the character stands on."""
    bb = bbox_alpha(im)
    return None if bb is None else bb[1] + bb[3]


def target_feet_line(paths, size):
    """Where the feet belong in a `size` canvas, learned from the frames that
    already ARE that size. PixelLab expands the canvas for some animations; the
    rotations are the authority on where the floor is, so we measure them rather
    than assume the centre. Falls back to a 6px gap if nothing is at size yet."""
    feet = []
    for p in paths:
        im = Image.open(p).convert("RGBA")
        if im.size == (size, size):
            f = feet_line(im)
            if f is not None:
                feet.append(f)
    if not feet:
        return size - 6
    feet.sort()
    return feet[len(feet) // 2]


def normalise_feet(im, size, target):
    """Crop/pad to size x size with the feet landing on `target`.

    Horizontal: symmetric, so the character keeps its offset from the canvas
    centre. Vertical: driven entirely by the feet, never the canvas centre —
    centring a 120px animation frame inside 96px floats the character off the
    floor, which is the whole failure this exists to prevent.

    Raises if the transform would clip any opaque pixel."""
    if im.size == (size, size) and feet_line(im) == target:
        return im, False
    before = opaque_px(im)
    dx = (im.width - size) // 2
    f = feet_line(im)
    dy = 0 if f is None else f - target
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(im, (-dx, -dy))
    after = opaque_px(out)
    if after != before:
        raise ValueError(
            f"normalising {im.size} -> {size}x{size} (feet {f} -> {target}) "
            f"clips {before - after} opaque px; raise --norm or check the frame")
    return out, True


def bbox_alpha(im, thresh=128):
    """Trim box the pak will use. Reported so anchor drift between frames is
    visible before anything reaches the device."""
    px = im.load()
    w, h = im.size
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if px[x, y][3] >= thresh:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return (min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)


def hexs(c):
    return "#%02x%02x%02x" % c[:3]


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("indir")
    ap.add_argument("outdir")
    ap.add_argument("--dist", type=int, default=12,
                    help="max RGB distance merged into one colour (default 12)")
    ap.add_argument("--dark", type=int, default=150,
                    help="r+g+b below this counts as outline (default 150)")
    ap.add_argument("--keep-outline", action="store_true")
    ap.add_argument("--pad", type=int, default=0,
                    help="centre into a square canvas of this size, e.g. 96")
    ap.add_argument("--norm", type=int, default=0,
                    help="normalise every frame to this square canvas, anchored "
                         "on the FEET not the centre, e.g. 96. Handles frames "
                         "larger than the target (PixelLab expands the canvas "
                         "for some animations), which --pad cannot.")
    ap.add_argument("--report", action="store_true",
                    help="write clean_report.json alongside the output")
    args = ap.parse_args()

    paths = []
    for root, _dirs, files in os.walk(args.indir):
        for f in sorted(files):
            if f.lower().endswith(".png"):
                paths.append(os.path.join(root, f))
    if not paths:
        sys.exit(f"no PNGs under {args.indir}")

    print(f"{len(paths)} frames")

    before = collect_colours(paths)
    mapping, reps = build_palette(before, args.dist)
    print(f"palette: {len(before)} -> {len(reps)} colours (dist {args.dist})")

    report = {"input": args.indir, "dist": args.dist, "dark": args.dark,
              "frames": [], "palette": []}

    feet_target = None
    if args.norm:
        feet_target = target_feet_line(paths, args.norm)
        report["norm"] = {"size": args.norm, "feet_line": feet_target}
        print(f"normalise: {args.norm}x{args.norm}, feet on y={feet_target}")

    total_removed = 0
    normalised = 0
    for p in paths:
        im = Image.open(p).convert("RGBA")
        px = im.load()
        w, h = im.size

        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a > 127:
                    px[x, y] = mapping[(r, g, b)] + (255,)
                else:
                    px[x, y] = (0, 0, 0, 0)   # kill semi-transparent fringe

        removed = 0 if args.keep_outline else strip_outline(im, args.dark)
        total_removed += removed

        rel = os.path.relpath(p, args.indir)

        did_norm = False
        if args.norm:
            try:
                im, did_norm = normalise_feet(im, args.norm, feet_target)
            except ValueError as e:
                sys.exit(f"{rel}: {e}")
            normalised += did_norm
        elif args.pad:
            im = pad_to(im, args.pad)

        dst = os.path.join(args.outdir, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        im.save(dst)

        bb = bbox_alpha(im)
        report["frames"].append({"file": rel, "outline_px": removed,
                                 "bbox": bb, "canvas": list(im.size),
                                 "normalised": did_norm})
        flag = "  norm" if did_norm else ""
        print(f"  {rel:44} outline -{removed:4}  bbox {bb}{flag}")

    after = collect_colours([os.path.join(args.outdir,
                                          os.path.relpath(p, args.indir))
                             for p in paths])
    print(f"\nfinal palette ({len(after)}):")
    for col, n in after.most_common():
        role = ""
        if len(after) <= 8:
            role = "  <- candidate key colour" if n < 100 else ""
        print(f"  {hexs(col)}  {n:5}{role}")
        report["palette"].append({"hex": hexs(col), "count": n})

    print(f"\noutline pixels removed: {total_removed}")
    if args.norm:
        print(f"frames normalised to {args.norm}x{args.norm}: {normalised}")
        fl = [f["bbox"][1] + f["bbox"][3] for f in report["frames"] if f["bbox"]]
        print(f"feet line after normalise: {min(fl)}-{max(fl)} (target {feet_target})")

    # bbox spread across frames — anchor drift shows up here first
    bbs = [f["bbox"] for f in report["frames"] if f["bbox"]]
    if bbs:
        ws = [b[2] for b in bbs]
        hs = [b[3] for b in bbs]
        print(f"bbox width  {min(ws)}-{max(ws)}   height {min(hs)}-{max(hs)}")
        if max(ws) - min(ws) > 4 or max(hs) - min(hs) > 4:
            print("  ^ spread >4px: check rotations for drift before packing")

    if args.report:
        rp = os.path.join(args.outdir, "clean_report.json")
        with open(rp, "w") as fh:
            json.dump(report, fh, indent=2)
        print(f"\nwrote {rp}")


if __name__ == "__main__":
    main()
