// BUNNY HOP — bunbun's Crossy Road (Jon 8/12). A little bunbun hops up an
// endless carrot farm: tractor lanes to dodge (driven by fellow bunnies from
// the work art), farmers and hay bales rolling through, and STREAMS you can
// only cross by riding floating logs — drift off the edge and it's a splash.
// Pick your bunny first: baby, teen, or grown-up (the real pak sprites).
// Score = how far you hop. GB-fine scale, crayon border, clipped erases.
//
// Included into main.cpp after palette/tft/sfx/prefs/esp_random/tttStroke and
// after spriteLoad/gameSpriteBlit — the games' bridge to the real art.
#pragma once

static bool g_ccPanel = false;

static const int CC_COLS = 14, CC_CELL = 16, CC_VIS = 9;   // 14 x 9 visible cells
static const int CC_X0 = 8, CC_Y0 = 52;                    // field 224 x 144, ends y196
static const int CC_FW = CC_COLS * CC_CELL, CC_FH = CC_VIS * CC_CELL;

// BACK, top-left (menu redesign P1 — every way out is the same corner).
static const int CC_DONE_X = 4, CC_DONE_TY = 4, CC_DONE_W = 52, CC_DONE_H = 22;   // ends y25: clear of the y28 stat band (review 8/13 #1)
static const int CC_PAD = 36;
// L/R near the edges, nudged slightly inward (Jon 8/13) — thumbs land there
// two-handed without hanging off the glass; U/D stay stacked in the centre.
static const int CC_PAD_CX = 102, CC_PAD_LX = 38, CC_PAD_RX = 166;
static const int CC_PAD_UY = 210, CC_PAD_MY = 242, CC_PAD_DY = 274;

// a soft paper-blue for the streams (muted, sits beside the bone tones)
#define CC_WATER_COL ((uint16_t)0x7D59)

// lane types
enum { CC_GRASS = 0, CC_ROAD_E, CC_ROAD_W, CC_WATER_E, CC_WATER_W };
static bool ccIsWater(uint8_t t) { return t == CC_WATER_E || t == CC_WATER_W; }
static bool ccIsRoad(uint8_t t) { return t == CC_ROAD_E || t == CC_ROAD_W; }

#define CC_RING 24
static uint8_t g_ccType[CC_RING];
static float g_ccSpd[CC_RING];       // px per tick (sign = direction)
static float g_ccObX[CC_RING][2];    // two obstacles/logs per lane
static uint8_t g_ccObW[CC_RING];     // obstacle width for the lane
static int g_ccGen;                  // highest world row generated

static int g_ccBase;                 // world row at the BOTTOM of the field
static int g_ccRow;                  // player world row
static float g_ccPx;                 // player centre x in FIELD px (drifts on logs)
static int g_ccBest;
static uint8_t g_ccState;            // 0 run, 1 over, 2 choose-your-bunny
static uint8_t g_ccChar = 0;         // 0 baby, 1 teen, 2 grown-up (NVS "ccchar")
static uint32_t g_ccTickAt, g_ccTouchMs;
static uint16_t g_ccHigh = 0;
static bool g_ccHighDirty = false;

// THE HAWK (Crossy Road's forward pressure): dawdle and a shadow crosses the
// field; keep dawdling and the hawk takes you. Any hop resets the clock.
static uint32_t g_ccLastHopAt;
static bool g_ccHawkWarned;
static uint8_t g_ccDeathBy;          // 0 squashed, 1 splash, 2 hawk

// g_ccChar is a PICKER, not the pet's current phase — Carrot Chase lets you run as any of the
// three. So it maps to a phase and the art comes from petFrameKey(), the one phase->folder map
// the render path also uses. The literal frame names used to live here as a third copy.
static const uint8_t CC_CHAR_PHASE[3] = {PH_BABY, PH_TEEN, PH_ADULT};
static const char *CC_CHAR_NAME[3] = {"baby", "teen", "grown-up"};

static inline int ccRingIx(int worldRow) { return ((worldRow % CC_RING) + CC_RING) % CC_RING; }
static inline int ccRowY(int worldRow) {
  return CC_Y0 + (CC_VIS - 1 - (worldRow - g_ccBase)) * CC_CELL;
}

static void ccFlushHigh() {
  if (!g_ccHighDirty) return;
  g_ccHighDirty = false;
  prefs.begin("bunbun", false);
  prefs.putUShort("cchi", g_ccHigh);
  prefs.end();
}

// Lane generation. Difficulty climbs with height; runs are capped so there is
// always a landing: max 3 roads or 2 waters back to back.
static void ccGenRow(int worldRow) {
  int ix = ccRingIx(worldRow);
  if (worldRow <= 1) { g_ccType[ix] = CC_GRASS; return; }
  // Progression stretch (review 8/13): the old curve was stationary by row
  // 36 — 10x earlier than the parent (~250). Opening meadow is real now,
  // and the treadmill doesn't max out until ~row 80.
  int busyChance = 25 + (worldRow * 2) / 3;
  if (busyChance > 78) busyChance = 78;
  int roadsBelow = 0, watersBelow = 0;
  for (int k = 1; k <= 3; k++) {
    uint8_t t = g_ccType[ccRingIx(worldRow - k)];
    if (t == CC_ROAD_E || t == CC_ROAD_W) roadsBelow++;
    else break;
  }
  for (int k = 1; k <= 2; k++) {
    uint8_t t = g_ccType[ccRingIx(worldRow - k)];
    if (t == CC_WATER_E || t == CC_WATER_W) watersBelow++;
    else break;
  }
  if ((int)(esp_random() % 100) >= busyChance) { g_ccType[ix] = CC_GRASS; return; }
  // water shows up from row 8 on, roughly a third of busy lanes
  bool water = (worldRow >= 8) && (watersBelow < 2) && ((esp_random() % 3) == 0);
  // hazard bands LENGTHEN late (Crossy's long multi-lane blocks): up to 3
  // roads back-to-back early, up to 5 past row 60. Water stays capped at 2.
  int maxRoads = (worldRow > 60) ? 5 : 3;
  if (!water && roadsBelow >= maxRoads) { g_ccType[ix] = CC_GRASS; return; }
  float sp = 0.7f + 0.02f * worldRow + (esp_random() % 40) / 100.0f;
  if (sp > 2.6f) sp = 2.6f;
  bool east = (esp_random() & 1);
  // A second river NEVER drifts the same way as the one below it (Jon 8/13:
  // "two logs that never overlapped and I couldn't pass") — same-direction
  // logs can stay phase-locked forever; opposite drift brings a hop window
  // around every few seconds, guaranteed.
  if (water && watersBelow >= 1) {
    east = (g_ccType[ccRingIx(worldRow - 1)] != CC_WATER_E);
  }
  if (water) {
    g_ccType[ix] = east ? CC_WATER_E : CC_WATER_W;
    g_ccSpd[ix] = east ? sp * 0.7f : -sp * 0.7f;   // logs drift a bit slower
    g_ccObW[ix] = 44;                              // a rideable log
  } else {
    g_ccType[ix] = east ? CC_ROAD_E : CC_ROAD_W;
    g_ccSpd[ix] = east ? sp : -sp;
    uint32_t kind = esp_random() % 3;
    g_ccObW[ix] = (kind == 0) ? 26 : (kind == 1) ? 12 : 16;
  }
  g_ccObX[ix][0] = (float)(esp_random() % (CC_FW / 2));
  // desynced spacing (panel: locked half-field pairs made every lane the
  // same metronome — a ±20px wobble makes traffic need JUDGMENT)
  g_ccObX[ix][1] = g_ccObX[ix][0] + CC_FW / 2 + (float)(esp_random() % 40) - 20.0f;
  // logs shorten in two steps now (was one cliff at row 21)
  if (ccIsWater(g_ccType[ix])) {
    if (worldRow > 90)      g_ccObW[ix] = 36;
    else if (worldRow > 40) g_ccObW[ix] = 40;
  }
}

static void ccEnsureGen(int upTo) {
  while (g_ccGen < upTo) { g_ccGen++; ccGenRow(g_ccGen); }
}

// --- drawing ---

static void ccDrawBorder() {
  int x0 = CC_X0 - 4, y0 = CC_Y0 - 4, x1 = CC_X0 + CC_FW + 3, y1 = CC_Y0 + CC_FH + 3;
  tttStroke(x0, y0, x1, y0, C_INK, 1, 441);
  tttStroke(x1, y0, x1, y1, C_INK, 1, 442);
  tttStroke(x1, y1, x0, y1, C_INK, 1, 443);
  tttStroke(x0, y1, x0, y0, C_INK, 1, 444);
}

static void ccClipErase(int x, int y, int w, int h, uint16_t col) {
  int x0 = CC_X0, y0 = CC_Y0, x1 = CC_X0 + CC_FW, y1 = CC_Y0 + CC_FH;
  if (x < x0) { w -= (x0 - x); x = x0; }
  if (y < y0) { h -= (y0 - y); y = y0; }
  if (x + w > x1) w = x1 - x;
  if (y + h > y1) h = y1 - y;
  if (w > 0 && h > 0) tft.fillRect(x, y, w, h, col);
}

static void ccDrawLaneBg(int worldRow) {
  int y = ccRowY(worldRow), ix = ccRingIx(worldRow);
  uint8_t t = g_ccType[ix];
  tft.fillRect(CC_X0, y, CC_FW, CC_CELL, ccIsWater(t) ? CC_WATER_COL : C_BONE);
  if (t == CC_GRASS) {
    for (int k = 0; k < 5; k++) {
      int gx = CC_X0 + 8 + (int)(tttJit(500 + worldRow, k) % (CC_FW - 16));
      tft.drawFastVLine(gx, y + CC_CELL - 6, 4, C_HP);
      tft.drawPixel(gx - 1, y + CC_CELL - 4, C_HP);
      tft.drawPixel(gx + 1, y + CC_CELL - 4, C_HP);
    }
    // meadow dressing (UI-dev P5): deterministic per world row, so every
    // redraw paints the same cozy farm — this is FarmVille warmth, free
    uint32_t j = tttJit(520 + worldRow, 9);
    if ((j % 3) == 0) {                          // a little orange flower
      int fx = CC_X0 + 14 + (int)(j % (CC_FW - 28));
      tft.drawPixel(fx, y + 5, C_ORANGE); tft.drawPixel(fx - 1, y + 6, C_ORANGE);
      tft.drawPixel(fx + 1, y + 6, C_ORANGE); tft.drawPixel(fx, y + 7, C_ORANGE);
      tft.drawFastVLine(fx, y + 8, 3, C_HP);
    }
    if ((j % 4) == 1) {                          // a pair of fence posts
      int px2 = CC_X0 + 20 + (int)((j >> 4) % (CC_FW - 60));
      tft.drawFastVLine(px2, y + 4, 8, C_ORANGE_LO);
      tft.drawFastVLine(px2 + 14, y + 4, 8, C_ORANGE_LO);
      tft.drawFastHLine(px2, y + 6, 15, C_ORANGE_LO);
    }
    if ((j % 7) == 2) {                          // a rare paper butterfly
      int bx2 = CC_X0 + 10 + (int)((j >> 6) % (CC_FW - 20));
      tft.drawPixel(bx2, y + 4, C_PAPER); tft.drawPixel(bx2 + 2, y + 4, C_PAPER);
      tft.drawPixel(bx2 + 1, y + 5, C_INK_SOFT);
    }
  } else if (ccIsRoad(t)) {
    tft.drawFastHLine(CC_X0, y + 1, CC_FW, C_INK_SOFT);
    tft.drawFastHLine(CC_X0, y + CC_CELL - 2, CC_FW, C_INK_SOFT);
    for (int d = 0; d < CC_FW; d += 16)
      tft.drawFastHLine(CC_X0 + d + 4, y + CC_CELL / 2, 6, C_BONE_LO);
  } else {
    // little wave ticks on the stream
    for (int d = 0; d < CC_FW; d += 20) {
      int wx = CC_X0 + d + (int)(tttJit(700 + worldRow, d) % 8);
      tft.drawFastHLine(wx, y + 4 + (d % 8), 5, C_PAPER);
    }
  }
}

static void ccDrawObstacle(int worldRow, int k) {
  int ix = ccRingIx(worldRow);
  int y = ccRowY(worldRow);
  int x = CC_X0 + (int)g_ccObX[ix][k];
  int w = g_ccObW[ix];
  uint8_t t = g_ccType[ix];
  if (x + w < CC_X0 + 2 || x > CC_X0 + CC_FW - 2) return;
  if (ccIsWater(t)) {
    // a floating log: warm brown, ring at the leading end
    tft.fillRoundRect(x, y + 4, w, 9, 4, C_ORANGE_LO);
    tft.drawRoundRect(x, y + 4, w, 9, 4, C_INK);
    int ex = (g_ccSpd[ix] > 0) ? x + w - 6 : x + 2;
    tft.drawCircle(ex + 2, y + 8, 2, C_INK);
    return;
  }
  // road traffic: the purpose-made PixelLab farm sprites (8/13). Side
  // profiles face RIGHT in the pak; flip when the lane runs west. Scaled by
  // collision width so the visual is never smaller than the hitbox (deaths
  // must look deserved), clipped to this lane's cell so a half-entered
  // sprite can't touch the border or a neighbouring row.
  // blits clip to y+1..y+CC_CELL-2 — EXACTLY the mover's erase band (the
  // 8/13 trail bug: sprite rows outside the erase band were never wiped)
  bool west = g_ccSpd[ix] < 0;
  int cx = x + w / 2, cy = y + CC_CELL / 2;
  if (w > 20) {
    if (spriteLoad("games/tractor")) {
      gameSpriteBlit(cx, cy, (float)w / g_meta.w,
                     x < CC_X0 + 2 ? CC_X0 + 2 : x - 2, y + 1,
                     CC_X0 + CC_FW - 3, y + CC_CELL - 2, west, C_BONE);
      return;
    }
    tft.fillRoundRect(x, y + 3, w, 8, 2, C_HP);
    tft.fillRect(x + (g_ccSpd[ix] > 0 ? w - 8 : 0), y + 1, 8, 5, C_HP);
    tft.fillCircle(x + 5, y + 12, 2, C_INK);
    tft.fillCircle(x + w - 5, y + 12, 2, C_INK);
  } else if (w > 13) {
    if (spriteLoad("games/hay")) {
      gameSpriteBlit(cx, cy, (float)(CC_CELL - 2) / g_meta.h,
                     x < CC_X0 + 2 ? CC_X0 + 2 : x - 2, y + 1,
                     CC_X0 + CC_FW - 3, y + CC_CELL - 2, west, C_BONE);
      return;
    }
    tft.fillCircle(cx, cy, 7, C_ORANGE_LO);
    tft.drawCircle(cx, cy, 7, C_INK);
    tft.drawCircle(cx, cy, 3, C_INK_SOFT);
  } else {
    if (spriteLoad("games/farmer")) {
      // farmer is pre-sized in the pak (7x14): scale 1 — no block sampling,
      // so his colours survive (the 8/13 all-black bug was the darkest-pixel
      // downsample eating a 32x61 source whole)
      gameSpriteBlit(cx, cy, (float)(CC_CELL - 2) / g_meta.h,
                     x < CC_X0 + 2 ? CC_X0 + 2 : x - 2, y + 1,
                     CC_X0 + CC_FW - 3, y + CC_CELL - 2, west, C_BONE);
      return;
    }
    tft.fillRect(x + 3, y + 6, 6, 7, C_HP);
    tft.fillCircle(x + 6, y + 4, 3, C_PAPER);
    tft.drawFastHLine(x + 2, y + 2, 9, C_ORANGE_LO);
    tft.drawFastHLine(x + 4, y + 1, 5, C_ORANGE_LO);
    tft.drawFastVLine(x + 4, y + 13, 2, C_INK);
    tft.drawFastVLine(x + 8, y + 13, 2, C_INK);
  }
}

// the player: the CHOSEN bunbun, blitted from the pak; vector fallback if the
// art is unreachable (fw_assets_writing etc.)
static void ccDrawPlayer() {
  int x = CC_X0 + (int)g_ccPx, y = ccRowY(g_ccRow) + CC_CELL / 2;
  if (spriteLoad(petFrameKey(CC_CHAR_PHASE[g_ccChar < 3 ? g_ccChar : 0]))) {
    gameSpriteBlit(x, y, 15.0f / (g_meta.w > g_meta.h ? g_meta.w : g_meta.h),
                   CC_X0, CC_Y0, CC_X0 + CC_FW - 1, CC_Y0 + CC_FH - 1);
  } else {
    tft.fillRoundRect(x - 6, y - 3, 12, 10, 4, C_PAPER);
    tft.drawRoundRect(x - 6, y - 3, 12, 10, 4, C_INK);
    tft.fillCircle(x - 2, y + 1, 1, C_INK);
    tft.fillCircle(x + 2, y + 1, 1, C_INK);
  }
}

static void ccDrawScore() {
  tft.fillRect(8, 28, 168, 16, C_BONE);
  char sc[16];
  snprintf(sc, sizeof(sc), "%d", g_ccBest);
  tft.setTextColor(C_ORANGE, C_BONE);
  tft.drawString(sc, 8, 28, 2);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  char hi[16];
  snprintf(hi, sizeof(hi), "best %u", g_ccHigh > 9999 ? 9999 : g_ccHigh);
  tft.drawRightString(hi, 176, 32, 1);
}

static void ccPad(int x, int y, char dir) {
  int cx = x + CC_PAD / 2, cy = y + CC_PAD / 2;
  tft.fillRoundRect(x, y, CC_PAD, CC_PAD, 8, C_BONE_LO);
  tft.drawRoundRect(x, y, CC_PAD, CC_PAD, 8, C_INK);
  switch (dir) {
    case 'U': tft.fillTriangle(cx - 8, cy + 5, cx + 8, cy + 5, cx, cy - 8, C_INK); break;
    case 'D': tft.fillTriangle(cx - 8, cy - 5, cx + 8, cy - 5, cx, cy + 8, C_INK); break;
    case 'L': tft.fillTriangle(cx + 5, cy - 8, cx + 5, cy + 8, cx - 8, cy, C_INK); break;
    case 'R': tft.fillTriangle(cx - 5, cy - 8, cx - 5, cy + 8, cx + 8, cy, C_INK); break;
  }
}

static void ccDrawField() {
  // HARD CLIP: everything in the field draws inside a viewport, so a tractor
  // sliding in from the edge can never paint over (or past) the border
  // (Jon 8/12: "weird things on the borders like bunny hop")
  tft.setViewport(CC_X0, CC_Y0, CC_FW, CC_FH, false);
  for (int r = g_ccBase; r < g_ccBase + CC_VIS; r++) {
    ccDrawLaneBg(r);
    int ix = ccRingIx(r);
    if (g_ccType[ix] != CC_GRASS) { ccDrawObstacle(r, 0); ccDrawObstacle(r, 1); }
  }
  ccDrawPlayer();
  tft.resetViewport();
  ccDrawBorder();
}

static void drawCrossingPanel() {
  tft.fillScreen(C_BONE);
  // BACK top-left, title at x64 — the y28..44 stat band keeps its measured
  // columns untouched.
  drawBackChip(CC_DONE_X, CC_DONE_TY, CC_DONE_W, CC_DONE_H, "\x1B SHELF");
  tft.setTextColor(C_INK, C_BONE);
  tft.drawString("BUNNY HOP", 64, 8, 2);
  ccDrawScore();

  ccDrawField();
  ccPad(CC_PAD_CX, CC_PAD_UY, 'U');
  ccPad(CC_PAD_LX, CC_PAD_MY, 'L');
  ccPad(CC_PAD_RX, CC_PAD_MY, 'R');
  ccPad(CC_PAD_CX, CC_PAD_DY, 'D');

  if (g_ccState == 1) {
    int cy = CC_Y0 + CC_FH / 2;
    tft.fillRoundRect(CC_X0 + 14, cy - 30, CC_FW - 28, 60, 8, C_PAPER);
    tft.drawRoundRect(CC_X0 + 14, cy - 30, CC_FW - 28, 60, 8, C_INK);
    tft.setTextColor(C_INK, C_PAPER);
    char over[28];
    snprintf(over, sizeof(over), "%d hops!", g_ccBest);
    tft.drawCentreString(over, CC_X0 + CC_FW / 2, cy - 22, 2);
    tft.setTextColor(C_LOW, C_PAPER);
    const char *how = (g_ccDeathBy == 1) ? "splash!"
                    : (g_ccDeathBy == 2) ? "a hawk swooped in!" : "squashed!";
    tft.drawCentreString(how, CC_X0 + CC_FW / 2, cy - 4, 1);
    tft.setTextColor(C_INK_SOFT, C_PAPER);
    tft.drawCentreString("tap to hop again", CC_X0 + CC_FW / 2, cy + 12, 1);
  }
}

static void crossingBegin();

// entry point from the roster. The hopping bunny IS the family's bunbun —
// whatever stage he's grown to, same look, same colours (Jon 8/12: "have it
// match the stage our main game is in").
static void crossingReset() {
  g_ccChar = (S.phase == PH_BABY) ? 0 : (S.phase == PH_TEEN) ? 1 : 2;
  crossingBegin();
}

// actually begin a run
static void crossingBegin() {
  g_ccGen = -1;
  for (int r = 0; r < CC_RING; r++) g_ccType[r] = CC_GRASS;
  g_ccBase = 0;
  ccEnsureGen(CC_VIS + 2);
  g_ccRow = 1;
  g_ccPx = (float)(CC_COLS / 2) * CC_CELL + CC_CELL / 2;
  g_ccBest = 0; g_ccState = 0;
  g_ccTouchMs = millis();
  g_ccTickAt = millis() + 400;
  g_ccLastHopAt = millis() + 1500;   // grace before the hawk clock starts
  g_ccHawkWarned = false;
}

static void crossingOver(bool splash) {
  g_ccState = 1;
  g_ccDeathBy = splash ? 1 : 0;
  if (g_ccBest > (int)g_ccHigh) { g_ccHigh = (uint16_t)g_ccBest; g_ccHighDirty = true; }
  if (splash) sfxPlop(); else sfxLose();
}

// is the player standing on a log? returns drift speed via *drift
static bool ccOnLog(float *drift) {
  int ix = ccRingIx(g_ccRow);
  if (!ccIsWater(g_ccType[ix])) return true;   // solid ground counts
  for (int k = 0; k < 2; k++) {
    float ox = g_ccObX[ix][k];
    if (g_ccPx >= ox - 2 && g_ccPx <= ox + g_ccObW[ix] + 2) {
      if (drift) *drift = g_ccSpd[ix];
      return true;
    }
  }
  return false;
}

static bool ccSquashed() {
  int ix = ccRingIx(g_ccRow);
  if (!ccIsRoad(g_ccType[ix])) return false;
  for (int k = 0; k < 2; k++) {
    float ox = g_ccObX[ix][k];
    if (ox + g_ccObW[ix] > g_ccPx - 5 && ox < g_ccPx + 5) return true;
  }
  return false;
}

// one traffic tick (~33ms)
static void crossingTick() {
  if (g_ccState != 0) return;
  // THE HAWK: warn at 5s of dawdling (a cry + a passing shadow), strike at 8s
  // (panel: kid-rate — Piper must never feel rushed, Sam must not camp).
  // SIGNED math: g_ccLastHopAt starts 1.5s in the future as launch grace, and
  // the unsigned subtraction underflowed to ~4 billion = instant hawk on the
  // first tick of every run (adversarial review, 8/13 — game-breaker).
  int32_t idle = (int32_t)(millis() - g_ccLastHopAt);
  if (idle > 8000) {
    g_ccState = 1;
    g_ccDeathBy = 2;
    if (g_ccBest > (int)g_ccHigh) { g_ccHigh = (uint16_t)g_ccBest; g_ccHighDirty = true; }
    sfxLose();
    drawCrossingPanel();
    return;
  }
  static uint32_t shadowUntil = 0;
  if (idle > 5000 && !g_ccHawkWarned) {
    g_ccHawkWarned = true;
    beep(1400, 0.09f, SFX_TRIANGLE, 0.05f);          // a distant hawk cry
    beep(1150, 0.12f, SFX_TRIANGLE, 0.05f, 0.10f);
    // a shadow sweeps the field; cleaned up by a full redraw moments later
    tft.setViewport(CC_X0, CC_Y0, CC_FW, CC_FH, false);
    for (int s = 0; s < CC_FW; s += 8)
      tft.drawFastVLine(CC_X0 + s, CC_Y0 + 8, CC_FH - 16, C_INK_SOFT);
    tft.resetViewport();
    shadowUntil = millis() + 500;
  }
  if (shadowUntil && millis() > shadowUntil) {
    shadowUntil = 0;
    ccDrawField();                                    // sweep the shadow away
  }
  int py = ccRowY(g_ccRow);
  bool playerOnWater = ccIsWater(g_ccType[ccRingIx(g_ccRow)]);
  // ALL field drawing under the viewport clip; every exit resets it first
  tft.setViewport(CC_X0, CC_Y0, CC_FW, CC_FH, false);
  // the player drifts with the log BEFORE traffic moves (rides its speed)
  if (playerOnWater) {
    float drift = 0;
    if (ccOnLog(&drift)) {
      ccClipErase((int)g_ccPx + CC_X0 - 9, py, 19, CC_CELL, CC_WATER_COL);
      g_ccPx += drift;
      if (g_ccPx < 4 || g_ccPx > CC_FW - 4) {   // carried off the field edge
        tft.resetViewport();
        crossingOver(true);
        drawCrossingPanel();
        return;
      }
    } else {
      tft.resetViewport();
      crossingOver(true);                        // splash!
      drawCrossingPanel();
      return;
    }
  }
  for (int r = g_ccBase; r < g_ccBase + CC_VIS; r++) {
    int ix = ccRingIx(r);
    uint8_t t = g_ccType[ix];
    if (t == CC_GRASS) continue;
    int y = ccRowY(r);
    uint16_t bg = ccIsWater(t) ? CC_WATER_COL : C_BONE;
    for (int k = 0; k < 2; k++) {
      int ox = CC_X0 + (int)g_ccObX[ix][k];
      ccClipErase(ox - 1, y + 1, g_ccObW[ix] + 4, CC_CELL - 2, bg);
      g_ccObX[ix][k] += g_ccSpd[ix];
      if (g_ccSpd[ix] > 0 && g_ccObX[ix][k] > CC_FW + 8) g_ccObX[ix][k] = -g_ccObW[ix] - 8;
      if (g_ccSpd[ix] < 0 && g_ccObX[ix][k] < -g_ccObW[ix] - 8) g_ccObX[ix][k] = CC_FW + 8;
    }
    if (ccIsRoad(t)) {
      tft.drawFastHLine(CC_X0, y + 1, CC_FW, C_INK_SOFT);
      tft.drawFastHLine(CC_X0, y + CC_CELL - 2, CC_FW, C_INK_SOFT);
    }
    ccDrawObstacle(r, 0);
    ccDrawObstacle(r, 1);
  }
  if (ccSquashed()) {
    tft.resetViewport();
    crossingOver(false);
    drawCrossingPanel();
    return;
  }
  ccDrawPlayer();   // last, so the bunny rides ON TOP of logs and traffic
  tft.resetViewport();
}

static void crossingHop(char dir) {
  if (g_ccState != 0) return;
  int nr = g_ccRow;
  float npx = g_ccPx;
  if (dir == 'U') nr++;
  else if (dir == 'D') nr--;
  else if (dir == 'L') npx -= CC_CELL;
  else if (dir == 'R') npx += CC_CELL;
  if (npx < CC_CELL / 2 || npx > CC_FW - CC_CELL / 2 || nr < g_ccBase || nr < 0) return;
  sfxTick();
  g_ccLastHopAt = millis();          // any hop resets the hawk clock
  g_ccHawkWarned = false;
  // erase the bunny at its old spot, restore that lane's look — clipped
  tft.setViewport(CC_X0, CC_Y0, CC_FW, CC_FH, false);
  int oy = ccRowY(g_ccRow);
  int oix = ccRingIx(g_ccRow);
  uint16_t obg = ccIsWater(g_ccType[oix]) ? CC_WATER_COL : C_BONE;
  // erase confined to the OLD lane only (UI-dev audit A1: the 3px overdraw
  // stamped this lane's background onto the neighbours — bone stripes on
  // water, blue smudges on grass). The bunny's true footprint fits the cell.
  ccClipErase((int)g_ccPx + CC_X0 - 9, oy, 19, CC_CELL, obg);
  if (ccIsRoad(g_ccType[oix])) {
    tft.drawFastHLine(CC_X0, oy + 1, CC_FW, C_INK_SOFT);
    tft.drawFastHLine(CC_X0, oy + CC_CELL - 2, CC_FW, C_INK_SOFT);
  }
  if (g_ccType[oix] != CC_GRASS) { ccDrawObstacle(g_ccRow, 0); ccDrawObstacle(g_ccRow, 1); }
  tft.resetViewport();
  g_ccRow = nr; g_ccPx = npx;
  if (g_ccRow > g_ccBest) { g_ccBest = g_ccRow; ccDrawScore(); }
  if (g_ccRow - g_ccBase > CC_VIS - 4) {
    g_ccBase = g_ccRow - (CC_VIS - 4);
    ccEnsureGen(g_ccBase + CC_VIS + 2);
    ccDrawField();
  } else {
    tft.setViewport(CC_X0, CC_Y0, CC_FW, CC_FH, false);
    ccDrawPlayer();
    tft.resetViewport();
  }
  // landings settle instantly: squash on roads, splash on open water
  if (ccSquashed()) { crossingOver(false); drawCrossingPanel(); return; }
  if (!ccOnLog(NULL)) { crossingOver(true); drawCrossingPanel(); return; }
}

// 'X' BACK, 'U' 'D' 'L' 'R', 0 none
static char ccBtnHit(int tx, int ty) {
  if (tx < CC_DONE_X + CC_DONE_W + 12 && ty <= CC_DONE_TY + CC_DONE_H) return 'X';   // no slop into the score digits (review 8/13 #2)
  if (tx >= CC_PAD_CX && tx <= CC_PAD_CX + CC_PAD) {
    if (ty >= CC_PAD_UY && ty <= CC_PAD_UY + CC_PAD) return 'U';
    if (ty >= CC_PAD_DY && ty <= CC_PAD_DY + CC_PAD) return 'D';
  }
  if (ty >= CC_PAD_MY && ty <= CC_PAD_MY + CC_PAD) {
    if (tx >= CC_PAD_LX && tx <= CC_PAD_LX + CC_PAD) return 'L';
    if (tx >= CC_PAD_RX && tx <= CC_PAD_RX + CC_PAD) return 'R';
  }
  return 0;
}
