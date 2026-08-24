# Dog pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
ADULT-ONLY pack (session doctrine 2026-08-18).

## Adult character v1 (tunic) — v3 create_character, humanoid, size 96, view "low top-down", detail "low detail", outline "single color black outline" — [FAIL on style + SUPERSEDED by wardrobe change]

Character id 37e3aae4-25ad-4d62-bca3-9454a00f436f ("Dog"), 3 generations. FAILURES at 4x:
eyes came back with WHITE SCLERA in most rotations (direct breach of "never open eyes with
white sclera"); smiling jowly mouth instead of a short line; noticeably heavier shading than
the croc contract; north view draws the tail over the garment. Kept in PixelLab as record.
If the overalls roll repeats the white eyes, the fix is the croc DETERMINISTIC PC-side eye
rule (white px in the head region with no pale-region neighbour painted dark), NOT a
CRITICAL eye clause — the croc v2 experiment proved that clause backfires.

```
A high-contrast vector illustration of a dog in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A dog character standing upright on two legs, rounded head with a short blunt rounded muzzle and two soft floppy ears hanging down the sides of the head, two small round eyes set far apart as solid black dots, a small solid black oval nose and a short black line mouth, and a short curved tail sticking up behind the body. Wearing a simple flat bright purple A-line tunic dress. Solid light brown fur, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

## WARDROBE CHANGE (owner, 2026-08-18): purple overalls replace the tunic, all packs

Before: Wearing a simple flat bright purple A-line tunic dress.
After:  Wearing purple overalls with a square bib and shoulder straps.

## Adult character v2 (overalls) — same settings, garment words swapped only — [PENDING]

Character id 3df87269-0d8f-4a92-a104-2d2e66d9921c ("Dog overalls"), 3 generations.

```
A high-contrast vector illustration of a dog in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A dog character standing upright on two legs, rounded head with a short blunt rounded muzzle and two soft floppy ears hanging down the sides of the head, two small round eyes set far apart as solid black dots, a small solid black oval nose and a short black line mouth, and a short curved tail sticking up behind the body. Wearing purple overalls with a square bib and shoulder straps. Solid light brown fur, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

### Overalls roll 1 (3df87269) — rotations never became downloadable during review; superseded by roll 2 before evaluation.

### Overalls roll 2 — strict garment clause — character 980deb9d-7704-4ee0-bc55-385ae4c4d385 ("Dog overalls v2"), 3 gens — [PENDING]
After: Wearing plain bright purple overalls with a square bib and two shoulder straps, no pockets, no buttons, no shirt underneath, bare arms and shoulders.

## OWNER SIGN-OFF 2026-08-18 (curated in PixelLab): KEEPER = 3df87269 "Dog overalls" (roll 1)

Verified at 4x after the keep: solid black dot eyes with brow patches, floppy ears, oval
nose, closed frown mouth, bib pocket (ratified), tail behind in every view.

## Phase 2 on the keeper — croc/canonical prompts, species word "dog"

- Adult_Sit state fc9befa2 (96px) · Adult_Sleep state 393911a4 (override 120x120)
- Adult_Walk east group cb78959e (west mirrored at install)
- Adult_Eat 5eb1e62f · Adult_Bathe 745ad9eb · Adult_Pickup be2e5890
- Adult_Angry ea3958c1 · Adult_Sick 029144f2 · Adult_Bored 400cd940 · Adult_Tired 613c7b50
- Adult_Love 77905d3a · Adult_Hungry a645825b · Adult_Happy 2cb70851

### Adult_Pickup roll 1 (be2e5890) — [FAIL-REROLL] owner live review: "dog adult pickup has
### his tail in his hand." Deleted (delete-first), re-issued byte-identical — group 2c119c34.
### New standing doctrine: tailed species get a per-frame hands-check on every animation.

### [FAIL-RETRY] Both dog states failed: "Failed to download north image: HTTP 404" — the
### keeper's north rotation synced late server-side and the state jobs raced it. One
### identical retry each per doctrine: Adult_Sit 141371b3 (was fc9befa2), Adult_Sleep
### a2990741 (was 393911a4, override 120). Prompts byte-identical.

### [FAIL-REROLL] Both dog state retries failed again — a DIFFERENT keeper rotation 404d
### each time (north, then north-west): the keeper's stored rotation set is broken
### server-side, an infrastructure fault, not an art fault. Escalation (croc reference-fix
### lineage): rebuilt a healthy rotation source as "Dog state source" 21244f11 — v3
### create_character seeded with the keeper's own clean SOUTH rotation as reference_image,
### prompt = the keeper's overalls prompt verbatim. States derive from that source; the
### installed Adult_Idle stays the keeper's own art.

### Dog states, roll 3 off "Dog state source" 21244f11: Adult_Sit 2dec732d (96px),
### Adult_Sleep fc80a340 (override 120x120). Both prompts add the sanctioned garment
### stability clause " Still wearing the same purple overalls with the square bib and
### shoulder straps." (the penguin's sit retry undressed it without this clause).

### [FAIL-REROLL] Adult_Love roll 1 (77905d3a): hands-check FAIL — the dog cradled its TAIL
### in its arms across the chest in frames 4-7 ("clasping its hands together" became
### clasping the tail; third tail-in-hand strike overall). Deleted (delete-first);
### re-issued byte-identical — group 8189a935.
### Hands-check results, all other dog clips: Walk/Eat/Bathe/Angry/Sick/Tired/Hungry/Happy
### PASS (tail behind, hands empty). Bored: tail sweeps low in FRONT of the legs in frames
### 4-6 but is never held — PASS with note. Pickup roll 2 (2c119c34): PASS on hands; frame
### 06 had an 81px detached tail fragment, scrubbed by connected-component check.

## PACK COMPLETE 2026-08-18 — installed to assets/characters/dog in the croc layout
(clean --dist 70 --keep-outline --norm 96; garment pair-swapped to #9151d3/#5d229d at
install; Adult_Walk_West mirrored from the cleaned east). Verified by rendering and
looking; details and flags in run.json.

## 2026-08-23 — Adult_Dance (audit fill-in; the clip `dance/anim` really wants)

Keeper `21244f11-9d54-4e4c-a128-6d26888f8982`, v3, south only, frame_count 6 (7 stored), 1 generation.
Animation group `55e88a26-85b0-451d-80e6-962471a3069d`.

The penguin's verified dance prompt with the species word substituted — the sanctioned
Phase-2 pattern ("croc/canonical prompts, species word") — plus the one body-part swap
"flipper wings" -> "arms", the same precedent as the beak-for-mouth substitution.

```
The dog stands upright and dances on the spot, bouncing rhythmically from one foot to the other while both arms lift and swing outward to the sides in time with the bounce, the head bobbing gently with each beat, then returning to the neutral upright posture so the motion repeats seamlessly. Empty hands, nothing in the mouth, no props.
```

Install: raw came back **112x112** (v3 output size varies per character — measure, never assume).
Cleaned `--dist 70 --keep-outline`, NO `--norm` (normalise_feet shifts each frame separately and
would flatten the bounce). One shared offset dx=8 dy=4 for all 7 frames, anchored on the clip's
LOWEST extent so nothing sits below this pack's floor row 90. Zero opaque px clipped. Lift 5px.
Garment mapped as an explicit PAIR to #9151d3/#5d229d — nearest-colour matching flattened the
cat's base+shade into one value and sent the frog's shade to the wrong colour, which is exactly
the failure the pair doctrine warns about. Everything else mapped to the pack's nearest existing
colour, with a guard that refuses to collapse two source colours into one.
check_pack: PASS, anchor purples missing in 0 frames.

## 2026-08-23 — Adult_Walk_NorthEast / _SouthEast (audit fill-in: the VERTICAL walk)

Why: `walk-north/anim` and `walk-south/anim` had no source clip in this pack, so walking up or
down the screen fell through `charSpriteKey`'s idle rescue — he GLIDED in his idle pose instead
of walking. Only the croc and capybara ever had the diagonals.

Keeper `21244f11-9d54-4e4c-a128-6d26888f8982`, v3, directions north-east + south-east in ONE group
`37fd3f67-8fef-481b-a22a-253fcb2a12fe`, frame_count 6 (7 stored). 2 generations.
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
#9151d3/#5d229d. check_pack PASS; `mkspecies.py dog` now reports no MISSING clips.

## 2026-08-23 — Adult_Play (audit fill-in; `play/anim` was substituting an emote)

Keeper `21244f11-9d54-4e4c-a128-6d26888f8982`, v3, south, frame_count 6 (7 stored). Group `1085d287-b657-4e04-be70-55e41e8d0841`.
Structure reuses the cat's recorded Adult_Play scaffolding (crouch -> wiggle -> spring -> settle,
"Keep the face exactly identical to the source", garment-fixed clause) with the action changed;
the cat's own clip is a cat-specific batting/pouncing move that does not transfer verbatim.

```
The dog crouches down slightly, wiggles briefly, then springs back up and claps its hands together quickly several times in front of its chest while bouncing on the spot, then settles back into its resting posture. Keep the face exactly identical to the source. Its purple outfit remains fixed in place. Empty hands, nothing held, no props.
```

**ROLL 1 FAILED — THE NAMING-SUMMONS DOCTRINE, EXACTLY.** The first prompt said "bats at the AIR"
and "Empty hands, no props", and the generator summoned props anyway: the dog got a cream ball on his
chest (frame 3) and a small purple object in his raised paw (frames 4-5); the capybara got a tan object
at its hand (frame 4). Naming an action that IMPLIES a target is enough - you do not have to name the
toy. Roll 2 replaced the batting with CLAPPING, which implies no target, and came back clean. Do NOT
add "no ball" to the prompt: naming it summons it.

Install: cleaned `--dist 70 --keep-outline`, no `--norm`; one shared offset anchored on the
clip's lowest extent onto this pack's floor row; zero opaque px clipped; garment mapped as an
explicit pair to #9151d3/#5d229d. check_pack PASS.
