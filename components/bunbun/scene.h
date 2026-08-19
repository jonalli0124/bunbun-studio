// scene.h — the room, read from a file instead of compiled in.
//
// WHY THIS EXISTS (Jon, 2026-08-15): "we need to streamline the process for the dozens of new
// scenes, characters, and new actions coming". Until now a room's layout lived in main.cpp as
// constants — RUG_X, BOUNDS_ADULT, BLOCKS_ADULT — so trying a new arrangement meant editing C,
// rebuilding and OTA-ing the whole firmware. That does not scale to dozens of scenes, and it is
// the step the scene builder was supposed to remove.
//
// A scene now arrives as a small JSON file at /spiffs/scene.json, uploaded over the EXISTING
// /api/fs/upload endpoint. tools/scene_push.py converts a builder export into it, resolving the
// awkward parts on the PC where they are easy:
//   - sprite names are resolved to pak names ("table" -> "items/table")
//   - the builder anchors a sprite by its FEET, drawItemAt() takes its TOP: converted
//   - the builder's per-asset draw factor k is folded into one device scale
// so the firmware only ever reads pre-resolved numbers. Less parsing on the device is less that
// can go wrong on the device.
//
// SAFETY, in order of importance:
//   1. A missing, unreadable, oversized or malformed file leaves the compiled-in room EXACTLY as
//      it is. Every accessor falls back. You cannot lose the room by uploading rubbish.
//   2. Nothing here touches saved state. The pet, its age and its stats are untouched by a scene
//      load — this only ever describes furniture.
//   3. Fixed caps on every array, one fixed filename (no accumulation on SPIFFS, which has bitten
//      us before), and the file is read once at boot into ~2KB of static storage.
#pragma once
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

#define SCENE_PATH        "/spiffs/scene.json"
// ROOMS WITH ROLES (Jon 8/18: "define some scenes, work, bathroom, kitchen, main room.
// Each one tied to an action... he does this by walking to the side of the screen").
// A world is nothing but role-tagged scene files; a device with only scene.json behaves
// exactly as before. Kitchen lives to the LEFT of the main room, bathroom and work to
// the RIGHT - that is all the geometry there is.
// 6144 -> 8192 when custom animations arrived: eight anims add ~600 bytes to a file that was
// already brushing 5KB on a full room, and a scene refused for SIZE fails with the same silence
// as a malformed one.
#define SCENE_MAX_BYTES 8192
#define SCENE_ROLE_MAIN     0
#define SCENE_ROLE_KITCHEN  1
#define SCENE_ROLE_BATH     2
#define SCENE_ROLE_WORK     3
static const char *SCENE_ROLE_PATHS[4] = {
  SCENE_PATH, "/spiffs/scene-kitchen.json", "/spiffs/scene-bathroom.json",
  "/spiffs/scene-work.json"};
static uint8_t g_scCurRole  = SCENE_ROLE_MAIN;   // which room he is IN
static uint8_t g_scLoadRole = SCENE_ROLE_MAIN;   // which file sceneLoad() reads
static bool    g_scRoleAvail[4] = {false, false, false, false};
static int16_t g_scWorkMin = 0;                  // "work lasts [N] minutes"; 0 = one visit
// WHICH ACTS EACH ROOM OFFERS - the routing table behind "actions should only happen
// in the room where they are available" (Jon). Filled by sceneRolesScan from each
// role file's anims[].act, so the errand can ask the whole WORLD, not guess by role.
#define SCENE_ACT_EAT    1
#define SCENE_ACT_BATH   2
#define SCENE_ACT_WASH   4
#define SCENE_ACT_TOILET 8
#define SCENE_ACT_WORK   16
#define SCENE_ACT_SLEEP  32
static uint8_t g_scRoleActs[4] = {0, 0, 0, 0};
static int sceneActBit(const char *act) {
  if (!act || !act[0]) return 0;
  if (!strcmp(act, "eat"))    return SCENE_ACT_EAT;
  if (!strcmp(act, "bath"))   return SCENE_ACT_BATH;
  if (!strcmp(act, "wash"))   return SCENE_ACT_WASH;
  if (!strcmp(act, "toilet")) return SCENE_ACT_TOILET;
  if (!strcmp(act, "work"))   return SCENE_ACT_WORK;
  if (!strcmp(act, "sleep"))  return SCENE_ACT_SLEEP;
  return 0;
}
static void sceneRolesScan() {
  for (int r = 0; r < 4; r++) {
    g_scRoleActs[r] = 0;
    FILE *f = fopen(SCENE_ROLE_PATHS[r], "rb");
    g_scRoleAvail[r] = (f != nullptr);
    if (!f) continue;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n <= SCENE_MAX_BYTES) {
      char *buf = (char *)malloc((size_t)n + 1);
      if (buf) {
        size_t got = fread(buf, 1, (size_t)n, f);
        buf[got] = 0;
        cJSON *root = cJSON_Parse(buf);
        if (root) {
          const cJSON *anims = cJSON_GetObjectItem(root, "anims");
          const cJSON *a;
          cJSON_ArrayForEach(a, anims) {
            const cJSON *ac = cJSON_GetObjectItem(a, "act");
            if (cJSON_IsString(ac)) g_scRoleActs[r] |= sceneActBit(ac->valuestring);
          }
          cJSON_Delete(root);
        }
        free(buf);
      }
    }
    fclose(f);
  }
}
#define SCENE_MAX_PROPS   24
#define SCENE_MAX_BLOCKS   8
#define SCENE_MAX_CLOUDS   8
#define SCENE_CLOUD_STYLES 5
#define SCENE_MAX_CATSPOTS 6
#define SCENE_MAX_FLOORPTS 12
#define SCENE_MAX_LOOSE     4
#define SCENE_MAX_PERCH     6

struct SceneProp {
  // 32, not 28, and it must never shrink again: this buffer has to hold ANY name the pak can
  // hold. PakEntry::name is char[32] and pakFind() compares 32 bytes, so a packed name is up to
  // 31 characters plus its NUL. At 28 the strncpy below kept only 27, and every longer name was
  // silently truncated into a string that matches nothing in the index — spriteLoad() failed,
  // drawItemAtXY() returned early, and the prop drew NOTHING while the scene still listed it.
  // Nothing logged, because a missing sprite is a normal, quiet condition everywhere else.
  // This is what hid three of Jon's imported sprites on 6D1C (the woven rug, the shelf placed
  // twice, the framed picture): the builder shortens imported names to the pak's 31-char limit,
  // so every import lands at exactly 31 and every one of them was 4 characters over this cap.
  // The hand-authored names shipped in the pak are all short ("items/table"), which is why the
  // bug stayed invisible until the first child-imported sprite arrived.
  char  name[32];      // a pak name, already resolved: "items/table"
  float x, y;          // x is the sprite's centre, y its TOP - drawItemAt()'s convention
  float sx, sy;        // width and height scale, the builder's k already folded in
  float rot;           // degrees clockwise, as set in the builder
  bool  flip;
  int8_t z;            // drawn low z first; z <= 0 goes BEHIND the pet, z > 0 in front of him
};

static SceneProp g_scProp[SCENE_MAX_PROPS];
static int       g_scPropN = 0;
static Block     g_scBlock[SCENE_MAX_BLOCKS];
static int       g_scBlockN = 0;
static Bounds    g_scBounds = {0, 0, 0, 0};
static bool      g_scHasBounds = false;
static bool      g_scOK = false;          // a usable scene was read
// Bumped every time a scene is successfully read. Anything that CACHES something derived from the
// scene has to watch this, not just its own inputs: buildLightMap() stamped its cache key at boot
// — before SPIFFS is even mounted, so before any scene exists — and its only other trigger was a
// light-scale change, which defaults to 100 and so never fires. The scene's lamps therefore
// arrived ten seconds after the map was built and nothing ever asked again.
static uint16_t  g_scGen = 0;
static char      g_scName[24] = "";
static char      g_scRoom[32] = "";   // "rooms/room-farm", if the scene names one

// ---- WHERE THE VISITING CAT MAY SLEEP ----
// The builder lets a child drop `cat-sleep` marks — on the chair, on a shelf, on the floor by the
// fire — and until now every one of them was thrown away at the PC end as "an instruction, not
// furniture", leaving the device to use catChairSpot()'s compiled-in constant. That constant was
// measured off a room whose chair sat at x=246; move the chair and she sleeps on bare boards
// beside it, which is exactly what 6D1C showed with Jon's chair at x=207.
// This is the scene contract's half of it: the builder says WHERE, and the sim still decides
// WHICH and WHEN, picking one mark per visit. No marks means the compiled-in spot, unchanged.
struct CatSpot { int16_t x, y; };
static CatSpot g_scCat[SCENE_MAX_CATSPOTS];
static int     g_scCatN = 0;

// ---- THE GROUND, AS IT WAS ACTUALLY DRAWN ----
// A child draws the floor as a POLYGON, and in a room seen in perspective that polygon is a
// trapezoid: narrow along the back wall, wide at the front. `bounds` is the inscribed rectangle
// and it is still what picks a target and what everything else has always used — but a rectangle
// cannot express a taper, so at the BACK of Jon's farmhouse it let him stand about 14px past the
// skirting on either side, up on the wall. The polygon travels too, and a chosen point is tested
// against it.
static CatSpot g_scFloor[SCENE_MAX_FLOORPTS];    // same {x,y} shape; not a cat, just a point
static int     g_scFloorN = 0;

// ---- THINGS THAT CAN BE KNOCKED OVER ----
// Jon, twice, ten minutes apart, the second time prefixed "remember this workflow": "the cat
// should walk in around the room for a bit and then knock anything out of the place where it
// sleeps ... it wakes up and leaves then bunbun goes and places the object back where it was.
// the cat can also play with the yarn, but that stays tied to the ground".
// The PC works out which props are loose, which of those ROLL rather than topple, and the
// nearest landing of the right family for each — so the device never needs to know that a jar
// is a jar. `prop` indexes g_scProp; the object's home is simply where that prop was placed.
struct SceneLoose {
  int8_t  prop;          // which prop this is
  int8_t  roller;        // rolls along the floor instead of toppling; a toy, never tidied
  int16_t lx, ly;        // where it lands when knocked (canvas top), or INT16_MIN for nowhere
  char    down[32];      // what it becomes once it is over; empty keeps its own art
};
static SceneLoose g_scLoose[SCENE_MAX_LOOSE];
static int        g_scLooseN = 0;

// ---- THE SURFACES SHE CAN STAND ON ----
// The top of every table, chair, rocker and crib the child placed — computed on the PC from the
// builder's own SEAT_SRC table, because that is source-pixel knowledge about each sprite.
// Shelves are deliberately absent: a shelf is not something a cat springs to from the floor, so
// getting to one means going up by way of something that is. Jon: "the table must be used to get
// to the shelf", and "the cat also gets down from the shelf using the table".
static CatSpot g_scPerch[SCENE_MAX_PERCH];
static int     g_scPerchN = 0;
// THE SCENE'S LAMPS, NAMED RATHER THAN GUESSED.
// This used to match on the name containing "sconce" or "lamp", which made a prop called
// `items/clamp` or `items/lamppost` a light source, and could never see a child's imported
// fixture at all (pak_safe gives an import a `-hash` suffix). The builder decides what is a
// lamp — anything flagged, else the sconces and lamps it finds — and now says so explicitly,
// along with the per-lamp controls it offers: power, size, and a bulb-height override.
struct SceneLamp {
  int8_t  prop;        // which prop is the fixture
  int16_t power;       // brightness, percent; 100 = as the builder ships it
  int16_t size;        // spread, percent
  int16_t bulbY;       // bulb height above the anchor, or INT16_MIN to derive it
  // THE FIXTURE'S ANCHOR, as the builder holds it: canvasBottom - pad*k, which is the point
  // geomFor() hangs the bulb off. The device only receives the canvas TOP, and recovering the
  // anchor from the pak gives the bottom of the INK instead — 9px lower for the sconce, whose
  // authored pad is 9. `pad` is not the trim and cannot be derived here, so the PC sends the
  // answer. INT16_MIN = an older scene.json; fall back to the ink bottom as before.
  int16_t anchorY;
};
static SceneLamp g_scLamp[4];
static int       g_scLampN = 0;


// ---- AND WHERE BUNBUN GOES, AND WHAT HE DOES THERE ----
// The other half of the mark system. A child drops `play` on the rug and he walks over and
// plays there — the same sentence as dropping `cat-sleep` on a table, for the other actor.
// `anim` is already a device animation key; the PC did the mapping from the builder's BUN_POSES.
struct BunMark { int16_t x, y; char anim[16]; };
static BunMark g_scBun[SCENE_MAX_CATSPOTS];
static int     g_scBunN = 0;

// ---- THE ANIMATIONS THE CHILD MADE ----
// This is the port the Scene Assembler was always heading for (Jon: "the real port to the
// device is all that is left"). An animation authored in Animation Creation — frame order,
// held frames, attached props, facing — is COMPOSED on the PC into flat 96px frames, packed
// into the pak under one folder (canim/<name>/0..N-1), and arrives here as a table entry:
// key, folder, frame count, fps, mode. The firmware then plays it exactly like any built-in
// clip; findAnim() consults this table after ANIMS[], so a mark whose key names a custom
// animation simply works, and the pet finally SITS in the chair the child sat him in instead
// of standing beside it (the old CLIP_TO_POSE "sit -> idle" apology).
// Same safety story as everything else in this file: a missing pak folder degrades to the
// idle frame (spriteLoad's own fallback), a malformed entry is skipped, and no entry here
// can touch the pet's saved state.
// 8 -> 10 when the travel kit arrived: a room keeps its 8 authored animations and the
// package's walk_e/walk_w clips ride in the two extra slots without evicting anything.
#define SCENE_MAX_ANIMS 10
struct SceneAnim {
  char    key[16];       // the mark key, e.g. "c_bathing" — fits BunMark::anim exactly
  char    folder[32];    // the pak folder, e.g. "canim/bathing" — frames at <folder>/<i>
  uint8_t frames;        // how many composed frames the pak holds
  float   fps;           // playback rate, hold already folded in by the PC
  uint8_t mode;          // 0 loop, 1 pingpong, 2 once — main.cpp's M_LOOP/M_PINGPONG/M_ONCE
  uint8_t anywhere;      // 1 = plays wherever he happens to stop, not at a mark (the
                         // assembler's "anywhere he walks" rule, kept as a RULE rather than
                         // flattened to one fake mid-floor point)
  float   breathe;       // scale pulse percent (0-15), pinned at the feet - the editor's
                         // breathing is RUNTIME motion and cannot be baked into frames; a
                         // one-frame sleep lives or dies by this ("i dont see the breathing")
  uint8_t bper;          // breathe cycle, in animation frames (editor default 8)
  // The SEMANTIC KIND, from the clip the child built on ("bath","sleep","eat","sit",
  // "hungry","tired","bored","sick","angry","love","idle"). Jon's split: EMOTIONS are
  // states shown wherever he stands; ACTIONS have a place. This is how a button or a mood
  // finds the child's own animation for what is happening.
  char    act[8];
  uint8_t behind;        // 1 = the performance happens BEHIND the thing it uses (the
                         // editor's depth control: bathing IN the tub, not on top of it)
  float   dur;           // performance seconds; 0 = the assembler default (2 loops + rest)
  char    txt[44];       // the ticker line for this animation; empty = the act's stock line
};
static SceneAnim g_scAnim[SCENE_MAX_ANIMS];
static int       g_scAnimN = 0;

// ---- WHERE HE MAY NOT LOITER ----
// The assembler's keep-out, with the assembler's MEANING (Jon: "i want the logic of the
// assembler to stand"). A keep-out was arriving as a solid block — a wall — but in the
// builder it has always meant "walk through freely, never stop here" (see the builder's
// canStop/zoneAt pair). So the polygons now travel as themselves and only DESTINATION
// PICKING consults them; routing and walking never do.
#define SCENE_MAX_NOGO     4
#define SCENE_NOGO_PTS     12
static CatSpot g_scNoGo[SCENE_MAX_NOGO][SCENE_NOGO_PTS];
static int     g_scNoGoPts[SCENE_MAX_NOGO];
static int     g_scNoGoN = 0;

// ---- the room's WEATHER AND LIGHT CHARACTER ----
// NOT its clock. The builder has a time-of-day slider and a LIGHTING: OFF button; both are PREVIEW
// controls and neither is carried. The device's light follows the real clock - a shelf ornament
// that is bright at breakfast and dim at bedtime is the whole point of the product, and a scene
// that pinned the hour would kill it on every unit that ever loaded that scene.
// What travels is what KIND of sky this room has: how many clouds, how big, how fast, how thick,
// which shape, and how wet the weather is. The device still decides WHEN, from its own clock and
// its own dice. A scene may say "this is a rainy room"; it may never say "it is raining".
// Percentages, so absent == 100 == exactly what the firmware ships. Every field is optional and
// every default reproduces today's behaviour bit for bit.
struct SceneEnv {
  int16_t cloudN;    // 0..SCENE_MAX_CLOUDS, 0 = no clouds;  -1 = the scene said nothing
  int16_t cloudW;    // size,    percent  30..300
  int16_t cloudS;    // drift,   percent  10..300
  int16_t cloudA;    // opacity, percent  10..100  (60 == the shipped 3/5 blend)
  int16_t cloudT;    // style index, 0 = the shipped table
  int16_t rainW;     // 0 never, 1 shipped cadence, 2 often, 3 very often;  -1 = said nothing
  int16_t lightS;    // light throw, percent  30..220
  // The authored weather box (Jon 8/18: "you draw a box where they occur").  Device px,
  // 240x180 space; skyW <= 0 = not authored, keep the window-detection heuristics.
  int16_t skyX, skyY, skyW, skyH;
  int16_t stars;     // 0..40 twinkling night stars inside the sky shape; 0 = none
};
// the scene's travel-size dial: the species pak is baked at a fixed 0.5 of the source art
// (quality base), and walking is scaled by (ts / 0.5) at draw time - so the assembler's
// master dial reaches the device without rebaking a single frame
static float g_scTravel = 0;         // 0 = the scene said nothing
static const SceneEnv SCENE_ENV_DEFAULT = { -1, 100, 100, 60, 0, -1, 100, 0, 0, 0, 0, 0 };
static SceneEnv g_scEnv     = SCENE_ENV_DEFAULT;
// the sky POLYGON ("they need to be polygons"): weather clips to the drawn shape;
// the bbox in g_scEnv.sky* only bounds cloud drift and the row gate
static int16_t g_scSkyPts[12][2];
static uint8_t g_scSkyN = 0;
// celestial bindings (ENVIRONMENT-SPEC layer 2): a pak sprite that follows the clock
// across the sky shape. t: 1 = the day (sun), 2 = the night (moon).
struct SceneCel { char s[32]; uint8_t t; };
static SceneCel g_scCel[2];
static uint8_t g_scCelN = 0;
static bool     g_scHasEnv  = false;

static bool sceneLoad();
static int  g_scTries = 0;
static uint32_t g_scNextTry = 0;

// Call before reading anything. Keeps looking every 10s until it finds one - it does NOT give up.
// The first version stopped after three tries in the first 15 seconds, on the theory that a unit
// with no scene should not keep touching the filesystem. But SPIFFS is mounted by the HOST's
// spiffs_storage component and bunbun's task can start before that mount lands, so on a real
// device all three tries fell before the mount and the scene stayed invisible until the next
// reboot - which is exactly what happened on 6D1C. A failed fopen costs nothing; keep looking.
static void sceneEnsure() {
  if (g_scOK) return;
  uint32_t now = millis();
  if (g_scNextTry && now < g_scNextTry) return;
  g_scNextTry = now + 10000;
  g_scTries++;
  sceneRolesScan();
  if (sceneLoad())
    Serial.printf("scene: loaded \"%s\" - %d prop(s), %d block(s), %d anim(s), bounds %s, "
                  "sky %s (try %d)\n",
                  g_scName[0] ? g_scName : "(unnamed)", g_scPropN, g_scBlockN, g_scAnimN,
                  g_scHasBounds ? "yes" : "no", g_scHasEnv ? "set" : "default", g_scTries);
  else if (g_scTries == 1 || g_scTries == 6)
    Serial.printf("scene: nothing at %s yet (try %d)\n", SCENE_PATH, g_scTries);
}

static bool sceneActive() { sceneEnsure(); return g_scOK; }
static const char *sceneName() { return g_scName; }

// One number, clamped, with "the scene did not mention it" as a distinct answer. Rubbish in the
// file must never be able to produce a sky nobody can see through.
static int sceneEnvInt(const cJSON *o, const char *k, int lo, int hi, int dflt) {
  const cJSON *v = cJSON_GetObjectItem(o, k);
  if (!cJSON_IsNumber(v)) return dflt;
  int n = v->valueint;
  if (n < lo) n = lo;
  if (n > hi) n = hi;
  return n;
}

// Reads the file if there is one. Returns true only when the result is usable; on any doubt it
// leaves everything cleared and the caller keeps the compiled-in room.
static bool sceneLoad() {
  bcMark(17);        // BC_SCENELOAD — the malloc + cJSON parse, which runs in the render path
  g_scOK = false; g_scPropN = 0; g_scBlockN = 0; g_scHasBounds = false; g_scName[0] = 0;
  g_scEnv = SCENE_ENV_DEFAULT; g_scHasEnv = false; g_scCatN = 0; g_scFloorN = 0;
  g_scSkyN = 0; g_scCelN = 0;
  g_scRoom[0] = 0;      // the one field this used to forget, leaving a stale room across retries
  g_scLooseN = 0; g_scPerchN = 0; g_scBunN = 0; g_scLampN = 0; g_scAnimN = 0; g_scNoGoN = 0;
  g_scTravel = 0;

  FILE *f = fopen(SCENE_ROLE_PATHS[g_scLoadRole], "rb");
  if (!f) return false;                       // no scene file: the normal case, not an error
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  if (len <= 1 || len > SCENE_MAX_BYTES) { fclose(f); return false; }
  char *buf = (char *)malloc(len + 1);
  if (!buf) { fclose(f); return false; }
  size_t got = fread(buf, 1, len, f);
  fclose(f);
  buf[got] = 0;

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!root) return false;

  {
    const cJSON *tv = cJSON_GetObjectItem(root, "ts");
    g_scTravel = cJSON_IsNumber(tv) ? (float)tv->valuedouble : 0;
    if (g_scTravel < 0.1f || g_scTravel > 1.5f) g_scTravel = 0;
  }
  const cJSON *nm = cJSON_GetObjectItem(root, "name");
  if (cJSON_IsString(nm)) { strncpy(g_scName, nm->valuestring, sizeof(g_scName) - 1); }
  const cJSON *rm = cJSON_GetObjectItem(root, "room");
  if (cJSON_IsString(rm)) { strncpy(g_scRoom, rm->valuestring, sizeof(g_scRoom) - 1); }
  {
    const cJSON *wk = cJSON_GetObjectItem(root, "wk");
    // no wk in the scene = ONE VISIT: he goes, does the job once, stays his 10s
    // minimum, and walks home (Jon: "it should be at least 10s and then return").
    // A number here is the opt-in to a real timed session.
    g_scWorkMin = cJSON_IsNumber(wk)
                      ? (int16_t)constrain((int)cJSON_GetNumberValue(wk), 0, 240) : 0;
  }

  // the walkable rectangle
  const cJSON *b = cJSON_GetObjectItem(root, "bounds");
  if (cJSON_IsObject(b)) {
    const cJSON *x0 = cJSON_GetObjectItem(b, "x0"), *x1 = cJSON_GetObjectItem(b, "x1");
    const cJSON *y0 = cJSON_GetObjectItem(b, "y0"), *y1 = cJSON_GetObjectItem(b, "y1");
    if (cJSON_IsNumber(x0) && cJSON_IsNumber(x1) && cJSON_IsNumber(y0) && cJSON_IsNumber(y1)) {
      Bounds t = {x0->valueint, x1->valueint, y0->valueint, y1->valueint};
      // a nonsense rectangle is worse than no rectangle: he would have nowhere to stand
      if (t.x1 - t.x0 >= 40 && t.y1 - t.y0 >= 8 &&
          t.x0 >= 0 && t.x1 <= 320 && t.y0 >= 0 && t.y1 <= 240) {
        g_scBounds = t; g_scHasBounds = true;
      }
    }
  }

  // scenery footprints his feet must stay in front of
  const cJSON *bl = cJSON_GetObjectItem(root, "blocks");
  if (cJSON_IsArray(bl)) {
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, bl) {
      if (g_scBlockN >= SCENE_MAX_BLOCKS) break;
      const cJSON *x0 = cJSON_GetObjectItem(it, "x0"), *x1 = cJSON_GetObjectItem(it, "x1"),
                  *yf = cJSON_GetObjectItem(it, "yFront");
      if (!cJSON_IsNumber(x0) || !cJSON_IsNumber(x1) || !cJSON_IsNumber(yf)) continue;
      if (x1->valueint <= x0->valueint) continue;
      Block t = {x0->valueint, x1->valueint, yf->valueint};
      g_scBlock[g_scBlockN++] = t;
    }
  }

  // the furniture itself
  const cJSON *pr = cJSON_GetObjectItem(root, "props");
  if (cJSON_IsArray(pr)) {
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, pr) {
      if (g_scPropN >= SCENE_MAX_PROPS) break;
      const cJSON *n = cJSON_GetObjectItem(it, "n");
      const cJSON *x = cJSON_GetObjectItem(it, "x"), *y = cJSON_GetObjectItem(it, "y");
      const cJSON *s = cJSON_GetObjectItem(it, "s");
      if (!cJSON_IsString(n) || !cJSON_IsNumber(x) || !cJSON_IsNumber(y)) continue;
      SceneProp p;
      memset(&p, 0, sizeof(p));
      strncpy(p.name, n->valuestring, sizeof(p.name) - 1);
      p.x = (float)x->valuedouble;
      p.y = (float)y->valuedouble;
      p.sx = cJSON_IsNumber(s) ? (float)s->valuedouble : 1.0f;
      const cJSON *sv = cJSON_GetObjectItem(it, "sy");
      p.sy = cJSON_IsNumber(sv) ? (float)sv->valuedouble : p.sx;   // square unless told otherwise
      if (p.sx <= 0.01f || p.sx > 8.0f) p.sx = 1.0f;
      if (p.sy <= 0.01f || p.sy > 8.0f) p.sy = p.sx;
      const cJSON *fl = cJSON_GetObjectItem(it, "f");
      p.flip = cJSON_IsTrue(fl);
      const cJSON *rt = cJSON_GetObjectItem(it, "r");
      p.rot = cJSON_IsNumber(rt) ? (float)rt->valuedouble : 0.0f;
      const cJSON *z = cJSON_GetObjectItem(it, "z");
      // CLAMP to the passes sceneDrawProps() actually paints (-2..2). The builder's layer
      // buttons are bare `z++` / `z--` with no limit, so three clicks of "bring forward" gives
      // z=3 — which converts, uploads, is accepted, and is counted in the boot log's prop
      // total, and then matches no pass and draws NOTHING. That is the silent invisible prop
      // this whole file exists to prevent; a child would just see the thing missing.
      int zv = cJSON_IsNumber(z) ? z->valueint : 0;
      if (zv > 2) zv = 2;
      if (zv < -2) zv = -2;
      p.z = (int8_t)zv;
      g_scProp[g_scPropN++] = p;
    }
  }

  // the ground, as a polygon
  const cJSON *fl = cJSON_GetObjectItem(root, "floor");
  if (cJSON_IsArray(fl)) {
    cJSON *pt = NULL;
    cJSON_ArrayForEach(pt, fl) {
      if (g_scFloorN >= SCENE_MAX_FLOORPTS) break;
      if (!cJSON_IsArray(pt) || cJSON_GetArraySize(pt) < 2) continue;
      const cJSON *px = cJSON_GetArrayItem(pt, 0), *py = cJSON_GetArrayItem(pt, 1);
      if (!cJSON_IsNumber(px) || !cJSON_IsNumber(py)) continue;
      // CLAMP wide, do not reject. The point of this is only to stop a silly number wrapping
      // into a negative one on the way into int16_t; it is NOT a canvas test.
      // A floor is SUPPOSED to run past the edges of the room — that is where the doorway is,
      // and SCENE_OFFSTAGE exists for exactly that. Jon's farmhouse floor has a corner at
      // (322,239), and an earlier version of this check that demanded 0..320 threw his whole
      // polygon away, which put the floor back to the rectangle and silently undid the feature.
      // Measured: 18 of 45 sampled positions were off the drawn floor with that check in place.
      int fx = px->valueint, fy = py->valueint;
      if (fx < -1000) fx = -1000; else if (fx > 1000) fx = 1000;
      if (fy < -1000) fy = -1000; else if (fy > 1000) fy = 1000;
      CatSpot p = {(int16_t)fx, (int16_t)fy};
      g_scFloor[g_scFloorN++] = p;
    }
    // Fewer than three points is not a shape. Half a polygon would be worse than none: it would
    // reject most of the room and leave him pinned in a corner.
    if (g_scFloorN < 3) g_scFloorN = 0;
  }

  // where bunbun goes, and what he does when he gets there
  const cJSON *bm = cJSON_GetObjectItem(root, "bun");
  if (cJSON_IsArray(bm)) {
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, bm) {
      if (g_scBunN >= SCENE_MAX_CATSPOTS) break;
      const cJSON *x = cJSON_GetObjectItem(it, "x"), *y = cJSON_GetObjectItem(it, "y");
      const cJSON *a = cJSON_GetObjectItem(it, "a");
      if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) || !cJSON_IsString(a)) continue;
      if (x->valueint < 0 || x->valueint > 320 || y->valueint < 0 || y->valueint > 240) continue;
      BunMark m;
      memset(&m, 0, sizeof(m));
      m.x = (int16_t)x->valueint; m.y = (int16_t)y->valueint;
      strncpy(m.anim, a->valuestring, sizeof(m.anim) - 1);
      g_scBun[g_scBunN++] = m;
    }
  }

  // the animations the child made - key, pak folder, frame count, fps, mode
  const cJSON *cn = cJSON_GetObjectItem(root, "anims");
  if (cJSON_IsArray(cn)) {
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, cn) {
      if (g_scAnimN >= SCENE_MAX_ANIMS) break;
      const cJSON *k = cJSON_GetObjectItem(it, "k"), *fo = cJSON_GetObjectItem(it, "f");
      const cJSON *n = cJSON_GetObjectItem(it, "n"), *fp = cJSON_GetObjectItem(it, "fps");
      if (!cJSON_IsString(k) || !k->valuestring[0]) continue;
      if (!cJSON_IsString(fo) || !fo->valuestring[0]) continue;
      if (!cJSON_IsNumber(n)) continue;
      // A key longer than the buffer would be silently truncated into a string the marks
      // can't match — the SceneProp::name lesson, applied BEFORE it bites this time.
      if (strlen(k->valuestring) >= sizeof(g_scAnim[0].key)) continue;
      if (strlen(fo->valuestring) >= sizeof(g_scAnim[0].folder)) continue;
      int nf = n->valueint;
      if (nf < 1 || nf > 64) continue;
      SceneAnim sa;
      memset(&sa, 0, sizeof(sa));
      strncpy(sa.key, k->valuestring, sizeof(sa.key) - 1);
      strncpy(sa.folder, fo->valuestring, sizeof(sa.folder) - 1);
      sa.frames = (uint8_t)nf;
      float f = cJSON_IsNumber(fp) ? (float)fp->valuedouble : 7.0f;
      sa.fps = (f < 0.5f) ? 0.5f : (f > 24.0f) ? 24.0f : f;
      sa.mode = (uint8_t)sceneEnvInt(it, "m", 0, 2, 0);
      sa.anywhere = (uint8_t)sceneEnvInt(it, "aw", 0, 1, 0);
      const cJSON *br = cJSON_GetObjectItem(it, "br");
      float b2 = cJSON_IsNumber(br) ? (float)br->valuedouble : 0.0f;
      sa.breathe = (b2 < 0) ? 0 : (b2 > 15) ? 15 : b2;
      sa.bper = (uint8_t)sceneEnvInt(it, "bp", 2, 24, 8);
      const cJSON *ac = cJSON_GetObjectItem(it, "act");
      if (cJSON_IsString(ac)) strncpy(sa.act, ac->valuestring, sizeof(sa.act) - 1);
      sa.behind = (uint8_t)sceneEnvInt(it, "dp", 0, 1, 0);
      const cJSON *du = cJSON_GetObjectItem(it, "dur");
      float dv = cJSON_IsNumber(du) ? (float)du->valuedouble : 0.0f;
      sa.dur = (dv < 0) ? 0 : (dv > 300) ? 300 : dv;
      const cJSON *tx = cJSON_GetObjectItem(it, "txt");
      if (cJSON_IsString(tx)) strncpy(sa.txt, tx->valuestring, sizeof(sa.txt) - 1);
      g_scAnim[g_scAnimN++] = sa;
    }
  }

  // where he may not loiter - polygons, with the builder's own meaning
  const cJSON *ng = cJSON_GetObjectItem(root, "nogo");
  if (cJSON_IsArray(ng)) {
    cJSON *poly = NULL;
    cJSON_ArrayForEach(poly, ng) {
      if (g_scNoGoN >= SCENE_MAX_NOGO) break;
      if (!cJSON_IsArray(poly)) continue;
      int n = 0;
      cJSON *pt = NULL;
      cJSON_ArrayForEach(pt, poly) {
        if (n >= SCENE_NOGO_PTS) break;
        if (!cJSON_IsArray(pt) || cJSON_GetArraySize(pt) < 2) continue;
        const cJSON *px = cJSON_GetArrayItem(pt, 0), *py = cJSON_GetArrayItem(pt, 1);
        if (!cJSON_IsNumber(px) || !cJSON_IsNumber(py)) continue;
        int fx = px->valueint, fy = py->valueint;
        if (fx < -1000) fx = -1000; else if (fx > 1000) fx = 1000;
        if (fy < -1000) fy = -1000; else if (fy > 1000) fy = 1000;
        g_scNoGo[g_scNoGoN][n].x = (int16_t)fx;
        g_scNoGo[g_scNoGoN][n].y = (int16_t)fy;
        n++;
      }
      if (n >= 3) { g_scNoGoPts[g_scNoGoN] = n; g_scNoGoN++; }
    }
  }

  // which props are lamps, and how each is tuned
  const cJSON *lp = cJSON_GetObjectItem(root, "lamp");
  if (cJSON_IsArray(lp)) {
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, lp) {
      if (g_scLampN >= (int)(sizeof(g_scLamp) / sizeof(g_scLamp[0]))) break;
      const cJSON *pi = cJSON_GetObjectItem(it, "i");
      if (!cJSON_IsNumber(pi)) continue;
      if (pi->valueint < 0 || pi->valueint >= g_scPropN) continue;   // props are parsed first
      SceneLamp L;
      L.prop = (int8_t)pi->valueint;
      L.power = (int16_t)sceneEnvInt(it, "pw", 10, 400, 100);
      L.size  = (int16_t)sceneEnvInt(it, "sz", 10, 400, 100);
      const cJSON *by = cJSON_GetObjectItem(it, "by");
      L.bulbY = cJSON_IsNumber(by) ? (int16_t)by->valueint : INT16_MIN;
      const cJSON *ay = cJSON_GetObjectItem(it, "ay");
      L.anchorY = cJSON_IsNumber(ay) ? (int16_t)lroundf((float)ay->valuedouble) : INT16_MIN;
      g_scLamp[g_scLampN++] = L;
    }
  }

  // the surfaces she can stand on
  const cJSON *pc = cJSON_GetObjectItem(root, "perch");
  if (cJSON_IsArray(pc)) {
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, pc) {
      if (g_scPerchN >= SCENE_MAX_PERCH) break;
      const cJSON *x = cJSON_GetObjectItem(it, "x"), *y = cJSON_GetObjectItem(it, "y");
      if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) continue;
      if (x->valueint < -40 || x->valueint > 360 || y->valueint < 0 || y->valueint > 240) continue;
      CatSpot p = {(int16_t)x->valueint, (int16_t)y->valueint};
      g_scPerch[g_scPerchN++] = p;
    }
  }

  // the knockable things
  const cJSON *lo = cJSON_GetObjectItem(root, "loose");
  if (cJSON_IsArray(lo)) {
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, lo) {
      if (g_scLooseN >= SCENE_MAX_LOOSE) break;
      const cJSON *pi = cJSON_GetObjectItem(it, "i");
      if (!cJSON_IsNumber(pi)) continue;
      if (pi->valueint < 0 || pi->valueint >= g_scPropN) continue;   // props are parsed first
      SceneLoose L;
      memset(&L, 0, sizeof(L));
      L.prop = (int8_t)pi->valueint;
      const cJSON *rl = cJSON_GetObjectItem(it, "roll");
      L.roller = cJSON_IsNumber(rl) ? (rl->valueint ? 1 : 0) : 0;
      const cJSON *lx = cJSON_GetObjectItem(it, "lx"), *ly = cJSON_GetObjectItem(it, "ly");
      if (cJSON_IsNumber(lx) && cJSON_IsNumber(ly)) {
        L.lx = (int16_t)lx->valuedouble;
        L.ly = (int16_t)ly->valuedouble;
      } else {
        L.lx = L.ly = INT16_MIN;                 // no landing placed: it scatters
      }
      const cJSON *dn = cJSON_GetObjectItem(it, "down");
      if (cJSON_IsString(dn)) strncpy(L.down, dn->valuestring, sizeof(L.down) - 1);
      g_scLoose[g_scLooseN++] = L;
    }
  }

  // where the cat may sleep
  const cJSON *ct = cJSON_GetObjectItem(root, "cat");
  if (cJSON_IsObject(ct)) {
    const cJSON *sl = cJSON_GetObjectItem(ct, "sleep");
    if (cJSON_IsArray(sl)) {
      cJSON *it = NULL;
      cJSON_ArrayForEach(it, sl) {
        if (g_scCatN >= SCENE_MAX_CATSPOTS) break;
        const cJSON *x = cJSON_GetObjectItem(it, "x"), *y = cJSON_GetObjectItem(it, "y");
        if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) continue;
        // A mark off the screen would send her walking out of the room to lie down in the wall.
        if (x->valueint < 0 || x->valueint > 320 || y->valueint < 0 || y->valueint > 240) continue;
        CatSpot s = {(int16_t)x->valueint, (int16_t)y->valueint};
        g_scCat[g_scCatN++] = s;
      }
    }
  }

  // the sky's character
  const cJSON *ev = cJSON_GetObjectItem(root, "env");
  if (cJSON_IsObject(ev)) {
    g_scEnv.cloudN = sceneEnvInt(ev, "cn", 0, SCENE_MAX_CLOUDS,        -1);
    g_scEnv.cloudW = sceneEnvInt(ev, "cw", 30, 300,                   100);
    g_scEnv.cloudS = sceneEnvInt(ev, "cs", 10, 300,                   100);
    g_scEnv.cloudA = sceneEnvInt(ev, "ca", 10, 100,                    60);
    g_scEnv.cloudT = sceneEnvInt(ev, "ct",  0, SCENE_CLOUD_STYLES - 1,  0);
    g_scEnv.rainW  = sceneEnvInt(ev, "rw",  0, 3,                      -1);
    g_scEnv.lightS = sceneEnvInt(ev, "ls", 30, 220,                   100);
    g_scEnv.stars = sceneEnvInt(ev, "st", 0, 40, 0);
    const cJSON *cel = cJSON_GetObjectItem(ev, "cel");
    if (cJSON_IsArray(cel)) {
      int nc = cJSON_GetArraySize(cel);
      if (nc > 2) nc = 2;
      g_scCelN = 0;
      for (int i = 0; i < nc; i++) {
        const cJSON *it = cJSON_GetArrayItem(cel, i);
        const cJSON *sn = cJSON_GetObjectItem(it, "s");
        const cJSON *tt = cJSON_GetObjectItem(it, "t");
        if (!cJSON_IsString(sn) || !sn->valuestring[0]) continue;
        strncpy(g_scCel[g_scCelN].s, sn->valuestring, 31);
        g_scCel[g_scCelN].s[31] = 0;
        g_scCel[g_scCelN].t = (uint8_t)((int)cJSON_GetNumberValue(tt) == 2 ? 2 : 1);
        g_scCelN++;
      }
    }
    const cJSON *sp = cJSON_GetObjectItem(ev, "sp");   // the drawn sky POLYGON, flat x,y pairs
    if (cJSON_IsArray(sp)) {
      int np = cJSON_GetArraySize(sp) / 2;
      if (np > 12) np = 12;
      int minx = 240, miny = 180, maxx = 0, maxy = 0;
      g_scSkyN = 0;
      for (int i = 0; i < np; i++) {
        int px = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(sp, i * 2));
        int py = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(sp, i * 2 + 1));
        if (px < 0) px = 0; if (px > 239) px = 239;
        if (py < 0) py = 0; if (py > 179) py = 179;
        g_scSkyPts[g_scSkyN][0] = (int16_t)px; g_scSkyPts[g_scSkyN][1] = (int16_t)py;
        g_scSkyN++;
        if (px < minx) minx = px; if (px > maxx) maxx = px;
        if (py < miny) miny = py; if (py > maxy) maxy = py;
      }
      if (g_scSkyN >= 3 && maxx - minx >= 8 && maxy - miny >= 8) {
        g_scEnv.skyX = (int16_t)minx; g_scEnv.skyY = (int16_t)miny;
        g_scEnv.skyW = (int16_t)(maxx - minx + 1); g_scEnv.skyH = (int16_t)(maxy - miny + 1);
      } else g_scSkyN = 0;
    }
    const cJSON *sb = cJSON_GetObjectItem(ev, "sb");   // [x, y, w, h] - the drawn sky box
    if (cJSON_IsArray(sb) && cJSON_GetArraySize(sb) == 4) {
      int bx = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(sb, 0));
      int by = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(sb, 1));
      int bw = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(sb, 2));
      int bh = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(sb, 3));
      if (bx < 0) bx = 0; if (by < 0) by = 0;
      if (bx > 239) bx = 239; if (by > 179) by = 179;
      if (bx + bw > 240) bw = 240 - bx;
      if (by + bh > 180) bh = 180 - by;
      if (bw >= 8 && bh >= 8) {
        g_scEnv.skyX = (int16_t)bx; g_scEnv.skyY = (int16_t)by;
        g_scEnv.skyW = (int16_t)bw; g_scEnv.skyH = (int16_t)bh;
      }
    }
    g_scHasEnv = true;
  }

  cJSON_Delete(root);

  // A scene that says nothing is not a scene. Only claim to be active if it actually describes
  // something, otherwise the room silently loses its furniture for no reason.
  // An env-only scene - "the same room, but cloudier" - is a legitimate scene.
  // The floor and the room count too. A child who picked a room and drew the ground he may walk
  // on, without yet placing a prop, has made a scene — and judging it "not a scene" also meant
  // sceneEnsure() never stopped retrying, so it re-read and re-parsed the file every 10s forever.
  g_scOK = (g_scPropN > 0 || g_scHasBounds || g_scBlockN > 0 || g_scHasEnv || g_scCatN > 0 ||
            g_scFloorN > 0 || g_scRoom[0] || g_scBunN > 0 || g_scAnimN > 0);
  if (g_scOK) g_scGen++;
  return g_scOK;
}

// May he STOP here? False only inside a keep-out polygon. The same even-odd test as the
// floor below. Walking and routing must never call this — a keep-out is not a wall.
static bool sceneCanLoiter(int x, int y) {
  for (int p = 0; p < g_scNoGoN; p++) {
    bool in = false;
    const int n = g_scNoGoPts[p];
    for (int i = 0, j = n - 1; i < n; j = i++) {
      const int yi = g_scNoGo[p][i].y, yj = g_scNoGo[p][j].y;
      if ((yi > y) == (yj > y)) continue;
      const int xi = g_scNoGo[p][i].x, xj = g_scNoGo[p][j].x;
      if ((float)x < (float)(xj - xi) * (float)(y - yi) / (float)(yj - yi) + (float)xi) in = !in;
    }
    if (in) return false;
  }
  return true;
}

// The child's animation for a semantic act, and the mark where it happens. moodAnim() asks
// for emotions (played in place); the FEED/BATH/SLEEP buttons and the needs system ask for
// actions (walked to). NULL/-1 falls back to the built-in art, exactly as before.
static const char *sceneActAnim(const char *act) {
  if (!act || !act[0]) return NULL;
  // RANDOM among the matches, not first-wins (council 8/17, filing 3): the assembler rolls
  // dice between same-act animations, so the device rolls the same dice - a declared
  // divergence closes or the golden rule is a slogan.
  const char *cand[SCENE_MAX_ANIMS];
  int n = 0;
  for (int i = 0; i < g_scAnimN; i++)
    if (!strcmp(g_scAnim[i].act, act)) cand[n++] = g_scAnim[i].key;
  if (!n) return NULL;
  return cand[esp_random() % (uint32_t)n];
}
static int sceneActMark(const char *act) {
  if (!act || !act[0]) return -1;
  int cand[SCENE_MAX_CATSPOTS];
  int n = 0;
  for (int m = 0; m < g_scBunN; m++)
    for (int i = 0; i < g_scAnimN; i++)
      if (!strcmp(g_scAnim[i].key, g_scBun[m].anim) && !strcmp(g_scAnim[i].act, act)) {
        cand[n++] = m;
        break;
      }
  if (!n) return -1;
  return cand[esp_random() % (uint32_t)n];
}

// A random "anywhere" animation the scene brought, or NULL. The assembler's rule kept AS a
// rule: these play wherever he happens to settle, never at a pinned point.
static const char *sceneAnywhereAnim(void) {
  int cand[SCENE_MAX_ANIMS], n = 0;
  for (int i = 0; i < g_scAnimN; i++)
    if (g_scAnim[i].anywhere) cand[n++] = i;
  if (!n) return NULL;
  return g_scAnim[cand[esp_random() % (uint32_t)n]].key;
}

// Is this point on the floor the child drew? True when the scene gave no polygon, so a scene
// without one behaves exactly as it did before: the rectangle is the only word on it.
// Even-odd crossing test, which is right for the simple non-self-intersecting outlines the
// builder produces.
static bool sceneFloorHas(int x, int y) {
  if (!g_scOK || g_scFloorN < 3) return true;
  bool in = false;
  for (int i = 0, j = g_scFloorN - 1; i < g_scFloorN; j = i++) {
    const int yi = g_scFloor[i].y, yj = g_scFloor[j].y;
    if ((yi > y) == (yj > y)) continue;
    const int xi = g_scFloor[i].x, xj = g_scFloor[j].x;
    if ((float)x < (float)(xj - xi) * (float)(y - yi) / (float)(yj - yi) + (float)xi) in = !in;
  }
  return in;
}

// How far the floor reaches left and right along one row. Rejection sampling gives up after a
// dozen tries, and whatever it is holding then still has to be somewhere he can stand — so the
// caller clamps into this instead of accepting a point out on the wall.
static bool sceneFloorSpan(int y, int *x0, int *x1) {
  if (!g_scOK || g_scFloorN < 3) return false;
  int lo = 32767, hi = -32768;
  for (int i = 0, j = g_scFloorN - 1; i < g_scFloorN; j = i++) {
    const int yi = g_scFloor[i].y, yj = g_scFloor[j].y;
    if ((yi > y) == (yj > y)) continue;
    const int xi = g_scFloor[i].x, xj = g_scFloor[j].x;
    const int xc = xi + (int)((float)(xj - xi) * (float)(y - yi) / (float)(yj - yi));
    if (xc < lo) lo = xc;
    if (xc > hi) hi = xc;
  }
  if (hi - lo < 8) return false;            // a sliver is not somewhere to stand
  *x0 = lo; *x1 = hi;
  return true;
}

// The floor's VERTICAL lane range at one x — the builder's floorSpanAt(). This, not the
// horizontal span, is the shape grounding needs: on a trapezoid the lanes he may stand in move
// as he crosses the room, so the clamp has to be computed at his own x.
// False when there is no polygon, or he is in the doorway, or that column has no floor at all —
// in every one of those cases the caller leaves him exactly where he is.
#define SCENE_OFFSTAGE 10                // beyond the room edges is the doorway, not the floor
// CACHED on x, because keepLegal() asks this every frame and the answer only changes when he
// moves to a new column. Uncached it is 120 rows x N edges of point-in-polygon per frame, which
// is real work on a device already running its render at about 10Hz.
static int g_laneX = -1, g_laneTop = 0, g_laneBot = 0;
static bool g_laneOK = false;
static uint16_t g_laneGen = 0xffff;

static bool sceneFloorLane(int x, int *top, int *bot) {
  if (!g_scOK || g_scFloorN < 3) return false;
  if (x < SCENE_OFFSTAGE || x > 320 - SCENE_OFFSTAGE) return false;
  if (x == g_laneX && g_laneGen == g_scGen) {          // same column, same scene: same answer
    if (!g_laneOK) return false;
    *top = g_laneTop; *bot = g_laneBot;
    return true;
  }
  int t = 32767, b = -32768;
  for (int y = 120; y <= 239; y++)
    if (sceneFloorHas(x, y)) { if (y < t) t = y; if (y > b) b = y; }
  g_laneX = x; g_laneGen = g_scGen; g_laneOK = (b >= t);
  g_laneTop = t; g_laneBot = b;
  if (!g_laneOK) return false;
  *top = t; *bot = b;
  return true;
}

// Put a chosen point onto the drawn floor: clamp it into that row's span, with a few pixels of
// margin so his feet do not sit exactly on the edge.
static void sceneFloorClamp(int *x, int y) {
  int fx0, fx1;
  if (!sceneFloorSpan(y, &fx0, &fx1)) return;
  if (*x < fx0 + 4) *x = fx0 + 4;
  if (*x > fx1 - 4) *x = fx1 - 4;
}

// One of the scene's sleeping marks, chosen fresh per visit, or false to keep the compiled-in
// spot. The choice is the SIM's, not the builder's: a child marks every place the cat is allowed
// to settle and which one she picks today stays a small surprise.
static int g_scCatLast = -1;
static bool sceneCatSpot(float *x, float *y) {
  sceneEnsure();
  if (!g_scOK || g_scCatN == 0) return false;
  int pick = (int)(esp_random() % (uint32_t)g_scCatN);
  // NOT THE SAME ONE AGAIN, usually. The builder keeps c.lastMark and takes a different mark
  // 60% of the time. Without it a uniform roll can sit on one spot for visit after visit — and
  // with four marks and one jar, "she clears her spot" is a beat you might never see happen.
  if (g_scCatN > 1 && pick == g_scCatLast && (esp_random() % 100) < 60)
    pick = (pick + 1 + (int)(esp_random() % (uint32_t)(g_scCatN - 1))) % g_scCatN;
  g_scCatLast = pick;
  *x = g_scCat[pick].x; *y = g_scCat[pick].y;
  return true;
}

// ---- what main.cpp asks ----

// The room painting this scene wants, or nullptr to keep the one the pet's phase would choose.
// Checked against the pak by the caller: a scene naming a room that was never packed must not
// leave the room blank.
static bool sceneLoadRole(uint8_t r) {
  if (r > 3 || !g_scRoleAvail[r]) return false;
  uint8_t prev = g_scLoadRole;
  g_scLoadRole = r;
  if (sceneLoad()) { g_scCurRole = r; sceneRolesScan(); return true; }
  g_scLoadRole = prev;
  return false;
}
static int sceneWorkMin() { return g_scWorkMin; }

static const char *sceneRoom() {
  return (g_scOK && g_scRoom[0]) ? g_scRoom : nullptr;
}

// The room's weather character, or nullptr to keep the firmware's own. NEVER a time of day.
static const SceneEnv *sceneEnv() {
  sceneEnsure();
  return (g_scOK && g_scHasEnv) ? &g_scEnv : nullptr;
}

// Does the scene place this piece of furniture itself? The room draws a few things on its own -
// the cat clock, the wall sconce, the radio - and a scene that includes them would otherwise get
// two of each, which is what 6D1C showed: two cat clocks, one from each source.
static bool sceneHas(const char *pakName) {
  if (!g_scOK) return false;
  for (int i = 0; i < g_scPropN; i++)
    if (!strncmp(g_scProp[i].name, pakName, sizeof(g_scProp[i].name))) return true;
  return false;
}

// The walkable rectangle, if the scene gave one. Callers keep their own default otherwise.
static bool sceneBounds(Bounds *out) {
  if (!g_scOK || !g_scHasBounds) return false;
  *out = g_scBounds; return true;
}

// One of the bunbun marks, chosen at random, or false if the child placed none.
static bool sceneBunMark(int *x, int *y, const char **anim) {
  sceneEnsure();
  if (!g_scOK || g_scBunN == 0) return false;
  const BunMark *m = &g_scBun[esp_random() % (uint32_t)g_scBunN];
  *x = m->x; *y = m->y; *anim = m->anim;
  return true;
}

// The first prop whose pak name CONTAINS this word — "rug", "crib" — so the compiled-in
// furniture spots can land on the child's furniture instead of the shipped room's.
// A substring rather than an exact name because an imported rug is called something like
// "items/round_woven_floor_ru-92ed": the child's word for it survives, the rest is a hash.
static const SceneProp *sceneFindPropLike(const char *word) {
  if (!g_scOK) return nullptr;
  for (int i = 0; i < g_scPropN; i++)
    if (strstr(g_scProp[i].name, word)) return &g_scProp[i];
  return nullptr;
}

// A named prop, if the scene placed one. Needed by anything that has to draw ON a prop rather
// than just draw it — the cat clock's swinging tail and its hands, which are lines the firmware
// paints over the sprite and which were therefore lost the moment a scene supplied the clock.
static const SceneProp *sceneFindProp(const char *pakName) {
  if (!g_scOK) return nullptr;
  for (int i = 0; i < g_scPropN; i++)
    if (!strncmp(g_scProp[i].name, pakName, sizeof(g_scProp[i].name))) return &g_scProp[i];
  return nullptr;
}

static int sceneLampCount() { return g_scOK ? g_scLampN : 0; }
static const SceneProp *sceneLamp(int want) {
  if (!g_scOK || want < 0 || want >= g_scLampN) return nullptr;
  return &g_scProp[g_scLamp[want].prop];
}
static const SceneLamp *sceneLampCfg(int want) {
  if (!g_scOK || want < 0 || want >= g_scLampN) return nullptr;
  return &g_scLamp[want];
}

// The scenery footprints, if the scene gave any.
static bool sceneBlocks(const Block **out, int *n) {
  if (!g_scOK || g_scBlockN == 0) return false;
  *out = g_scBlock; *n = g_scBlockN; return true;
}

// Draws the scene's furniture. Low z first, so a rug goes under a table.
//
// WHERE THE PET SITS IN THAT STACK (Jon, live hardware: "bunbun is behind objects instead of in
// front"). He is painted in the band pipeline, before any of this runs, so without help every
// prop lands on top of him. The rule, and it is one line:
//
//     z <= 0  ->  BEHIND the pet and the cat        z > 0  ->  IN FRONT of them
//
// The default layer is 0, so a room a child furnishes without touching the layer control does the
// thing he asked for: he walks in front of the tables, the chairs and the rug, exactly as the
// builder's big window shows it. Deliberately raising something to the front layer still puts it
// over him — that is what the control is for, and the sim already keeps his feet in front of
// anything with a block, so nothing he can walk behind is load-bearing.
//
// `front` picks which half to paint. main.cpp calls this twice, back to back, with the pet's
// stencil armed for the first pass: see petStenHides() there for how the cutout works.
#define SCENE_Z_FRONT 1                  // the lowest z that draws over the pet
// Both in main.cpp, and both take the anchor the BUILDER uses: gx is the centre of the sprite's
// whole canvas and gy is that canvas's top, which is exactly what scene_push.py sends. Do not
// swap in drawItemAtXY here — it anchors the trimmed ink instead, and every prop rides high.
static void drawScenePropAtXY(const char *name, float gx, float gy,
                              float sx, float sy, bool flip);
// A loose prop is drawn where the SIM has it, not where it was placed — knocked over, rolling,
// or up in bunbun's arms. main.cpp owns that state; this asks it. Returns false for every
// ordinary prop, which is all of them until a cat gets involved.
static bool looseDrawOverride(int propIndex, float *x, float *y, const char **name);
static void drawItemRot(const char *name, float gx, float gy,
                        float sx, float sy, bool flip, float deg);
static void sceneDrawProps(bool front) {
  sceneEnsure();
  if (!g_scOK) return;
  for (int pass = -2; pass <= 2; pass++) {
    if ((pass >= SCENE_Z_FRONT) != front) continue;
    for (int i = 0; i < g_scPropN; i++)
      if (g_scProp[i].z == pass)
        {
        float px = g_scProp[i].x, py = g_scProp[i].y;
        const char *pn = g_scProp[i].name;
        const bool moved = looseDrawOverride(i, &px, &py, &pn);
        if (g_scProp[i].rot != 0.0f && !moved)
          drawItemRot(pn, px, py, g_scProp[i].sx, g_scProp[i].sy, g_scProp[i].flip,
                      g_scProp[i].rot);
        else
          drawScenePropAtXY(pn, px, py, g_scProp[i].sx, g_scProp[i].sy, g_scProp[i].flip);
      }
  }
}
