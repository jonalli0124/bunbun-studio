# Penguin pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
ADULT-ONLY pack (session doctrine 2026-08-18).

## Adult character v1 (tunic) — v3 create_character, humanoid, size 96, view "low top-down", detail "low detail", outline "single color black outline" — [SUPERSEDED by wardrobe change before full review]

Character id b2210af4-7f26-4f4f-85e9-feb2037cdd2a ("Penguin"), 3 generations. 7/8 rotations
downloaded before the wardrobe change landed; not fully reviewed.

```
A high-contrast vector illustration of a penguin in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A penguin character standing upright on two legs, rounded head and a plump oval body, two small round eyes set far apart as solid black dots, a short black pointed beak, and two small flipper wings held at the sides of the body. Wearing a simple flat bright purple A-line tunic dress. Solid dark blue-grey feathers with a pale cream belly, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

## WARDROBE CHANGE (owner, 2026-08-18): purple overalls replace the tunic, all packs

Before: Wearing a simple flat bright purple A-line tunic dress.
After:  Wearing purple overalls with a square bib and shoulder straps.

## Adult character v2 (overalls) — same settings, garment words swapped only — [PENDING]

Character id 62a882ac-ee76-4df0-ac28-60cca41ffb36 ("Penguin overalls"), 3 generations.

```
A high-contrast vector illustration of a penguin in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A penguin character standing upright on two legs, rounded head and a plump oval body, two small round eyes set far apart as solid black dots, a short black pointed beak, and two small flipper wings held at the sides of the body. Wearing purple overalls with a square bib and shoulder straps. Solid dark blue-grey feathers with a pale cream belly, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

### Overalls roll 1 (62a882ac) — [FAIL, closest of the five]
Handsome plump penguin, dot eyes, correct flippers — but a stitched bib pocket (owner rule:
no pockets), glossy highlight shading beyond the flat contract, muted purple.

### Overalls roll 2 — strict garment clause — character e78bfb0e-2b45-4fad-89c5-f9a1239d8b15 ("Penguin overalls v2"), 3 gens — [PENDING]
After: Wearing plain bright purple overalls with a square bib and two shoulder straps, no pockets, no buttons, no shirt underneath, bare arms and shoulders.

## OWNER SIGN-OFF 2026-08-18 (curated in PixelLab): KEEPER = e78bfb0e "Penguin overalls v2"
(the strict-clause roll; bib pocket, seat puff, cream belly + orange chest band and gloss
shading are ratified by the keep — owner-ratified detail overrides the flat contract)

## Phase 2 on the keeper — croc/canonical prompts, species word "penguin".
## ONE recorded substitution in the two state prompts (face follows the species, croc
## precedent): "a short black pointed beak" for "a short black line mouth".

- Adult_Sit state 351d7988 (96px) · Adult_Sleep state 895749ea (override 120x120)
- Adult_Walk east group ca68f239 (west mirrored at install)
- Adult_Eat 31df05c1 · Adult_Bathe 74b052a8 · Adult_Pickup e286637f
- Adult_Angry 28f125f1 · Adult_Sick 7e5bf93d · Adult_Bored 3042ea40 · Adult_Tired d5907ced
- Adult_Love ba2e8876 · Adult_Hungry f1796ced · Adult_Happy 2e369444

### [FAIL-RETRY] Adult_Sit 351d7988 failed: "Failed to download north-east image: HTTP 500"
### (transient storage error). One identical retry per doctrine: Adult_Sit 07fb6782.

### [FAIL-REROLL] Adult_Sit retry 07fb6782 completed but UNDRESSED the penguin — all 8
### rotations came back in natural colours with no overalls (kept in PixelLab as fallback).
### Roll 3 = f653d9dd, prompt + the croc/capybara garment stability clause pattern:
### " Still wearing the same purple overalls with the square bib and shoulder straps."
### appended before the empty-hands sentence (the sanctioned "Still wearing the same..."
### pattern from the baby-onesie states, not invented wording).

## PACK COMPLETE 2026-08-18 — installed to assets/characters/penguin in the croc layout
(clean --dist 70 --keep-outline --norm 96; garment pair-swapped to #9151d3/#5d229d at
install; Adult_Walk_West mirrored from the cleaned east). Verified by rendering and
looking; details and flags in run.json.

## 2026-08-23 — Adult_Dance (NEW CLIP, added after PACK COMPLETE)

Why: `danceKitKey()` tests `kitHasClip("dance/anim")`, which is false for every species, so
the firmware silently falls back to jump — D468 reports `anim: jump / says: RK is dancing!`.
Only `teen-dance/0..8` art has ever existed. This is the first real dance art in the project.

Keeper character `e78bfb0e-2b45-4fad-89c5-f9a1239d8b15` ("Penguin overalls v2"),
v3, south only, frame_count 6 (7 stored: 1 reference + 6 animated), 96px.
Animation group `11c222c4-3ae8-4ab0-b3ff-5461ae34ba02`. 1 generation. Completed first try.

Follows the house emote pattern exactly — "The penguin stands..." + concrete motion +
the seamless-loop clause (dance is `M_LOOP` in firmware) + the standing constraint sentence.
Nothing about style, palette or garment is stated; v3 carries those from the keeper.

```
The penguin stands upright and dances on the spot, bouncing rhythmically from one foot to the other while both flipper wings lift and swing outward to the sides in time with the bounce, the head bobbing gently with each beat, then returning to the neutral upright posture so the motion repeats seamlessly. Empty hands, nothing in the mouth, no props.
```

Owner, on the frames: *"ooh the penguin adult dance looks great!"* — PASS.

NOT YET INSTALLED. Still to do before the device can play it: download the 7 frames, clean
(`--dist 70 --keep-outline --norm 96`), pair-swap the garment to #9151d3/#5d229d as the rest
of the pack was, and install as `dance/anim` so `kitHasClip` finds it.

### Adult_Dance raw geometry — measured, not assumed (7 frames pulled and inspected)

Raw came back **112x112**, not the 96 the 2026-08-18 pack frames came back at. Feet-normalise
to 96 is therefore mandatory here (v3 output size has moved; see the pipeline rule).

| frame | width | feet row |
|---|---|---|
| 0 (reference) | 70 | 94 |
| 1 | 80 | 94 |
| 2 | 86 | 92 |
| 3 | 86 | **88** |
| 4 | 70 | 91 |
| 5 | 84 | 94 |
| 6 | 86 | 94 |

Two findings that change how this must be installed:

1. **Max width 86 is NOT a new overhang risk.** Installed `Adult_Happy` is already 89 wide and
   ships fine; `Adult_Idle` is 70. The dance sits inside what this pack already carries.
2. **Do NOT feet-normalise per frame.** Every installed clip in this pack sits at feet row 90
   dead flat, because those clips have no vertical travel. The dance *does* — frame 3 lifts 6px
   (88 vs 94). Normalising each frame to a fixed feet row would flatten the hop and delete the
   bounce that makes it read as dancing. Anchor the whole clip on one frame's offset and shift
   all seven by that same amount.

### 2026-08-23 — wired into the tools (verified by LOADING, not by grepping)

- `tools/mkspecies.py` — `dance/anim` recipe now `("Adult_Dance", "Adult_Happy", "Adult_Love")`.
- `tools/src/scene_tool.html` — `ACT_TEMPLATE.dance` is now the list `['Adult_Dance','Adult_Happy']`
  and `templateFor()` accepts a list, taking the first clip the pack actually has. Same
  best-first rule as the mkspecies recipe tuples, so both sides of the contract read alike.
- The Animation Creator needed NO code change: `mkdata.gather_clips()` enumerates directories
  rather than a whitelist, so `Adult_Dance` joins the picker as soon as the art exists.

Proved in the running pages, not by reading source:

    attach_editor.html  charSel=penguin -> clip picker 14 -> 16 options, Adult_Dance present
                        in both the clip picker and the attach (aClip) picker
    scene_tool.html     templateFor({actAs:'dance'},'penguin') -> "penguin:Adult_Dance"
                        templateFor({actAs:'dance'},'bunny')   -> "bunny:Adult_Happy"  (fallback intact)
                        templateFor({actAs:'dance'},'croc')    -> null   (has neither)
                        templateFor({actAs:'bath'},'penguin')  -> "penguin:Adult_Bathe" (string path intact)
    No page errors in either console (only a Chrome extension's own exception).

OPEN: penguin is the ONLY pack with Adult_Dance. The Creator opens on the BASE pack, whose 14
clips have no dance, so dance is not offered until you switch animal. bunny/cat/dog/frog fall
back to Adult_Happy; croc and capybara have neither and resolve to null.

## 2026-08-23 — Adult_Walk_NorthEast / _SouthEast (audit fill-in: the VERTICAL walk)

Why: `walk-north/anim` and `walk-south/anim` had no source clip in this pack, so walking up or
down the screen fell through `charSpriteKey`'s idle rescue — he GLIDED in his idle pose instead
of walking. Only the croc and capybara ever had the diagonals.

Keeper `e78bfb0e-2b45-4fad-89c5-f9a1239d8b15`, v3, directions north-east + south-east in ONE group
`b5bbca3a-ecd3-4f83-b5c4-415545e0b444`, frame_count 6 (7 stored). 2 generations.
West halves (NorthWest / SouthWest) are PC mirrors, not generations — the recorded croc rule.

Prompt is the croc's walk line reused VERBATIM (it is direction-agnostic; the direction is the
API parameter, not the wording):

```
walking forward with a long exaggerated stride, legs swinging far forward and far back, feet lifting high and clear of the ground on each step. Empty hands, nothing in the mouth, no props.
```

Install: raw 112x112. BOTH directions cleaned in ONE `clean_sprite` pass (`os.walk` recurses, so
one shared palette across the whole set) — cleaning them separately produced two palettes whose
near-duplicates then collapsed onto single pack colours. `--dist 70 --keep-outline`, no `--norm`.
Shared offset per direction anchored on that direction's LOWEST extent onto pack floor 90
(NE dy=5, SE dy=6). Zero opaque px clipped. Garment mapped as an explicit pair to
#9151d3/#5d229d. check_pack PASS; `mkspecies.py penguin` now reports no MISSING clips.

## 2026-08-23 — Adult_Play (audit fill-in; `play/anim` was substituting an emote)

Keeper `e78bfb0e-2b45-4fad-89c5-f9a1239d8b15`, v3, south, frame_count 6 (7 stored). Group `7e472766-f79f-420a-a014-3f48a72381d4`.
Structure reuses the cat's recorded Adult_Play scaffolding (crouch -> wiggle -> spring -> settle,
"Keep the face exactly identical to the source", garment-fixed clause) with the action changed;
the cat's own clip is a cat-specific batting/pouncing move that does not transfer verbatim.

```
The penguin crouches down slightly, wiggles briefly, then springs back up and playfully bats at the air in front of it with one flipper wing, swiping quickly downward several times while bouncing on the spot, then settles back into its resting posture. Keep the face exactly identical to the source. Its purple outfit remains fixed in place. Empty hands, no props.
```

Install: cleaned `--dist 70 --keep-outline`, no `--norm`; one shared offset anchored on the
clip's lowest extent onto this pack's floor row; zero opaque px clipped; garment mapped as an
explicit pair to #9151d3/#5d229d. check_pack PASS.
