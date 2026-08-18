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
