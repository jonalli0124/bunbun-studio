"""Build the self-contained data blob for the shareable Scene Builder.

Everything the page needs is embedded: every clip frame, every prop, every room
background. No network fetches, so the published page works offline and inside a
strict CSP.
"""
import os, io, json, base64
from PIL import Image

from repo_paths import species_dir, OBJECTS, ROOMS as ROOMDIR, TOOLS
CLEAN = species_dir()
PROPS = OBJECTS
MARKS = OBJECTS
ROOMS = ROOMDIR
OUT   = os.path.join(TOOLS, 'build', 'scene_data.js')

# from components/bunbun/game.h — the real gameplay window
SCR_W, SCR_H, FLOOR_Y = 320, 240, 200
PHASES = {
    "baby":  {"scale": 0.78 * 1.45, "bounds": [34, 288, FLOOR_Y - 26, FLOOR_Y + 8],
              "prefix": "Baby_",  "travel": "Crawl"},
    "adult": {"scale": 1.00 * 1.45, "bounds": [26, 298, FLOOR_Y - 24, FLOOR_Y + 30],
              "prefix": "Adult_", "travel": "Walk"},
}

def uri(path, optimise=True):
    im = Image.open(path).convert("RGBA")
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=optimise)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode(), im

def alpha_box(im):
    bb = im.getchannel("A").point(lambda v: 255 if v > 127 else 0).getbbox()
    return list(bb) if bb else None

data = {"scr": {"w": SCR_W, "h": SCR_H, "floor": FLOOR_Y},
        "phases": PHASES, "clips": {}, "props": {}, "rooms": {}}

for name in sorted(os.listdir(CLEAN)):
    d = os.path.join(CLEAN, name)
    if not os.path.isdir(d):
        continue
    files = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    if not files:
        continue
    frames, feet = [], []
    for f in files:
        u, im = uri(os.path.join(d, f))
        bb = alpha_box(im)
        frames.append(u)
        if bb:
            feet.append(bb[3])
    data["clips"][name] = {
        "frames": frames,
        "feet": max(feet) if feet else 90,      # baseline: lowest opaque row
        "size": im.size[0],
    }

for kind in ("marks", "props", "lights"):
    folder = os.path.join(OBJECTS, kind)
    if not os.path.isdir(folder):
        continue
    for f in sorted(os.listdir(folder)):
        if not f.endswith(".png"):
            continue
        u, im = uri(os.path.join(folder, f))
        data["props"][os.path.splitext(f)[0]] = {
            "img": u, "w": im.width, "h": im.height, "kind": kind[:-1]}

for f in sorted(os.listdir(ROOMS)):
    if not f.endswith(".png") or f.startswith("_"):
        continue
    u, im = uri(os.path.join(ROOMS, f))
    if im.size != (SCR_W, SCR_H):
        continue
    data["rooms"][os.path.splitext(f)[0]] = u

blob = json.dumps(data, separators=(",", ":"))
with open(OUT, "w", encoding="utf-8") as fh:
    fh.write("window.SCENE_DATA=" + blob + ";\n")

print(f"clips {len(data['clips'])}  props {len(data['props'])}  rooms {len(data['rooms'])}")
print(f"frames {sum(len(c['frames']) for c in data['clips'].values())}")
print(f"{OUT}  {os.path.getsize(OUT)/1024/1024:.2f} MB")
