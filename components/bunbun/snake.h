// Carrot Chase — bunbun snake. A hungry bun grows one segment for every
// carrot it catches; walls and its own tail end the run. Steer with the four
// arrow buttons under the field. Same hand-drawn, paper-and-ink look as
// tic-tac-toe.
//
// Two things Jon asked for (8/12): the board must NOT refresh the whole screen
// on every step (that flashes and reads as jarring), and movement is by BUTTON,
// not by tapping the play area. So the field is drawn once and then only the
// cells that actually change are repainted each step, and a control row of
// arrows lives below the field.
//
// Included into main.cpp after the palette, tft, sfx, prefs and esp_random are
// all in scope. State + logic + render live here; the loop dispatch and the
// roster hook live in main.cpp beside the other games.
#pragma once

static bool g_snakePanel = false;

// GB-fine scale (Jon 8/12: "I really want this to shine"): a 16x12 field of
// small 12px cells — the bun is a sprite in a world, not a tile in a grid.
static const int SNK_COLS = 16, SNK_ROWS = 12, SNK_CELL = 12;
static const int SNK_X0 = 24, SNK_Y0 = 54;      // field 192x144, ends y198 (D-pad at 204)
static const int SNK_MAX = SNK_COLS * SNK_ROWS;

// BACK lives top-left now (menu redesign P1 — every way out is the same
// corner, on every screen); the bottom stays free for the proper D-pad.
static const int SNK_DONE_X = 4, SNK_DONE_TY = 4, SNK_DONE_W = 52, SNK_DONE_H = 22;   // ends y25: clear of the y28 stat band (review 8/13 #1)

// A real D-pad CROSS under the field (Jon 8/12: the four-in-a-row arrows were
// awkward — up and down as horizontal neighbours reads wrong). 36px pads:
// UP top-centre, LEFT / RIGHT on the middle row, DOWN bottom-centre.
static const int SNK_PAD = 36;
// L/R near the edges, nudged slightly inward (Jon 8/13)
static const int SNK_PAD_CX = 102, SNK_PAD_LX = 38, SNK_PAD_RX = 166;
static const int SNK_PAD_UY = 210, SNK_PAD_MY = 242, SNK_PAD_DY = 274;

static uint8_t g_snkBody[SNK_MAX];   // packed cell = x + y*COLS; [0] = head
static int g_snkLen;
static int g_snkDx, g_snkDy;         // current heading
static int g_snkPendDx, g_snkPendDy; // steered heading, applied on the next step
static uint8_t g_snkCarrot;
static int g_snkScore;
static uint8_t g_snkState;           // 0 = running, 1 = over
static uint32_t g_snkStepAt, g_snkTouchMs;
static uint16_t g_snkHigh = 0;
static bool g_snkHighDirty = false;
static int g_snkVacated;             // cell the tail left this step, or -1 (grew)
static bool g_snkAte;                // a carrot was eaten this step

static int g_snkQDx = 0, g_snkQDy = 0;   // 2-deep turn queue (panel 8/13)

// LEVELS (Jon 8/12, Game Boy baseline): every 5 carrots is a level. Each
// level speeds the bun up AND drops garden stones onto the field — obstacles
// that end the run just like walls, the way the classic's later mazes do.
static int g_snkLevel;               // 1-based
static int g_snkEaten;               // carrots eaten (level clock; score now pays by level)
#define SNK_MAX_STONES 10
static uint8_t g_snkStones[SNK_MAX_STONES];
static int g_snkNStones;

static inline int snkCX(uint8_t c) { return c % SNK_COLS; }
static inline int snkCY(uint8_t c) { return c / SNK_COLS; }
static inline uint8_t snkCell(int x, int y) { return (uint8_t)(x + y * SNK_COLS); }
static inline int snkPX(uint8_t c) { return SNK_X0 + snkCX(c) * SNK_CELL; }
static inline int snkPY(uint8_t c) { return SNK_Y0 + snkCY(c) * SNK_CELL; }

static bool snkStoneAt(uint8_t c) {
  for (int i = 0; i < g_snkNStones; i++)
    if (g_snkStones[i] == c) return true;
  return false;
}

static bool snkOccupied(uint8_t c, int skipTail) {
  for (int i = 0; i < g_snkLen - skipTail; i++)
    if (g_snkBody[i] == c) return true;
  return false;
}

static void snkSpawnCarrot() {
  for (int t = 0; t < 300; t++) {
    uint8_t c = (uint8_t)(esp_random() % SNK_MAX);
    if (!snkOccupied(c, 0) && !snkStoneAt(c)) { g_snkCarrot = c; return; }
  }
  for (uint8_t c = 0; c < SNK_MAX; c++)
    if (!snkOccupied(c, 0) && !snkStoneAt(c)) { g_snkCarrot = c; return; }
}

// A new stone for a new level: never on the snake, the carrot, an existing
// stone, or directly ahead of the bun's nose (a stone materialising in your
// path would be a cheap death, not a challenge).
static void snkDropStone() {
  if (g_snkNStones >= SNK_MAX_STONES) return;
  int hx = snkCX(g_snkBody[0]), hy = snkCY(g_snkBody[0]);
  for (int t = 0; t < 300; t++) {
    uint8_t c = (uint8_t)(esp_random() % SNK_MAX);
    int cx = snkCX(c), cy = snkCY(c);
    // ahead-check WRAPS like the bun does (panel 8/13: near an edge a stone
    // could legally land exactly in the tunnel he was about to come out of)
    bool ahead = false;
    for (int k = 1; k <= 3 && !ahead; k++) {
      int ax = (hx + g_snkDx * k + SNK_COLS) % SNK_COLS;
      int ay = (hy + g_snkDy * k + SNK_ROWS) % SNK_ROWS;
      ahead = (cx == ax && cy == ay);
    }
    if (!snkOccupied(c, 0) && !snkStoneAt(c) && c != g_snkCarrot && !ahead) {
      g_snkStones[g_snkNStones++] = c;
      return;
    }
  }
}

static void snkFlushHigh() {
  if (!g_snkHighDirty) return;
  g_snkHighDirty = false;
  prefs.begin("bunbun", false);
  prefs.putUShort("snkhi", g_snkHigh);
  prefs.end();
}

static void snakeReset() {
  int cx = SNK_COLS / 2, cy = SNK_ROWS / 2;
  g_snkLen = 3;
  g_snkBody[0] = snkCell(cx, cy);
  g_snkBody[1] = snkCell(cx - 1, cy);
  g_snkBody[2] = snkCell(cx - 2, cy);
  g_snkDx = 1; g_snkDy = 0; g_snkPendDx = 1; g_snkPendDy = 0;
  g_snkQDx = 0; g_snkQDy = 0;   // audit A4: a queued turn must not haunt the next run
  g_snkScore = 0; g_snkState = 0;
  g_snkLevel = 1; g_snkEaten = 0; g_snkNStones = 0;
  g_snkVacated = -1; g_snkAte = false;
  g_snkTouchMs = millis();
  g_snkStepAt = millis() + 550;      // a beat to read the board before it moves
  snkSpawnCarrot();
}

// --- cell-sized drawing primitives; everything stays WITHIN its cell so a
//     plain fillRect erase leaves no stray pixels behind. ---

static void snkEraseCell(int c) {
  if (c < 0) return;
  tft.fillRect(snkPX((uint8_t)c), snkPY((uint8_t)c), SNK_CELL, SNK_CELL, C_BONE);
}

static void snkDrawBody(uint8_t c) {
  int px = snkPX(c) + 2, py = snkPY(c) + 2;
  tft.fillRoundRect(px, py, SNK_CELL - 4, SNK_CELL - 4, 4, C_ORANGE);
  tft.drawRoundRect(px, py, SNK_CELL - 4, SNK_CELL - 4, 4, C_ORANGE_LO);
}

static void snkDrawCarrot(uint8_t c) {
  int px = snkPX(c), py = snkPY(c), cx = px + SNK_CELL / 2;
  // every 5th carrot is GOLDEN (pays 3x level — the bonus-bug beat): drawn
  // as its own vector treat so it reads different at a glance
  if ((g_snkEaten + 1) % 5 == 0) {
    tft.fillTriangle(cx - 5, py + 6, cx + 5, py + 6, cx, py + SNK_CELL - 2, C_DISC);
    tft.drawTriangle(cx - 5, py + 6, cx + 5, py + 6, cx, py + SNK_CELL - 2, C_ORANGE_LO);
    tft.drawLine(cx, py + 6, cx - 3, py + 2, C_HP);
    tft.drawLine(cx, py + 6, cx + 3, py + 2, C_HP);
    tft.drawPixel(cx + 4, py + 3, C_PAPER);    // the glint
    tft.drawPixel(cx + 5, py + 4, C_PAPER);
    return;
  }
  // the real carrot art when the pak carries it (Jon 8/12) — the PixelLab
  // game sprite landed as games/carrot (8/13); items/carrot kept as fallback
  if (spriteLoad("games/carrot") || spriteLoad("items/carrot")) {
    gameSpriteBlit(cx, py + SNK_CELL / 2,
                   (float)SNK_CELL / (g_meta.h > g_meta.w ? g_meta.h : g_meta.w),
                   px, py, px + SNK_CELL - 1, py + SNK_CELL - 1, false, C_BONE);
    return;
  }
  tft.fillTriangle(cx - 5, py + 6, cx + 5, py + 6, cx, py + SNK_CELL - 2, C_ORANGE);
  tft.drawTriangle(cx - 5, py + 6, cx + 5, py + 6, cx, py + SNK_CELL - 2, C_ORANGE_LO);
  tft.drawLine(cx, py + 6, cx - 3, py + 2, C_HP);
  tft.drawLine(cx, py + 6, cx, py + 1, C_HP);
  tft.drawLine(cx, py + 6, cx + 3, py + 2, C_HP);
}

static void snkDrawHead() {
  int px = snkPX(g_snkBody[0]) + 1, py = snkPY(g_snkBody[0]) + 1;
  int s = SNK_CELL - 2;
  // the head is BUNBUN HIMSELF, stage-matched (Jon 8/12: "use the carrot
  // sprite and the bunbun sprite so it looks like he is picking up carrots")
  // The phase->folder map lives in petFrameKey() so this and the render path cannot drift
  // apart, and so a character pack reaches the games and not just the room.
  const char *f = petFrameKey(S.phase);
  if (spriteLoad(f)) {
    gameSpriteBlit(px + s / 2, py + s / 2,
                   (float)SNK_CELL / (g_meta.h > g_meta.w ? g_meta.h : g_meta.w),
                   px - 1, py - 1, px + s, py + s, false, C_BONE);
    return;
  }
  // vector fallback: ears + dot eyes
  tft.fillRoundRect(px + 2, py, 3, 5, 1, C_PAPER);
  tft.fillRoundRect(px + s - 5, py, 3, 5, 1, C_PAPER);
  tft.fillRoundRect(px, py + 3, s, s - 3, 4, C_PAPER);
  tft.drawRoundRect(px, py + 3, s, s - 3, 4, C_INK);
  int ex = px + s / 2 + g_snkDx * 2, ey = py + 3 + (s - 3) / 2 + g_snkDy * 2;
  tft.fillCircle(ex - 3, ey, 1, C_INK);
  tft.fillCircle(ex + 3, ey, 1, C_INK);
}

static void snkDrawStone(uint8_t c) {
  int px = snkPX(c) + 2, py = snkPY(c) + 3;
  // a garden stone: a lumpy grey-ish pebble with a crayon outline
  tft.fillRoundRect(px, py, SNK_CELL - 4, SNK_CELL - 6, 5, C_INK_SOFT);
  tft.drawRoundRect(px, py, SNK_CELL - 4, SNK_CELL - 6, 5, C_INK);
  tft.drawPixel(px + 3, py + 3, C_BONE_LO);      // a glint
  tft.drawPixel(px + 4, py + 3, C_BONE_LO);
}

// The hand-drawn field border, in the tic-tac-toe crayon stroke. Redrawn on
// its own — cheap — so no erase can ever leave the field frameless for more
// than a beat (Jon 8/12: "make sure the game border doesn't disappear").
static void snkDrawBorder() {
  int fw = SNK_COLS * SNK_CELL, fh = SNK_ROWS * SNK_CELL;
  int x0 = SNK_X0 - 5, y0 = SNK_Y0 - 5, x1 = SNK_X0 + fw + 4, y1 = SNK_Y0 + fh + 4;
  tttStroke(x0, y0, x1, y0, C_INK, 1, 401);
  tttStroke(x1, y0, x1, y1, C_INK, 1, 402);
  tttStroke(x1, y1, x0, y1, C_INK, 1, 403);
  tttStroke(x0, y1, x0, y0, C_INK, 1, 404);
}

// Header band y28..44 only — never touches the frame (top at 49 with wobble).
static void snkDrawScore() {
  tft.fillRect(8, 28, 168, 16, C_BONE);
  char sc[16];
  snprintf(sc, sizeof(sc), "%d", g_snkScore > 9999 ? 9999 : g_snkScore);
  tft.setTextColor(C_ORANGE, C_BONE);
  tft.drawString(sc, 8, 28, 2);
  tft.setTextColor(C_HP, C_BONE);
  char lv[16];
  snprintf(lv, sizeof(lv), "Lv%d", g_snkLevel);
  tft.drawString(lv, 72, 32, 1);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  char hi[16];
  snprintf(hi, sizeof(hi), "best %u", g_snkHigh > 9999 ? 9999 : g_snkHigh);
  tft.drawRightString(hi, 176, 32, 1);
}

static void snkPad(int x, int y, char dir) {
  int cx = x + SNK_PAD / 2, cy = y + SNK_PAD / 2;
  tft.fillRoundRect(x, y, SNK_PAD, SNK_PAD, 8, C_BONE_LO);
  tft.drawRoundRect(x, y, SNK_PAD, SNK_PAD, 8, C_INK);
  switch (dir) {
    case 'U': tft.fillTriangle(cx - 8, cy + 5, cx + 8, cy + 5, cx, cy - 8, C_INK); break;
    case 'D': tft.fillTriangle(cx - 8, cy - 5, cx + 8, cy - 5, cx, cy + 8, C_INK); break;
    case 'L': tft.fillTriangle(cx + 5, cy - 8, cx + 5, cy + 8, cx - 8, cy, C_INK); break;
    case 'R': tft.fillTriangle(cx - 5, cy - 8, cx - 5, cy + 8, cx + 8, cy, C_INK); break;
  }
}
static void drawSnkDpad() {
  snkPad(SNK_PAD_CX, SNK_PAD_UY, 'U');
  snkPad(SNK_PAD_LX, SNK_PAD_MY, 'L');
  snkPad(SNK_PAD_RX, SNK_PAD_MY, 'R');
  snkPad(SNK_PAD_CX, SNK_PAD_DY, 'D');
}

// Full-screen draw: the opening frame, and any time the whole board must be
// rebuilt (replay, game-over card). The per-step path below repaints only the
// handful of cells that moved.
static void drawSnakePanel() {
  tft.fillScreen(C_BONE);
  // BACK top-left, title beside it at x64 — the y28..44 stat band below is
  // untouched, so every measured column fit in snkDrawScore still stands.
  drawBackChip(SNK_DONE_X, SNK_DONE_TY, SNK_DONE_W, SNK_DONE_H, "\x1B SHELF");
  tft.setTextColor(C_INK, C_BONE);
  tft.drawString("CARROT CHASE", 64, 8, 2);
  snkDrawScore();

  // play field frame — hand-drawn crayon border
  int fw = SNK_COLS * SNK_CELL, fh = SNK_ROWS * SNK_CELL;
  (void)fw; (void)fh;
  snkDrawBorder();

  for (int i = 0; i < g_snkNStones; i++) snkDrawStone(g_snkStones[i]);
  snkDrawCarrot(g_snkCarrot);
  for (int i = g_snkLen - 1; i >= 1; i--) snkDrawBody(g_snkBody[i]);
  snkDrawHead();

  drawSnkDpad();

  if (g_snkState == 1) {
    int cy = SNK_Y0 + fh / 2;
    tft.fillRoundRect(SNK_X0 + 6, cy - 34, fw - 12, 68, 8, C_PAPER);
    tft.drawRoundRect(SNK_X0 + 6, cy - 34, fw - 12, 68, 8, C_INK);
    tft.setTextColor(C_INK, C_PAPER);
    char over[32];
    snprintf(over, sizeof(over), "%d carrots!", g_snkScore);
    tft.drawCentreString(over, SNK_X0 + fw / 2, cy - 26, 2);
    tft.setTextColor(C_HP, C_PAPER);
    char lvl[24];
    snprintf(lvl, sizeof(lvl), "made it to level %d", g_snkLevel);
    tft.drawCentreString(lvl, SNK_X0 + fw / 2, cy - 4, 1);
    tft.setTextColor(C_INK_SOFT, C_PAPER);
    tft.drawCentreString("tap to play again", SNK_X0 + fw / 2, cy + 14, 1);
  }
}

static void snakeGameOver() {
  g_snkState = 1;
  if (g_snkScore > g_snkHigh) { g_snkHigh = g_snkScore; g_snkHighDirty = true; }
  sfxLose();
}

// Advance one cell; sets g_snkState on death, and records what changed so the
// caller can repaint just those cells.
static void snakeStep() {
  g_snkVacated = -1; g_snkAte = false;
  uint8_t oldTail = g_snkBody[g_snkLen - 1];
  g_snkDx = g_snkPendDx; g_snkDy = g_snkPendDy;
  if (g_snkQDx || g_snkQDy) {          // promote the queued second turn
    g_snkPendDx = g_snkQDx; g_snkPendDy = g_snkQDy;
    g_snkQDx = g_snkQDy = 0;
  }
  int hx = snkCX(g_snkBody[0]) + g_snkDx;
  int hy = snkCY(g_snkBody[0]) + g_snkDy;
  // Walls WRAP (Jon 8/12): out one side, in from the other — the burrow has
  // tunnels. Only stones and your own tail end the run.
  if (hx < 0) hx = SNK_COLS - 1;
  if (hx >= SNK_COLS) hx = 0;
  if (hy < 0) hy = SNK_ROWS - 1;
  if (hy >= SNK_ROWS) hy = 0;
  uint8_t nh = snkCell(hx, hy);
  bool eat = (nh == g_snkCarrot);
  if (snkOccupied(nh, eat ? 0 : 1) || snkStoneAt(nh)) { snakeGameOver(); return; }
  for (int i = (g_snkLen < SNK_MAX ? g_snkLen : g_snkLen - 1); i > 0; i--)
    g_snkBody[i] = g_snkBody[i - 1];
  g_snkBody[0] = nh;
  if (eat) {
    if (g_snkLen < SNK_MAX) g_snkLen++;
    g_snkEaten++;
    // SCORE PAYS BY LEVEL now (progression review 8/13, Snake II's rule:
    // the same food is worth more at higher speed). The every-5th carrot
    // is GOLDEN and pays triple — our take on the bonus bug.
    bool golden = (g_snkEaten % 5 == 0);
    g_snkScore += golden ? 3 * g_snkLevel : g_snkLevel;
    g_snkAte = true;
    if (g_snkScore > g_snkHigh) { g_snkHigh = g_snkScore; g_snkHighDirty = true; }
    // LEVEL UP every 5 carrots: faster; from level 3 a stone drops too
    // (opening retune 8/13 — the classic keeps its first minutes obstacle-free)
    if (golden) {
      g_snkLevel++;
      if (g_snkLevel >= 3) snkDropStone();
      sfxWin();                        // the level fanfare
    } else {
      sfxGrow();                       // rising sparkle — the bun got bigger
    }
    snkSpawnCarrot();
  } else {
    g_snkVacated = oldTail;
  }
}

// Step, then repaint only what moved. No full-screen clear -> no flash.
static void snkStepAndDraw() {
  uint8_t neck = g_snkBody[0];    // the cell about to become body[1]
  int lvlBefore = g_snkLevel;
  snakeStep();
  if (g_snkState == 1) { drawSnakePanel(); return; }   // game-over card needs the full draw
  if (g_snkLevel != lvlBefore) {
    // level-up WITHOUT the full-panel flash (panel: it broke this file's own
    // no-flash law) — the only things that changed are one stone + the header
    if (g_snkNStones > 0) snkDrawStone(g_snkStones[g_snkNStones - 1]);
    snkDrawScore();
  }
  snkEraseCell(g_snkVacated);     // clear the tail's old cell (no-op if we grew)
  snkEraseCell(neck);             // clear the old head, then paint it as body
  snkDrawBody(neck);
  snkDrawHead();
  if (g_snkAte) { snkDrawCarrot(g_snkCarrot); snkDrawScore(); }
  // Border insurance (Jon 8/12: "the border must not disappear"): re-stroke
  // one crayon edge per step, rotating — the frame continuously heals from
  // any stray erase without a visible redraw.
  static uint8_t edge = 0;
  int fw = SNK_COLS * SNK_CELL, fh = SNK_ROWS * SNK_CELL;
  int x0 = SNK_X0 - 5, y0 = SNK_Y0 - 5, x1 = SNK_X0 + fw + 4, y1 = SNK_Y0 + fh + 4;
  switch (edge++ & 3) {
    case 0: tttStroke(x0, y0, x1, y0, C_INK, 1, 401); break;
    case 1: tttStroke(x1, y0, x1, y1, C_INK, 1, 402); break;
    case 2: tttStroke(x1, y1, x0, y1, C_INK, 1, 403); break;
    case 3: tttStroke(x0, y1, x0, y0, C_INK, 1, 404); break;
  }
}

// Speed by LEVEL (Game Boy pacing): each level is one clear notch faster, and
// the pace holds steady within a level — learnable, not creeping. Level 1 is
// deliberately GENTLE (Jon 8/12: "too fast initially") — a little kid's first
// game should feel winnable; the squeeze arrives with the stones.
static uint32_t snkStepMs() {
  // opening retune 8/13: gentler first levels, same 110ms endgame — the
  // ceiling just arrives at level 12 instead of 9
  int ms = 450 - g_snkLevel * 30;
  return (uint32_t)(ms < 110 ? 110 : ms);
}

// Set a new heading from a button; never allow an instant 180 into yourself.
// Two-deep turn queue (panel 8/13): a fast U-then-L inside one step used to
// lose the first press — the classic "I pressed that!" death. The second
// press now waits its turn instead of overwriting the first. (Queue state
// g_snkQDx/QDy is declared up top with the rest of the snake state.)
static void snkSetDir(int ndx, int ndy) {
  if (ndx == g_snkPendDx && ndy == g_snkPendDy) return;   // no re-click chatter
  // first slot free? (pending == current heading means nothing queued yet)
  if (g_snkPendDx == g_snkDx && g_snkPendDy == g_snkDy) {
    if (ndx == -g_snkDx && ndy == -g_snkDy) return;       // no 180 into yourself
    g_snkPendDx = ndx; g_snkPendDy = ndy;
  } else {
    if (ndx == -g_snkPendDx && ndy == -g_snkPendDy) return; // no queued 180 either
    g_snkQDx = ndx; g_snkQDy = ndy;
  }
  sfxTick();                                              // a click per turn
}

// Which control did a tap land on? 'X' BACK, 'L' 'U' 'D' 'R', or 0 for none.
static char snkBtnHit(int tx, int ty) {
  if (tx < SNK_DONE_X + SNK_DONE_W + 12 && ty <= SNK_DONE_TY + SNK_DONE_H) return 'X';   // no slop into the score digits (review 8/13 #2)
  if (tx >= SNK_PAD_CX && tx <= SNK_PAD_CX + SNK_PAD) {
    if (ty >= SNK_PAD_UY && ty <= SNK_PAD_UY + SNK_PAD) return 'U';
    if (ty >= SNK_PAD_DY && ty <= SNK_PAD_DY + SNK_PAD) return 'D';
  }
  if (ty >= SNK_PAD_MY && ty <= SNK_PAD_MY + SNK_PAD) {
    if (tx >= SNK_PAD_LX && tx <= SNK_PAD_LX + SNK_PAD) return 'L';
    if (tx >= SNK_PAD_RX && tx <= SNK_PAD_RX + SNK_PAD) return 'R';
  }
  return 0;
}
