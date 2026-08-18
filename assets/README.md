# assets — where art lives

Three kinds of thing, three homes. Anything new goes in one of them; if it does not fit,
that is worth a conversation before inventing a fourth.

```
assets/
  characters/<species>/<clip>/<view>.png   ANIMATIONS   what the character does
  objects/marks/<name>.png                 FEELINGS     overlays above the head
  objects/props/<name>.png                 THINGS       what it holds, or what stands in the room
  objects/lights/<name>.png                LIGHTS       fixtures that cast light
  rooms/<name>.png                         ROOMS        what it stands in
  bodies/                                  work in progress on the shared body
```

---

## 1. Animations — `characters/<species>/<clip>/`

One folder per clip, one PNG per frame or per view. The folder name IS the clip name.

```
characters/capybara/Adult_Bathe/00.png 01.png ...      a timed animation, numbered
characters/capybara/Baby_Sleep/east.png south.png ...  a pose, one file per direction
```

**Rules that are not negotiable, because the firmware and the tools both assume them:**

- **96×96 canvas**, every frame, no exceptions. The device rejects sprites over 128px
  (`spriteLoad`), and the packer measures width per sprite.
- **Feet on a common baseline.** The cleaner's `--norm 96` anchors every frame on its feet, not
  its centre, so a character does not bob between clips. The capybara's line is y=90.
- **Flat palette, no anti-aliasing.** Six colours: a base and a shade for fur, the same for the
  garment, plus outline and one fixed detail colour. Base+shade swap together as a *pair* at
  runtime — that is what makes recolouring a species work.
- **`Adult_` / `Baby_` prefixes.** A deliberate divergence from the original bunny pack, whose
  adult clips are unprefixed. Keep it.
- **Check every rotation's bbox against the canvas edge the moment art lands.** Ink touching
  x=0 or x=95 is cropped art, it is cropped in the *raw* generator output, and it is
  unrecoverable later. Two of the capybara's sleep clips shipped that way unnoticed.

Run the cleaner before committing anything here:

```
py tools/clean_sprite.py <raw_dir> <out_dir> --dist 70 --keep-outline --norm 96 --report
```

The palette resolves across whatever frames are in the pass, so **clean the whole species at
once** — adding or removing one clip can shift which colours survive.

## 2. Objects — `objects/marks/` and `objects/props/`

**Two kinds, and the split is not cosmetic — it changes what the tools let you do with them.**

- **`marks/`** — how the pet *feels*: `angry`, `love`, `tired`, `sick`, `hungry`, `bored`,
  `happy`, `dirty`, `bubbles`. Composited above the head. Shared across every species, so they
  are generated once and never again.
- **`props/`** — *things*: `soap`, `duck`, `tub`, `bed`, `plant`, `food_bowl`. These can be
  attached to a hand OR placed in the room at a floor position.

The editor offers marks and props as separate groups when attaching to the character, and offers
**only props** when placing in the room — a feeling is not furniture and cannot be stood on a
floor. The build tags each one `kind: "mark" | "prop"` from the folder it sits in, so moving a
file between the two folders is all it takes to change its behaviour.

Flat PNGs, trimmed to their own ink, any size — the tools scale them at placement time. A prop
stands on its own bottom edge, so whatever the lowest opaque row is becomes where it meets the
floor.

- **`lights/`** — fixtures: `sconce`, `lamp`. Placing one in the room turns it into a light
  source immediately, with no extra step, and drops it at wall height. Everything else about it
  is an ordinary object — position, scale, rotation, depth.

  A fixture's light follows the builder's rules, which are not obvious and were expensive to
  learn: a fixture **mounted high on a wall throws from 27px BELOW its mount** (the firmware's
  sconce), while anything standing on the floor throws from its shade near the top of the
  sprite; the beam goes **toward the middle of the room**, never following the art's flip flag;
  and the floor pool lies **flat**, never tilting with the beam. Aim one somewhere else with
  its own rotation.

  Any ordinary prop can still be made a light with the **make light** button — the folder just
  saves the step for things that are obviously lamps.

Marks are shared across every species, so they are generated once and never again. Generate them
in **batches**: one `create_1_direction_object` call at size ≤42 returns 64 candidates for the
price of a single object.

Known gap: `angry` is white outline only and disappears against a pale room. Redraw it inside
the next batch rather than on its own.

## 3. Rooms — `rooms/`

Backgrounds, **320×240 exactly** — the real gameplay window (`components/bunbun/game.h`:
`SCR_W`, `SCR_H`). The floor line is y=200. Anything not 320×240 is skipped by the tools rather
than scaled, deliberately, so a wrong-sized room fails loudly.

---

## What is NOT committed

`tools/attach_data.json`, `tools/scene_data.js` and `tools/playhouse.html` are **generated
bundles** — megabytes of base64 rebuilt from the files above:

```
py tools/mkdata.py          # -> attach_data.json   (the attach editor)
py tools/mkscene.py         # -> scene_data.js      (the scene builder)
py tools/build_playhouse.py # -> playhouse.html     (one shareable file)
```

Rebuild after changing any art. The editor fetches its data `no-store`, so a reload is enough —
but a stale bundle is the reason art can appear not to have changed.

Paths resolve through `tools/repo_paths.py`, so a clone works anywhere.
