"""One command installs a generated ITEM or ROOM everywhere it needs to live.

    py tools/new_asset.py item  <png>  <name>          # a prop: "hard hat", "toolbox"
    py tools/new_asset.py light <png>  <name>          # a prop that can be a light source
    py tools/new_asset.py mark  <png>  <name>          # a feeling shown over the head
    py tools/new_asset.py room  <png>  <name>          # a 320x240 room: "factory"

Jon: "we need to build an item and room generation pipeline ... he could work at a factory,
he could be a construction worker." Generation happens in-session (PixelLab, per
GENERATION.md's verbatim-prompt discipline); THIS is the install half, so a fresh PNG shows
up in both tools and reaches the device on the next port with zero hand-copying:

  item  -> assets/objects/props/<name>.png          (the repo's art tree, the tools' source)
        -> Downloads/bunbun_web_test/assets/items/  (the pak factory - the device path)
  room  -> assets/rooms/<name>.png
        -> Downloads/bunbun_web_test/assets/rooms/room-<name>.png  (pak name rooms/room-<name>)
  then  -> py tools/build.py refresh (mkdata) so the web tools SEE it immediately.

Rooms must be exactly 320x240 (resized NEAREST if close, refused if wildly off). Items keep
their size; the tools scale them. Names are lowercased [a-z0-9 _-], and a case-collision with
existing art is refused rather than silently merged (the Rug lesson).
"""
import io, pathlib, re, subprocess, sys

REPO = pathlib.Path(__file__).resolve().parent.parent
WEB = pathlib.Path("C:/Users/Jon/Downloads/bunbun_web_test/assets")


def clean(name):
    n = re.sub(r"[^a-z0-9 _-]", "", name.lower()).strip()
    if not n:
        sys.exit("that name has nothing usable in it")
    return n


def no_case_twin(folder: pathlib.Path, fname: str):
    if not folder.is_dir():
        return
    for f in folder.iterdir():
        if f.name.lower() == fname.lower() and f.name != fname:
            sys.exit(f"case-collision: {f.name} already exists in {folder} - Windows would "
                     f"merge the files but the pak would not. Pick another name.")


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    kind, src, name = sys.argv[1], pathlib.Path(sys.argv[2]), clean(" ".join(sys.argv[3:]))
    if not src.exists():
        sys.exit(f"no file at {src}")
    from PIL import Image
    im = Image.open(src).convert("RGBA")

    if kind == "room":
        if im.size != (320, 240):
            if abs(im.width - 320) <= 64 and abs(im.height - 240) <= 48:
                im = im.resize((320, 240), Image.NEAREST)
                print(f"  resized {src.name} -> 320x240 (NEAREST)")
            else:
                sys.exit(f"a room is 320x240; this is {im.width}x{im.height}")
        a = REPO / "assets" / "rooms" / f"{name}.png"
        b = WEB / "rooms" / f"room-{name}.png"
        no_case_twin(a.parent, a.name); no_case_twin(b.parent, b.name)
        im.save(a); im.convert("RGB").save(b)
        print(f"  room installed: {a}")
        print(f"                  {b}  (pak name rooms/room-{name})")
    elif kind in ("item", "light", "mark"):
        sub = {"item": "props", "light": "lights", "mark": "marks"}[kind]
        a = REPO / "assets" / "objects" / sub / f"{name}.png"
        b = WEB / "items" / f"{name}.png"
        no_case_twin(a.parent, a.name); no_case_twin(b.parent, b.name)
        a.parent.mkdir(parents=True, exist_ok=True)
        b.parent.mkdir(parents=True, exist_ok=True)
        im.save(a); im.save(b)
        print(f"  {kind} installed: {a}")
        print(f"                   {b}  (pak name items/{name})")
    else:
        sys.exit(__doc__)

    r = subprocess.run([sys.executable, str(REPO / "tools" / "build.py")],
                       capture_output=True, text=True)
    print("  " + (r.stdout or "").strip().splitlines()[-1])
    print("  refresh the tool pages and it is there; the next port carries it to the device")


if __name__ == "__main__":
    main()
