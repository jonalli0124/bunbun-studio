// bunbun — game state and tuning, ported from the browser build.
//
// The numbers here are the ones tuned in the HTML prototype and should stay in step with it.
// Anything measured on the hardware (touch calibration, colour) lives elsewhere.
#pragma once
#include <Arduino.h>

static const int SCR_W = 320, SCR_H = 240;
static const int FLOOR_Y = 200;

// ---- life cycle, in minutes ----
// 0-6h    crawling only
// 6-9h    learning to walk: wobbly walks that tumble back into a crawl
// 9-24h   walking properly
// 24-48h  teen — school instead of work
// 48h+    adult
static const long TODDLER_AT = 360, WALKER_AT = 540, BABY_END = 1440;
static const long TEEN_END = 2880;      // a second day as a teen before growing up

static const float HEALTH_FLOOR = 12.0f;
static const float DECAY_NIGHT  = 0.45f;   // needs drift far slower after dark

// Per-minute decay rates. The browser's values divided by FOUR (they were /8, which turned out
// to be too placid — a glance usually found nothing to do, so there was no reason to pick it
// up). A full need now empties in ~1.9h (adult) / ~1.2h (baby), so checking in every couple of
// hours finds something worth doing, while walking away for the afternoon still does not
// return a bunbun in crisis. The browser's original /1 rates emptied adult food in 28 minutes,
// which is the frantic end this is deliberately nowhere near.
//
// DECAY_NIGHT still halves all of this after dark, so overnight neglect stays forgiving.
//
// sleepGain is deliberately NOT scaled with the rest. Needs draining slowly is what makes the
// game relaxing; recovery being slow just makes it tedious, and a nap you cannot sit and watch
// finish is not a beat. Empty to full is ~2.5 min (baby) / ~3 min (adult).
struct PhaseRates { float food, fun, clean, energy, sleepGain, meterDecay; };
static const PhaseRates RATES_BABY  = {1.40f, 1.06f, 0.56f, 0.96f, 40.0f, 0.62f};
// A teen eats more than an adult and tires faster, but is tidier than a baby — the needy
// middle. SCHOOL (the disc meter) also slips a little quicker than an adult's work does.
static const PhaseRates RATES_TEEN  = {1.16f, 0.88f, 0.48f, 0.76f, 36.0f, 0.54f};
// RULED 2026-08-24 (the meter re-tune, docs/METER-TUNING-2026-08-24.md): the day is
// tuned so the owner can narrate it - food empties in ~4.8h (2-3 meals a day), fun the
// same (a play session per stretch of day), clean in ~14h (one bath a day), energy in
// ~5.5h (a nap plus the night), work in ~8h (one shift). NOTE phaseOf() is hard-wired
// adult, so BABY/TEEN above are DORMANT until the aging revival - only this line is live.
static const PhaseRates RATES_ADULT = {0.35f, 0.35f, 0.12f, 0.30f, 32.0f, 0.20f};

// Sprites are authored at 96x96 but drawn larger, exactly as the browser does
// (cScale: baby 0.78*1.45, adult 1.0*1.45). Scaling at blit time rather than baking it into
// the pack keeps the art ~650KB instead of ~1.3MB, which is what leaves room for the music.
static const float SCALE_BABY  = 0.78f * 1.45f;   // 1.131
static const float SCALE_TEEN  = 0.90f * 1.45f;   // 1.305 — between the two, as he is
static const float SCALE_ADULT = 1.00f * 1.45f;   // 1.45

// walkable floor per phase
struct Bounds { int x0, x1, y0, y1; };
static const Bounds BOUNDS_BABY  = {34, 288, FLOOR_Y - 26, FLOOR_Y + 8};
// Tuned in the browser against room-teen.png, not estimated off the PNG. x1 stops at 268
// because a potted plant fills the bottom-right corner; y0 sits below the wall/floor junction
// so he stands ON the floorboards rather than up among the furniture.
static const Bounds BOUNDS_TEEN  = {30, 268, FLOOR_Y + 4, FLOOR_Y + 36};
static const Bounds BOUNDS_ADULT = {26, 298, FLOOR_Y - 24, FLOOR_Y + 30};

// Scenery footprints: his FEET must land at or below yFront, so he reads as standing in
// front of the furniture rather than inside it. Measured off each room's painting.
struct Block { int x0, x1, yFront; };
static const Block BLOCKS_BABY[]  = {{6, 96, FLOOR_Y - 8}};
// Tuned in the browser against room-teen.png. Deliberately WELL below each piece's real front
// edge (bed ~y208, chair wheels ~y215): the floor strip in front of the bed is only ~32px deep
// while he is ~60px tall, so standing at the true edge read as being inside the furniture.
//   bed x0-152 | beanbag+chair x196-252 | desk x246-320
static const Block BLOCKS_TEEN[]  = {{0, 152, FLOOR_Y + 22},
                                     {196, 252, FLOOR_Y + 26},
                                     {246, 320, FLOOR_Y + 22}};
static const Block BLOCKS_ADULT[] = {{0, 50, FLOOR_Y + 20},
                                     {78, 118, FLOOR_Y - 8},
                                     {268, 320, FLOOR_Y + 27}};

// STAGE_HATCHING is the 5-frame crack sequence, which plays ONCE after the third tap.
// While STAGE_EGG the egg holds on frame 0, intact — the HTML pins animT to 0 for exactly
// this reason, so the shell doesn't crack before you've done anything to it.
enum Stage { STAGE_EGG, STAGE_ALIVE, STAGE_HATCHING };
// PH_TEEN is appended, NOT inserted, so the existing saved phase values keep their meaning
// and v4 saves stay loadable. Order of life is baby -> teen -> adult; order of the enum is
// not, and phaseOf() is the only thing that decides which comes when.
enum Phase { PH_BABY, PH_ADULT, PH_TEEN };

struct GameState {
  uint32_t magic;
  uint16_t version;
  uint8_t  stage;         // Stage
  uint8_t  phase;         // Phase
  int64_t  ageMs;         // ACCUMULATED, never derived from the wall clock
  int64_t  savedWallMs;   // wall clock at last save. RULED 2026-08-24: powered-off time
                          // is FREE - no catch-up decay on boot. A deliberate kindness
                          // (the Nintendogs model), not an unfinished feature.
  float    food, fun, clean, energy, health, disc;
  uint8_t  lights;
  uint8_t  sick;
  int16_t  x, y;
  uint8_t  poopN;                 // messes on the floor; SWEEP clears them
  int16_t  poopX[4], poopY[4];
  // Character pack, CHARACTER-PACKS.md §8.1. APPENDED, never inserted. loadStateFrom() reads
  // min(stored, sizeof) into a zeroed struct, so every pet saved before this field existed
  // arrives with species_idx 0 — the base bunny — and keeps its age. That only holds while 0
  // is the correct default, which is the rule for every field added after this one.
  uint8_t  species_idx;
  // ---- the 2026-08-24 meter re-tune (all APPENDED, all zero-default-correct) ----
  // love2: the LOVE meter, finally in the save so DECAY persists (it used to live only
  // in the NVS "love" key, written on cuddle events only - so every reboot restored the
  // last cuddle's value and love could never really go down). Stored as value+1 so 0
  // stays the "not migrated yet" sentinel: a pre-retune save arrives with 0 here and
  // the loader keeps the legacy NVS value instead.
  float    love2;
  // neglectMin: the sickness accumulator, persisted so a reboot is no longer an amnesty
  // (it was a function-local static; between that, the every-wake grace reset and the
  // <15 gates, sickness never fired and MEDS was a dead button).
  float    neglectMin;
  // The LOVE BANK: the meter is about today, the bank is about everything before.
  // loveLevel (0-5 hearts, the fifth added by the owner 8/24) only ever rises; loveProg counts good days (meter >= 60 at
  // the night tuck-in) toward the next heart, 3 per heart. loveDay/sniffDay are
  // (epoch-day % 65000)+1 day stamps (0 = never) guarding one bank check and one rare-
  // sniffle roll per day.
  uint8_t  loveLevel;
  uint8_t  loveProg;
  uint16_t loveDay;
  uint16_t sniffDay;
  // careMiss (2026-08-24, framework pass): the Tamagotchi care-mistake ledger. A need
  // that sits on its cue floor (<25) for 15+ real minutes while he is awake logs one
  // miss (once per excursion). The nightly bank check now requires love >= 60 AND at
  // most one miss - a day of ignored calls does not bank, however cuddled the evening
  // was. Reset after each daily check. Zero-default correct.
  uint8_t  careMiss;
};

static const uint32_t SAVE_MAGIC = 0x4E554221;   // "!BUN"
// Bumped to 2 so saves from before the egg stage existed are rejected and you get a fresh
// egg rather than resuming a part-grown bunbun with a stale age.
// DELIBERATELY NOT BUMPED for species_idx. loadStateFrom() accepts SAVE_VERSION or 3 and
// nothing else, so bumping to 5 would reject the pet currently on the canary — the exact
// failure §0.1 forbids. A field appended with 0 as its correct default changes no existing
// field's meaning, so the version does not need to move. Bump it only when a field's MEANING
// changes.
static const uint16_t SAVE_VERSION = 4;   // v3 + the slower, relaxed decay rates
