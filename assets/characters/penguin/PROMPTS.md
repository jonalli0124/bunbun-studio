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
