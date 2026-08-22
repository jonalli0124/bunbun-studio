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
