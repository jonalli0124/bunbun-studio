"""Stop room furniture from stealing the sky's colours.

The firmware tints the sky for weather, but only for palette entries it decides are "outside":
a sky-classified colour must appear more than 4 times inside the window box AND have at least
80% of its total occurrences there (main.cpp:552-576). A wall or a picture frame painted in the
same blue as the sky drags that colour under the 80% bar, and it then drops out of BOTH masks —
so a patch of real sky stops responding to weather while the rest clouds over.

This repaints the OFFENDING pixels outside the window to the nearest colour already in the room
that is not sky, so the palette does not grow and nothing inside the window is touched.

    py tools/fix_room_sky.py <room.png> [out.png]
"""
import sys, os
from PIL import Image


def is_sky(c):
    """Byte-for-byte the firmware's classifier (main.cpp, the g_isSky loop)."""
    r, g, b = c[:3]
    return b > 170 and b > r + 80 and g > r + 30 and 90 < g < 235 and r < 150


def window_box(im):
    """The widest run of sky-bearing columns in y10..140 — how the device finds the window."""
    px = im.load()
    cols = [any(is_sky(px[x, y]) for y in range(10, min(141, im.height)))
            for x in range(im.width)]
    best = run = start = bstart = 0
    for x, hit in enumerate(cols + [False]):
        if hit:
            if run == 0:
                start = x
            run += 1
            if run > best:
                best, bstart = run, start
        else:
            run = 0
    if not best:
        return None
    ys = [y for y in range(10, min(141, im.height))
          for x in range(bstart, bstart + best) if is_sky(px[x, y])]
    return (bstart, min(ys), bstart + best - 1, max(ys))


def main(src, dst=None):
    im = Image.open(src).convert("RGB")
    px = im.load()
    box = window_box(im)
    if not box:
        print("no window found — nothing to do (and the device will see no sky at all)")
        return 1
    x0, y0, x1, y1 = box
    inside = lambda x, y: x0 <= x <= x1 and y0 <= y <= y1

    tot, inb = {}, {}
    for y in range(im.height):
        for x in range(im.width):
            c = px[x, y]
            if not is_sky(c):
                continue
            tot[c] = tot.get(c, 0) + 1
            if inside(x, y):
                inb[c] = inb.get(c, 0) + 1

    # GUARD: a window split by a mullion produces two column runs, and the widest-run rule
    # finds only one pane. Repainting then destroys the other pane. If most of the room's sky
    # sits outside the detected box, the box is wrong - refuse rather than repaint.
    total_sky = sum(tot.values())
    inside_sky = sum(inb.values())
    if total_sky and inside_sky / total_sky < 0.75:
        print(f"REFUSING: only {inside_sky}/{total_sky} sky px ({inside_sky/total_sky:.0%}) fall in "
              f"the detected window {x0}..{x1}.")
        print("The window is probably split by a bar or mullion, so the widest-run rule found one")
        print("pane. The device has the same blind spot - regenerate with a SINGLE undivided pane.")
        return 1

    risky = [c for c in tot if inb.get(c, 0) <= 4 or inb.get(c, 0) / tot[c] < 0.8]
    if not risky:
        print(f"window {x0}..{x1} x {y0}..{y1} — every sky colour already passes. Nothing to do.")
        return 0

    safe = [c for c in im.getcolors(1 << 16) and
            [c for _, c in im.getcolors(1 << 16)] if not is_sky(c)]
    if not safe:
        print("no non-sky colour to remap onto"); return 1

    def nearest(c):
        return min(safe, key=lambda s: (s[0]-c[0])**2 + (s[1]-c[1])**2 + (s[2]-c[2])**2)

    remap = {c: nearest(c) for c in risky}
    changed = 0
    for y in range(im.height):
        for x in range(im.width):
            c = px[x, y]
            if c in remap and not inside(x, y):
                px[x, y] = remap[c]
                changed += 1

    for c in risky:
        r, g, b = c; nr, ng, nb = remap[c]
        print(f"  #{r:02x}{g:02x}{b:02x} -> #{nr:02x}{ng:02x}{nb:02x} "
              f"({tot[c]-inb.get(c,0)} px outside the window)")
    dst = dst or src
    im.save(dst)
    print(f"repainted {changed} px outside the window; wrote {os.path.basename(dst)}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None))
