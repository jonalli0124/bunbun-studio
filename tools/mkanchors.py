"""Bake every character frame into one file the anchor editor can open offline.

The editor is a single page like the others, so it cannot read assets/ off disk - the local
server's root is tools/build. Everything it draws has to travel inside it.

Emits tools/build/anchor_data.js:

    window.ANCHOR_DATA = {
      species: {
        penguin: {
          display: "Penguin",
          attach: { ... whatever character.json already holds ... },
          clips: {
            "Adult_Idle":  { kind:"directional", names:["south","south-east",...],
                             frames:{ "south":"data:image/png;base64,...", ... } },
            "Adult_Eat":   { kind:"frames", names:["00","01",...], frames:{...} }
          }
        }, ...
      }
    }

Two clip shapes exist and both matter: a directional clip is one image per compass point, a
frame clip is a numbered strip. The editor scrolls whichever a clip has.
"""
import base64, io, json, os, pathlib

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
CHARS = REPO / "assets" / "characters"
OUT = HERE / "build" / "anchor_data.js"

# the compass order the packs use, so the editor's strip reads round the circle rather than
# alphabetically (east, north, north-east... is nobody's idea of an order)
COMPASS = ["south", "south-east", "east", "north-east",
           "north", "north-west", "west", "south-west"]


def frames_of(clip_dir):
    names = sorted(p.stem for p in clip_dir.glob("*.png"))
    if "south" in names:
        ordered = [n for n in COMPASS if n in names] + [n for n in names if n not in COMPASS]
        return "directional", ordered
    return "frames", names


def main():
    species = {}
    total = 0
    for sp_dir in sorted(CHARS.iterdir()):
        if not sp_dir.is_dir():
            continue
        meta = {}
        cj = sp_dir / "character.json"
        if cj.exists():
            try:
                meta = json.loads(cj.read_text(encoding="utf-8"))
            except Exception:
                meta = {}
        clips = {}
        for clip_dir in sorted(sp_dir.iterdir()):
            if not clip_dir.is_dir():
                continue
            kind, names = frames_of(clip_dir)
            if not names:
                continue
            frames = {}
            for n in names:
                raw = (clip_dir / (n + ".png")).read_bytes()
                frames[n] = "data:image/png;base64," + base64.b64encode(raw).decode("ascii")
                total += 1
            clips[clip_dir.name] = {"kind": kind, "names": names, "frames": frames}
        if not clips:
            continue
        species[sp_dir.name] = {
            "display": meta.get("display") or sp_dir.name.title(),
            "attach": meta.get("attach") or {},
            "space": meta.get("space", "untrimmed"),
            "clips": clips,
        }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    body = json.dumps({"species": species}, separators=(",", ":"))
    io.open(OUT, "w", encoding="utf-8").write("window.ANCHOR_DATA=" + body + ";")
    mb = OUT.stat().st_size / 1024 / 1024
    print("animals %d  clips %d  frames %d  ->  %.2f MB"
          % (len(species), sum(len(s["clips"]) for s in species.values()), total, mb))


if __name__ == "__main__":
    main()
