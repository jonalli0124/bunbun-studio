# The visiting cat

The nine clips the firmware draws for her, plus the two stills `drawCat()` falls back to.
`CAT_CLIP[]` in `components/bunbun/main.cpp` names these folders and their frame counts:

    cat-walk 9 @10fps   cat-sit 9 @6     cat-petted 7 @5    cat-sleep 4 @2
    cat-stretch 9 @7    cat-look 7 @6    cat-swat 9 @8      cat-jump 9 @9    cat-lie 9 @7

Plus `items/cat` and `items/cat-sleep`, used when a clip frame is missing.

## Why these are here

They were **lost from the source tree**. `tools/pullcat.mjs`, which packed them, is gone, and no
`.bunbun` world package carries them - so a device brought up through `/build` alone had NO cat
art at all. `drawCat()` failed `spriteLoad()` every frame and hit its silent `return`: she walked
in, sat, napped and left completely invisible, which read for weeks as "the cat never comes".

Recovered 2026-08-22 from `docs/reference/placer-scene-builder.html`, the only surviving copy.
Two things confirmed it was the right source: all nine fps values matched `CAT_CLIP` exactly, and
every clip was one frame short - because frame 0 of each is the shared tray image (`u`), which
`framesOf()` prepends. That frame is included here as `0.png`.

## Two rules

**Frame numbering is the firmware's, not the builder's.** `<clip>/0.png` IS the tray image. Do not
renumber.

**64x64 canvas, drawn from the feet.** `drawCat()` anchors on the canvas centre, not the ink, so
these must not be trimmed or re-padded - clips trim to anywhere from 39 to 55 rows tall and
centring the ink would lift her off the floor as she settles.
