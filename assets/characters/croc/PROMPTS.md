# Croc pack — verbatim prompt log

Rule: REUSE THE EXACT PROMPT. Every prompt below is recorded byte-for-byte as sent.
Status marks: [PASS] verified good, [FAIL] rejected (kept for the record), [PENDING] awaiting review.

## Adult character v1 — v3 create_character, humanoid, size 96, view "low top-down", detail "low detail", outline "single color black outline" — [FAIL — eyes only]

Character id d539a019-fdba-43b0-8632-d7fc57dc2f46 ("Croc"), 3 generations. All 8 rotations
excellent: upright biped, protruding toothed snout in near-profile even facing south (this
sidesteps the Stage-0 frog problem entirely), eye bumps on top, scute tail, pale belly,
purple keying tunic. REJECTED for one defect: the eyes came back as white highlight slits
inside the eye bumps — a direct breach of "never open eyes with white sclera"
(project-bunbun-art-style-rules). Kept in PixelLab as fallback.

```
A high-contrast vector illustration of a crocodile in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A crocodile character standing upright on two legs, broad rounded head with a long blunt rounded snout protruding forward, two small rounded eye bumps on top of the head, two small round eyes set far apart as solid black dots, a short black line mouth, and a long thick crocodile tail with triangular scutes along the top edge, curving down behind the body to the ground. Wearing a simple flat bright purple A-line tunic dress. Solid green skin, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

Skeleton copied from the capybara's proven prompt (CAPYBARA-PILOT.md §2): [style clause] [species body clause] [garment clause] [palette clause] — only the species body clause differs, built from the Checkpoint A composite (_composite_41_12: protruding snout, eye bumps, dot eyes, scute tail).

## Adult character v2 — same settings, ONE change: appended CRITICAL eye clause — [FAIL — do not reuse the CRITICAL eye suffix]

Character id 5d6aabaa-bf73-482e-86d4-044163423f3e ("Croc v2"), 3 generations.
Prompt = v1 prompt verbatim + this suffix:

```
 CRITICAL: the eyes are two small solid black dots only, with no white sclera, no highlights, no eye shine.
```

## Canonical animation prompts (from CAPYBARA-PILOT.md §4a, species word swapped only)

### walk (adult, v3, frame_count=6, directions east / north-east / south-east; west, north-west, south-west are PC mirrors)

```
walking forward with a long exaggerated stride, legs swinging far forward and far back, feet lifting high and clear of the ground on each step. Empty hands, nothing in the mouth, no props.
```

### eat (v3, south, frame_count=6, both phases byte-identical)

```
The crocodile stands upright and steady, maintaining a calm forward gaze as it begins a rhythmic chewing motion. Its jaw moves up and down, causing its cheeks to puff slightly with each bite. Simultaneously, the crocodile brings its hands toward its mouth in time with the chewing, then lowers them back to its sides. Throughout the motion, it occasionally tilts its head slightly to one side before returning to its neutral, centered posture, creating a seamless and repetitive eating animation. Empty hands, nothing in the mouth, no props, no food.
```

### bathe (v3, south, frame_count=6, both phases byte-identical)

```
The crocodile stands still and takes a deep breath, its chest gently expanding and contracting. It then tilts its head slightly to the side and begins to rhythmically sway from left to right as if gently scrubbing itself, with its purple outfit remaining fixed in place and its mouth remaining a short closed line throughout. After a few moments of swaying, the crocodile settles back into its initial upright, forward-facing posture, blinking its eyes once before returning to a calm, steady state. Empty hands, mouth shut, no props, no tub, no bubbles, no soap.
```

### crawl (baby, v3 ON the Baby_Crawl state, frame_count=4, east; west is a PC mirror)

```
crawling forward on hands and knees, alternating limb motion. Empty hands, nothing in the mouth, no props.
```

### pickup (adult only, v3, south, frame_count=6) — COMPOSED (pilot table wording, no verbatim source)

```
bending down to pick something up from the ground, then straightening back up. Empty hands in every frame, holding nothing, no props.
```

## State prompts

### Adult_Sleep / Baby_Sleep — capybara's verbatim good-pose prompt, species-neutral part unchanged

Adult (`Adult_Sleep`, use_color_palette_from_reference=True, override 120x120 — the pose is known to clip at 96):

```
Lying down on the ground, curled on one side, head resting on the ground, legs tucked in close to the body. Keep the face exactly identical to the source - two small solid black dot eyes and a short black line mouth, unchanged. Empty hands, nothing in the mouth, no props.
```

Baby (`Baby_Sleep`) — identical plus the onesie clause:

```
Lying down on the ground, curled up on one side, head resting on the ground, legs tucked in close to the body. Keep the face exactly identical to the source - two small solid black dot eyes and a short black line mouth, unchanged. Still wearing the same flat bright purple onesie. Empty hands, nothing in the mouth, no props.
```

(Note: the capybara original said "dot eyes set far apart"; the croc's eyes sit in bumps on top of the head, so the geometry words are dropped — the only edit, recorded here.)

### Adult_Sit / Baby_Sit — pilot §4 table wording + the proven face-stability sentence

```
Sitting down on the ground, legs extended forward. Keep the face exactly identical to the source - two small solid black dot eyes and a short black line mouth, unchanged. Empty hands, nothing in the mouth, no props.
```

Baby version appends: ` Still wearing the same flat bright purple onesie.` before the empty-hands sentence — same pattern as sleep.

### Baby_Crawl state — COMPOSED (capybara's state wording was never recorded)

```
On hands and knees in a crawling pose, belly staying low and level. Keep the face exactly identical to the source - two small solid black dot eyes and a short black line mouth, unchanged. Still wearing the same flat bright purple onesie. Empty hands, nothing in the mouth, no props.
```

## Emotion animation prompts — COMPOSED ONCE (no verbatim source exists; capybara's were never logged). v3, south, frame_count=6, byte-identical across adult and baby. Pattern: §4a narrative + stability cues; faces from §5 vocabulary.

### angry
```
The crocodile stands in place and plants its feet, leaning forward slightly with its fists clenched at its sides, shaking briefly with frustration, then stomps one foot before settling back upright. Its eyes become sharply slanted slits tilting down toward the centre of the face and its mouth becomes a short frown. Its purple outfit remains fixed in place. Empty hands, mouth shut, no props, no steam, no smoke.
```

### sick
```
The crocodile stands slightly hunched, swaying gently and unsteadily from side to side as if queasy, its shoulders drooping, then wobbles back to its upright posture. Its eyes become droopy half-closed curves sagging downward and its mouth becomes a small wavy line. Its purple outfit remains fixed in place. Empty hands, no props, no clouds, no bubbles.
```

### bored
```
The crocodile stands in place and slumps, its shoulders and head sinking slowly downward, then it shifts its weight from one foot to the other before returning to its slumped posture. Its eyes become flat half-lidded lines looking off to one side and its mouth becomes a small flat line. Its purple outfit remains fixed in place. Empty hands, mouth shut, no props.
```

### tired
```
The crocodile stands drowsily in place, its head and shoulders drooping lower and lower, then it tips its head back in a big slow yawn before drooping forward again. Its eyes become closed downward-curving arcs and its mouth opens into a small oval mid-yawn, then closes again. Its purple outfit remains fixed in place. Empty hands, no props.
```

### love
```
The crocodile stands upright and gives a happy little wiggle, swaying side to side and clasping its hands together in front of its chest, then rocks gently back to its resting posture. Its eyes become upward-curving happy arcs and its mouth becomes a wide smile. Its purple outfit remains fixed in place. Empty hands, no props, no hearts.
```

### hungry
```
The crocodile stands upright and looks around eagerly, rubbing its belly with one hand in slow circles, then returns to its neutral posture. Its eyes become round wide-open dots and its mouth becomes a small round oval. Its purple outfit remains fixed in place. Empty hands, nothing in the mouth, no props, no food.
```

## Baby onesie — v3 create_character (the untested cheap route from PILOT §3, ~3 gens vs 25-gen state) — [PASS]

Character id a1071ff2-d938-4da6-9e73-f4edebb01d5a ("Croc Baby Onesie"), settings identical to adult v1.
Adult v1 prompt with the §3 baby clauses folded in (head ~half height, short stubby limbs, short tail, onesie garment), CRITICAL eye suffix dropped (it backfired on v2 — the PC-side eye fix handles highlights deterministically):

```
A high-contrast vector illustration of a baby crocodile in the flat minimalist picture-book style of Dick Bruna. Ultra-clean geometry, uniform thick black outlines, no interior detail. A baby crocodile character standing upright on two legs, with the head noticeably larger relative to the body, about half the total height, and short stubby arms and legs, broad rounded head with a long blunt rounded snout protruding forward, two small rounded eye bumps on top of the head, two small round eyes set far apart as solid black dots, a short black line mouth, and a short thick crocodile tail with triangular scutes along the top edge, curving down behind the body to the ground. Wearing a simple flat bright purple onesie, one piece covering the whole body, short sleeves and short legs. Solid green skin, solid white background, no gradients, no shadows, no texture, bold primary color palette.
```

## PC-side eye fix (applies to EVERY downloaded croc frame, part of the clean pipeline)

The v3 generator will not draw literal solid-black dot eyes on a crocodile (two attempts, both
failed the same way). Deterministic fix instead: any pure-white pixel (r,g,b > 230) whose
8-neighbourhood contains NO pale jaw-yellow pixel (r,g > 170, b < 180) is painted black.
Eye highlights sit inside black surrounded by black/green and get painted; tooth glints touch
the pale jaw and survive. Verified at 8x on all 8 adult rotations.

## Crawl east — the three samples (regeneration at the animation level, delete-first each time)

- v1: canonical capybara prompt ("...Empty hands, nothing in the mouth, no props.") — [FAIL] jaw gaped wide open in 3/5 frames.
- v2: swapped the props clause for the shape clause per PILOT §4a ("Empty hands, mouth shut, no props.") — [FAIL, kept as fallback] milder but still-open jaw with pink tongue in 3/5 frames.
- v3 — [PASS, SHIPPED]. Added a §4a-pattern stability cue naming the snout. THE crawl prompt for long-snouted species:

```
crawling forward on hands and knees, alternating limb motion, the long snout staying level with the mouth remaining a thin closed line throughout. Empty hands, mouth shut, no props.
```

Lesson (mirrors the bipedal-cue finding): a long-jawed species needs the JAW pinned in any
profile travelling clip — the generic mouth clauses do not hold it. Pin it with "the long
snout staying level with the mouth remaining a thin closed line throughout".

## Final palette (--dist 70, 227 frames, feet y=90)

skin #659a3a + #33652b, tunic/onesie #a456ab + #6e3082, belly/jaw #d3db74 + #b29f3f — each
swaps as a PAIR; fixed #000000 outline. Accents: tooth-white #fcfefc (0.10%), mouth-pink
#e66183/#8f0b46 (eat/angry open jaw). Base<->base distances 123-145.

## Defect fixes 2026-08-17 (Jon's report: two tails on idle back views; crawl E/W nose+tail clipped)

### Adult idle back views — roll 1: v1 prompt verbatim + tail cue, plain v3 create_character — [FAIL]

Character "Croc v3 tail-fix" (281a53a3, deleted). Prompt = adult v1 prompt verbatim + this suffix:

```
 The tail emerges from under the hem of the tunic at the hips; the tunic covers the whole back; the tail is never drawn over the clothing.
```

3 generations. FAIL: N/NW came back with a giant belly-up tail curled over the whole back,
covering the tunic — worse than the defect. NE had a white eye crescent and an open toothy
jaw. Deleted (delete-first).

### Adult idle back views — roll 2: same prompt, v3 reference-image route — [PASS, SHIPPED]

Character "Croc tail-fix ref" (fe1cee6b-a3c6-46ca-a51d-c1c996275163), 2 generations.
Same prompt text byte-for-byte as roll 1 (v1 + tail cue), but passed as the rotation guide
with reference_image_base64 = the SHIPPED clean Adult_Idle/south.png. The reference pins the
identity; the tail cue lands: all three back views have ONE tail emerging from under the hem
at the hips and an uninterrupted tunic. Harvested north, north-west, AND north-east (the
borderline frame — judged by eye against the fixed two: the old NE's scute ridge climbed to
shoulder height, the new NE keeps the tail low like its neighbours, so it was swapped too).
The five other rotations untouched.

LESSON: for regenerating individual rotations of an existing character, the reference-image
route (seed with a shipped clean rotation) beats a fresh prompt-only character — same prompt
that failed prompt-only PASSED with the reference pinning identity.

### Baby crawl east — margin re-rolls (delete-first each)

- v4: shipped v3 crawl prompt verbatim + margin cue below — [FAIL, body still 95-96px wide,
  cannot fit 96 canvas; pose good, snout+tail complete on the expanded 120 canvas]

```
crawling forward on hands and knees, alternating limb motion, the long snout staying level with the mouth remaining a thin closed line throughout. Empty hands, mouth shut, no props. The whole body fits inside the canvas with a small margin; nothing touches the canvas edges.
```

- v5: same prompt + custom_start_frame = v4 frame 0 scaled 93%, keep_first_frame=false —
  [FAIL: the scaled guide had lost the snout-tip outline, and every generated frame
  faithfully reproduced the sheared flat snout]
- v6: same prompt + custom_start_frame = v4 frame 2 (a frame with an intact outlined snout)
  scaled 95% with a deterministic outline-close pass (opaque non-black pixel touching
  transparency -> black), keep_first_frame=true, frame_count=4 -> 5 frames — [PASS, SHIPPED]
  Ink 91-92 x 58-59 (vs 57-63 clipped before, -2%), margins >=2px all sides, one component
  per frame.

LESSON (extends the v3 snout-pinning lesson): a margin cue alone will not shrink the body —
v3 copies the reference size. To resize, feed a resized custom start frame; and ALWAYS
outline-close a scaled guide first, because v3 reproduces a broken silhouette verbatim.

### PC-side repairs on the new frames (deterministic, recorded)

- Eye fix run twice: on the raw AND again after the palette snap (dim highlights under the
  230 threshold get promoted to pure white by the snap — NE eye had 2 such px). Tooth glints
  with a pale-jaw neighbour survive both passes.
- Crawl frames 02/03/04 had a 1px-tall enclosed transparent slit between the jaw lines
  (mouth ajar); filled black = closed mouth. The daylight gap between hind leg and belly in
  frame 02 was left transparent (it is real limb separation, not a defect).
- Crawl feet-norm centres the ink horizontally (raw ink sat off-centre on the 124 canvas;
  the symmetric crop would have clipped 1px). All 5 frames share the shift; feet y=90.

## 2026-08-23 — Adult_Dance (audit fill-in; the clip `dance/anim` really wants)

Keeper `d539a019-fdba-43b0-8632-d7fc57dc2f46`, v3, south only, frame_count 6 (7 stored), 1 generation.
Animation group `84e28183-6599-4a40-a441-c891d7226a04`.

The penguin's verified dance prompt with the species word substituted — the sanctioned
Phase-2 pattern ("croc/canonical prompts, species word") — plus the one body-part swap
"flipper wings" -> "arms", the same precedent as the beak-for-mouth substitution.

```
The croc stands upright and dances on the spot, bouncing rhythmically from one foot to the other while both arms lift and swing outward to the sides in time with the bounce, the head bobbing gently with each beat, then returning to the neutral upright posture so the motion repeats seamlessly. Empty hands, nothing in the mouth, no props.
```

Install: raw came back **116x116** (v3 output size varies per character — measure, never assume).
Cleaned `--dist 70 --keep-outline`, NO `--norm` (normalise_feet shifts each frame separately and
would flatten the bounce). One shared offset dx=10 dy=11 for all 7 frames, anchored on the clip's
LOWEST extent so nothing sits below this pack's floor row 89. Zero opaque px clipped. Lift 4px.
Garment mapped as an explicit PAIR to #9151d3/#5d229d — nearest-colour matching flattened the
cat's base+shade into one value and sent the frog's shade to the wrong colour, which is exactly
the failure the pair doctrine warns about. Everything else mapped to the pack's nearest existing
colour, with a guard that refuses to collapse two source colours into one.
check_pack: PASS, anchor purples missing in 0 frames.

## 2026-08-23 — Adult_Play (audit fill-in; `play/anim` was substituting an emote)

Keeper `d539a019-fdba-43b0-8632-d7fc57dc2f46`, v3, south, frame_count 6 (7 stored). Group `01448769-6405-4df7-a05c-35e4521c6e9f`.
Structure reuses the cat's recorded Adult_Play scaffolding (crouch -> wiggle -> spring -> settle,
"Keep the face exactly identical to the source", garment-fixed clause) with the action changed;
the cat's own clip is a cat-specific batting/pouncing move that does not transfer verbatim.

```
The croc crouches down slightly, wiggles briefly, then springs back up and playfully bats at the air in front of it with one hand, swiping quickly downward several times while bouncing on the spot, then settles back into its resting posture. Keep the face exactly identical to the source. Its purple outfit remains fixed in place. Empty hands, no props.
```

Install: cleaned `--dist 70 --keep-outline`, no `--norm`; one shared offset anchored on the
clip's lowest extent onto this pack's floor row; zero opaque px clipped; garment mapped as an
explicit pair to #9151d3/#5d229d. check_pack PASS.
