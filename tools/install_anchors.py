"""Put anchors saved from the Anchor Editor back into the packs.

    py tools/install_anchors.py attach-penguin.json      # one animal
    py tools/install_anchors.py attach-all.json          # every animal in one file
    py tools/install_anchors.py attach-all.json --dry    # say what would change, touch nothing

The editor cannot write to disk - it is a page - so it downloads a file and this puts it where
the pipeline reads it: the "attach" block of assets/characters/<animal>/character.json.

Shape written (the editor's own resolution order, kept verbatim so nothing has to agree twice):

    "attach": {
      "_master":            { "head":[x,y], "face":[x,y], "hand_l":[x,y], ... },
      "Adult_Eat":          { ... }        # this whole animation differs
      "Adult_Eat#03":       { ... }        # just that one frame
    }

Coordinates are pixels in the frame's own untrimmed space, which is what character.json's
"space": "untrimmed" already promises.
"""
import io, json, pathlib, sys

REPO = pathlib.Path(__file__).resolve().parent.parent
CHARS = REPO / "assets" / "characters"
POINTS = ("head", "face", "hand_l", "hand_r", "foot_l", "foot_r")


def check(animal, attach):
    """Refuse nonsense rather than write it: a bad anchor is worse than none, because the
    pipeline will happily hang a cookie off it and nobody will know why the paw is wrong."""
    problems = []
    if not isinstance(attach, dict):
        return ["attach is not an object"]
    for key, pts in attach.items():
        if not isinstance(pts, dict):
            problems.append(f"{key}: not an object")
            continue
        for name, v in pts.items():
            if name not in POINTS:
                problems.append(f"{key}.{name}: not one of {', '.join(POINTS)}")
            elif (not isinstance(v, list) or len(v) != 2
                  or not all(isinstance(n, (int, float)) for n in v)):
                problems.append(f"{key}.{name}: should be [x, y], got {v!r}")
    if "_master" not in attach:
        problems.append("no _master - everything else falls back to it, so it must exist")
    return problems


def install(animal, attach, dry):
    cj = CHARS / animal / "character.json"
    if not cj.exists():
        print(f"  {animal}: no character.json at {cj} - SKIPPED")
        return False
    problems = check(animal, attach)
    if problems:
        print(f"  {animal}: REFUSED")
        for p in problems[:8]:
            print(f"      {p}")
        return False
    d = json.loads(cj.read_text(encoding="utf-8"))
    before = json.dumps(d.get("attach") or {}, sort_keys=True)
    after = json.dumps(attach, sort_keys=True)
    if before == after:
        print(f"  {animal}: already exactly this - nothing to do")
        return True
    n_over = len([k for k in attach if k != "_master"])
    print(f"  {animal}: master + {n_over} override(s)"
          + ("   [dry run]" if dry else ""))
    if dry:
        return True
    d["attach"] = attach
    tmp = cj.with_suffix(".json.tmp")
    io.open(tmp, "w", encoding="utf-8").write(json.dumps(d, indent=2) + "\n")
    tmp.replace(cj)
    return True


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry" in sys.argv
    if not args:
        sys.exit(__doc__)
    src = pathlib.Path(args[0])
    if not src.exists():
        sys.exit(f"no such file: {src}")
    blob = json.loads(src.read_text(encoding="utf-8"))
    print(f"--- {src.name}")
    ok = True
    if "all" in blob:
        for animal, attach in blob["all"].items():
            ok = install(animal, attach, dry) and ok
    elif "animal" in blob:
        ok = install(blob["animal"], blob.get("attach") or {}, dry)
    else:
        sys.exit("that file has neither an 'animal' nor an 'all' - is it from the editor?")
    if not dry and ok:
        print("\nInstalled. Run: py tools/build.py   (so the tools see it)")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
