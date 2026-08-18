# Bunny pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
Status marks: [PASS] verified good, [FAIL] rejected (kept for the record), [PENDING] awaiting review.

ADULT-ONLY pack (session doctrine 2026-08-18: "kill the baby stuff... only one age and it can be adult").
The bunny is the original mascot returning as a selectable species — a homage to the classic
bunbun look: white/grey fur, dot eyes, small black X mouth, vertical capsule ears.

## Adult character v1 — v3 create_character, humanoid, size 96, view "low top-down", detail "low detail", outline "single color black outline" — [PENDING]

Character id d0529bec-e029-4b91-a5a5-1fd9c97c01af ("Bunny"), 3 generations.
Skeleton copied from the croc/capybara proven prompt: [style clause] [species body clause]
[garment clause] [palette clause] — only the species body clause differs. Recorded geometry
edits (croc precedent — geometry words follow the species): "eyes set close together near the
centre of the face" (classic bunbun face layout, vs "set far apart"), "a small black X mouth"
(the bunbun signature, vs "a short black line mouth"), fur "pale grey" (white fur on the solid
white background would kill background separation; the order sanctions "white/grey").

```
A high-contrast vector illustration of a rabbit in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A rabbit character standing upright on two legs, simple rounded oval head, two identical tall vertical capsule ears standing straight up from the top of the head, two small round eyes set close together near the centre of the face as solid black dots, a small black X mouth, and a small round fluffy tail behind the body. Wearing a simple flat bright purple A-line tunic dress. Solid pale grey fur, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

CHECKPOINT A (self-review, 2026-08-18): [PASS] — all 8 rotations one animal; dot eyes close
together, X mouth on every front view, capsule ears, purple keying tunic, pale grey fur, round
cotton tail. Note: the tail sits ON the dress in back views rather than emerging under the hem
(croc rule); kept — it reads as the classic look and re-rolling risks the croc's giant-back-tail
failure. Adult base = "Bunny" d0529bec-e029-4b91-a5a5-1fd9c97c01af.

## State prompts — croc verbatim, ONE recorded substitution: "a small black X mouth" for
## "a short black line mouth" (the bunbun face follows the species, croc precedent)

### Adult_Sit (state ae3468e6, use_color_palette_from_reference=True, 96px) — [PENDING]

```
Sitting down on the ground, legs extended forward. Keep the face exactly identical to the source - two small solid black dot eyes and a small black X mouth, unchanged. Empty hands, nothing in the mouth, no props.
```

### Adult_Sleep (state f87f4db5, use_color_palette_from_reference=True, override 120x120 — the pose is known to clip at 96) — [PENDING]

```
Lying down on the ground, curled on one side, head resting on the ground, legs tucked in close to the body. Keep the face exactly identical to the source - two small solid black dot eyes and a small black X mouth, unchanged. Empty hands, nothing in the mouth, no props.
```

## Canonical animation prompts (croc/capybara verbatim, species word swapped only). All v3, frame_count=6.

### walk (directions east / north-east / south-east; west, north-west, south-west are PC mirrors) — group f3c7c580

```
walking forward with a long exaggerated stride, legs swinging far forward and far back, feet lifting high and clear of the ground on each step. Empty hands, nothing in the mouth, no props.
```

### eat (south) — group 3bd17ae1

```
The rabbit stands upright and steady, maintaining a calm forward gaze as it begins a rhythmic chewing motion. Its jaw moves up and down, causing its cheeks to puff slightly with each bite. Simultaneously, the rabbit brings its hands toward its mouth in time with the chewing, then lowers them back to its sides. Throughout the motion, it occasionally tilts its head slightly to one side before returning to its neutral, centered posture, creating a seamless and repetitive eating animation. Empty hands, nothing in the mouth, no props, no food.
```

### bathe (south) — group 0157f9fb — recorded substitution: "its mouth remaining a small closed X shape throughout" (X-mouth follows the species)

```
The rabbit stands still and takes a deep breath, its chest gently expanding and contracting. It then tilts its head slightly to the side and begins to rhythmically sway from left to right as if gently scrubbing itself, with its purple outfit remaining fixed in place and its mouth remaining a small closed X shape throughout. After a few moments of swaying, the rabbit settles back into its initial upright, forward-facing posture, blinking its eyes once before returning to a calm, steady state. Empty hands, mouth shut, no props, no tub, no bubbles, no soap.
```

### pickup (south) — group 3b3e6bb7

```
bending down to pick something up from the ground, then straightening back up. Empty hands in every frame, holding nothing, no props.
```

## Emotion animation prompts — croc's COMPOSED-ONCE set, species word swapped only. v3, south, frame_count=6.

### angry — group ed43256c
```
The rabbit stands in place and plants its feet, leaning forward slightly with its fists clenched at its sides, shaking briefly with frustration, then stomps one foot before settling back upright. Its eyes become sharply slanted slits tilting down toward the centre of the face and its mouth becomes a short frown. Its purple outfit remains fixed in place. Empty hands, mouth shut, no props, no steam, no smoke.
```

### sick — group 69fea26f
```
The rabbit stands slightly hunched, swaying gently and unsteadily from side to side as if queasy, its shoulders drooping, then wobbles back to its upright posture. Its eyes become droopy half-closed curves sagging downward and its mouth becomes a small wavy line. Its purple outfit remains fixed in place. Empty hands, no props, no clouds, no bubbles.
```

### bored — group 09fb5f7d
```
The rabbit stands in place and slumps, its shoulders and head sinking slowly downward, then it shifts its weight from one foot to the other before returning to its slumped posture. Its eyes become flat half-lidded lines looking off to one side and its mouth becomes a small flat line. Its purple outfit remains fixed in place. Empty hands, mouth shut, no props.
```

### tired — group 07331636
```
The rabbit stands drowsily in place, its head and shoulders drooping lower and lower, then it tips its head back in a big slow yawn before drooping forward again. Its eyes become closed downward-curving arcs and its mouth opens into a small oval mid-yawn, then closes again. Its purple outfit remains fixed in place. Empty hands, no props.
```

### love — group caf3b41f
```
The rabbit stands upright and gives a happy little wiggle, swaying side to side and clasping its hands together in front of its chest, then rocks gently back to its resting posture. Its eyes become upward-curving happy arcs and its mouth becomes a wide smile. Its purple outfit remains fixed in place. Empty hands, no props, no hearts.
```

### hungry — group 96eb8f4f
```
The rabbit stands upright and looks around eagerly, rubbing its belly with one hand in slow circles, then returns to its neutral posture. Its eyes become round wide-open dots and its mouth becomes a small round oval. Its purple outfit remains fixed in place. Empty hands, nothing in the mouth, no props, no food.
```

### happy — group c77fe8a0 — COMPOSED ONCE this session (no verbatim source existed; the croc pack
### has no happy clip). Pattern: croc emotion narrative + PILOT §4 play beat (hop in place, arms
### overhead) + §5 love/happy face vocabulary. Reuse VERBATIM for every later species.
```
The rabbit stands upright and bounces in a small happy hop in place, raising both arms up overhead, then lands softly and settles back into its resting posture. Its eyes become upward-curving happy arcs and its mouth becomes a wide smile. Its purple outfit remains fixed in place. Empty hands, no props, no sparkles, no confetti.
```

## OWNER SIGN-OFF 2026-08-18 — adult v1 REJECTED: [FAIL — tail carried in the left hand]

Jon: "The south view of the bunny has a tail in its left hand and it got carried to all
animations." Verified at 6x: the south rotation draws the round tail puff at the viewer-right
hem, touching the left paw — it reads as a held object, and every state/animation derived from
the base inherits it. Character d0529bec retained in PixelLab as the derivation record only;
its rotations and everything derived from them are REJECTED art.

## Adult v1 south — PC-side mirror fix (0 generations, deterministic, croc eye-fix spirit)

The Bruna south view is bilaterally symmetric and the viewer-LEFT half was clean, so the fixed
south = left half + its own mirror (axis x=47.5 measured from the head extent rows 20-40).
Result verified at 6x: two identical paws, no ball at the hem, ears/face intact. Saved as the
rotation reference (scratchpad south_fixed.png).

## Adult character v2 — v3 create_character, reference_image = the mirror-fixed south, same settings — [PENDING]

Character id bd380030-1a07-4d5e-8ed8-02faf3f27000 ("Bunny fix ref"), 2 generations.
Prompt = v1 prompt verbatim + this tail cue appended (croc tail-fix pattern, bunny words):

```
 The tail is a small round white puff sitting low on the centre of the back; it is never visible from the front and never held in the hands or carried in front of the body.
```

LESSON (croc, reconfirmed): to fix a defective rotation set, seed v3 with a clean rotation as
reference_image and keep the prompt otherwise verbatim — prompt-only re-rolls drift.

## WARDROBE CHANGE (owner, 2026-08-18): purple overalls replace the tunic, all packs

"Doesn't the tunic make it look feminine?" — the A-line read as a dress; overalls chosen.
Before: Wearing a simple flat bright purple A-line tunic dress.
After:  Wearing purple overalls with a square bib and shoulder straps.
Same two purples stay the keying/anchor constants. The v2 tunic fix roll (bd380030) is
OBSOLETE regardless of outcome — tail fix and overalls are folded into ONE regeneration per
the owner's instruction.

## Adult character v3 (overalls + tail cue) — v3 create_character, prompt-only, same settings — [PENDING]

Character id 047d7f3e-2645-4a1e-ad09-a1e763187813 ("Bunny overalls"), 3 generations.
Prompt = v1 prompt with the garment words swapped + the tail cue appended. Prompt-only,
because a reference image would pin the OLD tunic garment.

```
A high-contrast vector illustration of a rabbit in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A rabbit character standing upright on two legs, simple rounded oval head, two identical tall vertical capsule ears standing straight up from the top of the head, two small round eyes set close together near the centre of the face as solid black dots, a small black X mouth, and a small round fluffy tail behind the body. Wearing purple overalls with a square bib and shoulder straps. Solid pale grey fur, solid white background, no gradients, no shadows, no texture, bold primary color palette. The tail is a small round white puff sitting low on the centre of the back; it is never visible from the front and never held in the hands or carried in front of the body.
```

### Overalls roll 1 (047d7f3e) — [FAIL]
Reviewed at 4x: the white tail puff rendered FRONT-CENTRE on the bib in the south view (the
rejected v1 defect, relocated), a white undershirt appeared under the straps, and the bib
carries a pale patch pocket. East/NE views place the tail correctly; north omits it.

### Overalls roll 2 — strict garment clause — character f4a021e1-9d20-4b05-8454-e9e5d4c23990 ("Bunny overalls v2"), 3 gens — [PENDING]
Garment words only changed (reused verbatim across all five packs):
Before: Wearing purple overalls with a square bib and shoulder straps.
After:  Wearing plain bright purple overalls with a square bib and two shoulder straps, no pockets, no buttons, no shirt underneath, bare arms and shoulders.

## OWNER SIGN-OFF 2026-08-18 (curated in PixelLab): KEEPER = bd380030 "Bunny fix ref"

The owner kept the mirror-fixed TUNIC bunny as the mascot homage and deleted the others
(v1 d0529bec, overalls 047d7f3e, overalls-strict f4a021e1). Do NOT re-dress. Everything
below derives from bd380030. HONEST LEDGER: the full clip set generated on the rejected v1
base (2 states + 14 animation directions, ~78 gens) is waste by rejection — regenerated
below on the keeper.

## Phase 2 on the keeper — states + travel + 10 animations (prompts identical to the
## earlier logged set, byte-for-byte; only the base character changed)

- Adult_Sit state efc31306 (96px, palette-from-reference)
- Adult_Sleep state cae2a444 (override 120x120, palette-from-reference)
- Adult_Walk east group 389532b2 (west is a PC mirror at install)
- Adult_Eat 23699f44 · Adult_Bathe f2cefc26 · Adult_Pickup 89205e3b
- Adult_Angry 68ca182e · Adult_Sick 8feaa0a5 · Adult_Bored 6321cf6e · Adult_Tired c1645049
- Adult_Love 3b0f3181 · Adult_Hungry 7dea0bae · Adult_Happy 5b973900

## Install-time normalization (owner order): the garment base+shade pair is remapped to
## EXACTLY #9151d3 / #5d229d at install (pair-swap, never flattened). No anchor purples
## exist in any keeper's raw output — this is the deterministic fix, not a prompt job.
