> **CURRENT STATE (2026-08-19)**: worlds are live on hardware end to end. The flow is
> GitHub Pages tools -> one timestamped `.bunbun` package -> the device's own `/build`
> page (Send-to-bunbun retired; https cannot reach an http device). One animal at a time
> via species packages on the site's animal shelf; animations carry their animal and its
> travel kit. Adult-only is pinned in firmware. The complete feature picture lives in
> `docs/BUNBUN-THE-GAME.md`; the shipped world rules in `tools/WORLDS-SPEC.md`'s banner.

# Bunbun: from PixelLab to a living world on the wall

*A chat-ready overview — hand this file to any Claude and it knows the project.
Refreshed 2026-08-18; supersedes the 08-17 edition.*

## What this is

Bunbun is a virtual pet on a $20 ESP32-S3 touchscreen (AirPlay 2 speaker underneath) with
real needs on real clocks — hunger, hygiene, energy, a sleep schedule tied to actual
night — plus weather, visitors, and mini-games. The pet is never reset; updates carry its
age and history forward (non-negotiable).

On top sits a creation suite a seven-year-old runs alone, now public:

- **Repo**: https://github.com/jonalli0124/bunbun-studio (assets-as-data: commit a PNG,
  the studio grows)
- **Tools, hosted**: https://jonalli0124.github.io/bunbun-studio/
- **Release v0.1.0**: flashable public firmware (no fleet OTA, no telemetry; manual
  updates via USB or the device web page stay)

## The trilogy

> "You animate the creature, you animate the environment, and then you create the scene."

1. **Animation Creation** — one animation at a time: pick an ANIMAL (capybara, croc —
   any ANIMS-layout folder under assets/characters joins the picker), pick a clip,
   attach props with derived anchors, draw brand-new props in the pixel editor, tune
   breathing, set "size in the scene" (one dial, assembler units), and preview at true
   game scale in a real room under real light. Saves a file; the Moves Shelf hands it to
   the assembler live.
2. **The environment** (spec: ENVIRONMENT-SPEC.md; three of five layers shipped) — the
   assembler's "+ sky box" polygon declares WHERE the sky lives; procedural clouds,
   rain, and twinkling night stars run inside it; a placed sun or moon set to
   "follows: the day / the night" rides the real clock across it (rise 6:00 left, set
   20:00 right). Window frames occlude automatically wherever the art shows sky. Still
   to come: perch visitors (a bird landing where a kid says) and passers-by (the lofi
   train), plus an environment mode with a pre-canned shelf.
3. **The Scene Assembler** — rooms, objects, walkable/keep-out/activity/sky shapes,
   imported animations with rules (anywhere / affording / pinned / in an area), per-anim
   seconds + ticker words + a "counts as" override (the clip / his job / washing up),
   the master size dial, and Act-it-out running the device's own sim.

## The golden rules

- **The assembler is the spec.** The device must match the tool; fixes go in firmware.
- **The export is canon.** A kid's art replaces same-name art on the device.
- **Rooms are bare.** Furniture is the kids' job. (Outdoor skies ship EMPTY too — suns
  and clouds are alive now, never baked.)
- **The builder declares WHERE; the sim decides WHEN.** Kids author places and rules,
  never scripts. "Spot etiquette" extends this to manners (who may use a place, whether
  things get cleared first, whether they go back).
- **Adult-only.** One age, and it is adult. School is retired.

## The world (shipped 2026-08-18)

Scenes have ROLES: main room, kitchen, bathroom, work. A world is just role-tagged scene
files on the device — author each room like any scene, mark its role, send it; no other
step. Kitchen and bathroom sit off the LEFT of the main room, work off the RIGHT.

- EAT goes to the kitchen, BATH to the bathroom, the WORK button to the work room —
  always by walking off the side of the screen and entering the next room from the
  mirror edge, doing the thing at its authored mark, and walking home after.
- Undefined room: eat/bath happen where he is; work refuses.
- **The only passive trip**: nature's call. The old floor-mess mechanic is retired; the
  timer now sends him to the bathroom for the full routine — the authored SIT on the
  toilet, then washing up at the sink (act "wash"), then home.
- Work sessions own the clock: "work lasts N minutes" on the work scene; his job
  animations cycle until the session ends — a bug collector never stops early.
- The cat and the bird are main-room guests only.

## Carrying worlds to the device

- **The .bunbun package**: the assembler's "save a bunbun package" writes one file —
  scene + only the new/replacing art (rooms travel too, in the device's own palettized
  format). Works from any hosting, https included.
- **The device's own import page** at `http://<device-ip>/build`: drop the file, press
  the button; it merges into the pak, files the scene under its role, restarts, and
  proves the pet unharmed. This is the whole port, kid-operable, no PC.
- The Send-to-bunbun button does the same live on the home network.

## Generation (the art lane)

PixelLab in a Claude session; `py tools/new_asset.py item|light|mark|room <png> <name>`
installs everywhere. Doctrine in GENERATION.md: reuse winning prompts verbatim
(PROMPTS.md logs); verify by rendering and looking; naming a room type summons its
furniture (describe materials); outdoor walk bands need their own plain-flat clause;
mood words summon faces on scenery. Library today: 14 bare rooms, a full item tray, six
emotion marks, and the 12-piece environment starter set (clouds, suns, moons, stars,
birds).

## Hardware truths

- One pak (BUNP v2) carries all art, memory-mapped; the JS encoder is byte-identical to
  the build pipeline's. Scene files are ≤8KB JSON on SPIFFS.
- The public build (CONFIG_BUNBUN_PUBLIC_BUILD) strips fleet OTA/beacon/telemetry;
  the kid loop and manual updates remain. The canary device runs it.
- `/api/debug/act?a=eat|bath|work|potty` triggers acts remotely for testing.

## Next up

Perch visitors, the lofi train, the environment mode + pre-canned shelf (drifting cloud
loops, sun arcs, gliding birds), spot etiquette's authoring UI, wearables (the hard hat
that survives the door — WORLDS-SPEC.md), cloudless retakes of the outdoor rooms, and a
better cat clock (a placeable clock face that tells real time, now that shipped rooms
are optional).
