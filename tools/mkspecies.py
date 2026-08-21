"""Map a character pack (assembler clip layout) onto the firmware's ANIMS[] folders.

    py tools/mkspecies.py capybara

Jon: "i still see the rabbit on the device." The lookup half of species support shipped in
firmware Stage 1 (charSpriteKey tries "<species>/<folder>/<i>" and falls back to base), but
nothing ever PUT species frames in the pak. This writes them:

    assets/characters/capybara/Adult_Idle/south.png
        -> Downloads/bunbun_web_test/assets/capybara/idle-anim/0..4.png

The folder names are DICTATED by ANIMS[] in main.cpp (asymmetric on purpose: adult idle is
`idle-anim`, baby/teen are `baby-idle`/`teen-idle`) — never invent them. Frame counts are
padded to the ANIMS count (the counts are shared and authoritative; a missing frame index
falls back to frame 0 mid-cycle, which reads as a blip).

Honest gaps, printed at the end: clips the pack has no art for fall back to the BASE BUNNY
art for that key (INHERIT_UPRIGHT) — work/school/teen/dance stay bunny until those clips are
generated. Rotational source clips (Idle/Sit/Sleep) contribute a held rotation, not motion.
"""
import pathlib, shutil, sys

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT_ROOT = pathlib.Path("C:/Users/Jon/Downloads/bunbun_web_test/assets")

# The pack's characters are drawn LARGE in their 96px canvas (capybara idle: 58px wide) while
# the base bunny occupies 27px of his - and the device draws every species at the same canvas
# scale, so the raw frames came out as a giant. Halving is exact (2:1 NEAREST keeps pixels
# clean, no resampler mush) and lands the capybara at 29px - the bunny's own footprint.
# Anchored at bottom-centre, the same point spriteBlit() anchors on, so the feet stay put.
# = the scene's travelScale dial, passed by the port ("the export should have it all at
# 45% per the scene assembler") - the wandering pet is the size the assembler previews.
# 0.5, AND IT IS A CONTRACT, not a preference. The firmware brings a species frame up to the
# world's size with travelFactor() = scene.ts / 0.5 - it has no way to ask what scale the pack
# was baked at, so a pack built at anything else is drawn at the wrong size for the rest of its
# life. A dog pack baked at 0.70 (and then multiplied by 1.4 for a 70% world) is what "he keeps
# going bigger than the default on passive animations" was: his authored moves 56px tall, every
# feeling he shows by himself 78px. Pass argv[2] only if you are also changing that divisor.
SCALE = 0.5    # default; overridden by argv[2]

# ANIMS folder -> (frame count, source recipe). A recipe is either
#   ("anim", "Clip_Name")            numbered frames 00.png.., padded/truncated to count
#   ("rot",  "Clip_Name", "south")   one rotation view, repeated count times
SPEC_ADULT = {
    "idle-anim":        (5, ("rot",  "Adult_Idle", "south")),
    "adult-idle-n":     (1, ("rot",  "Adult_Idle", "north")),
    "walk-south/anim":  (8, ("anim", "Adult_Walk_SouthEast")),
    "walk-north/anim":  (8, ("anim", "Adult_Walk_NorthEast")),
    "walk-west/anim":   (8, ("anim", "Adult_Walk_West")),
    "walk-east/anim":   (8, ("anim", "Adult_Walk_East")),
    "jump/anim":        (5, ("rot",  "Adult_Idle", "south")),   # the arc is a transform
    "eat/anim":         (5, ("anim", "Adult_Eat")),
    "bath/anim":        (5, ("anim", "Adult_Bathe")),
    "sleep/anim":       (5, ("rot",  "Adult_Sleep", "south")),
    "tired-anim":       (5, ("anim", "Adult_Tired")),
    "bored-anim":       (5, ("anim", "Adult_Bored")),
    "hungry-anim":      (5, ("anim", "Adult_Hungry")),
    "sick-anim":        (5, ("anim", "Adult_Sick")),
    "angry-anim":       (5, ("anim", "Adult_Angry")),
    "love-anim":        (5, ("anim", "Adult_Love")),
    "play/anim":        (9, ("anim", "Adult_Love")),            # no play clip yet: love loops
    "adult-cuddle":     (9, ("anim", "Adult_Love")),
}
SPEC_BABY = {
    "baby-idle":        (9, ("rot",  "Baby_Onesie", "south")),
    "baby-sit":         (5, ("rot",  "Baby_Sit", "south")),
    "baby-sit-n":       (1, ("rot",  "Baby_Sit", "north")),
    "baby-sleep":       (5, ("rot",  "Baby_Sleep", "south")),
    "baby-jump":        (5, ("rot",  "Baby_Sit", "south")),
    "baby-crawl-south": (5, ("rot",  "Baby_Crawl", "south")),
    "baby-crawl-north": (5, ("rot",  "Baby_Crawl", "north")),
    "baby-crawl-west":  (5, ("anim", "Baby_Crawl_West")),
    "baby-crawl-east":  (5, ("anim", "Baby_Crawl_East")),
    # a capybara baby moves by crawling; the walk keys reuse the crawl art so a mid-phase
    # walker never flashes back into a bunny
    "baby-walk-south":  (5, ("rot",  "Baby_Crawl", "south")),
    "baby-walk-north":  (5, ("rot",  "Baby_Crawl", "north")),
    "baby-walk-west":   (5, ("anim", "Baby_Crawl_West")),
    "baby-walk-east":   (5, ("anim", "Baby_Crawl_East")),
    "baby-eat":         (5, ("anim", "Baby_Eat")),
    "baby-bath":        (5, ("anim", "Baby_Bathe")),
    "baby-tired":       (5, ("anim", "Baby_Tired")),
    "baby-bored":       (5, ("anim", "Baby_Bored")),
    "baby-hungry":      (5, ("anim", "Baby_Hungry")),
    "baby-love":        (5, ("anim", "Baby_Love")),
    "baby-angry":       (5, ("anim", "Baby_Angry")),
    "baby-sick":        (4, ("anim", "Baby_Sick")),
    "baby-cuddle":      (5, ("anim", "Baby_Love")),
    "baby-play":        (5, ("anim", "Baby_Love")),
}
# keys the base pack has that no recipe covers - printed so the fallback is a decision,
# never a surprise
KNOWN_FALLBACKS = ["baby-* (retired, not generated)", "work-basket", "work-drive", "work-dig", "work-carrot", "teen-* (all)",
                   "school-*", "teen-dance", "baby-fall-west/east", "egg (shared, correct)"]

PAK_NAME_MAX = 31


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: py tools/mkspecies.py <species-id>   (id: 1-8 chars a-z0-9)")
    sid = sys.argv[1]
    global SCALE
    if len(sys.argv) > 2:
        SCALE = min(1.0, max(0.15, float(sys.argv[2])))
    if not (1 <= len(sid) <= 8 and sid.isalnum() and sid.islower()):
        sys.exit("species id must be 1-8 chars, [a-z0-9] - it becomes a pak name prefix")
    src = REPO / "assets" / "characters" / sid
    if not src.is_dir():
        sys.exit(f"no pack at {src}")
    out = OUT_ROOT / sid
    if out.exists():
        shutil.rmtree(out)          # a rebuild replaces, never accumulates
    written, missing = 0, []
    # ADULT ONLY (Jon 8/19: "strip all baby we only have adults"). SPEC_BABY is kept
    # below as the record of how the baby set was built, but nothing generates it any
    # more - regenerating a pack used to quietly put all 25 baby clips back.
    for folder, (count, recipe) in SPEC_ADULT.items():
        clip = src / recipe[1]
        if not clip.is_dir():
            missing.append(f"{folder} <- {recipe[1]} (clip not in the pack)")
            continue
        if recipe[0] == "rot":
            f = clip / f"{recipe[2]}.png"
            if not f.exists():
                missing.append(f"{folder} <- {recipe[1]}/{recipe[2]} (rotation missing)")
                continue
            frames = [f] * count
        else:
            nums = sorted(clip.glob("[0-9][0-9].png"))
            if not nums:
                missing.append(f"{folder} <- {recipe[1]} (no numbered frames)")
                continue
            frames = [nums[i % len(nums)] if i >= len(nums) else nums[i] for i in range(count)]
        dest = out / folder
        dest.mkdir(parents=True, exist_ok=True)
        from PIL import Image
        for i, f in enumerate(frames):
            name = f"{sid}/{folder}/{i}"
            if len(name) > PAK_NAME_MAX:
                sys.exit(f"pak name too long: {name}")
            im = Image.open(f).convert("RGBA")
            w2, h2 = int(im.width * SCALE), int(im.height * SCALE)
            small = im.resize((w2, h2), Image.NEAREST)
            canvas = Image.new("RGBA", im.size, (0, 0, 0, 0))
            # the FEET LINE (y=90 of 96) sits on the canvas bottom - the device's anchor -
            # so a standing pet touches his walk line instead of hovering 6 scaled px above
            drop = int(round((im.height - 90) * SCALE))
            canvas.paste(small, ((im.width - w2) // 2, im.height - h2 + drop), small)
            canvas.save(dest / f"{i}.png")
            written += 1
    print(f"{sid}: {written} frames -> {out}")
    for m in missing:
        print("  MISSING:", m)
    print("  falls back to the base bunny art (deliberate, until clips exist):")
    for k in KNOWN_FALLBACKS:
        print("    -", k)


if __name__ == "__main__":
    main()
