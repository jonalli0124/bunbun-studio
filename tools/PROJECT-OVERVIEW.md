# Bunbun: from PixelLab to a living creature on the wall

*A chat-ready overview of the whole creation pipeline — hand this file to any Claude and it
will know the shape of the project. Written 2026-08-17.*

## What this is

Bunbun is a virtual pet that lives on a small ESP32-S3 touchscreen (a Freenove 320×240
"CYD" board). It has real needs on real clocks — hunger, dirt, tiredness, a sleep schedule
tied to actual night — plus weather, visiting birds, a cat that drops by, and mini-games.
The pet has been alive and continuously preserved for months (it is never allowed to reset;
over-the-air updates carry its age and history forward — that rule is non-negotiable).

What this document describes is the newer layer on top: **a creation pipeline that lets
kids make their own characters, animations, rooms, and scenes in a web browser, and push
them to the physical device with one button.** No code, no adult in the loop.

## The golden rule

**The scene assembler is the spec.** Whatever the web tool shows is what the device must
do. When the two disagree, the fix goes into the firmware or the port — never into the
tool. Corollary: **the export is canon** — if a kid's export contains art with the same
name as something already on the device, the kid's version replaces it.

The second law is the **scene contract**: the builder declares WHERE (floor shape,
keep-out zones, sleep spots, prop homes), and the simulation decides WHAT and WHEN
(dice-driven, fully procedural). A kid never scripts behaviour; they furnish a world and
the creature lives in it.

## The pipeline, stage by stage

### 1. PixelLab — raw art generation

All art is AI-generated pixel art via the PixelLab API (characters, rooms, items,
emotion marks). Hard-won doctrine:

- **REUSE THE EXACT PROMPT.** Every winning prompt is logged verbatim (`PROMPTS.md` files
  beside the art). Paraphrasing a winner is the #1 cause of wasted generations.
- **Verify art by rendering and looking, never by filename.** Files lie; eyes don't.
- The status endpoint lies about progress — poll for `completed`.
- Character pipeline specifics: template animations are 96px, v3 is 120px; walk/crawl are
  generated facing east and mirrored for west; v3 handles *motion within a silhouette*,
  while outline-changing poses (sit, sleep, crawl) must be generated as separate states.
- Rooms are **always bare** — walls, floor, window, light, no furniture. Furniture is the
  kids' job (the scene contract again). And a discovered gotcha: *naming the room type
  summons its furniture* — prompts describe materials ("tile grid, honey wood floor"),
  never say "bathroom."
- A finished creature is a **character pack**: ~200 frames across ~30 clips (idle, walk,
  sit, sleep, eat, bathe, jump, dance…), 8 compass directions for poses. Two packs are
  complete beyond the original bunny: a crocodile and a capybara (~$1.20–$1.30 each).
- Installing a new species is one script (`mkspecies.py`): it maps pack folders to the
  firmware's animation slots at a fixed base scale with a feet-line convention, and the
  device can switch species over HTTP.

### 2. The Animation Studio (attach editor + pixel editor)

A browser tool where kids author *custom moves*: pick any clip of the current character,
attach objects to it (a spoon, a soap bar, a hat), draw brand-new pixel art in an embedded
pixel editor, tune per-frame offsets, add breathing. Key ideas:

- Attachments use **derived anchors + authored offsets** — the tool computes where the paw
  or head is per frame; the kid nudges from there.
- Anything drawn in the pixel editor auto-attaches to the current animation ("keep it" →
  it's on the creature).
- Finished moves go to a **Moves Shelf** (browser localStorage) that the scene assembler
  reads — make a move in one tool, it appears in the other within seconds.
- Everything is quantized to *device pixels* so the preview breathes and moves exactly
  like the hardware will (sub-pixel motion that would be invisible on the device is
  invisible in the tool too).

### 3. The Scene Assembler

The world-building tool. Kids pick a bare room, scatter furniture and lights, draw the
walkable floor and keep-out zones, place sleep spots and activity marks, import moves from
the shelf, and wire each animation to a *meaning*:

- **Actions have places; emotions are states.** Eat vs hungry, bathe vs dirty, sleep vs
  tired: an *action* (eat/bathe/sleep) makes the creature walk to its authored place and
  perform there; an *emotion* plays wherever it stands. Idle and sit are the only
  undriven "filler" animations.
- Per-animation **duration in seconds** and a **custom ticker line** (the device's news
  ticker says the kid's words while the move plays).
- Depth ("behind the tub"), breathing (%, period), an "anywhere" rule for placeless moves,
  a master size dial, and keep-outs that forbid *loitering* but never walking.
- **Act it out** runs the actual simulation in the browser — same constants as the
  shipped firmware, so the preview is the truth.

### 4. Send to bunbun — the one-button device port

The assembler's button does the entire port in-browser:

1. Checks the pet is alive and reads its age (it must match after, too).
2. Downloads the device's current asset pak and archives a copy (a rolling stash, for
   disaster recovery).
3. Bakes every custom animation frame at its authored scale onto a fixed-geometry canvas
   with a **feet-row convention** (feet always at the same row, headroom above, a few
   below-feet rows) so nothing ever sits too high or too low on the device.
4. Merges the new art into the pak — same-name entries are *replaced* (export is canon) —
   in the device's own binary format (BUNP v2: named RLE-compressed RGB565 sprites;
   the JavaScript encoder is byte-identical to the Python one).
5. Uploads the pak (device self-reboots), then the scene JSON (rooms, props with painter's
   depth order, furniture collision blocks, no-go polygons, lamps, the animation table
   with all semantics above), then verifies.

The firmware side honors all of it dynamically — new animations, new species, new rooms,
new meanings — **with zero recoding per scene.** The animation table, errand system
(walk to the doorstep, settle onto the mark, perform, come home), breathing math
(assembler-verbatim), depth stencils, and ticker lines are all data-driven from the scene
file.

### The supporting cast

- `new_asset.py` — one command installs a generated item/room/emotion-mark into both the
  tools and the device-bound art tree.
- `scene_from_assembler.py` — the same port as the button, scriptable from a zip export.
- A disaster-recovery vault (`bunbun-dr`) — a standalone local repo with the firmware,
  pak sources, tools, runbooks, 40+ knowledge files, and pak archives, sufficient for a
  fresh Claude Code instance to pick the project up cold.
- Kid guides — illustrated, arrow-annotated HTML+PDF walkthroughs written for a 7-year-old
  ("Bunbun and Me"), validated by an agent role-playing a 7-year-old using only the guide.
- A council of reviewer personas that audits each milestone and files findings; their
  feedback rounds are folded back into the tools.

## Current state (2026-08-17)

- Full pipeline shipped and hardware-verified end to end; a family scene with seven custom
  animations, custom ticker lines, and authored durations is live on the device.
- The pet is a capybara today (species switching live); pet age preserved through ~17 OTA
  updates and ~13 pak writes.
- Generation lane opened for rooms/items/emotions: a bare kitchen, an approved
  construction "factory" room, 13 job/room items (hard hat, cones, stove, sink…), 6
  emotion marks. An outdoor set (construction site, meadow, more) is generating now, and
  furnished legacy rooms have been curated out of the kids' room picker.

## Features to come

1. **Multi-room worlds** (specced — `WORLDS-SPEC.md`). A world = a set of scenes; doorways
   are edge markers authored in the assembler; the creature walks off the side of the
   screen to reach the bathroom / kitchen / work, performs there, and comes home. One pak
   carries every room's art; each room's scene stays within today's format. The sim still
   decides when and which — doors only say where the screen edges go.
2. **Interacted objects — wearables & carryables** (specced, same file). A prop with a
   home and an attach slot: the creature walks to the wall hook, lifts the hard hat onto
   its head, and *keeps wearing it through the door into the factory* — worn things are
   creature-state, not room-state. Pick-up/put-back are errands on the existing machinery;
   the attach editor already authors how things sit per animation; the assembler grows a
   "can bunbun wear this?" toggle.
3. **Species art fallbacks** — fill the capybara's missing school/work/dance clips so
   every activity has native art (~one evening, ~$1).
4. **Room-name friendliness** — kid-facing display names in the picker.
5. **Floor tool as a drag-crayon**, richer accessibility for the Act-it-out/Send buttons,
   and full 240-pixel-tall tool rendering (currently letterboxed) — quality-of-life items
   on the backburner.

## How to talk about this project

Useful vocabulary: *the assembler* (scene tool), *the editor* (animation studio), *a pack*
(a species' generated clip set), *the pak* (the device's binary art bundle), *a mark* (an
authored place for an action), *an errand* (walk-settle-perform-return), *fillers* (idle
and sit), *the shelf* (moves passed between tools), *the golden rule* (assembler is spec),
*export is canon* (kid art replaces same-name device art).

The owner's intent, in his own words: **"I want my kids to be able to create their own
stuff tonight in the tools and port it out."** Everything above serves that sentence.
