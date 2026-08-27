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
#
# A clip name may also be a TUPLE of alternatives, best first: the first one the pack
# actually has wins. Packs are not uniform - the cat has an Adult_Play nobody else has, the
# croc has no Adult_Happy - and before this, one name meant one substitution for everybody,
# so the richest pack was levelled down to the poorest. Now a pack contributes whatever it
# has and only falls back when it must.
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
    # PLAY IS NOT LOVE. This said "no play clip yet: love loops", and it was accurate when it
    # was written - but it meant pressing PLAY and pressing CUDDLE produced the identical
    # animation on every animal, which reads as a broken button rather than as a substitution.
    # The cat has had a real Adult_Play since 2026-08-18 and it never reached a device.
    "play/anim":        (9, ("anim", ("Adult_Play", "Adult_Happy", "Adult_Love"))),
    # ADULT_HAPPY WAS PAID FOR AND MAPPED TO NOTHING. Seven frames, in five of the six packs,
    # generated on 2026-08-18 and never once installed on a device because no recipe named it.
    # The Scene Assembler has always offered it as "dancing"; the stock kit had no dance clip
    # at all, so emoteClip("dance", ...) fell through to jump - which is itself the idle pose
    # with an arc transform. So dancing, jumping and standing still were one image.
    # 2026-08-23: Adult_Dance is REAL dance art (penguin first), generated for this key rather
    # than borrowed from an emote. It goes first so a pack that has it stops substituting; the
    # Adult_Happy/Adult_Love fallback is untouched, so every other pack behaves exactly as before.
    "dance/anim":       (7, ("anim", ("Adult_Dance", "Adult_Happy", "Adult_Love"))),
    "adult-cuddle":     (9, ("anim", "Adult_Love")),
}
SPEC_BABY = {
    "baby-idle":        (9, ("rot",  "Baby_Onesie", "south")),
    "baby-sit":         (5, ("rot",  "Baby_Sit", "south")),
    "baby-sit-n":       (1, ("rot",  "Baby_Sit", "north")),
    "baby-sleep":       (5, ("rot",  "Baby_Sleep", "south")),
    # OWNER RULING 2026-08-25: a jumping baby is the Baby_Happy hop, not a seated pose
    # arcing through the air. Every pack has Baby_Happy since the happy fill-in.
    "baby-jump":        (5, ("anim", "Baby_Happy")),
    "baby-crawl-south": (5, ("rot",  "Baby_Crawl", "south")),
    "baby-crawl-north": (5, ("rot",  "Baby_Crawl", "north")),
    # THE DIAGONAL RULING (2026-08-25): baby crawls ship as diagonals only; E/W wear
    # the SE/SW art. The old _East/_West names stay as fallbacks for the pilot packs.
    "baby-crawl-west":  (5, ("anim", ("Baby_Crawl_SouthWest", "Baby_Crawl_West"))),
    "baby-crawl-east":  (5, ("anim", ("Baby_Crawl_SouthEast", "Baby_Crawl_East"))),
    # a capybara baby moves by crawling; the walk keys reuse the crawl art so a mid-phase
    # walker never flashes back into a bunny
    "baby-walk-south":  (5, ("rot",  "Baby_Crawl", "south")),
    "baby-walk-north":  (5, ("rot",  "Baby_Crawl", "north")),
    "baby-walk-west":   (5, ("anim", ("Baby_Crawl_SouthWest", "Baby_Crawl_West"))),
    "baby-walk-east":   (5, ("anim", ("Baby_Crawl_SouthEast", "Baby_Crawl_East"))),
    "baby-eat":         (5, ("anim", "Baby_Eat")),
    "baby-bath":        (5, ("anim", "Baby_Bathe")),
    "baby-tired":       (5, ("anim", "Baby_Tired")),
    "baby-bored":       (5, ("anim", "Baby_Bored")),
    "baby-hungry":      (5, ("anim", "Baby_Hungry")),
    "baby-love":        (5, ("anim", "Baby_Love")),
    "baby-angry":       (5, ("anim", "Baby_Angry")),
    "baby-sick":        (4, ("anim", "Baby_Sick")),
    "baby-cuddle":      (5, ("anim", "Baby_Love")),
    "baby-play":        (5, ("anim", ("Baby_Play", "Baby_Love"))),
}
# THE TEEN SET (owner 8/26: "finish the teen phases and get it incorporated"). Mirror of
# count and type of the adult roster - walks not crawls, his own art. teen-text keeps the
# base-bunny fallback deliberately: the roster has no phone clip and the owner's mirror
# rule doesn't include one. Fallback tuples land on the adult clip until the teen art is
# signed and installed, so a half-installed pack never goes blank.
SPEC_TEEN = {
    "teen-idle":        (5, ("rot",  "Teen_Idle", "south")),
    "teen-sleep":       (5, ("rot",  ("Teen_Sleep", "Adult_Sleep"), "south")),
    "teen-eat":         (5, ("anim", ("Teen_Eat", "Adult_Eat"))),
    "teen-bath":        (5, ("anim", ("Teen_Bathe", "Adult_Bathe"))),
    "teen-angry":       (5, ("anim", ("Teen_Angry", "Adult_Angry"))),
    "teen-sick":        (5, ("anim", ("Teen_Sick", "Adult_Sick"))),
    "teen-bored":       (5, ("anim", ("Teen_Bored", "Adult_Bored"))),
    "teen-tired":       (5, ("anim", ("Teen_Tired", "Adult_Tired"))),
    "teen-love":        (5, ("anim", ("Teen_Love", "Adult_Love"))),
    "teen-hungry":      (5, ("anim", ("Teen_Hungry", "Adult_Hungry"))),
    "teen-play":        (5, ("anim", ("Teen_Play", "Adult_Play", "Adult_Happy", "Adult_Love"))),
    "teen-dance":       (9, ("anim", ("Teen_Dance", "Adult_Dance", "Adult_Happy", "Adult_Love"))),
    "teen-cuddle":      (9, ("anim", ("Teen_Love", "Adult_Love"))),
    "teen-walk-east":   (5, ("anim", ("Teen_Walk_East", "Adult_Walk_East"))),
    "teen-walk-west":   (5, ("anim", ("Teen_Walk_West", "Adult_Walk_West"))),
    "teen-walk-south":  (5, ("anim", ("Teen_Walk_SouthEast", "Adult_Walk_SouthEast"))),
    "teen-walk-north":  (5, ("anim", ("Teen_Walk_NorthEast", "Adult_Walk_NorthEast"))),
}
# keys the base pack has that no recipe covers - printed so the fallback is a decision,
# never a surprise
KNOWN_FALLBACKS = ["work-basket", "work-drive", "work-dig", "work-carrot",
                   "teen-* (all but teen-idle)", "school-*", "teen-dance",
                   "baby-fall-west/east", "egg (shared, correct)"]

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
    # BABIES ARE BACK (Jon 2026-08-25: the growth spec - a baby becomes an adult at
    # five hearts; supersedes the 8/19 adult-only strip). Both phase sets generate.
    for folder, (count, recipe) in {**SPEC_ADULT, **SPEC_BABY, **SPEC_TEEN}.items():
        # best-first: the first alternative this pack actually carries
        names = recipe[1] if isinstance(recipe[1], tuple) else (recipe[1],)
        clip, chosen = None, None
        for nm in names:
            if (src / nm).is_dir():
                clip, chosen = src / nm, nm
                break
        if clip is None:
            missing.append(f"{folder} <- {'/'.join(names)} (clip not in the pack)")
            continue
        # SAY WHEN A SUBSTITUTION HAPPENS. A silent fallback is how "play" was the love clip
        # on every animal for four days without anyone being told.
        if chosen != names[0]:
            print(f"    {folder}: no {names[0]}, using {chosen}")
        if recipe[0] == "rot":
            f = clip / f"{recipe[2]}.png"
            if not f.exists():
                missing.append(f"{folder} <- {chosen}/{recipe[2]} (rotation missing)")
                continue
            frames = [f] * count
        else:
            nums = sorted(clip.glob("[0-9][0-9].png"))
            if not nums:
                missing.append(f"{folder} <- {chosen} (no numbered frames)")
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
