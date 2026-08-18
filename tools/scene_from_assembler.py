"""Turn a Scene Assembler export into something you can push to a bunbun.

    py tools/scene_from_assembler.py "Downloads/Capy Farmhouse_20260817.zip" --ip <device-ip>
    py tools/scene_from_assembler.py "Downloads/my scene_20260817-1018.json"
    py tools/scene_from_assembler.py scene.json --ip <device-ip>          # convert AND push
    py tools/scene_from_assembler.py scene.json --dry-run

THE ZIP IS THE WHOLE PORT (2026-08-17, "i want my kids to be able to create their own stuff
tonight in the tools and port it out"). Give this the assembler's "Export everything" zip and
an --ip and it does every step: the scene, the object art, and — new — the ANIMATIONS. The
zip's art/baked/<name>/NN.png are the composed frames (pet + props, one per order step); they
are downscaled to the pak's native 96px, packed as canim/<slug>/<i>, listed in the scene's
`anims` table, and each placed animation becomes a mark whose key names the child's own
animation. The pet finally SITS where a child sat him, in the child's own sit.

The pet's saved state is read before and after every device step and compared - a port that
resets someone's pet to a baby is worse than no port at all.

Same three jobs as scene_from_attach.py, for the assembler's richer shape:

  1. INSTALLS THE ART into Downloads/bunbun_web_test/assets/items - the folder the pak is built
     from and the folder scene_push reads geometry from. Art anywhere else silently draws
     nothing on the device.
  2. CONVERTS objects / walkable / keepOut / animations into scene_push's items+polys+env, and
     each PLACED animation into a pose item that scene_push turns into a device "bun" mark:
     walk there, perform that. The device performs its OWN animation for the key - your frame
     order, breathing and attached props do not travel yet; that is firmware work.
  3. PRINTS the remaining commands, or pushes with --ip.

What deliberately does not travel: TIME OF DAY (the device's light follows the real clock) and
rain-as-a-state (it travels as a cadence). What cannot travel yet, and is said out loud when it
is dropped: facing (the device's art has its own), activity-area bounds (a mark is a point, so
the area's centre goes), per-animation scale (the device uses its phase scale), and any custom
motion. Honest marks beat silent wrongness.
"""
import argparse, hashlib, json, pathlib, re, shutil, subprocess, sys, tempfile, time
import urllib.request, zipfile

PAK_NAME_MAX = 31
ASSETS = pathlib.Path(__file__).resolve().parent.parent / "pak-factory" / "assets"
ITEMS = ASSETS / "items"
CANIM = ASSETS / "canim"                      # the children's composed animation frames
PAK_PS1 = pathlib.Path(__file__).resolve().parent.parent / "pak-factory" / "convert_assets.ps1"
PAK_OUT = pathlib.Path(__file__).resolve().parent.parent / "pak-factory" / "bunbun.pak"
DEVICE_ANIM_LIMIT = 8                          # scene.h SCENE_MAX_ANIMS
SRC_PROPS = pathlib.Path(__file__).resolve().parent.parent / "assets" / "objects"
DEVICE_PROP_LIMIT = 24
DEVICE_MARK_LIMIT = 6

# assembler clip -> the pose-item name scene_push's BUN_POSES understands -> device key.
# The device has no "sit": the nearest honest thing is standing there (idle).
CLIP_TO_POSE = {
    "sit":   ("idle-anim", "he will STAND at the spot - the device has no sit animation yet"),
    "sleep": ("sleep", None), "bathe": ("bath", None), "eat": ("eat", None),
    "idle":  ("idle-anim", None), "love": ("love-anim", None), "play": ("play", None),
    "bored": ("bored-anim", None), "tired": ("tired-anim", None),
}


def pak_safe(name: str) -> str:
    if len(name) <= PAK_NAME_MAX:
        return name
    keep = PAK_NAME_MAX - 5
    return name[:keep] + "-" + hashlib.sha1(name.encode()).hexdigest()[:4]


OBJ_STEM = {}     # object name -> the stem its art actually lives under (case-collision safe)


def stem_for(name: str) -> str:
    if name in OBJ_STEM:
        return OBJ_STEM[name]
    return pak_safe("items/" + name.split("/")[-1])[len("items/"):]


def case_free_stem(stem: str) -> str:
    """THE CASE TRAP, second lesson (Jon: "thats not the rug from the export"): the child's
    "Rug" and the base pack's "rug" are DIFFERENT art, but Windows merges the files - the
    first fix pointed his scene at the old rug. A collision now gets its own suffixed name,
    so the child's art always wins for the child's object and the base art stays untouched."""
    try:
        low = stem.lower()
        for f in ITEMS.iterdir():
            if f.suffix.lower() == ".png" and f.stem.lower() == low and f.stem != stem:
                return pak_safe("items/" + stem[:24] + "-" +
                                hashlib.sha1(stem.encode()).hexdigest()[:4])[len("items/"):]
    except OSError:
        pass
    return stem


def find_art(name: str):
    """The PNG for an assembler object, wherever it lives in the tools' asset tree."""
    for sub in ("props", "lights", "marks"):
        p = SRC_PROPS / sub / f"{name}.png"
        if p.exists():
            return p
    return None


def anim_slug(name: str, taken: set) -> str:
    """One animation, one folder. canim/<slug> must fit the pak's 31 chars and the key
    c_<slug> must fit BunMark::anim's 15, so the slug itself stops at 13."""
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")[:13] or "anim"
    base, n = s, 2
    while s in taken:
        s = f"{base[:11]}{n}"; n += 1
    taken.add(s)
    return s


def zip_dirname(name: str) -> str:
    """The folder the assembler baked this animation into - its own sanitisation, verbatim."""
    return re.sub(r'[\\/:*?"<>|]+', "-", name)[:50]


def bake_install(zdir: pathlib.Path, anim: dict, slug: str, dry: bool):
    """Downscale one animation's composed 192px frames to the pak's 96px native and install
    them as canim/<slug>/<i>.png. Returns the frame count (0 = nothing baked for it)."""
    src = zdir / "art" / "baked" / zip_dirname(anim["name"])
    frames = sorted(src.glob("[0-9][0-9].png")) if src.is_dir() else []
    if not frames:
        return 0
    if not dry:
        from PIL import Image
        dest = CANIM / slug
        if dest.exists():
            shutil.rmtree(dest)                 # a re-port replaces, never accumulates
        dest.mkdir(parents=True)
        for i, f in enumerate(frames):
            im = Image.open(f).convert("RGBA")
            # halved to species size, feet on the canvas bottom. Newer bundles bake with
            # 40px of headroom (192x272) so props above the head survive; older 192x192
            # bundles land the same way, just without the extra sky.
            r = min(1.0, max(0.15, float(anim.get("scale", 1))))   # the AUTHORED size
            w2 = max(1, round(im.width * r / 2))                   # zip frames are 2x
            h2 = max(1, round(im.height * r / 2))
            small = im.resize((w2, h2), Image.NEAREST)
            canvas = Image.new("RGBA", (96, 136), (0, 0, 0, 0))
            # the FEET LINE lands at row 130 - the device blit's fixed anchor row. New
            # bundles bake feet at H-12 (below-feet rows preserved); bottom-flush bundles
            # (including tall ones from the +6 era) have feet AT the content bottom - so
            # MEASURE, don't assume: content touching the last rows means bottom-flush.
            alpha = im.getchannel("A")
            bbox = alpha.getbbox()
            content_bottom = bbox[3] if bbox else im.height
            feet_raw = content_bottom if content_bottom >= im.height - 3 else im.height - 12
            paste_y = 130 - round(feet_raw * r / 2)
            canvas.paste(small, ((96 - w2) // 2, paste_y), small)
            canvas.save(dest / f"{i}.png")
    return len(frames)


def device_pet(ip: str):
    """The two numbers a port must never move: how old the pet is and what phase it is in."""
    try:
        with urllib.request.urlopen(f"http://{ip}/api/system/info", timeout=8) as r:
            j = json.loads(r.read().decode())
        j = j.get("info", j)          # the payload nests under "info"
        return {"age_min": j.get("pet_age_min"), "phase": j.get("pet_phase")}
    except Exception:
        return None


def push_pak(ip: str) -> bool:
    """Rebuild the pak from the canonical asset tree and send it over WiFi. The device stops
    AirPlay, writes the assets partition and reboots itself."""
    print("  building the pak (convert_assets.ps1)...")
    r = subprocess.run(["powershell", "-NoProfile", "-File", str(PAK_PS1)],
                       capture_output=True, text=True)
    tail = (r.stdout or "").strip().splitlines()[-3:]
    for t in tail:
        print("   ", t)
    if r.returncode != 0 or not PAK_OUT.exists():
        print("  PAK BUILD FAILED"); sys.stderr.write(r.stderr or ""); return False
    data = PAK_OUT.read_bytes()
    if data[:4] != b"BUNP":
        print("  pak has no BUNP magic - refusing to send it"); return False
    print(f"  uploading pak ({len(data)//1024} KB) to {ip} - AirPlay stops, unit reboots...")
    req = urllib.request.Request(f"http://{ip}/api/ota/assets", data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=180) as resp:
        print("   ", resp.read().decode().strip())
    # it said "rebooting now" - wait for it to come back before anything else talks to it
    for _ in range(30):
        time.sleep(2)
        if device_pet(ip) is not None:
            print("  unit is back")
            return True
    print("  unit did not come back within 60s - check it before continuing")
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scene", help="the assembler's exported .json")
    ap.add_argument("--ip", help="push to this device after converting")
    ap.add_argument("--out")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--scene-only", action="store_true",
                    help="furniture and ground only; the pet's marks stay home")
    args = ap.parse_args()

    src = pathlib.Path(args.scene)
    if not src.exists():
        sys.exit(f"no such file: {src}")

    # THE ZIP IS THE WHOLE PORT: scene.json + the object art + the baked animation frames,
    # exactly as "Export everything" wrote them. A bare .json still works and simply has no
    # frames to bring - its animations fall back to the old nearest-pose mapping.
    zdir = None
    if src.suffix.lower() == ".zip":
        zdir = pathlib.Path(tempfile.mkdtemp(prefix="bunbun_port_"))
        with zipfile.ZipFile(src) as z:
            z.extractall(zdir)
        inner = list(zdir.glob("**/scene.json"))
        if not inner:
            sys.exit("that zip has no scene.json - is it really an 'Export everything' bundle?")
        zdir = inner[0].parent
        d = json.loads(inner[0].read_text(encoding="utf-8"))
    else:
        d = json.loads(src.read_text(encoding="utf-8"))
    if "animations" not in d and "objects" not in d:
        sys.exit("that file is not a Scene Assembler export")

    print(f"scene: '{d.get('name','?')}' in room '{d.get('room')}'  saved {d.get('savedAtLocal')}")
    problems, notes, installed = [], [], []

    # ---- 1. art ------------------------------------------------------------------------
    objects = d.get("objects") or []
    for o in sorted({o["object"] for o in objects}):
        stem = pak_safe("items/" + o.split("/")[-1])[len("items/"):]
        # zip art is the child's own and always wins for the child's name; a case-collision
        # with different base art moves to a suffixed name instead of merging
        p = (zdir / "art" / "items" / f"{o}.png") if zdir else None
        if p and p.exists():
            stem = case_free_stem(stem)
            OBJ_STEM[o] = stem
            dest = ITEMS / (stem + ".png")
            if not args.dry_run:
                ITEMS.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, dest)
                installed.append(dest.name)
            else:
                installed.append(f"would install {dest.name}  (from the zip)")
            continue
        dest = ITEMS / (stem + ".png")
        if dest.exists():
            OBJ_STEM[o] = stem
            continue                               # already where the pak is built from
        p = find_art(o)
        if p is None:
            problems.append(f"no art anywhere for '{o}' - it cannot reach the device")
            continue
        OBJ_STEM[o] = stem
        if args.dry_run:
            installed.append(f"would install {dest.name}  (from {p})")
        else:
            ITEMS.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, dest)
            installed.append(dest.name)

    # ---- 1b. the animations themselves --------------------------------------------------
    # Composed 96px frames into canim/<slug>, one scene.json anims entry each, capped at the
    # device's table. The KEY is what the marks below will name.
    anims_out, anim_key = [], {}
    taken = set()
    for a in (d.get("animations") or []):
        if len(anims_out) >= DEVICE_ANIM_LIMIT:
            notes.append(f'device holds {DEVICE_ANIM_LIMIT} animations - "{a["name"]}" did not fit')
            continue
        if not zdir:
            continue                              # a bare .json brings no frames
        slug = anim_slug(a["name"], taken)
        n = bake_install(zdir, a, slug, args.dry_run)
        if n == 0:
            notes.append(f'"{a["name"]}" has no baked frames in the zip - '
                         f'it falls back to the nearest built-in pose')
            continue
        fps = float(a.get("fps", 7)) / max(1, int(a.get("hold", 1)))
        fps = min(24.0, max(0.5, fps))
        key = f"c_{slug}"
        anim_key[a["name"]] = key
        base_act = a.get("clip", "").split("_", 1)[-1].lower().split("_")[0]
        entry = {"k": key, "f": f"canim/{slug}", "n": n, "fps": round(fps, 2), "m": 0,
                 "act": {"bathe": "bath"}.get(base_act, base_act)[:7]}
        if float(a.get("duration", 0) or 0) > 0:
            entry["dur"] = round(float(a["duration"]), 1)
        if (a.get("says") or "").strip():
            entry["txt"] = a["says"].strip()[:42]
        if a.get("depth") == "behind":
            entry["dp"] = 1
        if float(a.get("breathe", 0)) > 0:
            entry["br"] = round(float(a["breathe"]), 1)
            entry["bp"] = max(2, min(24, int(a.get("breathePeriod", 8))))
        # an "anywhere" rule TRAVELS AS A RULE (aw:1): the device plays it wherever he
        # settles, instead of at one flattened mid-floor point
        if not a.get("onItems") and a.get("inArea") is None and not (a.get("rule") or {}).get("needs"):
            entry["aw"] = 1
        anims_out.append(entry)
        installed.append(f"canim/{slug} ({n} frames)")

    # ---- 2. objects -> items -----------------------------------------------------------
    items = []
    # PAINTER'S ORDER TRAVELS AS ARRAY ORDER (the same fix the Send-to-bunbun button carries):
    # the assembler's z is a y-space depth key (~0-240) which the device would clamp to its
    # +/-2 passes - putting every prop in FRONT of the pet. Sort by the assembler's own key,
    # ship z=0 (rugs -1, under everything), and the device paints them in this order.
    ordered = sorted([o for o in objects],
                     key=lambda o: o.get("z") if o.get("z") is not None else o["y"])
    for o in ordered:
        if o.get("hidden"):
            notes.append(f"'{o['object']}' is hidden in the scene, so it stays home")
            continue
        it = {"a": stem_for(o["object"]), "x": int(round(o["x"])), "y": int(round(o["y"])),
              "sx": round(float(o.get("scale", 1)), 4), "sy": round(float(o.get("scale", 1)), 4),
              "flip": bool(o.get("flip")), "z": -1 if o.get("walkOver") else 0}
        if o.get("rotation"):
            it["rot"] = int(o["rotation"])
        if o.get("lamp"):
            it["lamp"] = True
            if o.get("lampPower") not in (None, 1):
                it["lampPower"] = float(o["lampPower"])
            if o.get("lampSize") not in (None, 1):
                it["lampSize"] = float(o["lampSize"])
            if isinstance(o.get("bulbY"), (int, float)):
                it["bulbY"] = o["bulbY"]
        items.append(it)

    # ---- 3. placed animations -> pose items (scene_push makes them bun marks) ----------
    oid_pos = {o.get("oid"): o for o in objects if o.get("oid") is not None}
    area_ctr = {}
    for z in d.get("zones") or []:
        if z.get("kind") == "spot" and z.get("pts"):
            xs = [p[0] for p in z["pts"]]; ys = [p[1] for p in z["pts"]]
            area_ctr[z.get("spotId")] = (sum(xs) / len(xs), sum(ys) / len(ys))

    marks = 0
    # The scene ported clean on 2026-08-17, so the pet goes too (Jon: "can we port over the
    # scene that i have with the logic?"). --scene-only brings back the furniture-first mode.
    SEND_MARKS = not args.scene_only
    for a in (d.get("animations") or []) if SEND_MARKS else []:
        # THE CHILD'S OWN ANIMATION WINS. When the zip brought composed frames, the mark names
        # them ("anim:c_<slug>") and the device plays the child's frames - the authored sit, the
        # attached soap, the chosen facing, all baked in. Only an animation without frames still
        # falls back to the nearest built-in pose.
        custom = anim_key.get(a["name"])
        if custom:
            pose, warn = f"anim:{custom}", None
        else:
            base = a.get("clip", "").split("_", 1)[-1].lower()
            pose, warn = CLIP_TO_POSE.get(base, (None, None))
        if pose is None:
            notes.append(f'"{a["name"]}" ({a.get("clip")}) has no device equivalent - dropped')
            continue
        pl = a.get("place") or {"dx": 0, "dy": 0}
        spots = []
        if a.get("onItems"):
            for oid in a["onItems"]:
                o = oid_pos.get(oid)
                if o:
                    spots.append((o["x"] + pl["dx"], o["y"] + pl["dy"],
                                  f'at the {o["object"]}'))
        elif a.get("inArea") is not None and a["inArea"] in area_ctr:
            cx, cy = area_ctr[a["inArea"]]
            spots.append((cx + pl.get("dx", 0), cy + pl.get("dy", 0),
                          f'in area Z{a["inArea"]}'))
        elif (a.get("rule") or {}).get("needs"):
            need = a["rule"]["needs"]
            for o in objects:
                if need in (o.get("can") or []):
                    spots.append((o["x"] + pl["dx"], o["y"] + pl["dy"],
                                  f'at the {o["object"]}'))
        elif custom:
            # an "anywhere" animation travels as a RULE (anims aw:1) - the device plays it
            # wherever he settles. No mark; the pinned mid-floor point was a flattening.
            notes.append(f'"{a["name"]}" plays anywhere he settles (rule, not a mark)')
            continue
        else:
            continue                                # "anywhere" is the device's own wandering
        for sx, sy, where in spots:
            if marks >= DEVICE_MARK_LIMIT:
                notes.append(f"the device holds {DEVICE_MARK_LIMIT} marks - "
                             f'"{a["name"]}" {where} did not fit')
                continue
            items.append({"a": pose, "x": int(round(sx)), "y": int(round(sy))})
            marks += 1
            msg = f'mark: "{a["name"]}" {where} -> device "{pose}"'
            if warn:
                msg += f"  ({warn})"
            if a.get("facing"):
                msg += "  [facing does not travel - the device's art faces its own way]"
            notes.append(msg)

    real_items = [i for i in items if "sx" in i]
    if len(real_items) > DEVICE_PROP_LIMIT:
        problems.append(f"{len(real_items)} objects; the device holds {DEVICE_PROP_LIMIT}")

    # ---- 4. ground + env ---------------------------------------------------------------
    polys = []
    for w in d.get("walkable") or []:
        polys.append({"floor": True, "pts": w["pts"]})
    for k in d.get("keepOut") or []:
        polys.append({"floor": False, "pts": k["pts"]})
    if not any(p["floor"] for p in polys):
        notes.append("no walkable floor - the device keeps its own band")

    w = d.get("weather") or {}
    env = {"clouds": bool(w.get("clouds")), "cloudN": int(w.get("count", 3)),
           "cloudSize": float(w.get("size", 1)), "cloudSpeed": float(w.get("speed", 1)),
           "cloudAlpha": float(w.get("opacity", 0.72)), "cloudStyle": w.get("style", "shipped"),
           "rain": bool(w.get("rainCadence")),
           "lightScale": float((d.get("light") or {}).get("lightScale", 1))}
    if (d.get("light") or {}).get("timeOfDay"):
        notes.append(f"time of day ({d['light']['timeOfDay']}) is NOT sent - the device's light "
                     f"follows the real clock")

    out = {"room": d.get("room"), "items": items, "polys": polys, "env": env,
           "ts": round(float(d.get("travelScale", 0) or 0), 3) or None,
           "_from": {"tool": "scene-assembler", "name": d.get("name"),
                     "savedAt": d.get("savedAt")}}
    if anims_out:
        out["anims"] = anims_out
    dest = pathlib.Path(args.out) if args.out else src.with_suffix(".scene.json")
    dest.write_text(json.dumps(out, indent=1), encoding="utf-8")

    # ---- 5. report ---------------------------------------------------------------------
    if installed:
        print(f"\nart into {ITEMS}:")
        for i in installed:
            print("   ", i)
    print(f"\nwrote {dest}")
    if d.get("animations") and not SEND_MARKS:
        notes.append(f"{len(d['animations'])} animation(s) stay home (--scene-only)")
    print(f"  {len(real_items)} object(s), {marks} mark(s), "
          f"{sum(1 for p in polys if p['floor'])} floor, "
          f"{sum(1 for p in polys if not p['floor'])} keep-out poly(s)")
    for n in notes:
        print("  note:", n)
    for p in problems:
        print("  PROBLEM:", p)
    if problems:
        sys.exit("\nnot pushing - fix the problems above first")

    if args.ip and not args.dry_run:
        # ---- 6. THE WHOLE PORT, in order, with the pet checked at both ends -------------
        # the species frames wear the SCENE's travelScale ("the export should have it all
        # at 45% per the scene assembler") - rebuilt every port, so the dial always matches
        # species frames bake at the FIXED 0.5 quality base; the scene's ts scales walking
        # at draw time on the device, so the master dial works from ANY port path
        r2 = subprocess.run([sys.executable, str(pathlib.Path(__file__).parent / "mkspecies.py"),
                             "capybara", "0.5"], capture_output=True, text=True)
        print(f"  species base 0.5: {(r2.stdout or '').strip().splitlines()[0] if r2.stdout else r2.stderr.strip()[:80]}")
        before = device_pet(args.ip)
        if before is None:
            sys.exit(f"\ncannot reach {args.ip} - nothing was sent")
        print(f"\npet before: age {before['age_min']} min, phase {before['phase']}")
        if installed:
            if not push_pak(args.ip):
                sys.exit("stopping - the scene was NOT pushed (art first, always)")
        print(f"  pushing the scene to {args.ip}...")
        r = subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve().parent / "scene_push.py"),
                            str(dest), args.ip], capture_output=True, text=True)
        sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
        if r.returncode != 0:
            sys.exit("scene push failed")
        after = None
        for _ in range(20):
            time.sleep(2)
            after = device_pet(args.ip)
            if after is not None:
                break
        if after is None:
            sys.exit("the unit has not come back - check it (the pet state was NOT verified)")
        print(f"pet after:  age {after['age_min']} min, phase {after['phase']}")
        if before["phase"] != after["phase"] or \
           (isinstance(before["age_min"], (int, float)) and isinstance(after["age_min"], (int, float))
                and after["age_min"] < before["age_min"] - 1):
            print("  !!! THE PET MOVED BACKWARDS - investigate NOW, restore from statebk if needed")
        else:
            print("  pet unchanged - port complete")
    else:
        print("\nnext:")
        if installed and not args.dry_run:
            print("  1. NEW ART went in - rebuild the pak and OTA it, or the device draws blanks:")
            print(f"     powershell -File {PAK_PS1}")
        if args.ip:
            print(f"  (dry run) would then push pak + scene to {args.ip}")
        else:
            print(f"  py C:\\Users\\Jon\\bunbun-nightly\\tools\\scene_push.py {dest} <device-ip>")


if __name__ == "__main__":
    main()
