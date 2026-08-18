// Basket Bounce — bunbun breakout. A basket paddle at the bottom knocks a
// carrot up into rows of garden veg; clear the patch to win, drop the carrot
// three times and it's over. HOLD the left/right buttons to slide the basket
// (continuous while held, not one nudge per tap). Same paper-and-ink look as
// the other games, and the same rule Jon set 8/12: no full-screen refresh per
// frame — only the carrot, the basket sliver that moved, and any smashed veg
// get repainted.
//
// Included into main.cpp after palette, tft, sfx, prefs, esp_random are in
// scope. Loop dispatch + roster hook live in main.cpp beside the other games.
#pragma once

static bool g_bbPanel = false;

// GB-fine scale (Jon 8/12): 8x6 small bricks — an Arkanoid wall, not six fat
// slabs. 48 breakables (plus stones) per level.
static const int BB_COLS = 8, BB_ROWS = 6;
static const int BB_BW = 25, BB_BH = 9, BB_BGAP = 2;
static const int BB_BX0 = 13, BB_BY0 = 62;

// inner play bounds — kept far enough inside the drawn frame that the ball's
// erase rect (radius+3 px) can never reach the border line and eat it
static const float BB_L = 15, BB_R = 225, BB_T = 63;
// frame top at 52: the header band ends at 44, so score redraws can never
// touch the border again (that overlap was the lingering "border weirdness")
static const int BB_FRAME_X = 4, BB_FRAME_Y = 52, BB_FRAME_W = 232, BB_FRAME_H = 198;

static const int BB_PAD_Y = 236, BB_PAD_W = 44, BB_PAD_H = 8;
static const float BB_PAD_SPEED = 6.0f;
static const int BB_BALLR = 3;                 // carrot ball radius — GB-fine

// BACK, top-left (menu redesign P1 — every way out is the same corner).
static const int BB_DONE_X = 4, BB_DONE_TY = 4, BB_DONE_W = 52, BB_DONE_H = 22;   // ends y25: clear of the y28 stat band (review 8/13 #1)
static const int BB_BTN_Y = 258, BB_BTN_H = 54;
// L / PAUSE / R (Jon 8/12: "there needs to be a start / stop button")
static const int BB_BTN_LX = 11, BB_BTN_RX = 137, BB_BTN_W = 92;
static const int BB_BTN_PX = 107, BB_BTN_PW = 26;
static bool g_bbPaused = false;

// cell types: 0 empty, 1 veg (breakable), 2 stone (UNBREAKABLE — bounces)
static uint8_t g_bbBrick[BB_COLS * BB_ROWS];
static int g_bbLeft;                            // veg remaining (stones don't count)
static float g_bbPadX;                          // basket left edge
static float g_bbPrevPadX;
// MULTIBALL (Jon 8/12: "we need carrot splits"): up to three carrots in
// flight; a life is lost only when the LAST one drops — the Arkanoid rule.
#define BB_MAX_BALLS 3
struct BBBall { float x, y, vx, vy, px, py; bool on; };
static BBBall g_bbBall[BB_MAX_BALLS];
// legacy aliases: ball[0] is the primary (launch/stuck logic rides on it)
#define g_bbX (g_bbBall[0].x)
#define g_bbY (g_bbBall[0].y)
#define g_bbVX (g_bbBall[0].vx)
#define g_bbVY (g_bbBall[0].vy)
#define g_bbPrevX (g_bbBall[0].px)
#define g_bbPrevY (g_bbBall[0].py)
static int g_bbScore, g_bbLives;
static uint8_t g_bbState;                       // 0 run, 1 lost (2=won retired: levels loop)
static uint32_t g_bbStepAt, g_bbTouchMs, g_bbLaunchAt;
static bool g_bbStuck;                          // carrot resting on the basket pre-launch
static uint16_t g_bbHigh = 0;
static bool g_bbHighDirty = false;

// LEVELS (Jon 8/12, Alleyway as the baseline): each level is a different
// brick PATTERN; clearing it loads the next, a touch faster, and tops a life
// back up (max 4). Patterns cycle, speed keeps climbing — it ends when the
// carrots run out, like the parent game.
static int g_bbLevel;   // 1-based
#define BB_PATTERNS 8
// pattern(level, c, r) -> cell type: 0 empty, 1 veg, 2 stone, 3 ROOT VEG
// (progression review 8/13, Arkanoid's silver brick: takes 2 hits, pays
// double — appears from level 5). Stones from level 3 (was 2 — the review
// showed the original saves its permanent geometry for later rounds).
// 8 silhouettes now, Alleyway-style; with the 11-phase stone hash the
// content period is lcm(8,11) = 88 levels.
static uint8_t bbPatternCell(int level, int c, int r) {
  int pat = (level - 1) & 7;
  bool veg;
  switch (pat) {
    case 0: veg = true; break;                          // full wall
    case 1: veg = ((c + r) & 1) == 0; break;            // checkerboard
    case 2: veg = (c >= r && c < BB_COLS - r); break;   // pyramid
    case 3: veg = (c & 1) == 0 || (r & 1) == 0; break;  // lattice
    case 4: veg = ((c + r) % 3) != 2; break;            // diagonal furrows
    case 5: veg = (c < 2 || c >= BB_COLS - 2 || r < 2); break;  // raised beds
    case 6: veg = (r == 0 || c == 0 || c == BB_COLS - 1 ||
                   (r >= 2 && r <= 3 && c >= 3 && c <= 4)); break; // ring+core
    default: veg = ((c * 5 + r * 3) % 7) < 4; break;    // scatter patch
  }
  if (!veg) return 0;
  int h = (c * 7 + r * 5 + level * 3) % 11;
  if (level >= 3 && (h == 0 || (level >= 4 && h == 5))) return 2;
  if (level >= 5 && h == 7) return 3;
  return 1;
}

// SPECIALS (Jon 8/12): some veg hide a treat that FALLS when popped — catch
// it with the basket. W = wide basket (a while), S = slow carrot (a while),
// heart = +1 life. One in flight at a time keeps it readable for little kids.
static bool g_bbSpec;              // a special is falling
static char g_bbSpecType;          // 'W','S','H'
static float g_bbSpecX, g_bbSpecY, g_bbSpecPrevY;
static uint32_t g_bbWideUntil, g_bbSlowUntil;
// THE INTRA-ROUND RATCHET (progression review 8/13 — Arkanoid's real ramp
// lives INSIDE the round): every paddle return adds a sliver of speed;
// losing a carrot rewinds it, exactly the parent's relief valve.
static float g_bbRoundSp;          // 0 .. BB_RATCHET_MAX, added per return
#define BB_RATCHET_STEP 0.035f
#define BB_RATCHET_MAX  0.65f
// Wide-basket ending feedback (Jon 8/13: "the wide ended and I missed it —
// no feedback"): last ~2s the basket blinks + a warning slide, and the
// shrink itself erases the wings and sounds a descending beep.
static bool g_bbWideBlink, g_bbWideWarned;
static uint32_t g_bbWideBlinkAt;
static int g_bbLastPW;

static int bbPadW() { return (millis() < g_bbWideUntil) ? 72 : BB_PAD_W; }

static inline int bbBrickX(int c) { return BB_BX0 + c * (BB_BW + BB_BGAP); }
static inline int bbBrickY(int r) { return BB_BY0 + r * (BB_BH + BB_BGAP); }

static void bbFlushHigh() {
  if (!g_bbHighDirty) return;
  g_bbHighDirty = false;
  prefs.begin("bunbun", false);
  prefs.putUShort("bbhi", g_bbHigh);
  prefs.end();
}

static void bbPlaceOnPaddle() {
  for (int i = 1; i < BB_MAX_BALLS; i++) g_bbBall[i].on = false;
  g_bbBall[0].on = true;
  g_bbX = g_bbPadX + bbPadW() / 2;   // centre on the CURRENT width (wide included)
  g_bbY = BB_PAD_Y - BB_BALLR - 1;
  g_bbVX = 0; g_bbVY = 0;
  g_bbStuck = true;
  g_bbLaunchAt = millis() + 700;   // a breath, then it flies
  g_bbPrevX = g_bbX; g_bbPrevY = g_bbY;
}

// SPLIT: clone the first live carrot into up to two more, fanned out
static void bbSplit() {
  int src = -1;
  for (int i = 0; i < BB_MAX_BALLS; i++) if (g_bbBall[i].on) { src = i; break; }
  if (src < 0) return;
  for (int i = 0; i < BB_MAX_BALLS; i++) {
    if (g_bbBall[i].on) continue;
    g_bbBall[i] = g_bbBall[src];
    g_bbBall[i].vx = g_bbBall[src].vx + ((i & 1) ? 1.4f : -1.4f);
    if (g_bbBall[i].vy > -1.0f && g_bbBall[i].vy < 1.0f) g_bbBall[i].vy = -2.4f;
  }
}

// Lay out the bricks for a level; called at game start and on each clear.
static void bbLoadLevel(int level) {
  g_bbLevel = level;
  g_bbLeft = 0;
  for (int r = 0; r < BB_ROWS; r++)
    for (int c = 0; c < BB_COLS; c++) {
      uint8_t t = bbPatternCell(level, c, r);
      g_bbBrick[r * BB_COLS + c] = t;
      if (t == 1 || t == 3) g_bbLeft++;   // roots must break to clear too
    }
  g_bbSpec = false;
  g_bbRoundSp = 0.0f;   // the intra-round ratchet rewinds every level
  bbPlaceOnPaddle();
}

static void breakoutReset() {
  g_bbPaused = false;
  g_bbScore = 0; g_bbLives = 3; g_bbState = 0;
  g_bbPadX = (240 - BB_PAD_W) / 2; g_bbPrevPadX = g_bbPadX;
  g_bbWideUntil = 0; g_bbWideWarned = false; g_bbWideBlink = false;
  g_bbLastPW = 0;
  g_bbTouchMs = millis();
  g_bbStepAt = millis() + 300;
  bbLoadLevel(1);
}

// --- drawing primitives, each self-contained so incremental repaint is clean ---

static void bbDrawBrick(int c, int r) {
  int x = bbBrickX(c), y = bbBrickY(r);
  uint8_t t = g_bbBrick[r * BB_COLS + c];
  if (t == 2) {
    // STONE: an unbreakable garden rock — grey, crayon-outlined, no stalk
    tft.fillRoundRect(x, y, BB_BW, BB_BH, 3, C_INK_SOFT);
    tft.drawRoundRect(x, y, BB_BW, BB_BH, 3, C_INK);
    tft.drawPixel(x + 5, y + 4, C_BONE_LO);
    tft.drawPixel(x + 6, y + 4, C_BONE_LO);
    return;
  }
  if (t == 3) {
    // ROOT VEG: takes two hits (the silver-brick analogue). Reads as a
    // stubborn beet — deep tone, crayon edge, double stalk.
    tft.fillRoundRect(x, y, BB_BW, BB_BH, 4, C_ORANGE_LO);
    tft.drawRoundRect(x, y, BB_BW, BB_BH, 4, C_INK);
    tft.drawFastVLine(x + BB_BW / 2 - 2, y - 2, 2, C_HP);
    tft.drawFastVLine(x + BB_BW / 2 + 2, y - 2, 2, C_HP);
    return;
  }
  uint16_t col = (r & 1) ? C_HP : C_ORANGE;       // alternating veg rows
  uint16_t edge = (r & 1) ? C_INK : C_ORANGE_LO;
  tft.fillRoundRect(x, y, BB_BW, BB_BH, 4, col);
  tft.drawRoundRect(x, y, BB_BW, BB_BH, 4, edge);
  // a little stalk so it reads as veg, not just a block
  tft.drawFastVLine(x + BB_BW / 2, y - 2, 2, C_HP);
}

// Repaint any bricks a just-erased rect overlapped (Jon 8/12: "sometimes the
// blocks get clipped off by the ball" — the ball's and the falling special's
// erase rects were shaving pixels off neighbouring bricks).
static void bbRepairBricks(int x, int y, int w, int h) {
  int c0 = (x - BB_BX0) / (BB_BW + BB_BGAP) - 1;
  int c1 = (x + w - BB_BX0) / (BB_BW + BB_BGAP) + 1;
  int r0 = (y - BB_BY0) / (BB_BH + BB_BGAP) - 1;
  int r1 = (y + h - BB_BY0) / (BB_BH + BB_BGAP) + 1;
  if (c0 < 0) c0 = 0;
  if (r0 < 0) r0 = 0;
  if (c1 >= BB_COLS) c1 = BB_COLS - 1;
  if (r1 >= BB_ROWS) r1 = BB_ROWS - 1;
  for (int r = r0; r <= r1; r++)
    for (int c = c0; c <= c1; c++)
      if (g_bbBrick[r * BB_COLS + c]) bbDrawBrick(c, r);
}

// Clip an erase rect to the field INTERIOR so it can never eat the border
// (Jon 8/12: "still having border clipping issues" — erases reached 1-2px
// past the frame and out-ate the heal).
static void bbClipErase(int x, int y, int w, int h) {
  int x0 = BB_FRAME_X + 4, y0 = BB_FRAME_Y + 4;
  int x1 = BB_FRAME_X + BB_FRAME_W - 4, y1 = BB_FRAME_Y + BB_FRAME_H - 4;
  if (x < x0) { w -= (x0 - x); x = x0; }
  if (y < y0) { h -= (y0 - y); y = y0; }
  if (x + w > x1) w = x1 - x;
  if (y + h > y1) h = y1 - y;
  if (w > 0 && h > 0) tft.fillRect(x, y, w, h, C_BONE);
}

static void bbEraseBrick(int c, int r) {
  tft.fillRect(bbBrickX(c), bbBrickY(r) - 2, BB_BW, BB_BH + 2, C_BONE);
}

static void bbDrawPaddle() {
  int x = (int)g_bbPadX, w = bbPadW();
  // the real basket art (games/basket, pre-sized 44x12 in the pak so it
  // blits 1:1 — no block sampling, no darkening). ONLY at normal width:
  // the W powerup keeps the drawn wide basket so the state reads at a
  // glance and the sprite is never stretched. Clip rows match the slide
  // erase band EXACTLY (8/13 trail bug).
  if (w == BB_PAD_W && (spriteLoad("games/basket") || spriteLoad("items/basket"))) {
    gameSpriteBlit(x + w / 2, BB_PAD_Y + BB_PAD_H / 2, 1.0f,
                   BB_FRAME_X + 4, BB_PAD_Y - 2,
                   BB_FRAME_X + BB_FRAME_W - 4, BB_PAD_Y + BB_PAD_H + 3,
                   false, C_BONE);
    return;
  }
  // wide (drawn) basket blinks orange in its last two seconds
  uint16_t fill = g_bbWideBlink ? C_ORANGE : C_BONE_EDGE;
  tft.fillRoundRect(x, BB_PAD_Y, w, BB_PAD_H, 4, fill);
  tft.drawRoundRect(x, BB_PAD_Y, w, BB_PAD_H, 4, C_INK);
  // woven-basket hint: two short verticals
  tft.drawFastVLine(x + w / 3, BB_PAD_Y + 2, BB_PAD_H - 4, C_INK);
  tft.drawFastVLine(x + 2 * w / 3, BB_PAD_Y + 2, BB_PAD_H - 4, C_INK);
}

static void bbDrawOneBall(int b) {
  if (!g_bbBall[b].on) return;
  int x = (int)g_bbBall[b].x, y = (int)g_bbBall[b].y;
  tft.fillCircle(x, y, BB_BALLR, C_ORANGE);
  tft.drawCircle(x, y, BB_BALLR, C_ORANGE_LO);
  tft.drawFastVLine(x, y - BB_BALLR - 2, 2, C_HP);   // tiny green top
}
static void bbDrawBall() {                            // all live carrots
  for (int b = 0; b < BB_MAX_BALLS; b++) bbDrawOneBall(b);
}

static void bbEraseBall(float px, float py) {
  tft.fillRect((int)px - BB_BALLR - 1, (int)py - BB_BALLR - 3,
               BB_BALLR * 2 + 3, BB_BALLR * 2 + 4, C_BONE);
}

// Crayon border, self-healing (Jon: "the game border doesn't disappear").
static void bbDrawBorder() {
  int x0 = BB_FRAME_X, y0 = BB_FRAME_Y, x1 = BB_FRAME_X + BB_FRAME_W,
      y1 = BB_FRAME_Y + BB_FRAME_H;
  tttStroke(x0, y0, x1, y0, C_INK, 1, 411);
  tttStroke(x1, y0, x1, y1, C_INK, 1, 412);
  tttStroke(x1, y1, x0, y1, C_INK, 1, 413);
  tttStroke(x0, y1, x0, y0, C_INK, 1, 414);
}

// Header band: y28..44 ONLY — it ends 4px above the frame (which starts at
// 52, wobble reaches 50), so a score redraw can never nick the border. Fixed
// columns: score 8..66 | level 70..110 | lives 114..152 | best right at 176.
static void bbDrawScore() {
  tft.fillRect(8, 28, 168, 16, C_BONE);
  char sc[16];
  snprintf(sc, sizeof(sc), "%d", g_bbScore > 9999 ? 9999 : g_bbScore);
  tft.setTextColor(C_ORANGE, C_BONE);
  tft.drawString(sc, 8, 28, 2);
  tft.setTextColor(C_HP, C_BONE);
  char lv[16];
  snprintf(lv, sizeof(lv), "Lv%d", g_bbLevel);
  tft.drawString(lv, 72, 32, 1);
  // lives: ONE carrot icon + a count at x100-122; "hi N" right-aligned from
  // ~134 — measured columns, no collision at any score (Jon 8/12, twice)
  tft.fillTriangle(100, 31, 106, 31, 103, 41, C_ORANGE);
  tft.setTextColor(C_INK, C_BONE);
  char lc[8];
  snprintf(lc, sizeof(lc), "x%d", g_bbLives);
  tft.drawString(lc, 110, 32, 1);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  char hi[16];
  snprintf(hi, sizeof(hi), "hi %u", g_bbHigh > 9999 ? 9999 : g_bbHigh);
  tft.drawRightString(hi, 176, 32, 1);
}

static void bbButton(int x, const char *lbl, bool left) {
  int cx = x + BB_BTN_W / 2, cy = BB_BTN_Y + BB_BTN_H / 2;
  tft.fillRoundRect(x, BB_BTN_Y, BB_BTN_W, BB_BTN_H, 7, C_BONE_LO);
  tft.drawRoundRect(x, BB_BTN_Y, BB_BTN_W, BB_BTN_H, 7, C_INK);
  if (left) tft.fillTriangle(cx + 10, cy - 11, cx + 10, cy + 11, cx - 12, cy, C_INK);
  else      tft.fillTriangle(cx - 10, cy - 11, cx - 10, cy + 11, cx + 12, cy, C_INK);
}

// the middle start/stop: || while running, a play-triangle while paused
static void bbPauseButton() {
  int cx = BB_BTN_PX + BB_BTN_PW / 2, cy = BB_BTN_Y + BB_BTN_H / 2;
  tft.fillRoundRect(BB_BTN_PX, BB_BTN_Y, BB_BTN_PW, BB_BTN_H, 7,
                    g_bbPaused ? C_ORANGE : C_BONE_LO);
  tft.drawRoundRect(BB_BTN_PX, BB_BTN_Y, BB_BTN_PW, BB_BTN_H, 7, C_INK);
  if (g_bbPaused) {
    tft.fillTriangle(cx - 4, cy - 7, cx - 4, cy + 7, cx + 6, cy, C_PAPER);
  } else {
    tft.fillRect(cx - 5, cy - 7, 3, 14, C_INK);
    tft.fillRect(cx + 2, cy - 7, 3, 14, C_INK);
  }
}

static void drawBreakoutPanel() {
  tft.fillScreen(C_BONE);
  // BACK top-left, title at x64 — the y28..44 stat band keeps its measured
  // columns untouched.
  drawBackChip(BB_DONE_X, BB_DONE_TY, BB_DONE_W, BB_DONE_H, "\x1B SHELF");
  tft.setTextColor(C_INK, C_BONE);
  tft.drawString("BASKET BOUNCE", 64, 8, 2);
  bbDrawScore();

  bbDrawBorder();

  for (int r = 0; r < BB_ROWS; r++)
    for (int c = 0; c < BB_COLS; c++)
      if (g_bbBrick[r * BB_COLS + c]) bbDrawBrick(c, r);

  bbDrawPaddle();
  bbDrawBall();
  bbButton(BB_BTN_LX, "L", true);
  bbButton(BB_BTN_RX, "R", false);
  bbPauseButton();

  if (g_bbState != 0) {
    int cy = 150;
    tft.fillRoundRect(28, cy - 34, 184, 68, 8, C_PAPER);
    tft.drawRoundRect(28, cy - 34, 184, 68, 8, C_INK);
    tft.setTextColor(C_INK, C_PAPER);
    tft.drawCentreString("carrot lost!", 120, cy - 26, 2);
    tft.setTextColor(C_HP, C_PAPER);
    char lvl[28];
    snprintf(lvl, sizeof(lvl), "reached level %d", g_bbLevel);
    tft.drawCentreString(lvl, 120, cy - 4, 1);
    tft.setTextColor(C_INK_SOFT, C_PAPER);
    tft.drawCentreString("tap to play again", 120, cy + 14, 1);
  }
}

static void breakoutOver(bool won) {
  g_bbState = won ? 2 : 1;
  if (g_bbScore > g_bbHigh) { g_bbHigh = g_bbScore; g_bbHighDirty = true; }
  if (won) sfxWin(); else sfxLose();
}

// brick index under a point, or -1
static int bbBrickAt(float x, float y) {
  if (y < BB_BY0 || y >= BB_BY0 + BB_ROWS * (BB_BH + BB_BGAP)) return -1;
  int r = (int)((y - BB_BY0) / (BB_BH + BB_BGAP));
  int c = (int)((x - BB_BX0) / (BB_BW + BB_BGAP));
  if (r < 0 || r >= BB_ROWS || c < 0 || c >= BB_COLS) return -1;
  // reject the gap between bricks
  if (x < bbBrickX(c) || x > bbBrickX(c) + BB_BW) return -1;
  int idx = r * BB_COLS + c;
  return g_bbBrick[idx] ? idx : -1;
}

// One physics tick. heldDir: -1 left, +1 right, 0 none. Repaints incrementally.
static void breakoutStep(int heldDir) {
  // paddle glides while a button is held (kept 4px clear of the crayon frame)
  g_bbPrevPadX = g_bbPadX;
  if (heldDir) {
    g_bbPadX += heldDir * BB_PAD_SPEED;
    if (g_bbPadX < BB_FRAME_X + 5) g_bbPadX = BB_FRAME_X + 5;
    if (g_bbPadX > BB_FRAME_X + BB_FRAME_W - bbPadW() - 5)
      g_bbPadX = BB_FRAME_X + BB_FRAME_W - bbPadW() - 5;
  }

  // wide-basket lifecycle feedback (Jon 8/13): warn, blink, then shrink LOUDLY
  {
    int pw = bbPadW();
    if (g_bbLastPW && pw != g_bbLastPW) {
      // width changed: wipe the whole band so the old wings can't linger
      bbClipErase(BB_FRAME_X + 4, BB_PAD_Y - 2, BB_FRAME_W - 8, BB_PAD_H + 6);
      bbDrawPaddle();
      if (pw < g_bbLastPW) {                       // the shrink moment
        beep(520, 0.06f, SFX_SQUARE, 0.05f);
        beep(340, 0.08f, SFX_SQUARE, 0.05f);
      }
    }
    g_bbLastPW = pw;
    if (millis() < g_bbWideUntil) {
      uint32_t leftMs = g_bbWideUntil - millis();
      if (leftMs < 2200) {
        if (!g_bbWideWarned) {
          g_bbWideWarned = true;
          beep(760, 0.05f, SFX_TRIANGLE, 0.05f);   // heads-up chirp
        }
        if (millis() >= g_bbWideBlinkAt) {
          g_bbWideBlinkAt = millis() + 220;
          g_bbWideBlink = !g_bbWideBlink;
          bbDrawPaddle();
        }
      }
    } else if (g_bbWideWarned || g_bbWideBlink) {
      g_bbWideWarned = false; g_bbWideBlink = false;
    }
  }

  g_bbPrevX = g_bbX; g_bbPrevY = g_bbY;

  if (g_bbStuck) {
    g_bbX = g_bbPadX + bbPadW() / 2;          // ride the basket until launch
    if (millis() >= g_bbLaunchAt) {
      g_bbStuck = false;
      // speed climbs with level (Alleyway pacing), capped where it stays
      // returnable on a 60ms-frame screen. L1 serves soft (opening retune
      // 8/13 — the original's first round is a disguised tutorial).
      float sp = 0.85f + 0.15f * (g_bbLevel - 1);
      if (sp > 1.9f) sp = 1.9f;         // panel: keep levels 6-12 biting
      // serve direction random (panel: identical serves made the first five
      // seconds of every life a memorized non-event)
      g_bbVX = ((esp_random() & 1) ? 1.7f : -1.7f) * sp;
      g_bbVY = -2.9f * sp;
      sfxTick();                        // launch blip
    }
  } else {
    // MULTIBALL physics: every live carrot moves, bounces, breaks, drops
    for (int b = 0; b < BB_MAX_BALLS; b++) {
      BBBall *bl = &g_bbBall[b];
      if (!bl->on) continue;
      bl->px = bl->x; bl->py = bl->y;
      bl->x += bl->vx; bl->y += bl->vy;
      // walls
      if (bl->x < BB_L) { bl->x = BB_L; bl->vx = -bl->vx; }
      if (bl->x > BB_R) { bl->x = BB_R; bl->vx = -bl->vx; }
      if (bl->y < BB_T) { bl->y = BB_T; bl->vy = -bl->vy; }
      // brick hit (sample the leading edge)
      int hit = bbBrickAt(bl->x, bl->y - (bl->vy < 0 ? BB_BALLR : -BB_BALLR));
      if (hit >= 0) {
        if (g_bbBrick[hit] == 2) {
          bl->vy = -bl->vy;                 // STONE: dull thunk, no damage
          beep(180, 0.05f, SFX_SQUARE, 0.05f);
        } else if (g_bbBrick[hit] == 3) {
          // ROOT VEG first hit: cracks down to normal veg, pays a little
          g_bbBrick[hit] = 1;
          bbDrawBrick(hit % BB_COLS, hit / BB_COLS);
          bl->vy = -bl->vy;
          g_bbScore += 5 * g_bbLevel;
          beep(260, 0.05f, SFX_SQUARE, 0.05f);   // firmer than the veg pop
          bbDrawScore();
        } else {
          g_bbBrick[hit] = 0; g_bbLeft--;
          int hr = hit / BB_COLS, hc = hit % BB_COLS;
          bbEraseBrick(hc, hr);
          bl->vy = -bl->vy;
          // back rows pay a premium, like the parent's color tiers
          g_bbScore += ((hr < 2) ? 9 : (hr < 4) ? 7 : 5) * g_bbLevel;
          sfxOK();
          bbDrawScore();
          if (!g_bbSpec && (esp_random() % 100) < 15) {
            g_bbSpec = true;
            uint32_t k = esp_random() % 10;
            g_bbSpecType = (k < 4) ? 'W' : (k < 8) ? 'S' : 'H';
            g_bbSpecX = bbBrickX(hc) + BB_BW / 2;
            g_bbSpecY = bbBrickY(hr) + BB_BH;
            g_bbSpecPrevY = g_bbSpecY;
          }
          if (g_bbLeft == 0) {              // LEVEL CLEAR: a proper ceremony
            if (g_bbScore > g_bbHigh) { g_bbHigh = g_bbScore; g_bbHighDirty = true; }
            if (g_bbLives < 4) g_bbLives++;
            sfxWin();
            gameSparkles(BB_FRAME_X + 8, BB_FRAME_Y + 8,
                         BB_FRAME_W - 16, BB_FRAME_H - 16, 600 + g_bbLevel);
            delay(450);                     // the beat the clear deserves
            bbLoadLevel(g_bbLevel + 1);
            drawBreakoutPanel();
            return;
          }
        }
      }
      // paddle: THREE bounce zones like the parent game (outer third = wide
      // angle, middle = medium, centre = steep) — aimable, learnable
      if (bl->vy > 0 && bl->y + BB_BALLR >= BB_PAD_Y &&
          bl->y + BB_BALLR <= BB_PAD_Y + BB_PAD_H + 2 &&
          bl->x >= g_bbPadX - 2 && bl->x <= g_bbPadX + bbPadW() + 2) {
        bl->y = BB_PAD_Y - BB_BALLR - 1;
        float off = (bl->x - (g_bbPadX + bbPadW() / 2)) / (bbPadW() / 2);  // -1..1
        float mag = (off < 0) ? -off : off;
        float ang = (mag > 0.66f) ? 3.0f : (mag > 0.33f) ? 1.9f : 0.8f;
        // Progression fix 8/13: the old code OVERWROTE vx with the zone
        // constant, silently discarding the level ramp on the first return.
        // Now the zone picks the ANGLE and the level+ratchet pick the SPEED.
        g_bbRoundSp += BB_RATCHET_STEP;
        if (g_bbRoundSp > BB_RATCHET_MAX) g_bbRoundSp = BB_RATCHET_MAX;
        // same family as the serve (review 8/13: mismatched coefficients
        // gave L1 a +19% jolt on the very first return)
        float base = 0.85f + 0.15f * (g_bbLevel - 1);
        if (base > 1.9f) base = 1.9f;
        float target = 3.36f * base + g_bbRoundSp;   // 3.36 = launch magnitude
        float ux = (off < 0 ? -ang : ang), uy = -2.9f;
        float ul = sqrtf(ux * ux + uy * uy);
        bl->vx = ux / ul * target;
        bl->vy = uy / ul * target;
        sfxTick();
      }
      // dropped past the basket: this carrot is gone; the LIFE only goes
      // when the last carrot follows it. Erase its last image FIRST (review
      // 8/13: the repaint loop skips off balls, so a dropped-mid-multiball
      // carrot left a permanent ghost; the border heal covers the nick).
      if (bl->y > BB_FRAME_Y + BB_FRAME_H) {
        bbEraseBall(bl->px, bl->py);
        // the unclipped erase just crossed the bottom frame: re-stroke it NOW
        // with the same jitter id so it's pixel-identical (UI-dev audit A3 —
        // every missed carrot was a ~1s notch until the rotating heal arrived)
        tttStroke(BB_FRAME_X, BB_FRAME_Y + BB_FRAME_H,
                  BB_FRAME_X + BB_FRAME_W, BB_FRAME_Y + BB_FRAME_H, C_INK, 1, 413);
        bl->on = false;
        bool any = false;
        for (int j = 0; j < BB_MAX_BALLS; j++) if (g_bbBall[j].on) any = true;
        if (!any) {
          g_bbLives--;
          g_bbRoundSp = 0.0f;   // death rewinds the ratchet — the parent's relief valve
          if (g_bbLives <= 0) { breakoutOver(false); drawBreakoutPanel(); return; }
          sfxLose();
          bbPlaceOnPaddle();
          drawBreakoutPanel();
          return;
        }
      }
    }
  }

  // SPECIALS in flight: fall, get caught, or hit the soil
  if (g_bbSpec) {
    g_bbSpecPrevY = g_bbSpecY;
    g_bbSpecY += 1.6f;
    bbClipErase((int)g_bbSpecX - 8, (int)g_bbSpecPrevY - 8, 17, 18);
    bbRepairBricks((int)g_bbSpecX - 8, (int)g_bbSpecPrevY - 8, 17, 18);
    bool caught = (g_bbSpecY + 6 >= BB_PAD_Y && g_bbSpecY <= BB_PAD_Y + BB_PAD_H &&
                   g_bbSpecX >= g_bbPadX - 4 && g_bbSpecX <= g_bbPadX + bbPadW() + 4);
    if (caught) {
      g_bbSpec = false;
      sfxGrow();
      if (g_bbSpecType == 'W') {   // panel: 15s deleted the difficulty
        g_bbWideUntil = millis() + 9000;
        g_bbWideWarned = false; g_bbWideBlink = false;   // fresh countdown
        bbDrawPaddle();
      }
      else if (g_bbSpecType == 'H') { if (g_bbLives < 4) g_bbLives++; bbDrawScore(); }
      else {                        // 'S': the carrot SPLITS — multiball!
        bbSplit();
        sfxWin();                   // the big one deserves the fanfare
      }
    } else if (g_bbSpecY > BB_PAD_Y + 2) {
      // missed — gone BEFORE it enters the border margin, so its clipped
      // erase always covers its full footprint (no ghost pixels at the frame)
      g_bbSpec = false;
    } else {
      // draw the falling treat: a little paper tile
      int sx = (int)g_bbSpecX, sy = (int)g_bbSpecY;
      tft.fillRoundRect(sx - 7, sy - 7, 15, 15, 4, C_PAPER);
      tft.drawRoundRect(sx - 7, sy - 7, 15, 15, 4, C_INK);
      if (g_bbSpecType == 'H') {    // a tiny heart
        tft.fillCircle(sx - 2, sy - 2, 2, C_LOW);
        tft.fillCircle(sx + 2, sy - 2, 2, C_LOW);
        tft.fillTriangle(sx - 4, sy - 1, sx + 4, sy - 1, sx, sy + 4, C_LOW);
      } else {
        tft.setTextColor(C_INK, C_PAPER);
        char t[2] = {g_bbSpecType, 0};
        tft.drawCentreString(t, sx, sy - 4, 1);
      }
    }
  }
  // (wide expiry is handled up top by the g_bbLastPW width-transition wipe —
  //  the old static-bool block here double-handled it and its function-static
  //  survived quit/restart, review 8/13 NIT 5)
  // ('S' is the SPLIT now — the slow-carrot effect and its expiry are gone)

  // Border insurance: re-stroke one crayon edge every ~quarter second so no
  // erase can leave the field frameless (Jon 8/12).
  static uint8_t healTick = 0, healEdge = 0;
  if (++healTick >= 16) {
    healTick = 0;
    int x0 = BB_FRAME_X, y0 = BB_FRAME_Y, x1 = BB_FRAME_X + BB_FRAME_W,
        y1 = BB_FRAME_Y + BB_FRAME_H;
    switch (healEdge++ & 3) {
      case 0: tttStroke(x0, y0, x1, y0, C_INK, 1, 411); break;
      case 1: tttStroke(x1, y0, x1, y1, C_INK, 1, 412); break;
      case 2: tttStroke(x1, y1, x0, y1, C_INK, 1, 413); break;
      case 3: tttStroke(x0, y1, x0, y0, C_INK, 1, 414); break;
    }
  }

  // incremental repaint: erase every live carrot's old spot, fix the paddle,
  // draw all carrots — every erase CLIPPED to the field interior
  bool nearPad = false;
  for (int b = 0; b < BB_MAX_BALLS; b++) {
    if (!g_bbBall[b].on) continue;
    int ex = (int)g_bbBall[b].px - BB_BALLR - 1, ey = (int)g_bbBall[b].py - BB_BALLR - 3;
    int ew = BB_BALLR * 2 + 3, eh = BB_BALLR * 2 + 4;
    bbClipErase(ex, ey, ew, eh);
    bbRepairBricks(ex, ey, ew, eh);   // heal any brick the erase shaved
    if (g_bbBall[b].py + BB_BALLR + 2 >= BB_PAD_Y) nearPad = true;
  }
  if ((int)g_bbPadX != (int)g_bbPrevPadX) {
    // erase the sliver the basket vacated, then redraw it — band matches
    // the sprite clip in bbDrawPaddle (PAD_Y-2 .. PAD_Y+PAD_H+3)
    int lo = (int)fminf(g_bbPadX, g_bbPrevPadX) - 1;
    int hi = (int)fmaxf(g_bbPadX, g_bbPrevPadX) + bbPadW() + 1;
    bbClipErase(lo, BB_PAD_Y - 2, hi - lo, BB_PAD_H + 6);
    bbDrawPaddle();
  } else if (nearPad) {
    bbDrawPaddle();   // a carrot was over the basket; erase may have nicked it
  }
  bbDrawBall();
}

static uint32_t bbStepMs() { return 16; }

// 'X' BACK, 'L'/'R' arrows, 'P' pause, 0 none
static char bbBtnHit(int tx, int ty) {
  if (tx < BB_DONE_X + BB_DONE_W + 12 && ty <= BB_DONE_TY + BB_DONE_H) return 'X';   // no slop into the score digits (review 8/13 #2)
  if (ty >= BB_BTN_Y && ty <= BB_BTN_Y + BB_BTN_H) {
    if (tx >= BB_BTN_LX && tx <= BB_BTN_LX + BB_BTN_W) return 'L';
    if (tx >= BB_BTN_RX && tx <= BB_BTN_RX + BB_BTN_W) return 'R';
    if (tx >= BB_BTN_PX && tx <= BB_BTN_PX + BB_BTN_PW) return 'P';
  }
  return 0;
}
