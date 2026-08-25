# Frog pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
ADULT-ONLY pack (session doctrine 2026-08-18).

## Adult character v1 (tunic) — v3 create_character, humanoid, size 96, view "low top-down", detail "low detail", outline "single color black outline" — [FAIL on eyes/mouth + SUPERSEDED by wardrobe change]

Character id 56bd9f29-546f-4c91-9c27-8f3afb1e388e ("Frog"), 3 generations. Body/eye-bump
silhouette excellent. FAILURES at 4x: eyes rendered as YELLOW ovals with dark pupils inside
the bumps (dot-eye breach — the croc's exact eye-bump failure mode); mouth a full-width
downturned arc instead of a short line. Planned fix if the overalls roll repeats it:
deterministic PC-side — repaint eye-yellow as skin green, keep the dark pupil as the dot
(croc precedent; never a CRITICAL eye clause).

```
A high-contrast vector illustration of a frog in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A frog character standing upright on two legs, broad rounded head with two small rounded eye bumps on top of the head, two small round eyes set far apart as solid black dots, a short black line mouth, and a smooth rounded body with no tail. Wearing a simple flat bright purple A-line tunic dress. Solid bright green skin, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

## WARDROBE CHANGE (owner, 2026-08-18): purple overalls replace the tunic, all packs

Before: Wearing a simple flat bright purple A-line tunic dress.
After:  Wearing purple overalls with a square bib and shoulder straps.

## Adult character v2 (overalls) — same settings, garment words swapped only — [PENDING]

Character id a487511c-ea92-4c63-a9ed-0a1dd8ad84d0 ("Frog overalls"), 3 generations.

```
A high-contrast vector illustration of a frog in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A frog character standing upright on two legs, broad rounded head with two small rounded eye bumps on top of the head, two small round eyes set far apart as solid black dots, a short black line mouth, and a smooth rounded body with no tail. Wearing purple overalls with a square bib and shoulder straps. Solid bright green skin, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

### Overalls roll 1 (a487511c) — [FAIL]
White T-shirt under the overalls, large white patch pocket on the bib (owner rule: no
pockets), eye dots carry white glints. Mouth improved vs tunic roll but still wide.

### Overalls roll 2 — strict garment clause — character f0329005-4477-4d5c-ac13-8de844e4dda5 ("Frog overalls v2"), 3 gens — [PENDING]
After: Wearing plain bright purple overalls with a square bib and two shoulder straps, no pockets, no buttons, no shirt underneath, bare arms and shoulders.

## OWNER SIGN-OFF 2026-08-18 (curated in PixelLab): KEEPER = f0329005 "Frog overalls v2"
(the strict-clause roll; its bib pocket and eye glints are ratified by the keep)

## Phase 2 on the keeper — croc/canonical prompts, species word "frog"

- Adult_Sit state f163aacd (96px) · Adult_Sleep state 24366aab (override 120x120)
- Adult_Walk east group bb878aa9 (west mirrored at install)
- Adult_Eat fe27d92e · Adult_Bathe 8c992842 · Adult_Pickup 1af713ea
- Adult_Angry c2ef1f0d · Adult_Sick 748c2431 · Adult_Bored fc98c8f5 · Adult_Tired c7f91051
- Adult_Love f2b45e2b · Adult_Hungry 67e10182 · Adult_Happy 1710ddc1

## PACK COMPLETE 2026-08-18 — installed to assets/characters/frog in the croc layout
(clean --dist 70 --keep-outline --norm 96; garment pair-swapped to #9151d3/#5d229d at
install; Adult_Walk_West mirrored from the cleaned east). Verified by rendering and
looking; details and flags in run.json.

## 2026-08-23 — Adult_Dance (audit fill-in; the clip `dance/anim` really wants)

Keeper `f0329005-4477-4d5c-ac13-8de844e4dda5`, v3, south only, frame_count 6 (7 stored), 1 generation.
Animation group `29539ad5-a784-4e4b-bfb0-a7308fb0bc7e`.

The penguin's verified dance prompt with the species word substituted — the sanctioned
Phase-2 pattern ("croc/canonical prompts, species word") — plus the one body-part swap
"flipper wings" -> "arms", the same precedent as the beak-for-mouth substitution.

```
The frog stands upright and dances on the spot, bouncing rhythmically from one foot to the other while both arms lift and swing outward to the sides in time with the bounce, the head bobbing gently with each beat, then returning to the neutral upright posture so the motion repeats seamlessly. Empty hands, nothing in the mouth, no props.
```

Install: raw came back **112x112** (v3 output size varies per character — measure, never assume).
Cleaned `--dist 70 --keep-outline`, NO `--norm` (normalise_feet shifts each frame separately and
would flatten the bounce). One shared offset dx=8 dy=7 for all 7 frames, anchored on the clip's
LOWEST extent so nothing sits below this pack's floor row 88. Zero opaque px clipped. Lift 6px.
Garment mapped as an explicit PAIR to #9151d3/#5d229d — nearest-colour matching flattened the
cat's base+shade into one value and sent the frog's shade to the wrong colour, which is exactly
the failure the pair doctrine warns about. Everything else mapped to the pack's nearest existing
colour, with a guard that refuses to collapse two source colours into one.
check_pack: PASS, anchor purples missing in 0 frames.

## 2026-08-23 — Adult_Walk_NorthEast / _SouthEast (audit fill-in: the VERTICAL walk)

Why: `walk-north/anim` and `walk-south/anim` had no source clip in this pack, so walking up or
down the screen fell through `charSpriteKey`'s idle rescue — he GLIDED in his idle pose instead
of walking. Only the croc and capybara ever had the diagonals.

Keeper `f0329005-4477-4d5c-ac13-8de844e4dda5`, v3, directions north-east + south-east in ONE group
`964ae34c-7fb9-4cea-9cd0-799f109f2c83`, frame_count 6 (7 stored). 2 generations.
West halves (NorthWest / SouthWest) are PC mirrors, not generations — the recorded croc rule.

Prompt is the croc's walk line reused VERBATIM (it is direction-agnostic; the direction is the
API parameter, not the wording):

```
walking forward with a long exaggerated stride, legs swinging far forward and far back, feet lifting high and clear of the ground on each step. Empty hands, nothing in the mouth, no props.
```

Install: raw 112x112. BOTH directions cleaned in ONE `clean_sprite` pass (`os.walk` recurses, so
one shared palette across the whole set) — cleaning them separately produced two palettes whose
near-duplicates then collapsed onto single pack colours. `--dist 70 --keep-outline`, no `--norm`.
Shared offset per direction anchored on that direction's LOWEST extent onto pack floor 88
(NE dy=9, SE dy=13). Zero opaque px clipped. Garment mapped as an explicit pair to
#9151d3/#5d229d. check_pack PASS; `mkspecies.py frog` now reports no MISSING clips.

## 2026-08-23 — Adult_Play (audit fill-in; `play/anim` was substituting an emote)

Keeper `f0329005-4477-4d5c-ac13-8de844e4dda5`, v3, south, frame_count 6 (7 stored). Group `a587918a-09b2-4561-b1ac-49e2ceb5d5b9`.
Structure reuses the cat's recorded Adult_Play scaffolding (crouch -> wiggle -> spring -> settle,
"Keep the face exactly identical to the source", garment-fixed clause) with the action changed;
the cat's own clip is a cat-specific batting/pouncing move that does not transfer verbatim.

```
The frog crouches down slightly, wiggles briefly, then springs back up and playfully bats at the air in front of it with one hand, swiping quickly downward several times while bouncing on the spot, then settles back into its resting posture. Keep the face exactly identical to the source. Its purple outfit remains fixed in place. Empty hands, no props.
```

**INSTALL BUG, MINE NOT THE GENERATOR'S.** The first install INVERTED this garment (67 light /
4463 dark against an idle of 4373/56). My auto-detect for the garment pair used `b>r>g`, which also
admits near-greys - the frog's `#a09ba8` passed it and, being brighter, was ranked as the garment BASE,
so the real garment `#7a52af` became the shade. Fixed with an explicit pair
(#7a52af -> #9151d3, #4a2784 -> #5d229d); result 4463/34, matching idle. The heuristic is now
hardened to require (b-g)>50 and a channel spread >50 so a grey can never be read as garment.

Install: cleaned `--dist 70 --keep-outline`, no `--norm`; one shared offset anchored on the
clip's lowest extent onto this pack's floor row; zero opaque px clipped; garment mapped as an
explicit pair to #9151d3/#5d229d. check_pack PASS.

## 2026-08-24 — Adult_Sleep regenerated with closed eyes (PC-side face fixes retired)

The 8/24 pixel eye-fix is superseded (owner: "weird blemishes"). Regenerated as a fresh
state on the Idle keeper — state e1275e45, override 120x120,
use_color_palette_from_reference=True — the canonical sleep prompt with the eye words
swapped for the proven closed clause:
```
Lying down on the ground, curled on one side, head resting on the ground, legs tucked in close to the body. Both eyes fully closed as short dark curved lines. Keep the rest of the face exactly identical to the source - <species mouth words>, unchanged. Empty hands, nothing in the mouth, no props.
```
The roll came back with MIXED poses across rotations (lying in some views, dozing
sitting up in others). Flagged to the owner with old-vs-new sheets; OWNER RULING:
"I think they will work fine... Generally they are better than the original" — KEPT.
(A pose-pinned alternative was generated on the old sleep state the same evening and
sits unused in PixelLab if the mixed poses ever grate.) Installed via clean --dist 70
--keep-outline --norm 96 (feet y=90); garment pair-swapped to the anchor purples.

## 2026-08-25 (late 8/24 night) — Baby Onesie preview roll — [PENDING owner review]

Character b78c91c2 ("Frog Baby Onesie"): v3, ~3 gens, adult creation prompt + baby clauses + onesie.
The croc-pilot baby clauses folded in verbatim: "with the head noticeably larger
relative to the body, about half the total height, and short stubby arms and legs" +
"Wearing a simple flat bright purple onesie, one piece covering the whole body, short
sleeves and short legs." South idle shown in the 8/24 nursery sheet; nothing installed.
