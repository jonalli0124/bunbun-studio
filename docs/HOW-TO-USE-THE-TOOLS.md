# How to use Animation Creation and the Scene Assembler

*Updated 2026-08-19: worlds, packages, and species.*

*The two web pages where you build everything your bunbun does — on the project's
GitHub Pages site (no install), or locally from `tools/build/`:*

- **Scene Assembler** — `scene_tool.html`
- **Animation Creation** — `attach_editor.html`

**Getting work onto the device** (the one flow): press **save a bunbun package** in the
assembler — one timestamped `.bunbun` file with every room, all the art, and the
animal's walk clips — then open `http://<device-ip>/build` and give it the file. A
package replaces all custom animations on the device. (The old "Send to bunbun" button
is gone; wherever this guide mentions it below, read "save a bunbun package + /build".)

**Rooms with roles**: the "this room is" selector turns one scene into a world — main,
kitchen (eating), bathroom (bath / wash / toilet), work. **Counts as** on any animation
makes it the room's meal / bath / wash-up / toilet / sleep / job, whatever the clip is
called. Work is one visit by default; the "work lasts" slider opts into a timed session.

**Animals**: pick the animal in Animation Creation before animating — the saved file
carries the animal and its walks, so it stays that animal in the assembler, the preview,
and on the device. Swap the device's animal with a species `.bunbun` from the site's
animal shelf.

Your work saves itself in the browser as you go (each computer/tablet keeps its own).
`Save scene` / `save animation` write files you can share, back up, or reopen anywhere.

Screenshots of both pages: `screenshots/animation-creation.jpg` and
`screenshots/scene-assembler.jpg`.

---

## Part 1 — Animation Creation (make ONE animation)

This page makes a single animation: the character doing one thing, with things attached.

1. **Name it** (top-left box). The saved file is named after it, and the clip you build on
   decides what the game treats it as — build eating on `Adult_Eat`, sleeping on
   `Adult_Sleep`, bathing on `Adult_Bathe`. That's the whole wiring; there is no other step.
2. **Pick the clip** — the character's base motion. A backdrop room is optional and only
   for judging colors; the real room is built in the Scene Assembler.
3. **The frame strip** (under the stage): click a frame to jump there, `◀ ▶` reorder,
   `⧉` duplicate, `×` skip, `−/+` scale that step. `reverse` and `ping-pong` reshape the
   whole loop. `speed` sets frames per second; `hold each frame` slows everything evenly.
4. **Attach things** (right column): pick a prop, `+ add`. Each layer has an anchor
   (left/right hand, above head, between hands, floor), scale, rotation, color/tint,
   jiggle, its own breathing, and `show on frames` (e.g. `3-5`) so a soap can appear only
   mid-scrub. The stack's order is the draw order — top draws in front. The character is a
   row in the stack, so "behind him" is just below his row.
5. **🎨 Draw your own prop** (the button by `+ add`): a pixel editor — pencil, fill,
   mirror, undo, start-from any existing prop. Saved drawings become real props: they
   attach, they bake, they reach the device, and they travel inside your saved file.
6. **Breathing** (left column): a gentle scale pulse pinned at the feet. This is what
   makes a held pose look alive. Rule of thumb: 4–6% with a period of ~8 is clearly
   visible; 0.5% is less than a pixel and shows nowhere — the preview is honest about it.
7. **💾 save animation** — writes the `.json` file. That file is what the Scene Assembler
   imports. (`load animation` reopens one; ✨ fresh wipes the stage — it asks twice.)

## Part 2 — Scene Assembler (build the room and its life)

1. **Name the scene**, pick the **room**, and **put things in it** from the dropdown.
   Each object can be resized, rotated, flipped, layered, locked (🔒 = clicks pass
   through it), hidden, or made a **light** with its own brightness and spread. Rugs are
   walked over automatically; furniture blocks by its footprint.
2. **Draw the ground** (color-coded buttons):
   - **+ walkable** (green) — the floor polygon. He only walks where you draw.
   - **+ keep out** (red) — "walk through freely, never stop here". Not a wall.
   - **+ activity area** (purple) — a named place you can bind animations to.
   Click around the shape, then click the first corner (or `finish shape`). Every shape
   can be renamed, hidden per-eye, or removed. `Ctrl+Z` undoes anything.
3. **Add animations** — import the files saved from Animation Creation. Each row has:
   - **on/off**, the ghost **eye** (👁 show/hide its preview ghost), the **lock**
   - **the rule** (the tag on the right): *anywhere he walks*, *only on things that afford
     it*, *pinned to specific items*, or *only in an activity area*
   - **drag the ghost** on the stage to fine-place exactly where it happens (the selected
     animation's ghost is always draggable, even if hidden)
   - **plays [N] s** — how long it runs (blank = two loops + a natural rest)
   - **he says [...]** — the ticker line on the device, in your words
   - ✏ rename · 💾 export one · ⟳ swap in a re-edited file · ⧉ duplicate · × remove
4. **Watch it live**: `Act it out` runs the same brain the device runs. The **make him:**
   row forces any animation right now (stock clips fill in for emotions you haven't made).
   `Game view` shows the shipped screen side by side. `Circles: on` toggles the helper
   rings. The preview shows exactly what the device can draw — same pixels, same sizes.
5. **HIS SIZE, everywhere** — the one dial that sets the character's size for every
   animation and the walking between them. Use this, not the per-animation sizes, unless
   you want a deliberate exception.
6. **Send it to the shelf**: type the device IP once, press **🐇 Send to bunbun**. The
   page does the entire port — art, animations, rules, words — and checks the pet before
   and after. (Or `Export everything` for a zip a grown-up can port with one command.)

## The room library (refreshed 2026-08-17)

Every room in the picker is **bare** — walls, floor, light, and nothing else. Furniture is
the kids' job; that's the whole idea. The furnished legacy rooms were retired to
`assets/rooms/furnished/` and no longer appear.

What's on the shelf now, besides the home rooms (farmhouse, farm, kitchen, school, and the
pet's own baby/teen/adult rooms):

- **kitchen-bare** — butter-yellow walls, white tile band, fresh-built and empty
- **construction** — the indoor factory (gray block walls, hazard stripes) — the style
  reference the whole library is graded against
- **washroom** — the calm bath-room: cream walls, peach tile band, honey tile floor
- **worksite** — outdoor construction: dirt yard, distant girders, hazard-stripe barrier
- **meadow** — rolling grass hills, flowers kept to the horizon, big open middle
- **clearing** — a sunlit glade ringed by trees
- **beach** — sandy shore, water on the horizon
- **station** — the lofi train stop: sunset sky, string lights, rails behind the platform

A room + items + your animations = a job or an outing. The construction worker is just the
worksite room, a hard hat and cones from the item tray, and a hammering loop you author —
no code, ever.

## Part 3 — Making brand-new rooms, items and feelings (grown-up lane)

When the kids want art that doesn't exist yet, generation happens in a Claude session
(PixelLab) and installation is one command from the repo:

```
py tools/new_asset.py room  <png> <name>    # a bare 320x240 room
py tools/new_asset.py item  <png> <name>    # furniture, toys, tools, food
py tools/new_asset.py light <png> <name>    # anything that should glow
py tools/new_asset.py mark  <png> <name>    # a feeling shown over the head
```

The tools see new art immediately; the next **Send to bunbun** carries it to the device.
The doctrine lives in `tools/GENERATION.md` — the short version:

- **Reuse the exact prompt.** Winners are logged verbatim in `assets/rooms/PROMPTS.md` and
  `assets/objects/PROMPTS.md`. Never paraphrase a winner.
- **Naming the room type summons its furniture** — describe materials ("tile grid, honey
  wood floor"), never say "bathroom".
- **Outdoor scenes decorate the walk band by default** — give the bottom strip its own
  "plain smooth flat … no flowers, no tall grass" clause.
- **Verify by rendering and looking**, never by filename. Daylight scenes carry the house
  style; night scenes come out muddy.

## Coming next (specced, not yet built)

- **Multi-room worlds** — doorways at the screen edges; he walks off one side and into the
  bathroom / kitchen / work, does the thing, and comes home. Each room is a normal scene.
- **Wearables** — a prop with a home and an attach slot: he lifts the hard hat off its
  hook, wears it through the door, and hangs it back up after his shift.

Both live in `tools/WORLDS-SPEC.md`. The whole-project story (PixelLab → Animation
Creation → Scene Assembler → device) is `tools/PROJECT-OVERVIEW.md` — hand that file to
any Claude chat and it knows the project.

## What the device does with it

- **Emotions** (hungry, tired, bored, sick, angry, love) show wherever he stands, using
  your animations when you've made them.
- **Actions** (eat, bath, sleep) happen when something drives them — the FEED/BATH/sleep
  buttons or his needs — and he walks to where you placed them. Sleep is special: he
  walks to your sleep spot, but the game decides when he wakes (that's his care, not a
  timer).
- **Idle and sit** are his everyday fillers — he visits those on his own rhythm.
- Everything else is his own dice: when he moves, which spot he picks, how long he
  lingers. You author the world; he lives in it.

## If something looks wrong

- Refresh the tool page first — updates land in the page, your work is safe in the save.
- The tools refuse loudly, not silently: watch the message line at the bottom. A scene
  that exceeds the device (24 objects, 8 animations, 6 placed spots) says exactly what
  didn't fit.
- The preview IS the truth. If the device disagrees with the preview, that's a bug in the
  port or firmware — report it that way.
