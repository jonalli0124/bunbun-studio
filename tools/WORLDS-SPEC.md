# Multi-room worlds — the spec (owner's notes, 2026-08-17 night)

> "Have the creature walk to the side of the room to go to a bathroom / work / kitchen —
> each room is updated via the scene / animation builder." — Jon

## The idea

A pet doesn't live in one room; it lives in a HOME with places to go. The kids author each
place with the exact tools they already have — a room is just a scene — and the creature
travels between them by **walking off the side of the screen** through a doorway, the way
the floor polygon has always been allowed to run past the edge ("that is where the doorway
is" — SCENE_OFFSTAGE's original reason for existing).

## The model (golden rule preserved: the assembler is the spec)

- **A world = a set of scenes.** Each scene is exactly today's scene (room, objects,
  ground, animations, rules, words). Nothing about a single room changes.
- **Doorways** are authored in the assembler: an edge marker (left or right side of the
  room) that names the scene it leads to. A doorway is a WHERE, like everything else.
- **The device holds one world**: `/spiffs/world.json` (the room list + doors + which room
  is home) plus one `/spiffs/scene-<n>.json` per room (each within today's 8KB cap — no
  format change per room). One pak carries every room's art, as it already does.
- **Travel**: when a DRIVEN act's place lives in another room (BATH pressed, the tub is in
  the bathroom scene), he walks to the doorway edge, exits off-screen, the room swaps, and
  he walks IN from the opposite edge to the destination — then performs exactly as today,
  and comes home the same way. Ambient wandering may occasionally visit other rooms (the
  filler cadence, one room-trip at a time, never frantic).
- **The sim still decides WHEN and WHICH.** Doors say only where a side of the screen goes.

## What stays untouched

- The pet's needs, sleep clock, cat visits, birds, weather — all per-room ambience follows
  the ACTIVE room's env, and the game's own clocks own everything they own today.
- scene.json compatibility: a device given a plain single scene.json behaves exactly as
  today. world.json is additive.

## Tool work

- Assembler: a room switcher (tabs across the top: home / bathroom / kitchen / work…),
  each tab a full scene; a "+ doorway" edge marker naming the destination room; Send
  pushes the whole world (pak merge already handles all rooms' art in one pass).
- Preview: Act it out follows him through doors (room swap in the preview = the truth of
  the device, as always).

## Firmware work

- world.json loader (room table, cap ~6 rooms), per-room scene storage, current-room
  state (RAM; home on boot), the door-walk transition (exit edge → load room → enter from
  the opposite edge), act resolution across rooms (sceneActMark searches all rooms; the
  errand routes through the door), ambient cross-room visits at a gentle rate.
- The renderer already draws whatever room the scene names; the swap is a scene reload
  plus an entrance walk — machinery that exists (sceneLoad + the cat's walk-in).

## Interacted objects — wearables & carryables (owner's notes, 2026-08-17)

> "Objects that the pet can interact with but also the creature. For example, you go to
> work by walking to the side wall, lift up a hat and put it on its head and then go to the
> factory room wearing the same hat." — Jon

- **An interacted object is a prop with a HOME and an ATTACH point.** At home it draws as a
  normal scene prop (the hat on its wall hook). Picked up, it leaves the scene layer and
  becomes an attachment on the creature — the attach editor already owns exactly this
  (derived anchors + authored offsets, `can_hold`); the new part is that the SCENE can grant
  and revoke the attachment at runtime.
- **Pick-up is an errand**, same machinery as bath/sleep: walk to the object's home mark, a
  short authored "take it" clip (optional — a simple fade-swap is the floor), then the prop
  rides the head/paw anchor from that frame on. Putting it back is the mirror errand.
- **Worn things persist across doors.** The hat is creature-state, not room-state, so the
  room swap in a world doesn't strip it — that's the whole point of the example: put the
  hat on at the wall, walk out the doorway, arrive at the factory still wearing it. Coming
  off shift, the return errand hangs it back on its hook.
- **Authored in the tools, honored by the device** (golden rule): the assembler marks a prop
  as takeable + names its attach slot (head / paw / back); the attach editor authors how it
  sits per animation; the export carries slot + offsets; the firmware composites the worn
  prop into the pet blit using the same anchor math as canim frames.
- Kids' framing: a prop gets a little "can bunbun wear this?" toggle; the ghost preview
  shows it riding along in Act-it-out.

## First world (the owner's picks, art already generating)

home (farmhouse) · bathroom (bare) · kitchen (bare) · construction site (bare) — bath acts
in the bathroom, eat acts in the kitchen, and a construction-worker shift (hard hat, cones,
authored hammering loop) at the site.
