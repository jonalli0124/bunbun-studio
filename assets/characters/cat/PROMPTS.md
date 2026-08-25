# Cat pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
ADULT-ONLY pack (session doctrine 2026-08-18). The cat doubles as the visiting cat skin.

## Adult character v1 (tunic) — v3 create_character, humanoid, size 96, view "low top-down", detail "low detail", outline "single color black outline" — [SUPERSEDED — wardrobe changed to overalls before sign-off]

Character id 7274d14a-621d-4c39-85ff-3055982264cf ("Cat"), 3 generations. Art read well at
4x: pointed ears, curled tail (attached at the back in every view, not held), dot eyes,
triangular nose. One drift noted: tabby STRIPES on the fur — interior detail beyond the croc
contract. Kept in PixelLab as identity fallback.

```
A high-contrast vector illustration of a cat in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A cat character standing upright on two legs, rounded head with two pointed triangular ears standing up on top of the head, two small round eyes set far apart as solid black dots, a small solid black triangular nose and a short black line mouth, and a long slender tail with a slightly curled tip, curving up behind the body. Wearing a simple flat bright purple A-line tunic dress. Solid orange fur, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

## WARDROBE CHANGE (owner, 2026-08-18): purple overalls replace the tunic, all packs

Before: Wearing a simple flat bright purple A-line tunic dress.
After:  Wearing purple overalls with a square bib and shoulder straps.
Only the garment words changed; the same two purples remain the keying/anchor constants.

## Adult character v2 (overalls) — same settings, garment words swapped only — [PENDING]

Character id 8b8523bf-d5d1-43cd-bd5d-8c3c42f3cd4c ("Cat overalls"), 3 generations.

```
A high-contrast vector illustration of a cat in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A cat character standing upright on two legs, rounded head with two pointed triangular ears standing up on top of the head, two small round eyes set far apart as solid black dots, a small solid black triangular nose and a short black line mouth, and a long slender tail with a slightly curled tip, curving up behind the body. Wearing purple overalls with a square bib and shoulder straps. Solid orange fur, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

### Overalls roll 1 (8b8523bf) — [FAIL]
Bare arms (good), but a stitched bib pocket (owner rule: no pockets), long whisker lines
(interior detail the tunic roll did not have), smiling mouth, slanted-dash eyes on south.

### Overalls roll 2 — strict garment clause — character 4ff6a69c-c034-419b-82fc-10a6f83c40d1 ("Cat overalls v2"), 3 gens — [PENDING]
After: Wearing plain bright purple overalls with a square bib and two shoulder straps, no pockets, no buttons, no shirt underneath, bare arms and shoulders.

## OWNER SIGN-OFF 2026-08-18 (curated in PixelLab): KEEPER = 8b8523bf "Cat overalls" (roll 1)

The owner kept the roll-1 overalls cat — tabby stripes, whiskers, smile and bib pocket are
RATIFIED by the keep (owner-ratified detail overrides the flat contract). The strict v2
(4ff6a69c) and tunic v1 (7274d14a) were deleted... v1 remains listed in PixelLab but is
not used. This cat also skins the game's visiting cat, so Sleep and Sit matter doubly and
it gets the extra Adult_Play batting clip.

## Phase 2 on the keeper — prompts are the croc/canonical set with the species word "cat";
## state prompts use the croc line-mouth wording verbatim

- Adult_Sit state 71a72048 (96px) · Adult_Sleep state ffe79ecd (override 120x120)
- Adult_Walk east group 95cde556 (west mirrored at install)
- Adult_Eat 11c74f9a · Adult_Bathe aa8332bf · Adult_Pickup 7feb6fd9
- Adult_Angry 974105b3 · Adult_Sick d269579d · Adult_Bored 694af05e · Adult_Tired 36c12570
- Adult_Love 8d8195ee · Adult_Hungry aa2886ce · Adult_Happy c5204105

### Adult_Play — COMPOSED ONCE this session (the batting/pouncing play clip, cat only). v3, south, frame_count=6, group 4c7361b0
```
The cat crouches down slightly with its rear swaying, wiggles briefly, then springs back up and playfully bats at the air in front of it with one front paw, swiping quickly downward several times while its tail swishes from side to side behind it, then settles back into its resting posture. Keep the face exactly identical to the source. Its purple outfit remains fixed in place. Empty hands, no props.
```
(Per the naming-summons doctrine the prompt names no toy, ball or yarn.)

## PACK COMPLETE 2026-08-18 — installed to assets/characters/cat in the croc layout
(clean --dist 70 --keep-outline --norm 96; garment pair-swapped to #9151d3/#5d229d at
install; Adult_Walk_West mirrored from the cleaned east). Verified by rendering and
looking; details and flags in run.json.

## 2026-08-23 — Adult_Dance (audit fill-in; the clip `dance/anim` really wants)

Keeper `8b8523bf-d5d1-43cd-bd5d-8c3c42f3cd4c`, v3, south only, frame_count 6 (7 stored), 1 generation.
Animation group `ba3ae46e-4c18-430b-a4c2-d5df8744524d`.

The penguin's verified dance prompt with the species word substituted — the sanctioned
Phase-2 pattern ("croc/canonical prompts, species word") — plus the one body-part swap
"flipper wings" -> "arms", the same precedent as the beak-for-mouth substitution.

```
The cat stands upright and dances on the spot, bouncing rhythmically from one foot to the other while both arms lift and swing outward to the sides in time with the bounce, the head bobbing gently with each beat, then returning to the neutral upright posture so the motion repeats seamlessly. Empty hands, nothing in the mouth, no props.
```

Install: raw came back **104x104** (v3 output size varies per character — measure, never assume).
Cleaned `--dist 70 --keep-outline`, NO `--norm` (normalise_feet shifts each frame separately and
would flatten the bounce). One shared offset dx=4 dy=-1 for all 7 frames, anchored on the clip's
LOWEST extent so nothing sits below this pack's floor row 89. Zero opaque px clipped. Lift 5px.
Garment mapped as an explicit PAIR to #9151d3/#5d229d — nearest-colour matching flattened the
cat's base+shade into one value and sent the frog's shade to the wrong colour, which is exactly
the failure the pair doctrine warns about. Everything else mapped to the pack's nearest existing
colour, with a guard that refuses to collapse two source colours into one.
check_pack: PASS, anchor purples missing in 0 frames.

## 2026-08-23 — Adult_Walk_NorthEast / _SouthEast (audit fill-in: the VERTICAL walk)

Why: `walk-north/anim` and `walk-south/anim` had no source clip in this pack, so walking up or
down the screen fell through `charSpriteKey`'s idle rescue — he GLIDED in his idle pose instead
of walking. Only the croc and capybara ever had the diagonals.

Keeper `8b8523bf-d5d1-43cd-bd5d-8c3c42f3cd4c`, v3, directions north-east + south-east in ONE group
`a38dd49a-ff91-46cb-9d6f-33bc912eb769`, frame_count 6 (7 stored). 2 generations.
West halves (NorthWest / SouthWest) are PC mirrors, not generations — the recorded croc rule.

Prompt is the croc's walk line reused VERBATIM (it is direction-agnostic; the direction is the
API parameter, not the wording):

```
walking forward with a long exaggerated stride, legs swinging far forward and far back, feet lifting high and clear of the ground on each step. Empty hands, nothing in the mouth, no props.
```

Install: raw 112x112. BOTH directions cleaned in ONE `clean_sprite` pass (`os.walk` recurses, so
one shared palette across the whole set) — cleaning them separately produced two palettes whose
near-duplicates then collapsed onto single pack colours. `--dist 70 --keep-outline`, no `--norm`.
Shared offset per direction anchored on that direction's LOWEST extent onto pack floor 89
(NE dy=5, SE dy=2). Zero opaque px clipped. Garment mapped as an explicit pair to
#9151d3/#5d229d. check_pack PASS; `mkspecies.py cat` now reports no MISSING clips.

## 2026-08-24 — Adult_Sleep regenerated with closed eyes (PC-side face fixes retired)

The 8/24 pixel eye-fix is superseded (owner: "weird blemishes"). Regenerated as a fresh
state on the Idle keeper — state bdf4840f, override 120x120,
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

Character 210d678c ("Cat Baby Onesie"): v3, ~3 gens, adult creation prompt + baby clauses + onesie.
The croc-pilot baby clauses folded in verbatim: "with the head noticeably larger
relative to the body, about half the total height, and short stubby arms and legs" +
"Wearing a simple flat bright purple onesie, one piece covering the whole body, short
sleeves and short legs." South idle shown in the 8/24 nursery sheet; nothing installed.

## 2026-08-25 (overnight) — THE BABY SET, owner-signed (states on the keeper, capybara model)

Target structure (owner): 1 character, 3 adult states, 4 baby states; baby walk is CRAWL.
Baby_Onesie b29cc958 = a WARDROBE STATE on the keeper (the earlier standalone-create
babies were owner-rejected and deleted: "i expected you to create states of the main
character"). Onesie clause, owner-ruled FOOTED ("make sure the onesie doesnt show their
feet. it is to add to the cuteness"): "Wearing a simple flat bright purple onesie, one
piece covering the whole body, short sleeves, and closed footed legs covering the feet
completely like footed pajamas." Derived from the signed onesie with the species face
words and the footed-hold clause "whose closed footed legs still cover the feet
completely" (proven on capybara): Baby_Sit 34624b26 (96) · Baby_Sleep 3dd62c3f
(override 120x120, the closed-eye clause) · Baby_Crawl 59f76a62 (96). All
use_color_palette_from_reference=True except the onesie itself. Reviewed in the 8/25
grid sheets; baby SIZE is render-time scale, never art. Nothing installed to the pak yet.

### Baby_Sleep — FOUR ROLLS 2026-08-25 (owner: "Cat sleep is a little weird")

Roll 1 (3dd62c3f, the batch prompt): onesie swallowed the body into a purple ball in
half the views — DELETED. Roll 2 (b7edea84, verbatim retry per doctrine): better, but
NW/W pure blobs and NE head raised awake — DELETED. Roll 3 (9df21b35, added "head and
tail rest outside the onesie and stay visible in every view"): the visibility clause
pulled her UPRIGHT — five views dozing sitting up — DELETED. Roll 4 (3c6b2cdc, KEPT):
"Lying flat on the ground on one side, curled up, never sitting up, in every view. The
head rests low on the ground with the pointed ears and face visible outside the onesie,
and the striped orange tail curls around the body outside the onesie." + the standard
closed-eye/footed clauses. All 8 views lying; residual quirks accepted: north shows the
face where the back of the head would be, south-east tucks the face away.
LESSON: a visibility clause without a posture pin RAISES the pose; pin "lying flat...
never sitting up" whenever asking for parts to stay visible in a lying state.

## 2026-08-25 — THE BABY EMOTES (v3, south, fc6, on the Baby_Onesie character)

Per the croc-pilot rule, the emote prompts ran BYTE-IDENTICAL across adult and baby —
the species' own recorded texts (with their documented face-word adaptations: dog's
smile mouth-hold in bathe, spark/imp's narrow-eye lines in hungry/love, frill's
dropped outfit clause) fired on the Baby_Onesie character, whose onesie and palette
carry through the animation automatically. The baby set is the croc-proven eight
(eat, bathe, angry, sick, bored, tired, love, hungry) plus play (each species' own
object-free play). One server-side loss (spark bored) re-fired byte-identical.
Reviewed in "The Baby Emotions" artifact; nothing installed to the pak yet.
