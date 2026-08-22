# Bunbun: The Game, As It Stands — 2026-08-21

*The complete current state, for talking about. Everything here is shipped, flashed, and
verified on hardware unless marked otherwise.*

---

## 1. What it is

A virtual pet that lives on an ESP32 speaker (it is also a real AirPlay 2 receiver — the
lofi box and the pet share the screen). The pet is a hand-raised tamagotchi with needs,
moods, a cat who visits, mini-games, wishes spoken into a microphone — and, as of this
week, a **world**: multiple rooms, a job, a bathroom routine, and a body that can be
swapped for any animal a kid draws.

Everything the pet does, wears, and lives in is **authored by children in two browser
tools** and delivered to the device as a single file. The firmware's job is to perform
the child's authorship faithfully — the golden rule of the whole project:

> **The scene assembler is the spec. The device matches the tool. Fixes go in the
> firmware or the port, never in the tool. The export is canon.**

## 2. The creation pipeline (the trilogy)

**You animate the creature → you animate the environment → you create the scene.**

1. **PixelLab** generates the animal art (character packs: bunny, capybara, cat, dog,
   frog, penguin, croc — adult-only by decision, purple overalls by decree).
2. **Animation Creation** (`attach_editor.html`) — the kid picks an animal, poses it,
   attaches props (their own drawings included), sets breathing/size, and saves an
   animation file. The file carries everything: the clip's frames, the animal's identity,
   and the animal's **travel kit** (idle + east/west walks) so the animation never turns
   into the wrong species anywhere downstream.
3. **Scene Assembler** (`scene_tool.html`) — the kid builds rooms: background, furniture,
   the walkable floor, keep-outs, activity areas, sky polygons, and drops animations in
   with rules for **where** each may happen. A live preview simulates the pet exactly as
   the device will behave (same constants, same manners).

Both tools run on the public GitHub Pages site — no installs, no local server.

## 3. Getting it onto the device: the one-package flow

- **`save a bunbun package`** produces one timestamped `.bunbun` file containing every
  room of the world plus all the baked art (BNDL container: scene JSON + a BUNP art
  fragment).
- On the device's own page — **`http://<device-ip>/build`** — the kid gives it the file.
  The page merges art into the pak, installs each room to its role slot, and verifies
  the pet survived (age/phase checked before and after — the non-negotiable).
- A world package **replaces all custom animations** on the device — the package is the
  whole story.
- **Species packages** are separate `.bunbun` files on the site's animal shelf: importing
  one swaps the pet's entire body (one animal lives on the device at a time), pet age
  untouched. A world made on a penguin reminds you to install the penguin.
- Send-to-bunbun is gone — an https site cannot fetch an http device; the package + the
  `/build` page works from anywhere and is the only path.

## 4. The world: rooms and roles

Five rooms, each an ordinary scene tagged with a **role**: **main** (home), **kitchen**
(left of home), **bathroom** (left), **work** (right), **outside** (right). A world that
never authors an outside still has one — the meadow is the default, so every world has
five rooms whether the child drew them or not. Doors are the screen edges; a
crossing walks out one side and in the mirror side of the next room, then **through the
middle of the room** before doing anything — every crossing reads as a real entrance.
Side-room to side-room always transits home. Rooms are toggled in the assembler ("this
room is…") and ship together in one package; the scene's name, clock, weather, and stars
are **world-level** and travel to every room.

### The room-trip doctrine (the law of movement) — rewritten 2026-08-21

> "When he goes to the room he should do the main action for the room and just stay in
> there until his meter is full." — and the corollary the owner added after watching it:
> "he should be doing all actions in that room", "while he is in the rooms getting his
> stats up, we need to not get stuck on the negative emotions."

**The room pays, not the button.** Pressing EAT used to hand over 35 food on the spot,
which made the journey decorative. Every room now pays its own meter **one tick a second**
for the time he spends in it — kitchen food, bathroom clean, work discipline, outside fun
— and the act that finishes pays nothing. A world with nowhere to eat or bathe still gets
the old top-up, or a pet in a bare world could never be fed.

**The room owns him until its gauge is full.** Written ONCE, in `sceneDoorTo()`: a walk
home from a room whose own gauge is unfull is *refused*. A command, bedtime, illness or
dance mode still outrank it. This was reported four separate times before it stuck,
because the decision to walk home was written out in FIVE places — the action clock in
`think()`, the mark/settle path, the fruitless-visit linger, the window the repeat opens
when it clears its own timer, and the "cannot be stranded in a side room" backstop. Four
of them returned regardless of the answer, so `think()` ended there every frame and never
reached the dispatcher. **A refusal is not an exit — check the return value.**

**A visit is lived in, not repeated.** The act he came for on the odd goes, the next of the
room's other moves on the even ones — a rota, not dice, so nothing in the room is left
unvisited. Between goes he strolls to a real point on the floor (the same pick the ambient
wander makes), not a sideways step. Measured on hardware: one bathroom visit produced bath
x10, love x4, idle x4, potty x1 and walking throughout.

**No sulking while the room is fixing it.** In any room that is not home whose gauge is
still filling, the negative moods are suppressed entirely and he shows the room's own idle
between goes. He cannot be hungry in the kitchen, behind on work at work, or bored outside
or in a game. Being genuinely poorly still outranks it — that is the one state a room does
not fix.

**And he steps away from the thing he used.** The assembler's preview always did this; the
device dropped to idle where it stood, which reads as hovering over the chair.

Unchanged from before:

- **Acts route by availability**: the room he's in wins if it offers the act; otherwise
  the act's home room; otherwise any room that has it.
- **Nothing steals a commanded trip** — but a command now *interrupts* a running
  animation rather than queueing behind it, because a button that waits out a five-second
  clip reads as a broken button.
- **No state can strand him** in a side room.
- **Doors are the whole near edge of the floor, and cannot be barricaded.** The doorway
  used to be one point nudged out of furniture; a wall of furniture had nowhere to nudge
  it to. The edge is searched outward from his own height, and if a child has walled it in
  completely he walks *through* it — a door that can be permanently blocked is a room you
  can be trapped in.

### Outside, dance and weather

- **Outside** holds him like home does, pays fun a tick a second, and sends him in when
  the gauge fills — with a floor of 45 seconds, because arriving with fun at 96 and
  turning round on the doorstep is the rule being right to the letter and wrong in spirit.
  He only *asks* to go out when fun is under 70.
- **Rain is a world condition**, not a property of a room, so both halves of the rule can
  be asked from anywhere: if it starts while he is out he comes in, and while it lasts the
  outside row refuses out loud rather than doing nothing.
- **Dance mode owns him outright, in whatever room he is standing in.** It used to wait
  for him to be completely free, and anything short of that fell through to the ordinary
  brain, which promptly sent him off to sit down.

## 5. Acts and counts-as

Any animation can be *the* eat / bath / wash / toilet / sleep / work of its room via the
**counts as** menu — the clip's name stops mattering. The acts are what buttons, moods,
and routines look for:

| Act | Who calls it | What happens |
|---|---|---|
| `eat` | EAT button, hunger | trip to the room offering eat; meal plays; 35% potty roll |
| `bath` / `wash` | BATHE button, potty | each accepts the other's fixture as fallback |
| `toilet` | the potty routine | falls back to a plain sit if no toilet is marked |
| `sleep` | ZZZ, auto-bedtime | the sleep **mark** is the bed — he walks to it |
| `work` | WORK button | see below |
| `idle` | between everything | the room's designated idle, anywhere-he-walks |
| `dance` | dance mode, the joy hop | **new 08-21** — replaces the stock hop everywhere |
| `love` | the CUDL button, spontaneous joy | **new 08-21** — the world's own happy |
| `bored` `tired` `hungry` `angry` `sick` `play` | the mood ladder | **new 08-21** |
| reserved `walk_e`/`walk_w` | the travel kit | how this world's animal walks |

**A feeling the world authors beats the kit.** `sceneAnywhereActAnim()` finds the child's
own drawing for an act — restricted to moves that play wherever he stops, because a
feeling happens where he is standing and a pinned one would drag him to its chair — and
`emoteClip()` prefers it, falling back to the shipped kit only when the world has none.
Because a `c_` clip resolves through the scene table in `emoteLineFor()`, **the "he says"
box beside a feeling is what the pet says when it takes him.** That is the whole of the
"rewrite what he says" feature: author the feeling, type the words.

**One copy, every room.** A move ticked **every room** stays in ONE room's list and the
*export* hands it to any room with nothing of its own for that feeling. The older ⇶
button (copy into every room) still exists but makes five copies to maintain; the tick
does not. A room holds **16** animations now (14 authored + the two walks the export
adds), raised from 10 because feelings ate the budget before a room had anything to do.

## 6. Work (the template all side rooms follow)

- **One visit** (default): out the right door, through the middle, **two randomly-chosen
  work performances** among the room's marked jobs, the 10-second floor filled with
  wandering, then home. Work pays the discipline meter and costs a little energy.
- **A session** (opt-in via the "work lasts N min" slider): the session owns the clock —
  work → short stroll → work — for N minutes. Press WORK again to call him home; any
  other command outranks the session.

## 7. The potty routine (the one passive outing)

35% chance per meal, ripening 4–9 minutes after eating — and a standing bladder arms a
trip every 8–15 minutes regardless, so the routine is part of his rhythm. The urge *keeps* until he's free, then: "bunbun needs the
potty!" → left door → middle → **toilet** (authored duration) → **sink** to wash hands →
home. Messes on the floor are retired; the world made them obsolete.

## 8. Sleep and the clock

- **Bedtime is inbound-only**: lights-out anywhere ends the working day, walks him home,
  then to the **sleep mark**, and his own sleep clip goes up the instant he arrives. The
  lamps in the room go out once he's actually settled.
- **Auto-nap**: inside the bedtime window (default 10pm–6am, adjustable), 10 idle minutes
  + 1 quiet minute → the nap screen; 60s later the panel sleeps (touch-wake, tap-tap).
  The 6am wake credits a full night's energy.
- **The clock**: taught once (on-device CLOCK row or `/api/debug/clock?min=N`), the
  timezone is learned and kept forever; wifi re-syncs handle everything including power
  loss. Only a power loss *without* wifi ever needs a hand again.

## 9. The pet itself

- **Adult forever** — aging is retired; nothing (fresh egg, restore, clock hiccup) can
  show a baby. Age still counts as the OTA pet-preservation signal.
- **Meters**: food, fun, clean, energy, health, discipline, love. Meals feed, baths
  clean, work disciplines, play (four mini-games) refills fun — play is earned, gated on
  basic care. *(Meter cadence is tomorrow's tuning project.)*
- **The ticker speaks in Jon's words** — the whole emote/action line table was reworded
  by the owner. It names whatever he's doing (no bare "busy"), announces every emotion
  (custom clips included — a child's own ticker line always wins), stays quiet over a
  sleeper, and never repeats within 15 seconds.
- **The cat** visits the main room on the builder's schedule: roams, claims her chair,
  gets petted (only when he's free — never mid-errand), knocks things over; he huffs
  (when free) and tidies (when free). Species carry points respected — the penguin
  carries the jar in his beak.
- **State is sacred**: saves every 20s, a slow backup every ~5 min that resets never
  touch, a size-tolerant loader, and now `POST /api/debug/restorebk` — the undo for an
  accidental "start over?" (used once, in anger, to bring Brave back).

## 10. The environment

- **Sky polygons** (≤12 points) drawn in the assembler mark where sky lives — clouds and
  weather render inside them, behind window frames when the art occludes.
- **Procedural clouds**, **twinkling stars**, and a **celestial body** — the sun crosses
  6:00→20:00, the moon takes the night, both following the device's real clock.
- **Rain** travels as a cadence. Weather is world-level and exports **pin the live sky**
  over every room (toggle it on before exporting — `rw`/`cn` in the file are the truth).
- **Lighting**: the builder is the lighting record — lamps the child places cast the
  cones; a custom room with no lamps gets an honest even room (no phantom sconce). The
  compiled-in cat clock (real time, swinging tail) lives in the main room only.

## 11. The device, day to day

- **AirPlay 2 receiver** all the while — the pet dances when music plays (and yields the
  floor to errands).
- **OTA everything**: firmware over wifi with a rollback safety window; art/pak over
  HTTP; scenes over the `/build` page. The public build strips firmware OTA but keeps
  AirPlay, SD, and asset upload.
- **The debug surface** (the night's workhorse):
  - `/api/debug/brain` — live role, lights, targets, current anim, potty stage, work
    state, and the four meters
  - `/api/debug/act?a=…` — bath/eat/work/sleep/wash/potty (ripens the timer) /hungry
    (instant food→20)
  - `/api/debug/clock?min=N`, `/api/debug/restorebk`, `/api/screenshot`,
    `/api/system/info` (with crash breadcrumbs), `/ws/logs`

## 12. Open threads — as of 2026-08-21 evening

**The one real blocker.** A panic the instant a phone connects to AirPlay. Confirmed
`reset_reason 4` with the render breadcrumb at **22 (BC_MESS)** — but the mess loop is
ruled out (`poopN` reads 0), and BC_MESS turned out to still be a *span*: with no disco up
and the light map cached, nothing stamps again until the next frame. It has since been
split into 24 CHARSPRITE / 25 BANDS / 26 PUSH / 27 PANEL, so the next occurrence names the
place. **Do not diagnose this from the source — read the device.**

**Known and deliberate, not yet fixed:**

- `/api/ota/assets` erases the whole art partition in one blocking multi-MB call, with the
  renderer stand-down (`fw_assets_writing()`) compiled out of the public build. Every
  `/build` install is that race, unguarded. It has survived every time; that is dice.
- `/api/ota/update` accepts any image with no authentication (CI-2608). The decision is to
  kill firmware OTA in the public build rather than sign it; the flag exists but does not
  cover this endpoint yet.
- Nothing records a pak's **bake scale** and nothing checks it at install. See
  `tools/GENERATION.md` — this is the cheapest remaining shippability win.

**The assembler is no longer in parity with the device.** Everything in section 4's
rewritten doctrine — stay-until-full, the rota, mood suppression, the room owning him —
lives only in firmware. The preview still models the old behaviour, which breaks the rule
the whole project runs on: *the tool is the spec*. First job next session.

**Wanted:** a real dance animation. Dance mode has never had one — it plays the `jump`
clip with the movement done in code (a 7px arc on the beat, a sway, a strut between
bursts), and for the five-pack cast `jump/anim` is five copies of `Adult_Idle/south.png`.
A drawn dance would be the first genuinely new *motion* in a pack rather than a re-pose.

**Further out:** perch visitors and the lofi train, visitor swap (skinnable cat),
when-blocks, wearables, the kid guide refresh, license decisions for public release.
