"""scene_push.py — send a scene from the builder to a device, with no rebuild and no OTA.

    python tools/scene_push.py my-scene.json <device-ip>
    python tools/scene_push.py my-scene.json <device-ip> --dry-run
    python tools/scene_push.py my-scene.json <device-ip> --no-restart

WHY (Jon, 2026-08-15): "we need to streamline the process for the dozens of new scenes". A room's
layout used to live in main.cpp as constants, so a new arrangement meant editing C, rebuilding and
OTA-ing the whole firmware. The device now reads /spiffs/scene.json (see components/bunbun/scene.h)
and this tool is the bridge: build a room in the scene tool, export it, run this, look at the shelf.

It does the awkward conversions HERE, on the PC, so the firmware only ever reads resolved numbers:

  ANCHOR    the builder positions a sprite by its FEET; drawItemAt() on the device takes the
            sprite's TOP. Converted using the same pad/height the builder itself draws with.
  SCALE     the builder draws at its own per-asset factor k, times the item's sx. The device takes
            one number. Folded to k*sx here.
  NAMES     builder keys are bare ("table"); the pak wants "items/table". Resolved by trying the
            pak's own naming, and anything that cannot be resolved is REPORTED rather than dropped
            silently - a prop that vanishes on the device with no explanation is the worst outcome.
  GEOMETRY  the builder's floor is a polygon and its keep-outs are polygons; the device's model is
            one walkable rectangle plus footprint blocks. Converted, with the loss stated out loud.

Both the k/pad/height table and the pak names come from placer.html, which is the artifact the
builder publishes - so this tool never guesses at numbers that already exist somewhere.
"""
import argparse, base64, difflib, hashlib, json, pathlib, re, sys, urllib.request

# a pak entry's name is 32 bytes and pakFind compares all 32, so 31 characters are usable
PAK_NAME_MAX = 31

# What the DEVICE can hold, which is a different number from what the PAK can hold, and the gap
# between them cost a day. scene.h's SceneProp::name was char[28] until 0.1.239, so sceneLoad()
# strncpy'd a scene name down to 27 characters and pakFind then matched nothing - the prop was
# listed, converted, pushed, accepted, and drew NOTHING. Silently, because a sprite that will not
# load is a normal quiet condition everywhere else in the firmware.
# It bit imported sprites and only imported sprites: pak_safe() shortens a long import to exactly
# PAK_NAME_MAX, which is 4 over the old cap, while every hand-authored name ("items/table") is far
# under it. Jon's rug, his two shelves and his framed picture all landed on it at once.
# 0.1.239 widens the field to char[32], so the two limits now agree. This check stays because the
# rest of the fleet is still on 0.1.236/237 and would truncate exactly the same way.
DEV_PROP_NAME_MAX     = 31   # scene.h SceneProp::name, char[32], from 0.1.239
DEV_PROP_NAME_MAX_OLD = 27   # ...and char[28] before it
DEV_NAME_FIX_VERSION  = "0.1.239"

ASSET_ITEMS = pathlib.Path(__file__).resolve().parent.parent / 'pak-factory' / 'assets' / 'items'

HERE = pathlib.Path(__file__).resolve().parent
PLACER = HERE.parent.parent / "AppData" / "Local" / "Temp"   # overridden by --placer

# poses are INSTRUCTIONS to the sim, not furniture: never drawn as scenery
MARK_PREFIX = ("cat-", "bun-", "items/cat", "ui/")
MARK_EXACT = {"cat-sleep", "cat-sit", "cat-look", "cat-swat", "cat-walk", "cat-walkE",
              "cat-walkW", "cat-jump", "cat-lie", "cat-petted", "cat-stretch",
              "jar-down", "idle-anim", "love-anim", "play", "jump", "sleep"}

# the builder names its cloud shapes; the device holds the same table by index
CLOUD_STYLE_IX = {"shipped": 0, "wisps": 1, "puffs": 2, "bank": 3, "scatter": 4}

# ---- THINGS THAT CAN BE KNOCKED OVER, AND WHERE THEY LAND ----
# Straight off the builder, which is the golden record for all three tables:
#   ROLLERS        = {yarn:1}                                    (placer.html:1512)
#   LANDING_POSES  = {'jar-down':1}                              (:1509)
#   LANDING_FAMILY = {'jar-down':['jar-up','jar-down'],'yarn':['yarn']}   (:1511)
# A landing is not scenery: it is where a knocked thing ENDS UP. And a roller is a toy rather
# than a chore — Jon, twice: "the cat can also play with the yarn, but that stays tied to the
# ground". The device is told which props are loose, which of those roll, and the landing for
# each, so it can run the loop without knowing any of these names itself.
ROLLERS = {"yarn"}
LANDING_POSES = {"jar-down"}
LANDING_FAMILY = {"jar-down": ["jar-up", "jar-down"], "yarn": ["yarn"]}
# what a knocked thing turns into, when the landing does not name it
KNOCK_POSE = {"jar-up": "jar-down"}

# ---- WHAT SHE CAN STAND ON, AND HOW HIGH ITS TOP IS ----
# placer.html:1662  SEAT_SRC = {chair:17,'items/chair':17,'chair-old':17,table:53,rocker:20,crib:26}
#                   surfaceOf(it) = SEAT_SRC[it.a] * k * (sy||s||1)
#                   perchOf(it)   = {x: it.x, y: it.y - surfaceOf(it)}
# in SOURCE pixels above the sprite's own anchor, so it scales with the prop.
# Note what is NOT in here: shelves. That is the point — a shelf is not something she can spring
# to from the floor, so reaching one means going up by way of something that is.
SEAT_SRC = {"chair": 17, "items/chair": 17, "chair-old": 17,
            "table": 53, "rocker": 20, "crib": 26}

# ---- A PLACED BUNBUN POSE IS AN INSTRUCTION TOO ----
# placer.html:1503 BUN_POSES. The cat half of this already travels as "cat":{"sleep":[...]};
# this is the other half — drop `play` somewhere and he walks over and plays there, which is the
# same sentence a child is writing when they drop `cat-sleep` on a table.
# Mapped here to the DEVICE's own animation keys so the firmware needs no table of its own.
BUN_POSES = {"idle-anim": "idle", "love-anim": "love", "play": "play", "eat": "eat",
             "sleep": "sleep", "bath": "bath", "bored-anim": "bored", "tired-anim": "tired",
             "baby-sleep": "sleep", "teen-sleep": "sleep", "baby-idle": "idle",
             "teen-idle": "idle", "baby-love": "love", "teen-love": "love"}


def load_assets(placer_html: pathlib.Path):
    """The builder's own asset table: per sprite its draw factor k, its size and its ink pad."""
    txt = placer_html.read_text(encoding="utf-8", errors="ignore")
    i = txt.find("const ASSETS=")
    if i < 0:
        sys.exit(f"could not find the asset table inside {placer_html}")
    # scan to the matching brace rather than to the first "};" - the blob contains plenty of both
    start = txt.index("{", i)
    depth, j = 0, start
    while j < len(txt):
        c = txt[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return json.loads(txt[start:j + 1])


def pak_safe(name: str) -> str:
    """A pak entry's name field is 32 bytes and pakFind compares all 32, so 31 characters are
    usable and TWO NAMES SHARING THEIR FIRST 31 ARE THE SAME NAME - the second is simply
    unreachable. Nothing warns; the sprite is just never drawn.

    Imported sprites are how this bites, because their names come from a file a child chose.
    Jon's own rug arrived as "round_woven_floor_rug_seen_fro", which becomes 36 characters once
    prefixed and is therefore unpackable - which is exactly why it never appeared on a device and
    why the preflight kept calling it a stale name.

    Shortening has to be DETERMINISTIC and applied in both places - the file written into the
    asset folder and the name the scene refers to - or they stop matching and the sprite vanishes
    again, this time for a new reason. Keep the readable head, then a short hash of the full name
    so two long names that share a prefix cannot collide."""
    if len(name) <= PAK_NAME_MAX:
        return name
    keep = PAK_NAME_MAX - 5                       # room for "-" plus 4 hex
    return name[:keep] + "-" + hashlib.sha1(name.encode()).hexdigest()[:4]


# Geometry the EXPORT stated for an imported sprite, filled in by unpack_imported(). The builder
# computed these on import; re-deriving them from pixels is a fallback, not the source.
EXPORT_META = {}


def imported_meta(name: str):
    if name in EXPORT_META:
        return EXPORT_META[name]
    return _imported_meta_from_pixels(name)


def _imported_meta_from_pixels(name: str):
    """The geometry of an IMPORTED sprite, derived the way the builder derives it.

    An import never appears in the page's baked asset table, so it has no k/pad/h to convert its
    placement with - which is why an imported shelf or rug could be placed in a scene and never
    reach a device. But the builder does not receive that geometry either: registerSprite() COMPUTES
    it from the PNG on import, as k=1, w/h from the image, and pad = the transparent rows below the
    lowest opaque row (alpha > 16). So the same numbers can be recovered here from the same pixels,
    and the conversion is identical to the one the builder would have done.
    """
    stem = pak_safe("items/" + name.split("/")[-1])[len("items/"):]
    p = ASSET_ITEMS / (stem + ".png")
    if not p.exists():
        return None
    try:
        from PIL import Image
    except ImportError:
        return None
    im = Image.open(p).convert("RGBA")
    px = im.load()
    bot = 0
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y][3] > 16:
                bot = y
    return {"w": im.width, "h": im.height, "pad": im.height - 1 - bot, "k": 1}


def unpack_imported(blob: dict) -> list:
    """An EXPORT-all file carries every sprite the user IMPORTED into the builder, as data URIs in
    _sprites. Those live in the browser's localStorage and nowhere else - so a shelf a child drew
    and dragged in can be placed in a scene and can never reach a device. Writing them into the
    asset folder is what lets the next pak build carry them, and is the whole reason a scene
    should travel with its own art."""
    imp = blob.get("_sprites") or {}
    written = []
    for name, rec in imp.items():
        # TWO SHAPES, and only one of them was handled. A real EXPORT writes a full asset RECORD
        # per sprite - {"u": "data:image/png;...", "w":48, "h":48, "pad":16, "k":1, "img":{}} -
        # not a bare data URI. The `isinstance(str)` test below used to be the whole filter, so
        # every object-shaped entry fell through `continue` and NOTHING was written, silently.
        # That is why imported art never reached the asset folder from an export, and why the
        # geometry had to be reverse-engineered from the PNGs by imported_meta() when the export
        # was carrying exact w/h/pad/k the whole time.
        meta = None
        if isinstance(rec, dict):
            uri = rec.get("u")
            if all(k in rec for k in ("w", "h", "pad", "k")):
                meta = {"w": rec["w"], "h": rec["h"], "pad": rec["pad"], "k": rec["k"]}
        else:
            uri = rec
        if not isinstance(uri, str) or "," not in uri:
            continue
        raw = base64.b64decode(uri.split(",", 1)[1])
        # written under the SAME shortened stem the scene will ask for, or the two disagree
        stem = pak_safe("items/" + name.split("/")[-1])[len("items/"):]
        dst = ASSET_ITEMS / (stem + ".png")
        dst.write_bytes(raw)
        if meta:
            EXPORT_META[name] = meta          # the builder's own numbers beat any we re-derive
        written.append((name, dst, len(raw)))
    return written


def pick_scene(blob: dict, want=None):
    """Accepts either a single scene or an EXPORT-all file holding many."""
    if "items" in blob:
        return blob, None
    names = [k for k in blob if not k.startswith("_")]
    if not names:
        sys.exit("no scenes in that file")
    if want and want in blob:
        return blob[want], want
    if want:
        sys.exit("no scene called '%s' - this file has: %s" % (want, ", ".join(names)))
    if len(names) > 1:
        print("  file holds %d scenes: %s - using '%s' (pass --scene to choose)"
              % (len(names), ", ".join(names), names[0]))
    return blob[names[0]], names[0]


def poly_bbox(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


def convert(scene: dict, assets: dict, verbose=True):
    props, blocks, unresolved, rotated = [], [], [], []
    floor = None
    bounds = None

    # --- geometry -------------------------------------------------------------------------
    nogos = []
    fb_blocks = []
    for poly in scene.get("polys", []):
        pts = poly.get("pts") or []
        if len(pts) < 3:
            continue
        x0, x1, y0, y1 = poly_bbox(pts)
        if poly.get("floor"):
            # The polygon itself travels now, as well as the rectangle derived from it. A room
            # drawn in perspective has a tapering floor, and the inscribed rectangle is wider than
            # the boards along the back wall - which is how bunbun came to stand on the skirting.
            # The rectangle stays: it is what picks a target and what everything else has always
            # used. The polygon is what a chosen point is then tested against.
            floor = [[int(round(p[0])), int(round(p[1]))] for p in pts][:12]
            # The device walks a rectangle. A tapering floor cannot be expressed, so take the
            # band that is walkable at EVERY x - the inscribed rectangle - rather than the
            # bounding box, which would let him stand off the front edge in the narrow part.
            top = max(p[1] for p in pts if p[1] < (y0 + y1) / 2) if any(
                p[1] < (y0 + y1) / 2 for p in pts) else y0
            bounds = {"x0": int(x0) + 6, "x1": int(x1) - 6, "y0": int(top) + 2, "y1": int(y1) - 2}
        else:
            # THE ASSEMBLER'S MEANING STANDS (Jon, 2026-08-17): a keep-out is "walk through,
            # never stop", so it travels as a polygon the firmware's destination picker
            # consults (scene.json "nogo") - it is NOT a wall, so it is no longer a block.
            nogos.append([[int(round(q[0])), int(round(q[1]))] for q in pts][:12])

    for z in scene.get("zones", []):
        blocks.append({"x0": int(z["x"]), "x1": int(z["x"] + z["w"]),
                       "yFront": int(z["y"] + z["h"])})

    # --- which things can be knocked over, and where they land ------------------------------
    # Worked out BEFORE the furniture loop, because a landing is skipped as a mark down there and
    # its coordinates would otherwise be gone by the time they are wanted.
    LOOSE_NAMES = {n for fam in LANDING_FAMILY.values() for n in fam} | ROLLERS
    LOOSE_NAMES -= LANDING_POSES          # a jar-down is the destination, not the thing that moves
    live = [it for it in scene.get("items", []) if not it.get("hide")]
    # every placed landing, with the family it serves
    landings = []
    for it in live:
        if it["a"] in LANDING_POSES:
            landings.append(it)
    # the LAST of several rollers is a landing too (placer.html isRollLanding) - one yarn is just
    # a yarn, but a second one is where the first is meant to come to rest
    for rn in ROLLERS:
        of = [it for it in live if it["a"] == rn]
        if len(of) > 1:
            landings.append(of[-1])
            LOOSE_NAMES = LOOSE_NAMES          # the last one stays out of the loose list below
    loose_props = []

    # --- the furniture --------------------------------------------------------------------
    for it in scene.get("items", []):
        name = it["a"]
        if name in MARK_EXACT or name.startswith(MARK_PREFIX) or name.startswith("anim:"):
            continue                                   # an instruction, not a thing in the room
        a = assets.get(name) or imported_meta(name)
        if not a:
            unresolved.append(name)
            continue
        k = a.get("k", 1.0)
        sx = it.get("sx", it.get("s", 1)) or 1
        sy = it.get("sy", it.get("s", 1)) or 1
        # the builder draws the bottom of the ink at y + pad*k*sy, and the sprite is h*k*sy tall
        top = (it["y"] + a.get("pad", 0) * k * sy) - a.get("h", 0) * k * sy
        pak = pak_safe(name if "/" in name else f"items/{name}")
        # FURNITURE BLOCKS (2026-08-17): footprints he walks around. Rugs (z=-1) never block.
        if int(it.get("z", 0)) >= 0:
            bw = a.get("w", 96) * k * sx
            fb_blocks.append({"x0": int(round(it["x"] - bw / 2)),
                              "x1": int(round(it["x"] + bw / 2)),
                              "yFront": int(round(it["y"])), "_w": bw})
        p = {"n": pak, "x": round(it["x"], 1), "y": round(top, 1),
             "s": round(k * sx, 4), "f": bool(it.get("flip")), "z": int(it.get("z", 0))}
        if abs(sy - sx) > 0.001:
            p["sy"] = round(k * sy, 4)        # stretched, and the device honours it
        props.append(p)
        if name in LOOSE_NAMES:
            loose_props.append((len(props) - 1, name, it))   # index into props, for the loose table
        if it.get('rot'):
            p['r'] = float(it['rot'])          # the device turns it too, so it is a replica
            rotated.append((name, it['rot']))

    # --- where the cat may sleep --------------------------------------------------------------
    # A `cat-sleep` mark is not furniture, so it is skipped above - but it is not nothing either.
    # It is the child saying "she is allowed to nap HERE", and until now every one of them died on
    # this PC while the device fell back to a constant measured off a room whose chair stood
    # somewhere else. The builder says WHERE; the device still picks WHICH and WHEN.
    # The mark's y is a FEET anchor, like everything the builder places. The device converts to the
    # centre it blits from, at the size it will actually draw her - it sizes the cat against the
    # PET, not against the builder's k, so that sum cannot be done here.
    cats = [{"x": int(round(it["x"])), "y": int(round(it["y"]))}
            for it in scene.get("items", [])
            if it["a"] == "cat-sleep" or it["a"] == "cat-lie"]
    # and the same for bunbun: where he goes, and what he does when he gets there
    buns = [{"x": int(round(it["x"])), "y": int(round(it["y"])), "a": BUN_POSES[it["a"]]}
            for it in live if it["a"] in BUN_POSES]
    # THE CHILD'S OWN ANIMATIONS (2026-08-17, "the real port"): an item named "anim:<key>" is a
    # mark whose key the firmware resolves against the scene's own anims table - the composed
    # frames are already in the pak. The key travels bare; findAnim() does the rest.
    buns += [{"x": int(round(it["x"])), "y": int(round(it["y"])), "a": it["a"][5:]}
             for it in live if it["a"].startswith("anim:") and len(it["a"]) > 5]

    # --- the loose table, now that every prop has an index -----------------------------------
    def _fam_of(nm):
        for land_a, fam in LANDING_FAMILY.items():
            if nm in fam:
                return land_a
        return None

    def _top_of(it, nm):
        """The canvas TOP for an item, the same conversion the props get."""
        a = assets.get(nm) or imported_meta(nm) or {}
        k = a.get("k", 1.0)
        sy = it.get("sy", it.get("s", 1)) or 1
        return (it["y"] + a.get("pad", 0) * k * sy) - a.get("h", 0) * k * sy

    # --- the lamps, explicitly, with the controls the builder gives them -------------------
    # placer.html lampItems(): anything flagged `lamp` wins; failing that the first sconce/lamp.
    # Carrying this stops the device GUESSING which prop is a light from its name — which would
    # make a prop called `items/clamp` a light source — and lets a child mark a candle, or a
    # window, and tune its brightness and spread exactly as the builder allows.
    # ONE DELIBERATE DIVERGENCE: with nothing flagged the builder lights only the FIRST fixture
    # it finds. Jon, looking at a room with two sconces: "only one light is on in the scene". So
    # the implicit case emits every fixture, not just the first.
    marked = [it for it in live if it.get("lamp")]
    lamp_items = marked or [it for it in live
                            if it["a"] in ("items/sconce", "sconce", "items/lamp", "lamp")]
    lamps = []
    for it in lamp_items:
        want = pak_safe(it["a"] if "/" in it["a"] else f"items/{it['a']}")
        idx = next((i for i, p in enumerate(props)
                    if p["n"] == want and abs(p["x"] - it["x"]) < 0.6), None)
        if idx is None:
            continue
        e = {"i": idx}
        if float(it.get("lampPower", 1)) != 1.0:
            e["pw"] = int(round(float(it["lampPower"]) * 100))
        if float(it.get("lampSize", 1)) != 1.0:
            e["sz"] = int(round(float(it["lampSize"]) * 100))
        if isinstance(it.get("bulbY"), (int, float)):
            e["by"] = int(round(it["bulbY"]))     # the builder's per-lamp bulb-height override
        # THE FIXTURE'S OWN ANCHOR, CARRIED RATHER THAN GUESSED.
        # geomFor() hangs the bulb off `it.y`, which in the builder is the object's FEET:
        # canvasBottom - pad*k. The device only receives the canvas TOP (props are converted for
        # drawItemAt), and it was recovering the anchor as the bottom of the INK -
        # `p->y + (offY + h) * sy`. Those are not the same point: the sconce carries pad=9, so
        # the device's bulb sat up to 9px lower than the builder's, which read exactly as Jon
        # described it - "the light off of the sconces is shifted too low relative to where they
        # are placed" and "the light should be coming all the way up to the lights there is a
        # gap". `pad` is an authored number in ASSETS, not the trim, so the device cannot derive
        # it; this is precisely the class of thing scene.h says to resolve on the PC.
        e["ay"] = round(float(it["y"]), 1)
        lamps.append(e)

    # the surfaces she can stand on, as points (the builder's perchOf)
    perches = []
    for it in live:
        src = SEAT_SRC.get(it["a"])
        if src is None:
            continue
        a = assets.get(it["a"]) or imported_meta(it["a"]) or {}
        k = a.get("k", 1.0)
        sy = it.get("sy", it.get("s", 1)) or 1
        perches.append({"x": int(round(it["x"])), "y": int(round(it["y"] - src * k * sy))})
    perches.sort(key=lambda p: p["y"])          # highest first, purely for readability

    loose = []
    for idx, nm, it in loose_props:
        fam = _fam_of(nm)
        # NEAREST matching landing, not any of them (Jon: "if i have two sets of jars how do i
        # ensure he puts the jar in the right spot?"). A jar knocked off the left table belongs
        # beside the left table.
        cands = [L for L in landings if (_fam_of(L["a"]) == fam or L["a"] == nm) and L is not it]
        entry = {"i": idx}
        if nm in ROLLERS:
            entry["roll"] = 1
        if cands:
            L = min(cands, key=lambda L: (L["x"] - it["x"]) ** 2 + (L["y"] - it["y"]) ** 2)
            entry["lx"] = round(L["x"], 1)
            entry["ly"] = round(_top_of(L, L["a"]), 1)
            if L["a"] != nm:
                entry["down"] = pak_safe(L["a"] if "/" in L["a"] else f"items/{L['a']}")
        # no landing placed: it still has to become something once it is over
        if "down" not in entry and nm in KNOCK_POSE:
            entry["down"] = pak_safe(f"items/{KNOCK_POSE[nm]}")
        loose.append(entry)

    room = scene.get("room", "")
    out = {"name": room[:20] or "scene"}
    if room:
        out["room"] = room if room.startswith("rooms/") else f"rooms/room-{room}"
    if bounds:
        out["bounds"] = bounds
    if blocks:
        out["blocks"] = blocks[:8]
    out["props"] = props[:24]
    if floor:
        out["floor"] = floor
    if loose:
        out["loose"] = loose[:4]
    if perches:
        out["perch"] = perches[:6]
    if lamps:
        out["lamp"] = lamps[:4]
    if cats:
        out["cat"] = {"sleep": cats[:6]}
    if buns:
        out["bun"] = buns[:6]
    if nogos:
        out["nogo"] = nogos[:4]
    if fb_blocks:
        # furniture footprints, biggest first, sharing the 8 slots with any zone blocks
        fb_blocks.sort(key=lambda b: -b["_w"])
        blocks.extend({k2: v for k2, v in b.items() if k2 != "_w"} for b in fb_blocks)
        out["blocks"] = blocks[:8]
    if scene.get("ts"):
        out["ts"] = scene["ts"]
    if scene.get("anims"):
        # [{k,f,n,fps,m}] - key, pak folder, frame count, fps (hold folded in), mode
        out["anims"] = scene["anims"][:8]

    # THE ROOM'S WEATHER CHARACTER - never its clock.
    # The builder's time slider is a preview control: the device's light follows the REAL clock,
    # which is the whole point of the thing. So tod / todOn / lampMode / phase are dropped here on
    # purpose, and rain travels as "how wet this room is", never as "it is raining".
    # Every key is omitted when it equals what the firmware already ships, so a scene that never
    # touched the weather panel adds zero bytes and a device with no env block behaves exactly as
    # it does today.
    e = scene.get("env") or {}
    env = {}
    if e:
        n = min(int(e.get("cloudN", 3)), 8) if e.get("clouds") else 0
        if n != 3:
            env["cn"] = n
        for key, src, dflt in (("cw", "cloudSize", 1.0), ("cs", "cloudSpeed", 1.0),
                               ("ca", "cloudAlpha", 0.60)):
            v = int(round(float(e.get(src, dflt)) * 100))
            if v != int(dflt * 100):
                env[key] = v
        ct = CLOUD_STYLE_IX.get(e.get("cloudStyle", "shipped"), 0)
        if ct:
            env["ct"] = ct
        # RAIN ON at save means "this is a rainy room" - showers come round more often. It does NOT
        # mean "start it raining": the device rolls its own weather on its own timer, and weather
        # that arrives on its own is the difference between weather and a cutscene.
        if e.get("rain"):
            env["rw"] = 2
        ls = int(round(float(e.get("lightScale", 1.0)) * 100))
        if ls != 100:
            env["ls"] = ls
    if env:
        out["env"] = env

    if verbose:
        print(f"  {len(out['props'])} prop(s), {len(out.get('blocks', []))} block(s), "
              f"bounds {'yes' if bounds else 'no'}")
        if len(props) > 24:
            print(f"  ! {len(props) - 24} prop(s) dropped - the device holds 24")
        if len(blocks) > 8:
            print(f"  ! {len(blocks) - 8} keep-out(s) dropped - the device holds 8")
        if out.get("env"):
            print(f"  sky: {out['env']}   (time of day is NOT sent - the device uses the real clock)")
        for n, r in rotated:
            print(f"    '{n}' carries {r} deg of rotation - the device turns it to match")
        for n in sorted(set(unresolved)):
            near = difflib.get_close_matches(n, assets.keys(), n=3, cutoff=0.3)
            hint = f" - did you mean {', '.join(near)}?" if near else ""
            print(f"  ! '{n}' is not a sprite the builder knows, so it was left out{hint}")
            print("    (it is not drawn in the builder either - the name is stale)")
    return out


def preflight(scene, dev, assets, brought, verbose=True):
    """Say - in plain words - everything that will NOT look right on the device, before sending.

    Jon, 2026-08-15: "a child expects to see exactly all the hard work in creating the scene on
    their device and needs help". Every failure this pipeline had today was SILENT: a rug with a
    stale name simply did not appear, a table that was not in the art pack simply did not appear,
    a sprite imported into the builder could never appear at all. A child cannot debug silence.
    So nothing is dropped without being named here, in a sentence that says what to do about it.
    """
    problems, notes = [], []

    placed = [it for it in scene.get("items", [])
              if it["a"] not in MARK_EXACT and not it["a"].startswith(MARK_PREFIX)
              and not it["a"].startswith("anim:")]      # a custom mark needs no prop art
    drawn = {p["n"] for p in dev.get("props", [])}
    for it in placed:
        name = it["a"]
        if name in assets:
            continue
        # An IMPORTED sprite is not in the builder's baked tray - it lives in the browser and is
        # written into the asset folder on its way to the pak. Checking only the tray is what made
        # this call Jon's own imported rug "a stale name" for a whole day. The real question is
        # not "does the builder's table know it" but "can the device draw it", so ask the folder.
        if (ASSET_ITEMS / (pak_safe("items/" + name.split("/")[-1])[len("items/"):] + ".png")).exists():
            continue
        near = difflib.get_close_matches(name, assets.keys(), n=1, cutoff=0.3)
        problems.append(
            '"' + name + '" is not a sprite the builder has, so it will be missing from the room.'
            + (' Did you mean "' + near[0] + '"? Delete it and place that instead.'
               if near else " Place it again from the tray."))

    # Names too long for the DEVICE to hold. The pak is not the last cap a name has to clear -
    # see DEV_PROP_NAME_MAX. This is checked against the converted props, not the builder's keys,
    # because it is the resolved "items/..." name that has to survive the device's buffer.
    for p in dev.get("props", []):
        n = p["n"]
        if len(n) > DEV_PROP_NAME_MAX:
            problems.append(
                f'"{n}" is {len(n)} letters and a bunbun can only hold {DEV_PROP_NAME_MAX}, so it '
                f"will not appear. Give the picture a shorter name and import it again.")
        elif len(n) > DEV_PROP_NAME_MAX_OLD:
            notes.append(
                f'"{n}" is {len(n)} letters, so it needs firmware {DEV_NAME_FIX_VERSION} or newer. '
                f"On an older bunbun it will be cut short to {DEV_PROP_NAME_MAX_OLD} and show "
                f"nothing at all - check the version before blaming the art.")

    if len(placed) > 24:
        problems.append(f"There are {len(placed)} things in the room and a bunbun can show 24. "
                        f"The last {len(placed) - 24} will not appear - remove a few.")
    blocks = len(dev.get("blocks", []))
    if blocks > 8:
        problems.append(f"There are {blocks} keep-out areas and a bunbun can use 8. "
                        f"Join some together or delete a few.")
    if not dev.get("bounds"):
        problems.append("There is no floor drawn, so bunbun will not know where he can walk. "
                        "Use FLOOR to draw the ground.")
    for p in dev.get("props", []):
        if p["x"] < -20 or p["x"] > 340:
            notes.append(f"{p['n']} is off the side of the screen at x{int(p['x'])}.")
    if brought:
        notes.append(f"{len(brought)} sprite(s) you imported are being copied into the art pack. "
                     f"They need the pack rebuilt and sent before they will show up.")
    # The sky. This check used to read "_weather"/"_light" - keys the builder has never written,
    # so the warning printed exactly zero times in its life. That is the silent failure this whole
    # function exists to kill, sitting inside the function itself.
    e = scene.get("env") or {}
    if e.get("clouds") and int(e.get("cloudN", 3)) > 8:
        notes.append(f"You asked for {int(e['cloudN'])} clouds and a bunbun can draw 8 - "
                     f"it will show 8.")
    if e:
        notes.append("The time of day is NOT sent: a bunbun's light follows the real clock, so "
                     "this room will be bright in the morning and dim at night on its own."
                     + (" Rain is sent as 'this room gets a lot of showers', not as 'it is "
                        "raining now'." if e.get("rain") else ""))

    if verbose:
        if problems:
            print("")
            print("  THINGS THAT WILL NOT LOOK RIGHT:")
            for m in problems:
                print(f"    - {m}")
        for m in notes:
            print(f"    note: {m}")
        if not problems:
            print("  everything in this scene can be shown on a bunbun")
    return problems


def push(dev_scene: dict, host: str, restart=True):
    body = json.dumps(dev_scene, separators=(",", ":")).encode()
    if len(body) > 8192:
        sys.exit(f"scene is {len(body)} bytes; the device reads at most 8192")
    if len(body) > 6144:
        print(f"  note: {len(body)} bytes - firmware BEFORE the custom-animation build "
              f"caps scenes at 6144 and will ignore this one entirely; update the unit first")
    url = f"http://{host}/api/fs/upload?path=/spiffs/scene.json"
    req = urllib.request.Request(url, data=body, method="POST",
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as r:
        print(f"  uploaded {len(body)} bytes -> {r.status} {r.read().decode()[:80]}")
    if restart:
        # the scene is read at start-up, so the device has to come round again to see it
        try:
            urllib.request.urlopen(
                urllib.request.Request(f"http://{host}/api/system/restart", method="POST"),
                timeout=8)
        except Exception:
            pass                      # it restarts before it can answer; that is the success case
        print("  restarting - it should be back in about 10 seconds")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scene", help="a scene exported from the builder")
    ap.add_argument("host", nargs="?", help="device IP, e.g. 192.168.0.42")
    ap.add_argument("--placer", default=None, help="path to placer.html (the builder artifact)")
    ap.add_argument("--dry-run", action="store_true", help="convert and print, send nothing")
    ap.add_argument("--no-restart", action="store_true")
    ap.add_argument("--force", action="store_true",
                    help="send even though something will be missing")
    ap.add_argument("--out", default=None, help="also write the device scene here")
    ap.add_argument("--scene", dest="scene_name", default=None,
                    help="which scene, when the file holds several")
    args = ap.parse_args()

    placer = pathlib.Path(args.placer) if args.placer else None
    if not placer or not placer.exists():
        hits = sorted(pathlib.Path(__file__).resolve().parent.rglob("placer.html"),
                      key=lambda p: p.stat().st_mtime, reverse=True)
        if not hits:
            sys.exit("could not find placer.html - pass --placer")
        placer = hits[0]
    print(f"assets from {placer}")
    assets = load_assets(placer)

    blob = json.loads(pathlib.Path(args.scene).read_text(encoding="utf-8"))
    brought = unpack_imported(blob)
    for name, dst, n in brought:
        print("  imported sprite '%s' -> %s (%d bytes)" % (name, dst.name, n))
        assets.setdefault(name, {"k": 1.0, "pad": 0, "h": 0, "w": 0})
    if brought:
        print("  ! new to the asset folder - rebuild the pak and send it, or they will not draw")
    scene, which = pick_scene(blob, args.scene_name)
    print("converting %s%s" % (args.scene, (" [%s]" % which) if which else ""))
    dev = convert(scene, assets)

    problems = preflight(scene, dev, assets, brought)
    if problems and not args.force and args.host and not args.dry_run:
        print("")
        print("  Not sending. Fix the above, or pass --force to send it anyway.")
        sys.exit(2)

    if args.out:
        pathlib.Path(args.out).write_text(json.dumps(dev, indent=1), encoding="utf-8")
        print(f"  wrote {args.out}")
    if args.dry_run or not args.host:
        print(json.dumps(dev, indent=1)[:1200])
        return
    push(dev, args.host, restart=not args.no_restart)


if __name__ == "__main__":
    main()
