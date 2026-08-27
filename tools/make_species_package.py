"""Build per-animal .bunbun packages from the full factory pak.

    py tools/make_species_package.py               # every species found in the pak
    py tools/make_species_package.py cat penguin   # just these

Jon: "can we have the device just keep one character at a time for the export/import?"
The device pak carries the base pack + ONE chosen animal; every other animal lives as a
package a kid feeds to http://<device-ip>/build. A species package is the normal BNDL
container with a tiny scene JSON of {"species":"<id>"} and a fragment holding only that
animal's pak entries (lifted verbatim from the full factory pak - no re-encoding). The
import page swaps animals wholesale: the incoming animal's entries replace every other
animal's, then the pet becomes it.

Output: tools/build/species/<id>.bunbun (published to the site by publish_site/sync).
"""
import json, pathlib, struct, sys

REPO = pathlib.Path(__file__).resolve().parent.parent
FULL_PAK = pathlib.Path("C:/Users/Jon/Documents/PlatformIO/Projects/bunbun_s3/assets/bunbun.pak")
if not FULL_PAK.exists():                       # the public repo builds it in-tree
    FULL_PAK = REPO / "pak-factory" / "bunbun.pak"
OUT = REPO / "tools" / "build" / "species"
SPECIES = ["capybara", "bunny", "cat", "dog", "frog", "penguin",
           "croc", "frill", "spark", "imp"]


def parse(b):
    assert b[:4] == b"BUNP", "not a BUNP pak"
    cnt = struct.unpack_from("<H", b, 6)[0]
    out = []
    for i in range(cnt):
        at = 8 + i * 56
        name = b[at:at + 32].split(b"\0")[0].decode()
        off, size = struct.unpack_from("<II", b, at + 32)
        ow, oh, w, h, ox, oy = struct.unpack_from("<HHHHhh", b, at + 40)
        out.append([name, b[off:off + size], ow, oh, w, h, ox, oy, b[at + 52]])
    return out


def build_pak(entries):
    head = struct.pack("<4sHH", b"BUNP", 2, len(entries))
    off = 8 + len(entries) * 56
    tab = b""
    blob = b""
    for name, data, ow, oh, w, h, ox, oy, fmt in entries:
        tab += struct.pack("<32sIIHHHHhhB3x", name.encode(), off, len(data),
                           ow, oh, w, h, ox, oy, fmt)
        blob += data
        off += len(data)
    return head + tab + blob


def main():
    want = sys.argv[1:] or SPECIES
    entries = parse(FULL_PAK.read_bytes())
    OUT.mkdir(parents=True, exist_ok=True)
    for sp in want:
        mine = [e for e in entries if e[0].startswith(sp + "/")]
        if not mine:
            print(f"{sp}: no entries in {FULL_PAK.name} - skipped")
            continue
        frag = build_pak(mine)
        scene = json.dumps({"species": sp}).encode()
        head = bytearray(16)
        head[0:5] = b"BNDL\x01"
        struct.pack_into("<II", head, 8, len(scene), len(frag))
        p = OUT / f"{sp}.bunbun"
        p.write_bytes(bytes(head) + scene + frag)
        print(f"{sp}: {len(mine)} entries, {p.stat().st_size // 1024} KB -> {p.name}")


if __name__ == "__main__":
    main()
