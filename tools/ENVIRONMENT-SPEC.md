# The living environment — the spec (owner's notes, 2026-08-18)

> "A sun slowly moving down with day and the moon moving up and the colors to adjust,
> stars twinkling, clouds generated and procedurally moving. A bird flying on and landing
> in a specific spot procedurally, or a train passing by for the station to give it that
> lofi feel." — Jon

The trilogy: **you animate the creature, you animate the environment, and then you create
the scene.** The environment obeys the same contract as the creature: the assembler
declares WHERE (the sky polygon, a perch, a path) and dials; the sim decides WHEN.
Pre-canned first — kids never face an empty shelf ("so the kids don't get overwhelmed
with the fact they have to build everything").

## The five layers

### 1. The sky clock (colors that follow the day)
The firmware already grades the room palette by the real clock (day/night dim, night
tint). This layer extends it: as the sun nears the horizon the grade warms (golden hour),
after dusk it cools. One curve, driven by the same clock that owns sleep. No authoring
needed — it simply follows layer 2's sun.

### 2. Celestial tracks (the sun sets, the moon rises)
A celestial element (sun / setting sun / moon / crescent — starter art generated) is
placed in the assembler and bound to a TRACK:
- `day`: rises at dawn on the sky polygon's left, arcs, sets at dusk — position is a pure
  function of the real clock, exactly like the assembler's time-of-day slider previews it.
- `night`: the moon, mirrored across the dark hours.
The element draws inside the sky polygon, behind clouds, occluded by room art exactly as
clouds are (the sky-polygon occlusion rule already shipped). Kill the baked suns: outdoor
rooms are being regenerated with EMPTY skies so celestial bodies are always live elements.

### 3. Procedural fields (no art, just dials)
- **Clouds** — SHIPPED: generated, drifting, styled, clipped to the sky polygon.
- **Rain** — SHIPPED: cadence-driven showers with afterglow (puddle, rainbow).
- **Stars** — NEXT: tiny bright pixels that twinkle inside the sky polygon after dark.
  One density dial + on/off. Composes free: stars behind drifting night clouds.

### 4. Visitors (fly in, land WHERE you said, leave)
> "A bird flying on and landing in a specific spot procedurally."
A new authored mark kind: a **perch**. The sim occasionally (its own dice, gentle
cadence) flies a visitor sprite in from an edge, lands it on a perch, lets it sit, and
sends it off. This is the window-bird and cat-visit machinery generalized to authored
spots — the flight-in, settle, depart beats all exist. Visitor elements are pre-canned
first (flying bird + flock in the starter set); custom ones come from the environment
mode of Animation Creation.

### 5. Passers-by (the lofi train)
> "A train passing by for the station to give it that lofi feel."
An authored **path line** (edge to edge, with a depth: behind the platform, in front of
the hills) + an element + a cadence dial ("rarely / sometimes / often"). The sim rolls;
the train slides across and is gone. Same machinery covers a delivery truck, a boat on
the beach horizon, a tumbleweed at the worksite. The station's rails are already drawn
at exactly the right band for this.

## 6. Spot etiquette (interactions between actors, places and things)

> "We need to include some loops or statements like: cat only sits here when clear. When
> cat wants to sit here it clears. When cat is not here, cleared object placed back." — Jon

The cat already DOES all of this — hardcoded: it roams, clears the sleeping spot, sleeps,
leaves, and bunbun tidies after (the yarn stays down). The feature is exposing those
beats as authorable knobs WITHOUT making kids write programs. No scripts, no loops — a
spot carries **etiquette**, three readable properties:

- **who may use it**: bunbun / the cat / visitors / anyone (today every mark is
  bunbun-only; the cat's spot is hardcoded)
- **when it is blocked**: `only when clear` (an object on it forbids use) vs
  `clears it first` (the actor moves the object aside to its landing, then uses the spot)
- **afterwards**: `things go back` (tidied when the actor leaves) vs `things stay down`
  (the yarn rule)

The UI reads as sentences on the mark, not as code: *"the cat may sleep here — it moves
things aside first, and they go back when it leaves."* The sim's existing beat machinery
(want → clear → use → leave → tidy) executes whatever the sentence says; WHEN remains
the sim's dice, per the contract. This generalizes in one stroke: bunbun's bath mark can
demand `only when clear` (a toy on the mat blocks bath time until someone tidies), a
perch can be `visitors only`, and a kid-authored fox can inherit the cat's manners.

Scene.json shape (draft): marks gain `who` (bitmask), `blk` (0 clear-required /
1 clears-first), `tidy` (0 stays / 1 goes back). Defaults reproduce today's behaviour
bit for bit: pet marks = bunbun/clears-first/goes-back; the cat's built-in spot keeps
its shipped manners.

## Authoring split (the tool-split doctrine applied)

- **Animation Creation, environment mode**: HOW an element looks and moves — no creature
  in the stack, loop-friendly, backdrop room for judging colors (preview-only, as
  always). Ships PRE-CANNED: drifting cloud loops, twinkling stars, sun arc, gliding
  birds, a rolling train.
- **Scene Assembler**: WHERE and rules — the sky polygon (shipped), perch marks, path
  lines, track bindings (day/night), density/cadence dials. The time-of-day slider
  previews the whole sky clock; Act-it-out shows visitors and passers-by from the same
  sim the device runs.
- **Device**: scene.json carries it all (env.sp shipped; env.stars, celestial bindings,
  perches, paths to come), within the same 8KB scene budget.

## Order of work

1. Stars (procedural, smallest, pure firmware + one dial) — proves layer 3 extension.
2. Celestial tracks + sky clock (sun/moon art exists; the clock math is the feature).
3. Perch visitors (generalize the window-bird).
4. Passers-by (the train).
5. Environment mode in Animation Creation + the pre-canned shelf.

Cloudless room retakes ride alongside: outdoor rooms regenerate with empty skies so
nothing is baked that should be alive.

## 7. Work sessions (owner's notes, 2026-08-18)

> "Work will require defining a room as work and tied to the work button. Also we will
> need to define how long the work session is. If someone wants to have bunbun be a bug
> collector and created some animations where they collect bugs I don't want to have it
> stop early." — Jon

- **Work is a driven act** like eat/bath/sleep, wired to the WORK button and (later) the
  clock. An animation becomes his job by a per-animation toggle in the assembler ("this
  is his work"), because no clip is named Adult_Work — the kid's bug-collecting loop IS
  the work.
- **The scene declares the session**: one scene-level number, "work lasts [N] minutes".
  During a session the sim cycles the work animations — several work animations
  alternate, wander-beats between them — and per-animation durations pace the CYCLE, not
  the SESSION. Nothing stops early: the session owns the clock, exactly the way sleep's
  night owns the morning.
- **Where**: work animations use their authored marks/areas as always; a scene with a
  work area IS the workplace ("defining a room as work"). In a multi-room world the act
  routes through the door like any other (WORLDS-SPEC).
- Adult-only corollary: SCHOOL is retired (the room left the picker; the schoolHours
  mechanic goes in the considered adult-only firmware pass).


## 8. When-blocks (owner's notes, 2026-08-18)

> "Simple block based loops? Like when [cat] leaves room [character] [adult_pickup]
> [jar down] to [jar up]" — Jon

Not Scratch — SENTENCES. A closed vocabulary of events and beats, authored as fill-in
rows exactly like spot etiquette, no nesting, no variables, no loops to write (rules
re-arm themselves; that IS the loop):

- **when** [the cat arrives / the cat leaves / a visitor lands / rain starts / rain
  stops / night falls / morning comes / he finishes eating / the train passes]
- **then** [play ANIMATION at MARK] and/or [OBJECT becomes OTHER-OBJECT] and/or
  [he says WORDS] — a few beats, capped like everything else.

The jar example: object STATE PAIRS (jar-down ⇄ jar-up) are the wearables idea
(section: interacted objects, WORLDS-SPEC) without the wearing — a beat swaps which of
two sprites a placed object shows, and the pick-up animation plays at its mark. The cat
already fires every event edge this needs (arrive/leave/settle); rain and night edges
exist in the weather and clock code; the beats run on the existing errand machinery.

Scene.json draft: "when":[{ev:"cat_gone", do:[{a:"c_pickup", m:2},{obj:12, alt:1},
{txt:"got the jar back!"}]}] — ≤8 rules, ≤4 beats each, within the 8KB budget.
Guardrails: needs, sleep and buttons always outrank a when-block; blocks never move him
between rooms (acts do that); a block that cannot run right now is skipped, not queued
forever. The Act-it-out preview fires the same events (its cat, its rain) so a kid
watches their rule work before it ships.
