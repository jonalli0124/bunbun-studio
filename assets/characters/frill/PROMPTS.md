# Frill pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
ADULT-ONLY pack. Second pack in the SHADED direction (after Imp). NO GARMENT —
check_pack runs with `--no-garment` (Jon 2026-08-21: hands get placed in
tools/attach_editor.html; the purple pair-swap does not apply).

## KEEPER = 2c74bef9-ac49-469f-808a-f562dbb98e47 "Frill bipedal pro"

Created in an earlier session, mode "pro", humanoid, size 96, view "low top-down".
**The creation prompt was never recorded** — the keeper predates this log. Ratified as
keeper by the owner on 2026-08-24 ("we need to finish the animations for imp, sparktuft,
and frill"), and confirmed against the IP check the same day (reads as a generic
axolotl/dragon — no lookalike concern; Imp and Sparktuft were re-rolled, Frill was not).

Appearance (recorded from looking at the rotations at 4x, for future prompt work): a small
teal bipedal axolotl-like creature, branching gill frills on both sides of the head, a
spiky ridge crest, cream belly, orange eyes, a long thick tail curling behind with a
paler fin edge. TAILED SPECIES — the hands-check rule from GENERATION.md applies to every
animation before install.

## Adult_Idle install 2026-08-24

The keeper's 8 rotations through `clean_sprite.py --dist 70 --keep-outline --norm 96`.
22 -> 8 colours; all 8 views feet-flat at row 72 (this pack's floor row), ink heights
51 all round, widths 39-47 (front-vs-profile variance, not drift). Palette:
#399290/#1b5962 body pair, #eeeaca/#cdc09c belly pair, #03121b outline, #818f7e,
#f49517/#f0c459 eye pair.

## Phase 2 — croc/canonical prompts, species word "axolotl"

Recorded substitutions, per the sanctioned croc/penguin precedent ("face follows the
species", one edit recorded once):
- Species word: "The axolotl ..." for "The crocodile ..." throughout.
- The garment clause ("Its purple outfit remains fixed in place.") is DROPPED from every
  prompt that carried it — Frill wears nothing. Dropping a clause, never inventing one.
- Dance: the croc's arms-for-flippers substitution reused as-is ("both arms lift and
  swing outward").
- Play: croc scaffolding, "with one hand" kept.

### walk (v3, frame_count=6, directions east / north-east / south-east in one group
### 2eeb84c2; west / north-west / south-west are PC mirrors) — croc line VERBATIM
```
walking forward with a long exaggerated stride, legs swinging far forward and far back, feet lifting high and clear of the ground on each step. Empty hands, nothing in the mouth, no props.
```

### eat (v3, south, fc6) — group 75e2fae7
```
The axolotl stands upright and steady, maintaining a calm forward gaze as it begins a rhythmic chewing motion. Its jaw moves up and down, causing its cheeks to puff slightly with each bite. Simultaneously, the axolotl brings its hands toward its mouth in time with the chewing, then lowers them back to its sides. Throughout the motion, it occasionally tilts its head slightly to one side before returning to its neutral, centered posture, creating a seamless and repetitive eating animation. Empty hands, nothing in the mouth, no props, no food.
```

### bathe (v3, south, fc6) — group 7964ed3c — outfit clause dropped, mouth clause kept
```
The axolotl stands still and takes a deep breath, its chest gently expanding and contracting. It then tilts its head slightly to the side and begins to rhythmically sway from left to right as if gently scrubbing itself, with its mouth remaining a short closed line throughout. After a few moments of swaying, the axolotl settles back into its initial upright, forward-facing posture, blinking its eyes once before returning to a calm, steady state. Empty hands, mouth shut, no props, no tub, no bubbles, no soap.
```

### pickup (v3, south, fc6) — group 022de6dd — croc line VERBATIM
```
bending down to pick something up from the ground, then straightening back up. Empty hands in every frame, holding nothing, no props.
```

### angry (v3, south, fc6) — group d19b7b5e
```
The axolotl stands in place and plants its feet, leaning forward slightly with its fists clenched at its sides, shaking briefly with frustration, then stomps one foot before settling back upright. Its eyes become sharply slanted slits tilting down toward the centre of the face and its mouth becomes a short frown. Empty hands, mouth shut, no props, no steam, no smoke.
```

### sick (v3, south, fc6) — group 14686797
```
The axolotl stands slightly hunched, swaying gently and unsteadily from side to side as if queasy, its shoulders drooping, then wobbles back to its upright posture. Its eyes become droopy half-closed curves sagging downward and its mouth becomes a small wavy line. Empty hands, no props, no clouds, no bubbles.
```

### bored (v3, south, fc6) — group 7c6bfffb
```
The axolotl stands in place and slumps, its shoulders and head sinking slowly downward, then it shifts its weight from one foot to the other before returning to its slumped posture. Its eyes become flat half-lidded lines looking off to one side and its mouth becomes a small flat line. Empty hands, mouth shut, no props.
```

### tired (v3, south, fc6) — group 51e587aa
```
The axolotl stands drowsily in place, its head and shoulders drooping lower and lower, then it tips its head back in a big slow yawn before drooping forward again. Its eyes become closed downward-curving arcs and its mouth opens into a small oval mid-yawn, then closes again. Empty hands, no props.
```

### love (v3, south, fc6) — group 5a58ac2f
```
The axolotl stands upright and gives a happy little wiggle, swaying side to side and clasping its hands together in front of its chest, then rocks gently back to its resting posture. Its eyes become upward-curving happy arcs and its mouth becomes a wide smile. Empty hands, no props, no hearts.
```

### hungry (v3, south, fc6) — group 0fbad99f
```
The axolotl stands upright and looks around eagerly, rubbing its belly with one hand in slow circles, then returns to its neutral posture. Its eyes become round wide-open dots and its mouth becomes a small round oval. Empty hands, nothing in the mouth, no props, no food.
```

### happy (v3, south, fc6) — group e0ffd5d8 — the bunny's COMPOSED-ONCE happy, species word swapped
```
The axolotl stands upright and bounces in a small happy hop in place, raising both arms up overhead, then lands softly and settles back into its resting posture. Its eyes become upward-curving happy arcs and its mouth becomes a wide smile. Empty hands, no props, no sparkles, no confetti.
```

### dance (v3, south, fc6) — group 4dc3e7d1 — penguin's verified dance, arms substitution per croc
```
The axolotl stands upright and dances on the spot, bouncing rhythmically from one foot to the other while both arms lift and swing outward to the sides in time with the bounce, the head bobbing gently with each beat, then returning to the neutral upright posture so the motion repeats seamlessly. Empty hands, nothing in the mouth, no props.
```

### play (v3, south, fc6) — group f1b91ed2 — croc scaffolding, outfit clause dropped
```
The axolotl crouches down slightly, wiggles briefly, then springs back up and playfully bats at the air in front of it with one hand, swiping quickly downward several times while bouncing on the spot, then settles back into its resting posture. Keep the face exactly identical to the source. Empty hands, no props.
```

## State prompts — face words follow the species (croc/penguin precedent): Frill's face
## is "the two orange eyes and the short mouth line". Outfit clause dropped, no garment.

### Adult_Sit (create_character_state, 96, use_color_palette_from_reference=True) — state character 0ba77cdd
```
Sitting down on the ground, legs extended forward. Keep the face exactly identical to the source - the two orange eyes and the short mouth line, unchanged. Empty hands, nothing in the mouth, no props.
```

### Adult_Sleep (create_character_state, override 120x120, use_color_palette_from_reference=True) — state character 0a1af7d0

**OWNER RULING 2026-08-24 (applies to every pack): "i know the prompt says eyes should
always be open but for sleep that shouldnt be the case."** Sleep states get CLOSED eyes,
overriding the face-stability clause. Applied to this pack as a deterministic PC-side
fix (the croc eye-fix tradition): every pixel of the eye pair #f49517/#f0c459 in the 8
installed sleep views was replaced with body #399290, with the cluster's bottom row
drawn in outline #03121b as the closed lid. Verified by rendering at 4x. Future sleep
states should ask for closed eyes in the prompt instead (see sparktuft).
```
Lying down on the ground, curled on one side, head resting on the ground, legs tucked in close to the body. Keep the face exactly identical to the source - the two orange eyes and the short mouth line, unchanged. Empty hands, nothing in the mouth, no props.
```

### Adult_Sleep REDO 2026-08-24 (create_character_state, override 120x120, use_color_palette_from_reference=True) — state character 54d8cbc4

**Owner, same day: "i dont love the pc side solution because it isnt quite right always
just like the fix for the sad dog... there are weird blemishes."** The pixel fix above
recoloured the iris but left the open-eye OUTLINES, so he still read awake (and slightly
cross). PC-side face fixes are retired; regenerate instead. The prompt is the original
sleep prompt with ONLY the eye words swapped for sparktuft's proven closed clause:
```
Lying down on the ground, curled on one side, head resting on the ground, legs tucked in close to the body. Both eyes fully closed as short dark curved lines. Keep the rest of the face exactly identical to the source - the short mouth line, unchanged. Empty hands, nothing in the mouth, no props.
```

## 2026-08-25 (late 8/24 night) — Baby Onesie preview roll — [PENDING owner review]

Character 568025f8 ("Frill Baby Onesie"): pro 25 gens, style_character=keeper 2c74bef9, composed from recorded appearance.
The croc-pilot baby clauses folded in verbatim: "with the head noticeably larger
relative to the body, about half the total height, and short stubby arms and legs" +
"Wearing a simple flat bright purple onesie, one piece covering the whole body, short
sleeves and short legs." South idle shown in the 8/24 nursery sheet; nothing installed.

## 2026-08-25 (overnight) — THE BABY SET, owner-signed (states on the keeper, capybara model)

Target structure (owner): 1 character, 3 adult states, 4 baby states; baby walk is CRAWL.
Baby_Onesie 52a97a58 = a WARDROBE STATE on the keeper (the earlier standalone-create
babies were owner-rejected and deleted: "i expected you to create states of the main
character"). Onesie clause, owner-ruled FOOTED ("make sure the onesie doesnt show their
feet. it is to add to the cuteness"): "Wearing a simple flat bright purple onesie, one
piece covering the whole body, short sleeves, and closed footed legs covering the feet
completely like footed pajamas." Derived from the signed onesie with the species face
words and the footed-hold clause "whose closed footed legs still cover the feet
completely" (proven on capybara): Baby_Sit 9d628d6d (96) · Baby_Sleep 981d267a
(override 120x120, the closed-eye clause) · Baby_Crawl 1ae70d1b (96). All
use_color_palette_from_reference=True except the onesie itself. Reviewed in the 8/25
grid sheets; baby SIZE is render-time scale, never art. Nothing installed to the pak yet.

## 2026-08-25 — THE BABY EMOTES (v3, south, fc6, on the Baby_Onesie character)

Per the croc-pilot rule, the emote prompts ran BYTE-IDENTICAL across adult and baby —
the species' own recorded texts (with their documented face-word adaptations: dog's
smile mouth-hold in bathe, spark/imp's narrow-eye lines in hungry/love, frill's
dropped outfit clause) fired on the Baby_Onesie character, whose onesie and palette
carry through the animation automatically. The baby set is the croc-proven eight
(eat, bathe, angry, sick, bored, tired, love, hungry) plus play (each species' own
object-free play). One server-side loss (spark bored) re-fired byte-identical.
Reviewed in "The Baby Emotions" artifact; nothing installed to the pak yet.

## Group-id ledger (2026-08-25 audit — every live group on the Baby_Onesie char):
baby emotes: play ba0bc057 · hungry cf982ca3 · love cab45f28 · tired a62b1778 · bored 32878ed7 · sick b0dbb95e · angry 01cdf2e2 · bathe 9a5c1235 · eat b5973fbe
baby happy (hop-overhead, canonical, awaiting sign-off): 1d4c0e80
baby crawl (SE+NE diagonals; stray east dirs deleted 8/25): f87dcb77

## Correction (8/25 audit): Adult_Bathe live group is 3c2df85f, not the logged 7964ed3c
(which does not exist on the keeper).

## 2026-08-25 evening: Baby_Happy INSTALLED (owner signed the review page) - the baby set is TEN emotions.

## 2026-08-25 late: THE TEEN (owner-signed lineup). Teen_Idle state a40cb92f (v2, one head frill folded)
on the keeper: short-sleeved bright orange t-shirt, fur/feathers slightly disheveled
with small tufts on the crown, and one frilly head gill folded over drooping forward. The V2 spiky-tuft look IS the ruling
(soft/contained variants lost). INSTALLED as Teen_Idle/ (8 rotations, feet y=90);
ships via the mkspecies teen-idle toehold. Teen size stays render-time SCALE_TEEN.

## 2026-08-25 late: WARDROBE -> LIGHT BLUE (owner order "set all clothes to light
blue"): the anchored pair is now #82c4ea/#4c7fb5 (was #9151d3/#5d229d). Adults and
onesies swapped together; teens stay orange; imp exempt. Exact both ways.

## 2026-08-26: THE TEEN SET COMPLETE (owner signed the sheets: "Go for it").
Idle a40cb92f · Teen_Sit 51187f78 · Teen_Sleep ae103780 (120x120, closed-eye clause).
Anims (v3 south fc6 on the idle char; walk 4 dirs E/NE/SE/NW fc6, W+SW mirrored):
eat efb79e4a · bathe aa116f88 · angry 003a7d7c · sick 909c1291 · bored ccc0ab6e · tired f09b8c79 · love c92ea32d · hungry 7b771edc · play 53f659f8 · happy 07d29554 · dance 545692bd · pickup 9df47239 · walk 7c69d1ac
Tee anchored to the teen wardrobe pair #fd9a05/#df5b0f; fur twins unified onto the adult
palette; PROOF: zero pair pixels outside Teen_ clips fleet-wide. Roster: 21 clips, a
mirror of count and type of the adult set.
