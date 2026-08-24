# Imp pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
ADULT-ONLY pack. First pack in the SHADED direction (see the note at the bottom).

## OWNER SIGN-OFF 2026-08-21: KEEPER = 98008edc-473a-4633-b216-de3b23cc0e6c "Imp sly brow"

Jon: "Imp sly brow is where it's at."

create_character, **mode "pro"** (25 generations), humanoid, size 96, view "low top-down".
Pro ignores outline/shading/detail, so the whole style rides in the text.

```
pixel art sprite of a small mischievous ghost imp creature standing on two short legs, two short arms and two short legs all four clearly separated from the body silhouette, a rounded head merging directly into a heavy rounded body, body widest at the base, saturated violet body with a paler lilac underside, narrow wedge-shaped eyes angled sharply down toward the centre of the face with a strong slanted brow ridge above each eye, looking straight ahead at the viewer, a wide toothy grin stretching most of the face, an irregular silhouette with a few pointed tufts breaking the outline, the creature fills the frame from top edge to bottom edge, hard dark outline in a deep tinted shade of the violet body and never pure black, a tight palette of about a dozen colours in two hue families with one warm accent against the cool body, banded cel shading with three value steps, separate shade ramps for body and underside, large flat areas of colour, no dithering, solid white background
```

Installed: `Adult_Idle/` = the keeper's 8 rotations, through
`clean_sprite.py --dist 70 --keep-outline`. 8 colours, ink 52x57.
NOT YET DONE: the 12 animations + 2 states (Adult_Sit, Adult_Sleep), the walk
east/west pair, check_pack, mkspecies.

## 2026-08-24 — IP re-roll (owner: "Nudge designs first")

The keeper read as Gengar at a glance (purple round body + pale belly + pointed back
tufts + slanted eyes + wide white toothy grin), flagged against the public-release
direction. Owner chose to nudge the design before animating. New roll "Imp smirk v2",
character 6d667a1b-aefd-4ca3-b427-0b472c2bb7f6, pro mode, humanoid, size 96, low
top-down — the keeper prompt VERBATIM except three recorded swaps that remove the
lookalike markers while keeping the signed-off sly brow and colourway:

- "a wide toothy grin stretching most of the face" -> "a small closed smirk curling up at one corner of the mouth"
- "an irregular silhouette with a few pointed tufts breaking the outline" -> "two long pointed ears drooping outward and down from the sides of the head, a thin pointed tail ending in an arrowhead tip curling out behind the body, a single flame-shaped tuft of hair rising from the crown of the head"
- (nothing else changed)

NOTE: the arrowhead tail makes imp a TAILED SPECIES — the hands-check rule applies to
every animation from here on. PENDING owner review; the 98008edc keeper stays in
PixelLab as fallback until sign-off.

## OWNER RULING 2026-08-24, same day: "let's just [use] the original that we had for both"

The v2 re-roll was REVIEWED AND SET ASIDE. **The original 98008edc "Imp sly brow" stays
the keeper** with the Gengar resemblance flagged and accepted by the owner. 6d667a1b
"Imp smirk v2" stays in PixelLab unused (fallback if the public-release question ever
reopens). Phase 2 runs on 98008edc. The original has no tail — the tailed-species
hands-check note above applies only to the unused v2.

```
pixel art sprite of a small mischievous ghost imp creature standing on two short legs, two short arms and two short legs all four clearly separated from the body silhouette, a rounded head merging directly into a heavy rounded body, body widest at the base, saturated violet body with a paler lilac underside, narrow wedge-shaped eyes angled sharply down toward the centre of the face with a strong slanted brow ridge above each eye, looking straight ahead at the viewer, a small closed smirk curling up at one corner of the mouth, two long pointed ears drooping outward and down from the sides of the head, a thin pointed tail ending in an arrowhead tip curling out behind the body, a single flame-shaped tuft of hair rising from the crown of the head, the creature fills the frame from top edge to bottom edge, hard dark outline in a deep tinted shade of the violet body and never pure black, a tight palette of about a dozen colours in two hue families with one warm accent against the cool body, banded cel shading with three value steps, separate shade ramps for body and underside, large flat areas of colour, no dithering, solid white background
```

## Phase 2 2026-08-24 — croc/canonical prompts on keeper 98008edc, species word "imp"

Same recorded substitutions as the frill pack (the sanctioned pattern): species word
swapped throughout, garment clause DROPPED (no clothes), dance uses the arms wording,
prompts otherwise byte-identical to the frill log — see
assets/characters/frill/PROMPTS.md for the full texts. State face words follow the
species: "the narrow wedge eyes with their slanted brows and the wide toothy grin,
unchanged".

- Adult_Walk group a0d6ede2 (east/north-east/south-east; west halves are PC mirrors)
- Adult_Eat 0f2e54fe · Adult_Bathe 3e8bb920 · Adult_Pickup 656655fe
- Adult_Angry 3f862bfa · Adult_Sick 987ccaa3 · Adult_Bored a29aea21 · Adult_Tired c88f5dbe
- Adult_Love b7079234 · Adult_Hungry 5dc1f8f1 · Adult_Happy c7b3e68c
- Adult_Dance 1eb8def3 · Adult_Play 9c692f14
- Adult_Sit state fb103589 (96) · Adult_Sleep state 6e73d79b (override 120x120), both
  use_color_palette_from_reference=True

## What the rolls before it taught (all superseded, kept for the record)

- v3 mode produced soft blobby output no wording could fix. **pro is the default for a
  shaded character**; v3 was only ever right for the FLAT Bruna cast.
- "friendly / small / round / big round eyes / held at the sides" are CUTENESS
  instructions. Three accidental baby animals before that was spotted.
- "half-lidded" reads as stoned, not sly. Mischief is the **brow**: a slanted ridge and a
  wedge eye angled down toward the centre. Jon: "looks...stoned and not quite mischievous".
- Pure black in the palette is what made the earlier rolls look muddy next to Frill —
  the darkest tone must be a deep TINT of the body hue.
- Raw generator output looks AI-made because of 25-50 near-duplicate shades.
  `clean_sprite.py` is part of the style, not a polish step.
