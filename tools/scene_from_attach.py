"""Turn an attach-editor bundle into something you can push to a bunbun.

    py tools/scene_from_attach.py my-scene.zip
    py tools/scene_from_attach.py my-scene.zip --ip <device-ip>

It does the three things that stand between a finished scene and a device:

  1. INSTALLS THE ART where the pak is built from. This is the step that has silently defeated
     every scene made outside the builder: the pak comes from
     Downloads/bunbun_web_test/assets, and scene_push reads an imported sprite's geometry from
     that same folder. Art anywhere else can be placed, converted, pushed, accepted - and draw
     nothing. Names are shortened with the SAME deterministic rule scene_push uses, so the file
     on disk and the name in the scene cannot drift apart.
  2. CONVERTS the export into the shape scene_push expects (items / polys / env).
  3. TELLS YOU THE TWO REMAINING COMMANDS, or runs the push itself with --ip.

Two things deliberately do NOT travel, because the device owns them:
  TIME OF DAY - the device's light follows the real clock. That is the product.
  RAIN AS A STATE - it travels as a cadence ("this room gets showers"), never as
     "it is raining now". The device keeps rolling its own dice.
"""
import argparse, hashlib, json, pathlib, shutil, subprocess, sys, zipfile, io

PAK_NAME_MAX = 31                      # scene_push.PAK_NAME_MAX - keep these in step
ASSETS = pathlib.Path(__file__).resolve().parent.parent / "pak-factory" / "assets"
ITEMS = ASSETS / "items"
ROOMS = ASSETS / "rooms"
DEVICE_PROP_LIMIT = 24                 # the device holds 24 props


def pak_safe(name: str) -> str:
    """Byte-for-byte scene_push.pak_safe. Two names sharing their first 31 characters are the
    same name to the pak, and the second is simply unreachable."""
    if len(name) <= PAK_NAME_MAX:
        return name
    keep = PAK_NAME_MAX - 5
    return name[:keep] + "-" + hashlib.sha1(name.encode()).hexdigest()[:4]


def stem_for(name: str) -> str:
    """The filename scene_push will look for in assets/items."""
    return pak_safe("items/" + name.split("/")[-1])[len("items/"):]


def load(src: pathlib.Path):
    """Accept either the ZIP bundle or a bare attach.json beside its assets."""
    if src.suffix.lower() == ".zip":
        z = zipfile.ZipFile(src)
        aj = next(n for n in z.namelist() if n.endswith("attach.json"))
        blob = json.loads(z.read(aj))
        art = {}
        for n in z.namelist():
            if "/assets/" in n and n.endswith(".png"):
                art[pathlib.PurePosixPath(n).name] = z.read(n)
                art[("ROOM:" if "/assets/room/" in n else "") +
                    pathlib.PurePosixPath(n).stem] = z.read(n)
        return blob, art
    blob = json.loads(src.read_text(encoding="utf-8"))
    art = {}
    base = src.parent
    for sub in ("assets/props", "assets/marks", "assets/lights", "assets/room"):
        d = base / sub
        if d.is_dir():
            for p in d.glob("*.png"):
                art[p.stem] = p.read_bytes()
                if sub.endswith("room"):
                    art["ROOM:" + p.stem] = p.read_bytes()
    return blob, art


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bundle", help="the .zip the editor made (or an attach.json)")
    ap.add_argument("--ip", help="push to this device when the conversion succeeds")
    ap.add_argument("--out", help="where to write the converted scene")
    ap.add_argument("--dry-run", action="store_true", help="convert and check, install nothing")
    args = ap.parse_args()

    src = pathlib.Path(args.bundle)
    if not src.exists():
        sys.exit(f"no such file: {src}")
    blob, art = load(src)
    scene = blob.get("scene") or {}
    if not scene:
        sys.exit("that file has no 'scene' block - it is not an attach-editor export")

    print(f"scene: {blob.get('clip')} in room '{scene.get('room')}'")
    problems, notes = [], []

    # ---- 1. install the art ------------------------------------------------------------
    installed = []
    used = sorted({o["object"] for o in scene.get("objects", [])})
    for name in used:
        data = art.get(name)
        if data is None:
            problems.append(f"no art for '{name}' in the bundle - it cannot reach the device")
            continue
        dest = ITEMS / (stem_for(name) + ".png")
        if args.dry_run:
            installed.append(f"would install {dest.name}")
        else:
            ITEMS.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            installed.append(dest.name)
        if stem_for(name) != name.split("/")[-1]:
            notes.append(f"'{name}' is too long for a pak name; installed as '{stem_for(name)}'")

    room = scene.get("room")
    if room:
        rd = art.get("ROOM:" + room)
        if rd and not args.dry_run:
            ROOMS.mkdir(parents=True, exist_ok=True)
            (ROOMS / f"{room}.png").write_bytes(rd)
            installed.append(f"{room}.png (room)")

    # ---- 2. convert --------------------------------------------------------------------
    lamps = {s["object"]: s for s in (scene.get("lamps") or {}).get("sources", [])}
    items = []
    for o in scene.get("objects", []):
        it = {"a": o["object"], "x": int(round(o["x"])), "y": int(round(o["y"])),
              "sx": round(float(o.get("scale", 1)), 4),
              "sy": round(float(o.get("scale", 1)), 4),
              "flip": bool(o.get("flip")), "z": int(o.get("z", o["y"]))}
        if o.get("rotation"):
            it["rot"] = int(o["rotation"])
        L = lamps.get(o["object"])
        if L and abs(L["x"] - o["x"]) < 2 and abs(L["y"] - o["y"]) < 2:
            it["lamp"] = True
            if float(L.get("power", 1)) != 1.0:
                it["lampPower"] = float(L["power"])
            if float(L.get("spread", 1)) != 1.0:
                it["lampSize"] = float(L["spread"])
            if isinstance(L.get("bulbY"), (int, float)):
                it["bulbY"] = L["bulbY"]
        items.append(it)

    if len(items) > DEVICE_PROP_LIMIT:
        problems.append(f"{len(items)} objects, and the device holds {DEVICE_PROP_LIMIT} - "
                        f"{len(items) - DEVICE_PROP_LIMIT} would be dropped")

    polys = []
    # `floor` was the floor LINE (a number) before walkable zones existed; `walkable` is the
    # rectangle list. Accept either, so scenes made before zones still convert.
    walk = scene.get("walkable")
    if walk is None:
        f = scene.get("floor")
        walk = f if isinstance(f, list) else []
    for f in walk:
        polys.append({"floor": True, "pts": [[f["x0"], f["y0"]], [f["x1"], f["y0"]],
                                             [f["x1"], f["y1"]], [f["x0"], f["y1"]]]})
    for k in scene.get("keepOut") or []:
        polys.append({"floor": False, "pts": [[k["x0"], k["y0"]], [k["x1"], k["y0"]],
                                              [k["x1"], k["y1"]], [k["x0"], k["y1"]]]})
    if not any(p["floor"] for p in polys):
        notes.append("no walkable floor drawn - the device keeps its own default band")

    w = scene.get("weather") or {}
    env = {"clouds": bool(w.get("clouds")), "cloudN": int(w.get("count", 3)),
           "cloudSize": float(w.get("size", 1)), "cloudSpeed": float(w.get("speed", 1)),
           "cloudAlpha": float(w.get("opacity", 0.72)),
           "cloudStyle": w.get("style", "shipped"),
           "rain": bool(w.get("rain")),
           "lightScale": float((scene.get("lamps") or {}).get("sizeScale", 1))}
    if w.get("rain"):
        notes.append("rain travels as a CADENCE - the room gets more showers; it does not "
                     "start raining on arrival")
    if scene.get("light", {}).get("minutes") is not None:
        notes.append(f"time of day ({scene['light'].get('timeOfDay')}) is NOT sent - the device's "
                     f"light follows the real clock")
    if abs(env["cloudAlpha"] - 0.60) > 0.001:
        notes.append(f"cloud opacity {int(env['cloudAlpha']*100)}% differs from the firmware's "
                     f"own 60% - it will travel and override it")

    affords = [(o["object"], o["can"]) for o in scene.get("objects", []) if o.get("can")]
    if affords:
        notes.append("affordances recorded in the scene file: " +
                     "; ".join(f"{n} may be {'/'.join(c)}" for n, c in affords))

    out = {"room": room, "items": items, "polys": polys, "env": env,
           "_from": {"tool": "attach-editor", "clip": blob.get("clip"),
                     "affordances": {n: c for n, c in affords}}}

    dest = pathlib.Path(args.out) if args.out else src.with_suffix(".scene.json")
    dest.write_text(json.dumps(out, indent=1), encoding="utf-8")

    # ---- 3. report ---------------------------------------------------------------------
    print(f"\ninstalled {len(installed)} file(s) into {ITEMS}")
    for i in installed:
        print("   ", i)
    print(f"\nwrote {dest}")
    print(f"  {len(items)} object(s), {sum(1 for p in polys if p['floor'])} floor, "
          f"{sum(1 for p in polys if not p['floor'])} keep-out")
    for n in notes:
        print("  note:", n)
    for p in problems:
        print("  PROBLEM:", p)
    if problems:
        sys.exit("\nnot pushing - fix the problems above first")

    print("\nnext:")
    print("  1. rebuild the pak so the device has the art:")
    print("     powershell -File "
          "C:\\Users\\Jon\\Documents\\PlatformIO\\Projects\\bunbun_s3\\tools\\convert_assets.ps1")
    push = ("  2. push the scene:\n"
            f"     py C:\\Users\\Jon\\bunbun-nightly\\tools\\scene_push.py {dest} <device-ip>")
    if args.ip:
        print("  2. pushing now...")
        r = subprocess.run([sys.executable,
                            str(pathlib.Path(__file__).resolve().parent / "scene_push.py"),
                            str(dest), args.ip], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
    else:
        print(push)


if __name__ == "__main__":
    main()
