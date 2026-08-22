"""Rebuild a .bunbun package from what is actually installed on a device.

    py tools/package_from_device.py bunbun-6D1C.local  [out.bunbun]

Jon, 2026-08-21, after a browser refresh took his session: "all i have is a bunbun package
with everything, but i cant load it". This is the other half of that - when even the package
is gone, the DEVICE still has the world, and this puts it back in a file the Scene Assembler
can open.

It reads the five scene files out of SPIFFS and the art out of the assets partition, keeps only
the pieces those scenes actually name, and writes the same BNDL container the assembler writes:

    "BNDL" ver=1 pad3 | u32 sceneLen | u32 fragLen | scene json | BUNP fragment
"""
import io, json, struct, sys, urllib.request

# The role has to travel as the NAME, not the index. device_import.html rebuilds the path as
# `/spiffs/scene-${role}.json` for anything truthy that is not "main" - so an integer 1 uploads
# to /spiffs/scene-1.json, a file the firmware never opens (SCENE_ROLE_PATHS in scene.h). The
# backup restored the home room, silently dropped the other four, and the page still said
# "All done!". That is the opposite of what a rescue file is for.
ROLES = [("scene", "main"), ("scene-kitchen", "kitchen"), ("scene-bathroom", "bathroom"),
         ("scene-work", "work"), ("scene-outside", "outside")]


def get(host, path, timeout=120):
    return urllib.request.urlopen(f"http://{host}{path}", timeout=timeout).read()


def parse_pak(buf):
    if buf[:4] != b"BUNP":
        raise SystemExit("that is not a BUNP pak")
    ver, count = struct.unpack_from("<HH", buf, 4)
    out = []
    for i in range(count):
        at = 8 + i * 56
        name = buf[at:at + 32].split(b"\x00")[0].decode("ascii", "replace")
        off, size = struct.unpack_from("<II", buf, at + 32)
        oW, oH, w, h = struct.unpack_from("<HHHH", buf, at + 40)
        oX, oY = struct.unpack_from("<hh", buf, at + 48)
        fmt = buf[at + 52]
        out.append(dict(name=name, origW=oW, origH=oH, w=w, h=h, offX=oX, offY=oY,
                        fmt=fmt, data=buf[off:off + size]))
    return ver, out


def build_pak(entries):
    head = bytearray(b"BUNP") + struct.pack("<HH", 2, len(entries))
    table = bytearray()
    blob = bytearray()
    base = 8 + len(entries) * 56
    for e in entries:
        nm = e["name"].encode("ascii")[:31]
        row = bytearray(56)
        row[0:len(nm)] = nm
        struct.pack_into("<II", row, 32, base + len(blob), len(e["data"]))
        struct.pack_into("<HHHH", row, 40, e["origW"], e["origH"], e["w"], e["h"])
        struct.pack_into("<hh", row, 48, e["offX"], e["offY"])
        row[52] = e["fmt"]
        table += row
        blob += e["data"]
    return bytes(head + table + blob)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    host = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "recovered.bunbun"

    scenes = []
    for name, role in ROLES:
        try:
            raw = get(host, f"/api/fs/download?path=/spiffs/{name}.json", 30)
        except Exception:
            continue
        if not raw.strip():
            continue
        try:
            d = json.loads(raw)
        except Exception:
            print(f"  {name}: unreadable, skipped")
            continue
        d["role"] = role
        scenes.append(d)
        print(f"  {name}: room={d.get('room')} props={len(d.get('props',[]))} "
              f"anims={len(d.get('anims',[]))}")
    if not scenes:
        sys.exit("no scenes on that device")

    print("  fetching the art...")
    _, entries = parse_pak(get(host, "/api/ota/assets"))
    by_name = {e["name"]: e for e in entries}

    # ONLY WHAT THE SCENES NAME. The whole pak is ~2 MB of everything the device owns; a package
    # is meant to carry the world's own art, and a smaller file is one the browser can open.
    want = set()
    for sc in scenes:
        # THE ROOM ITSELF. Without this the rescue file carries animations and furniture but no
        # room pictures, so restoring it gives you a world with nothing to stand in.
        if sc.get("room"):
            want.add(sc["room"])
        for p in sc.get("props", []):
            want.add(p.get("n", ""))
        for a in sc.get("anims", []):
            for i in range(int(a.get("n", 0) or 0)):
                want.add(f"{a.get('f','')}/{i}")
    keep = [by_name[w] for w in sorted(want) if w in by_name]
    missing = [w for w in sorted(want) if w and w not in by_name]
    print(f"  art: {len(keep)} piece(s) kept"
          + (f", {len(missing)} named but not in the pak" if missing else ""))
    for m in missing[:6]:
        print(f"      missing: {m}")

    body = json.dumps({"world": scenes}, separators=(",", ":")).encode("utf-8")
    frag = build_pak(keep)
    head = bytearray(16)
    head[0:8] = b"BNDL" + bytes([1, 0, 0, 0])
    struct.pack_into("<II", head, 8, len(body), len(frag))
    blob = bytes(head) + body + frag
    io.open(out, "wb").write(blob)
    print(f"\n  wrote {out}  ({len(blob)/1024:.0f} KB) - "
          f"{len(scenes)} room(s), open it with the assembler's Open scene button")


if __name__ == "__main__":
    main()
