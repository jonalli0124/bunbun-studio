#pragma once
// ============================================================================
// VISITORS — a data-driven creature system.   PROPOSAL, NOT YET WIRED IN.
// Nothing #includes this file; the shipped cat still runs from main.cpp.
// ============================================================================
//
// Jon, 2026-08-14: "we need to figure out a good workflow for adding creatures that
// interact with bunbun and the environment to enhance the lofi system... like the lazy
// cat that comes in and is pet and lays down for a bit on the chair and then wakes up
// yawns and leaves."
//
// The cat proved the FEELING is right. What it did not prove is that we can add a
// second one cheaply: she costs ~160 hand-written lines (main.cpp:3707-3864), and every
// one of those lines re-derives something generic — an eligibility guard list, per-room
// furniture anchors, a walk, a hop arc, the handshake that makes bunbun cross the room,
// a randomised re-visit timer, a scale that matches the room's perspective. Creature #3
// would copy all of it and drift on every detail.
//
// So: the BEHAVIOUR becomes a table, and the engine below is written once. Adding a
// visitor becomes a dozen lines of data plus art — which is a thing we can do on a whim,
// which is the whole point of a lofi world that gets quietly richer over time.
//
// THE CHARTER APPLIES TO GUESTS TOO. "Chill lofi... bunbun can grow and be a passive
// creature but you can also play a game if you like." A visitor may never demand
// anything. It arrives on its own schedule, it is happy to be ignored (the cat puts
// herself on the chair after 22s whether or not anyone noticed), it never blocks input,
// never costs stats, and never appears while the room is meant to be still — asleep,
// away, mid-game, dancing, or poorly.
//
// ---------------------------------------------------------------------------
// THE FOUR-STAGE WORKFLOW
// ---------------------------------------------------------------------------
//
// 1. ART — PixelLab objects, which unlike rooms PERSIST in the account (57 of them as
//    of today; see [[project-bunbun-room-recipe]] for why rooms don't).
//
//    ANCHOR TO BUNBUN. NEVER TO THE CAT. An earlier draft of this file told you to pass
//    style_object_id = <the tabby cat's id>. That was WRONG and cost a day: the original
//    items/cat.png is a shaded orange tabby with stripes and rendered volume — 50 colours
//    against bunbun's 12 — so it was never on-model, and using it as the anchor propagated
//    its own off-model look into the yarn, the frog and the hedgehog. Jon, 2026-08-14: "the
//    cat and yarn need to match bunbun style" / "it looks out of place".
//
//    Use tools/creature_style.py, which defaults to bunbun's idle sprite and crops to the
//    character before padding. State flatness as an explicit NEGATIVE in the description —
//    "no stripes, no fur texture, no shading, no highlights, no gradients" — because naming
//    the style alone does not get there. Tag results "bunbun-creature" / "bunbun-prop".
//
//    AUTHOR NEAR FINAL SIZE. A 64px sprite shrunk to ~27px has its 1px outline decimated
//    into gaps, which is why the flat cat still reads washed out beside bunbun even though
//    her palette is right: he is drawn at 1.45x and thickens, she is drawn at 0.5x and
//    breaks up. Match his outline colour too — pure black #040404, not a warm brown.
//
//    Then: trim_sprites.py -> assets/items/<name>{,-sleep}.png -> convert_assets.ps1.
//    Sprites stream from the pak. NEVER a static buffer — see the g_spr internal-RAM
//    famine in [[project-bunbun-lessons-2026-08-13]].
//
// 2. ANCHORS — where a creature may stand, per room, resolved from one table instead of
//    a bespoke catChairSpot() per creature. The rooms differ (the teen beanbag is not
//    the farmhouse chair), so the anchor is named and the geometry is looked up.
//
// 3. SCRIPT — the beat table below. A beat is one intention: come in, wait, be fussed
//    over, cross to the chair, hop up, sleep, stretch, leave.
//
// 4. GATES + SCHEDULE — declared, not coded. One bitmask and three numbers.
//
// ---------------------------------------------------------------------------

// ---- where a visitor can be, in the room's own coordinate space ----
// Resolved per phase, because the furniture genuinely moves between rooms.
enum CrAnchor : uint8_t {
  CR_A_OFF_LEFT,     // just off-screen, floor height
  CR_A_OFF_RIGHT,
  CR_A_INSIDE,       // a few steps in — far enough to read as "arrived"
  CR_A_CHAIR,        // the seat itself (farmhouse chair / nursery armchair / teen beanbag)
  CR_A_RUG,          // centre of the room, on the floor
  CR_A_WINDOW,       // the sill — read from the room art, never hardcoded (make_room.py)
  CR_A_BOWL,         // beside bunbun's things
};

// ---- what a beat does ----
enum CrOp : uint8_t {
  CR_ENTER,    // walk in from off-screen to the anchor, then advance
  CR_LINGER,   // hold position; with CRF_INVITE, bunbun comes over and it becomes a moment
  CR_GOTO,     // walk along the floor to the anchor
  CR_HOP,      // the quarter-second arc UP onto the anchor. A cat getting onto a chair
               // jumps; drifting upward looks like a ghost (Jon, 8/14)
  CR_REST,     // stay put for a randomised while, usually on a different sprite
  CR_STRETCH,  // the wide flex — scale on x, which reads as a stretch at this size
  CR_APPROACH, // walk to whichever loose prop is nearest, and stop a paw's length short
  CR_SWAT,     // wind up, then hit it — see the physics note below
  CR_WATCH,    // hold still and look at the thing you just sent skittering
  CR_EXIT,     // walk off; ending the visit and arming the next one
};

enum {
  CRF_INVITE   = 1 << 0,   // bunbun walks over during this beat; the beat ends when he arrives
  CRF_LOVE     = 1 << 1,   // the moment tops up love (a guest is good for you)
  CRF_PURR     = 1 << 2,
  CRF_YAWN     = 1 << 3,
  CRF_TICK     = 1 << 4,   // the small physical sound of a landing
  CRF_TIMEOUT  = 1 << 5,   // if nobody notices, move on anyway rather than waiting forever
};

struct CrBeat {
  CrOp        op;
  const char *sprite;      // pak key, or nullptr to keep the current one
  uint8_t     anchor;      // CrAnchor, for the movement ops
  float       speed;       // px/s
  uint16_t    minMs, maxMs;// duration window; the engine picks once, per visit
  uint8_t     flags;
  const char *say;         // one scrolling line, or nullptr. At most one per visit.
};

// ---- when a visitor may call at all ----
enum {
  CR_G_AWAKE    = 1 << 0,  // lights on, not asleep
  CR_G_HOME     = 1 << 1,  // not hopped away
  CR_G_IDLE     = 1 << 2,  // no action, no errand, no game
  CR_G_CALM     = 1 << 3,  // not dancing
  CR_G_WELL     = 1 << 4,  // not sick
  CR_G_DAY      = 1 << 5,  // not deep night
  CR_G_USUAL    = CR_G_AWAKE | CR_G_HOME | CR_G_IDLE | CR_G_CALM | CR_G_WELL | CR_G_DAY,
};

struct CreatureDef {
  const char *name;
  const char *sprite;      // the default pak key
  // DRAWN HEIGHT AS A FRACTION OF BUNBUN'S DRAWN HEIGHT. Not the firmware's fallback
  // constant, and not a fraction of the creature's own source canvas — both of those
  // mistakes were made and both render the creature too small. The firmware's cat works
  // out to bunbunDrawnH / 2.1, i.e. scaleK = 0.476.
  float       scaleK;      // e.g. the cat is 0.476, NOT 0.34
  uint16_t    gates;
  uint32_t    firstMs;     // earliest first visit after boot
  uint32_t    gapMs;       // typical spacing between visits
  uint32_t    jitterMs;    // random spread on top, so it never feels scheduled
  const CrBeat *beats;
  uint8_t     nBeats;
};

// ============================================================================
// THE CAT, expressed as data — every number lifted from the shipped implementation
// so this can be diffed against her behaviour rather than replacing it on faith.
// ============================================================================
static const CrBeat CAT_BEATS[] = {
  // in from the right, stopping a little inside the room
  { CR_ENTER,   "items/cat",       CR_A_INSIDE, 26.0f,     0,     0, 0, nullptr },
  // she waits to be noticed. He comes over; if he never does, she shrugs after 22s.
  { CR_LINGER,  nullptr,           0,            0,     1400, 22000,
    CRF_INVITE | CRF_TIMEOUT, nullptr },
  // the fuss — his "love" animation, a purr, and a little top-up
  { CR_LINGER,  nullptr,           0,            0,     3500,  3500,
    CRF_LOVE | CRF_PURR, "a cat came to visit!" },
  { CR_GOTO,    nullptr,           CR_A_CHAIR,  22.0f,     0,     0, 0, nullptr },
  { CR_HOP,     nullptr,           CR_A_CHAIR,   0,       450,   450, CRF_TICK, nullptr },
  { CR_REST,    "items/cat-sleep", 0,            0,    90000,210000, 0, nullptr },
  { CR_STRETCH, "items/cat",       0,            0,     2400,  2400, CRF_YAWN,
    "the cat woke up" },
  { CR_EXIT,    nullptr,           CR_A_OFF_RIGHT, 30.0f,   0,     0, 0, nullptr },
};

static const CreatureDef CREATURE_CAT = {
  "cat", "items/cat", 0.34f, CR_G_USUAL,
  300000,    // first visit 5 min in
  900000,    // ~15 min between visits
  1500000,   // +0-25 min of jitter
  CAT_BEATS, sizeof(CAT_BEATS) / sizeof(CAT_BEATS[0]),
};

// ============================================================================
// AND HERE IS THE POINT — three more visitors, entirely as data. No new engine
// code, no new draw path, no new scheduler. Art is the only real cost, and the
// style comes free from the cat via style_object_id.
// ============================================================================

// A hedgehog who trundles in, has a drink, and trundles out. Never interacts;
// some guests are just weather. Deliberately the cheapest possible creature: one
// sprite, three beats.
static const CrBeat HEDGEHOG_BEATS[] = {
  { CR_ENTER, "items/hedgehog", CR_A_INSIDE,    14.0f,    0,    0, 0, nullptr },
  { CR_REST,  nullptr,          0,               0,    6000,12000, 0, nullptr },
  { CR_EXIT,  nullptr,          CR_A_OFF_LEFT,  14.0f,    0,    0, 0, nullptr },
};
static const CreatureDef CREATURE_HEDGEHOG = {
  "hedgehog", "items/hedgehog", 0.26f, CR_G_USUAL,
  1200000, 2400000, 3600000,
  HEDGEHOG_BEATS, sizeof(HEDGEHOG_BEATS) / sizeof(HEDGEHOG_BEATS[0]),
};

// A moth at the window after dark — the one visitor whose gates INVERT the day
// rule. It never touches him; it just makes the evening feel occupied.
static const CrBeat MOTH_BEATS[] = {
  { CR_ENTER, "items/moth", CR_A_WINDOW,    30.0f,     0,    0, 0, nullptr },
  { CR_REST,  nullptr,      0,               0,     8000,20000, 0, nullptr },
  { CR_EXIT,  nullptr,      CR_A_OFF_RIGHT, 30.0f,     0,    0, 0, nullptr },
};
static const CreatureDef CREATURE_MOTH = {
  "moth", "items/moth", 0.14f,
  CR_G_HOME | CR_G_CALM | CR_G_WELL,       // welcome at night, welcome while he sleeps
  600000, 1200000, 1800000,
  MOTH_BEATS, sizeof(MOTH_BEATS) / sizeof(MOTH_BEATS[0]),
};

// A snail crossing the rug over several minutes. Pure background life: it is
// funny precisely because it is still there when you come back.
static const CrBeat SNAIL_BEATS[] = {
  { CR_ENTER, "items/snail", CR_A_RUG,        3.0f,     0,    0, 0, nullptr },
  { CR_REST,  nullptr,       0,               0,    20000,60000, 0, nullptr },
  { CR_EXIT,  nullptr,       CR_A_OFF_RIGHT,  3.0f,     0,    0, 0, nullptr },
};
static const CreatureDef CREATURE_SNAIL = {
  "snail", "items/snail", 0.18f, CR_G_USUAL,
  1800000, 3600000, 3600000,
  SNAIL_BEATS, sizeof(SNAIL_BEATS) / sizeof(SNAIL_BEATS[0]),
};

// ---------------------------------------------------------------------------
// ENGINE CONTRACT (to be implemented once, replacing updateCat/drawCat):
//
//   crTick(dt)     — advance the single active visitor; pick the next arrival when
//                    none is active. ONE VISITOR AT A TIME, always: it protects the
//                    sprite budget and, more importantly, keeps the room calm.
//   crDraw()       — draw in room space, with the furniture, so a visitor is occluded
//                    and lit exactly like the scenery. Sprite streams from the pak.
//   crCall(name)   — the debug hook the cat already has as bunbun_call_cat(), so a
//                    visit can be watched without waiting out its schedule.
//                    MUST be compiled out or auth-gated for non-family units, with
//                    the rest of /api/debug/*.
//
// Verification plan, because the cat is already shipped and loved: run the table
// engine alongside the existing code behind a build flag, call both from the debug
// hook, and confirm the beat timings and positions match before deleting anything.
// ---------------------------------------------------------------------------
