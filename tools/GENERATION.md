# Item, room and emotion generation — the pipeline

The third generation lane, beside characters (CHARACTER-PACKS.md) and animations (the
editor). Generation happens in a Claude session via the PixelLab MCP; installation is one
command; the next port carries everything to the device.

## The rules (same blood as the character doctrine)

1. **REUSE THE EXACT PROMPT.** Record every passing prompt verbatim in
   `assets/objects/PROMPTS.md` / `assets/rooms/PROMPTS.md`. Never paraphrase a winner.
2. **Verify art by rendering and looking** — never by filename. Reject drift from the house
   style: soft pixel art, warm palette, black-ish outlines, dot-eye world.
3. The status endpoint lies — poll for `completed`.

## Items (props, lights, emotion marks)

- Generator: `create_image_pixflux` (or pixen), **96px canvas or smaller**, transparent
  background, single object, no floor shadow baked in (the tools add depth).
- Style words that match the shipped tray: *"soft pixel art, warm colors, clean dark
  outline, children's game item"* — then the object. Check the winners file first.
- **Emotion marks** (feelings shown over the head in the editor: hearts, storm clouds,
  sparkles): small (≤48px), bold, readable at 24px on the panel.
- Install:
  ```
  py tools/new_asset.py item  <png> <name>     # furniture, toys, tools, food
  py tools/new_asset.py light <png> <name>     # anything that should glow
  py tools/new_asset.py mark  <png> <name>     # a feeling, drawn over the head
  ```

## Rooms — ALWAYS BARE (owner's rule, 2026-08-17)

A room is walls, floor, a window, light — **no furniture**. Furniture is the kids' job;
that's the whole scene contract. (The old bathroom.png with two tubs baked in is the
counter-example: nothing in it can be moved, sat on, or blocked correctly.)

- 320×240, the floor band roughly y≈200–240 like the shipped rooms, a window the firmware's
  sky/bird logic can live behind, wall tone that reads in both day and night tinting.
- Style: match `assets/rooms/farmhouse.png`'s rendering (soft pixel, warm).
- Install: `py tools/new_asset.py room <png> <name>` → tools see it immediately; the pak
  name is `rooms/room-<name>`; a scene picks it in the assembler's room dropdown.

## Jobs are scenes, not firmware

"He could be a construction worker at a factory" needs ZERO code: a bare factory room +
construction items (hard hat, cone, toolbox…) + animations authored in the editor on
whatever clips fit (idle/sit fillers with props held, an "anywhere" hammering loop). Rules,
durations and ticker lines make it a shift. The device plays it like anything else.

## The current wishlist (owner, 2026-08-17)

- Bare rooms: **bathroom**, **kitchen**, **factory / construction site**
- Item sets: bathroom (sink, toilet, towel rack, bath mat), kitchen (stove, fridge,
  counter, pot), construction (hard hat, traffic cone, toolbox, wooden crate, barrier)
- Emotion marks: heart, grumpy storm cloud, sparkles, sweat drop, music note, exclamation

## Lessons from the first run (2026-08-17, 22 assets, $0.26)

- **Naming the room type summons its furniture.** "Bathroom" baked in tubs and toilets
  four rolls straight, even with "nothing installed, no fixtures" in the prompt. The
  winners describe only MATERIALS (tile grid, butter-yellow walls, honey wood floor) and
  never say the room's name. Full verbatim log: assets/rooms/PROMPTS.md.
- img2img over an existing room clones it at high strength and loses the material words at
  low strength — text-only won every room.
- "warm colors" turns porcelain pink — drop it for white fixtures.
- A baked-in floor shadow can be scrubbed in PIL faster than a re-roll (the fridge).
- pixen drew the things pixflux refused (the toilet).

## Lessons from the second run (2026-08-17, 5 rooms in 7 rolls, $0.05)

- The material-not-name rule generalizes outdoors: "sandy shore" not beach, "dirt and
  gravel yard + distant girder silhouette" not construction site — zero baked-in vehicles,
  umbrellas or fixtures in 7 rolls.
- **Outdoor scenes decorate the walk band by default.** The one FAIL (meadow v1) put a
  flower/tall-grass hedge across the entire bottom edge, y≈200–240. The fix that won and
  then passed four straight outdoor rooms: give the bottom strip its own clause — "a plain
  smooth flat <material> strip along the bottom with no flowers and no tall grass" — and
  push decoration "only in the far distance near the horizon".
- The calm retake formula: the kitchen winner's tile-band structure in warm tones (cream
  walls, peach tile band, honey tile floor) reads bathroom-ish without the clinical
  full-wall tile grid that made bathroom-bare too busy.
- Night scenes come out flat-dark and muddled in the midground (backyard-night, generated
  but not installed) — daylight scenes carry the house style better.

## Lessons from the third run (2026-08-18, environment starter set, 12 assets in 21 gens, $0.15)

- **The background remover eats thin, pale and glowy shapes.** "wispy... wide and thin"
  and "a soft orange and pink glow" both returned COMPLETELY EMPTY PNGs (0 opaque pixels)
  under no_background — the whole subject went with the background. Fix: describe a SOLID
  body ("smooth white rounded bar shape", "solid orange half disc") and never use glow
  words. Check `Image.getbbox()` programmatically before judging any piece — an empty
  sprite thumbnails as innocent blank white.
- **Mood words summon faces on scenery.** "grumpy shading" put a full kawaii face on the
  storm cloud even with "no face" in the prompt; a face also snuck onto the shooting star
  and a red eye onto the flying bird. Scenery pieces must stay mood-free AND carry an
  explicit "no face no eyes no mouth" / "plain solid silhouette" clause.
- **Multi-object scatter sprites default to a neat row.** Three flock rolls produced lines
  before "spread out across the whole canvas from left edge to right edge at different
  heights" got a real scatter. Counts drift by one either way — remove or keep an extra
  via connected-component scrub in PIL rather than re-rolling.
- Whites/greys (clouds, moon) reuse the porcelain fix: drop "warm colors", say the color
  plus "soft gray shading, big and centered filling the canvas".

## Lesson from the character-pack idle runs (2026-08-18, owner ruling)

- **Never name a body part you want ABSENT from a view — including with a negative.** The
  bunny prompts said the tail is "never visible from the front and never held in the hands"
  and the model put a tail on the front twice running (once in the left hand, once centred on
  the bib). Naming it summons it — the same mechanism as "grumpy shading" summoning a face on
  the storm cloud. Words like "no tail" are still the word "tail".
- The cure is SURGERY plus a clean reference, not another roll: fix the offending view
  deterministically (PIL fill over flat regions, or inpaint), then derive the 8 rotations
  with v3 + reference_image from the fixed view. Prompt-only re-rolls of the whole base are
  the lottery; the reference route is the croc-proven fix.
- **Pin colors with words every time** (owner): describe the exact fur/marking colors in the
  prompt rather than accepting the palette the model picks. If the shape is right and only
  colors are wrong, prefer the recolour pair-swap over a re-roll.

## Phase-2 doctrine updates (owner, 2026-08-18, after base sign-off)

- **Never name the pocket either.** "no pockets, no buttons" put a pocket on the bib in
  five out of five overalls rolls — same summoning mechanism as the tail. Describe what IS
  there ("a completely blank smooth square bib") and never the thing that must be absent.
- **Owner-ratified detail overrides the flat contract** when it reads at device size: the
  kept cat has tabby stripes, whiskers and a smile; the kept penguin has a cream belly with
  an orange chest band and gloss shading. The croc contract stays the default for NEW
  generations, but a keep decision by the owner is final and is not "drift" to be fixed.
- **Exact anchor colours are an INSTALL-time pair-swap, never a generation-time hope.** A
  pixel audit found ZERO anchor purples in any kept base. At install, the garment's
  base+shade pair is remapped to exactly #9151d3/#5d229d (pairs, never flattened; fur and
  everything else untouched). Prompts only need to land NEAR purple.

- **TAILED SPECIES get a hands-check on EVERY animation before install** (owner, after the
  dog's pickup came back with its tail in its hand — the second tail-in-hand strike after
  the bunny base; it is a pattern, not a fluke). At 4x, look specifically at BOTH hands in
  every frame for tail intrusions. Applies to dog, cat, and any future tailed species
  (fox, etc.); penguin and frog are exempt. The cure order: re-roll the same verbatim
  prompt (never naming the tail) -> inpaint the tail out -> reference-frame regeneration.

## Lessons from the shaded-character run (2026-08-21, ~$2, a dozen rolls)

The evening that produced Imp, and every mistake in it was mine. Read this before
generating a character or you will repeat them.

### 1. `mode="pro"` is the default for a character base, not `v3`

Every pack in this repo was made with **v3**, and that was right for the FLAT Bruna cast —
flat is what v3 does well. For a shaded, outlined creature v3 produces soft blobby output
that **no amount of prompt wording fixes**; three rolls were spent chasing a "rendering
problem" that was really a generator choice. `pro` got there first try.

- v3: 3 generations, ~$0.02, humanoid only
- pro: 25 generations, ~$0.17, and it accepts `body_type="quadruped"`
- **v3 cannot do quadrupeds at all.** pro/standard take bear/cat/dog/horse/lion templates.
- The rig changes the ANIMATION SET on offer: humanoid gives backflips and roundhouse
  kicks; quadruped gives idle / sitting / standing / eating / drinking / walk / run —
  nearly the bunbun clip list already.
- pro **ignores** outline/shading/detail params. The whole style must ride in the text.

### 2. `clean_sprite.py` is part of the style, not a polish step

Raw PixelLab output carries 25–50 near-duplicate shades along every edge — its own
docstring calls it quantisation noise — and **that noise is what reads as "AI generated"**.
Every shipped pack went through `--dist 70 --keep-outline` before anyone saw it. Measured:
31 → 6 colours, 49 → 8, 60 → 9. Judging raw output produced wrong conclusions twice in one
evening. **Never review an uncleaned sheet.**

### 3. The house style is four numbers, not adjectives

Measured off a reference sprite, and worth asserting in `check_pack.py` one day:

| | value |
|---|---|
| distinct colours | ~13 |
| edge pixels that are very dark | **100%** — there IS a hard outline |
| that outline's hue | a deep shade of the **body** colour, never pure black |
| lightness steps in the body hue | 5–6, banded, not a gradient |
| ink aspect | as wide as tall for a squat creature |

Pure black in the palette is what made one roll look muddy beside another. And **material
separation** — skin / shell / leaves each with their own 2–3 shade ramp — is what makes a
creature read as designed rather than generated.

### 4. Cute vs mischievous is six features, not a vibe

Eye **shape** (round vs narrow wedge), eye **size**, mouth **width**, **stance** (tucked vs
planted and weighted), **silhouette** (clean circle vs broken by points), and **brow
angle**. Words like *friendly, small, round, held at the sides* are cuteness instructions —
three accidental baby animals were generated before that was spotted.

The brow is the one nobody thinks to mention and it carries the whole reading:
**"half-lidded" produces stoned, not sly.** Sly is a wedge angled down toward the centre of
the face with a slanted ridge above it.

### 5. Rotation drift is a real defect with a threshold

`clean_sprite.py` reports the bbox spread across the eight rotations and warns past 4px.
Measured that evening: 26px on one roll, 17px on another, 5–8px on the pro rolls. A
drifting creature visibly swells and shrinks as he turns on the device. Re-roll it — it is
independent of whether the art is good.

### 6. Make the creature fill the canvas

`mkspecies` halves everything (see the contract below), so a creature drawn at 47px in a
96px frame lands at ~23px on the device and its 1px details vanish. Say **"the creature
fills the frame from top edge to bottom edge"**. The one roll that got that clause came
back at 70px where its siblings were ~50.

## Lessons from the shaded-pack completion run (2026-08-24, imp/frill/spark, ~$2)

- **An action verb that requires an object summons the object.** "playfully bats at the
  air ... swiping quickly downward" put white motion streaks on the imp (roll 1), grey
  dust puffs on it (roll 2), and a TEAL TOY in the sparktuft's hand — "no props" cannot
  outweigh a verb whose meaning demands a prop. Same naming-summons mechanism as the tail
  and the pocket, in verb form (the owner spotted it). Croc/penguin survived the old
  wording by luck. THE play prompt now describes pure body motion: "...springs back up
  and hops in place twice, waving both arms up and down in time with each hop..."
  (verbatim in spark/PROMPTS.md). Dog's play was re-rolled onto it too.
- **Canonical eye-change wording follows the SPECIES' eyes.** "eyes become round
  wide-open dots" / "upward-curving happy arcs" balloon a narrow-eyed character's eyes
  into round hollow orbs that read as deranged (owner: "a bit crazy"). For sly-eyed
  species substitute e.g. "Its narrow wedge eyes curve into pleased upward arcs beneath
  their heavy slanted brows and its wide grin stays exactly as it is." — recorded per
  pack.
- **OWNER RULING: sleep states have CLOSED eyes, every pack.** Overrides the
  face-stability clause. Ask in the state prompt: "both eyes fully closed as short dark
  curved lines" — the clause is now proven on spark, imp, and frill.
- **OWNER RULING 2026-08-24 (evening): PC-SIDE FACE FIXES ARE RETIRED.** The pixel
  surgeries above were tried first and left blemishes — the frill's recoloured iris
  kept its open-eye OUTLINES ("he still read awake"), the dog's flipped smiles left
  artifacts across twenty frames (owner: "i dont love the pc side solution... weird
  blemishes"). Faces get REGENERATED with the adjusted prompt instead. Deterministic
  pixel work remains fine for everything that is not a face: mirrors, palette
  pair-swaps, feet shifts, fragment scrubs.
- **Fix the source, not the copies.** A face defect on a keeper (the dog's frown)
  reaches every clip; fix it once as a state on the keeper, then regenerate the
  affected clips from the fixed state with their recorded verbatim prompts. Done for
  the dog 2026-08-24 (Idle_smile → sit/sleep/walk/pickup/bathe/play).
- **Pose coherence: edit states ON states.** A fresh sleep roll from an Idle keeper
  can come back rotationally incoherent (lying in five views, sitting in three —
  bunny/cat/frog/penguin, 2026-08-24). To change one facial feature of an existing
  coherent state, run create_character_state ON that state ("Exactly the same lying
  curled sleeping pose, unchanged. Both eyes fully closed...") — the pose is pinned
  by the source rotations and only the edit moves.
- **Species ids cap at 8 chars** (`mkspecies`): "sparktuft" became pack dir/id `spark`.

## THE PAK CONTRACT — the species kit is baked at 0.5, always

The firmware sizes any frame the pack supplied with `travelFactor() = scene.ts / 0.5`.
**The pak carries no record of what scale it was baked at**, so a kit baked at anything
else is drawn wrong for the rest of its life.

The Scene Assembler used to bake the kit at the WORLD's size, so at master 70 the kit
shipped at 70% and the device multiplied it by 1.4 on top — the size applied twice.
Symptoms, all one bug: *"he keeps going bigger than the default on passive animations"*,
*"he just got bigger on his heart is full"*, *"dance mode and he is really big the whole
time"*. **No master setting can fix it, because the error is a ratio and not an offset.**

`tools/mkspecies.py` defaults to `SCALE = 0.5` for the same reason (it defaulted to 0.35,
which contradicted the firmware).

**Open gap:** nothing records the bake scale in the pak and nothing checks it at install.
Recording it as a field and having `/build` refuse or warn on a mismatch retires this whole
class of bug — the cheapest remaining shippability win on the list.

## Slugs are one namespace for the WHOLE world

`canim/<slug>/` is the animation's name cut to **13 characters**, and the pak is shared
across every room. The dedupe set used to be created per room, so two animations in
DIFFERENT rooms whose names agreed that far both wrote to one folder, the second one's
frames were dropped by the already-seen guard, and one room silently performed the other's
drawing. "penguin Adult Love" and "penguin Adult Eat (2)" both cut to `penguin-adult`,
which is how a kitchen ate a love animation, hearts and all.

