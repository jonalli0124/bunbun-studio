// Garden Guard — bunbun space-invaders. A bunny at the bottom of the patch
// defends it from a marching row of garden pests (crows). HOLD left/right to
// slide, tap FIRE to fling a seed up. Clear the flock to win; let one reach the
// ground and it's over. Same paper-and-ink look, same 8/12 rule: no per-frame
// full-screen clear — the seed, the moved slice of the bunny, and any pest that
// changes are all that repaint.
//
// Included into main.cpp after palette, tft, sfx, prefs, esp_random are in
// scope. Loop dispatch + roster hook live in main.cpp beside the other games.
#pragma once

static bool g_ggPanel = false;

// GB-fine scale (Jon 8/12): an 8x4 flock of small crows — a formation that
// reads like the parent game, not six fat birds.
static const int GG_COLS = 8, GG_ROWS = 4;
static const int GG_CELLW = 25, GG_CELLH = 16;
static const int GG_PW = 15, GG_PH = 10;         // drawn pest size within the cell
static const int GG_FX = 12, GG_FY = 62;         // formation home (top-left cell origin)

static const int GG_FRAME_X = 4, GG_FRAME_Y = 52, GG_FRAME_W = 232, GG_FRAME_H = 198;
static const int GG_BUN_Y = 228, GG_BUN_W = 22, GG_BUN_H = 14;
static const float GG_BUN_SPEED = 5.0f;
static const int GG_GROUND = GG_FRAME_Y + GG_FRAME_H - 6;   // pests must not cross this

// BACK, top-left (menu redesign P1 — every way out is the same corner).
static const int GG_DONE_X = 4, GG_DONE_TY = 4, GG_DONE_W = 52, GG_DONE_H = 22;   // ends y25: clear of the y28 stat band (review 8/13 #1)
static const int GG_BTN_Y = 258, GG_BTN_H = 54;
static const int GG_BTN_LX = 8, GG_BTN_FX = 84, GG_BTN_RX = 168;
static const int GG_BTN_LW = 72, GG_BTN_FW = 72, GG_BTN_RW = 64;

static bool g_ggAlive[GG_COLS * GG_ROWS];
static int g_ggLeft;
static int g_ggOffX, g_ggOffY;      // formation offset from home, px
static int g_ggPrevOffX, g_ggPrevOffY;
static int g_ggDir;                 // +1 right, -1 left
static float g_ggBunX, g_ggPrevBunX;
static bool g_ggBullet;             // a seed is in flight
static float g_ggBX, g_ggBY, g_ggPrevBX, g_ggPrevBY;
static int g_ggScore;
static uint8_t g_ggState;           // 0 run, 1 lost (waves loop; no terminal win)
static uint32_t g_ggStepAt, g_ggMarchAt, g_ggTouchMs;
static uint16_t g_ggHigh = 0;
static bool g_ggHighDirty = false;

// WAVES + RETURN FIRE (Jon 8/12, Space Invaders parity): the crows CAW BACK —
// they drop acorns the bunny must dodge. Three hearts; a hit costs one, with
// a moment of blinking grace. Clearing the flock brings the next wave, lower
// and faster, exactly like the parent game.
static int g_ggWave;                // 1-based
static int g_ggHearts;              // bunny lives
static uint8_t g_ggDeathBy = 0;     // 0 = crows landed, 1 = out of hearts
#define GG_MAX_ACORNS 3
static bool g_ggAcorn[GG_MAX_ACORNS];
static float g_ggAX[GG_MAX_ACORNS], g_ggAY[GG_MAX_ACORNS];
static float g_ggPrevAX[GG_MAX_ACORNS], g_ggPrevAY[GG_MAX_ACORNS];
static uint32_t g_ggGraceUntil;     // blinking grace after a hit
static uint32_t g_ggNextAcornAt;
// THE MAGPIE (progression review 8/13 — Space Invaders' UFO, the one
// risk/reward layer the original had that we lacked): every so often a
// black-and-white bird crosses the strip above the flock; shoot it for a
// mystery bonus. Spending your only seed on it while acorns fall IS the
// gamble, same as the parent.
static bool g_ggMag;
static float g_ggMagX, g_ggMagPrevX;
static int g_ggMagDir;
static uint32_t g_ggMagNextAt;
static const int GG_MAG_Y = GG_FRAME_Y + 4;   // strip above the formation home

static inline int ggPestX(int c, int offx) { return GG_FX + offx + c * GG_CELLW; }
static inline int ggPestY(int r, int offy) { return GG_FY + offy + r * GG_CELLH; }

static void ggFlushHigh() {
  if (!g_ggHighDirty) return;
  g_ggHighDirty = false;
  prefs.begin("bunbun", false);
  prefs.putUShort("gghi", g_ggHigh);
  prefs.end();
}

// Start a wave: fresh flock, starting a bit lower and marching sooner each
// wave. Called at game start (wave 1) and after each cleared flock.
static void ggLoadWave(int wave) {
  g_ggWave = wave;
  for (int i = 0; i < GG_COLS * GG_ROWS; i++) g_ggAlive[i] = true;
  g_ggLeft = GG_COLS * GG_ROWS;
  int drop = (wave - 1) * 8;                     // each wave starts lower...
  if (drop > 48) drop = 48;                      // ...but never unwinnably low
  g_ggOffX = g_ggPrevOffX = 0; g_ggOffY = g_ggPrevOffY = drop;
  g_ggDir = 1;
  g_ggBullet = false;
  for (int i = 0; i < GG_MAX_ACORNS; i++) g_ggAcorn[i] = false;
  g_ggMarchAt = millis() + 700;
  // opening retune (8/13): wave 1 breathes like the original's first rack —
  // acorns arrive later and slower early, catching today's heat by wave 4
  g_ggNextAcornAt = millis() + (wave == 1 ? 2500 : 1500);
  g_ggMag = false;
  g_ggMagNextAt = millis() + 12000 + (esp_random() % 12000);
}

static void gardenReset() {
  g_ggBunX = (240 - GG_BUN_W) / 2; g_ggPrevBunX = g_ggBunX;
  g_ggScore = 0; g_ggState = 0;
  g_ggHearts = 3;
  g_ggGraceUntil = 0;
  g_ggTouchMs = millis();
  g_ggStepAt = millis() + 300;
  ggLoadWave(1);
}

// --- drawing primitives ---

static void ggDrawPest(int c, int r, int offx, int offy) {
  int x = ggPestX(c, offx), y = ggPestY(r, offy);
  // the REAL crow (games/crow, 8/13) — fit inside the 15x10 pest box so the
  // march erase band still covers it exactly; scale by the tighter axis
  if (spriteLoad("games/crow")) {
    float sx = (float)GG_PW / g_meta.w, sy = (float)GG_PH / g_meta.h;
    gameSpriteBlit(x + GG_PW / 2, y + GG_PH / 2, sx < sy ? sx : sy,
                   x - 2, y, x + GG_PW + 1, y + GG_PH - 1, false, C_BONE);
    return;
  }
  tft.fillRoundRect(x, y, GG_PW, GG_PH, 3, C_INK);
  // two pale eyes so it reads as a critter facing you
  tft.fillCircle(x + 4, y + 4, 1, C_PAPER);
  tft.fillCircle(x + GG_PW - 4, y + 4, 1, C_PAPER);
  // little wing ticks
  tft.drawFastHLine(x - 2, y + 3, 2, C_INK);
  tft.drawFastHLine(x + GG_PW, y + 3, 2, C_INK);
}

static void ggErasePest(int c, int r, int offx, int offy) {
  tft.fillRect(ggPestX(c, offx) - 2, ggPestY(r, offy), GG_PW + 4, GG_PH, C_BONE);
}

static void ggDrawBun() {
  int x = (int)g_ggBunX, y = GG_BUN_Y;
  // the REAL bunbun art when the pak answers (Jon 8/12: "use our sprites");
  // hand-drawn fallback keeps the game alive during art updates.
  // spriteLoad/gameSpriteBlit are in scope: this header is included after them.
  // stage-matched like the other games (8/13: this one was hardcoded to the
  // baby frame and never grew up with the pet)
  // The phase->folder map lives in petFrameKey() so this and the render path cannot drift
  // apart, and so a character pack reaches the games and not just the room.
  const char *bunFrame = petFrameKey(S.phase);
  if (spriteLoad(bunFrame)) {
    gameSpriteBlit(x + GG_BUN_W / 2, y + GG_BUN_H / 2 - 2,
                   20.0f / (g_meta.h > g_meta.w ? g_meta.h : g_meta.w),
                   GG_FRAME_X + 4, GG_FRAME_Y + 4,
                   GG_FRAME_X + GG_FRAME_W - 4, GG_FRAME_Y + GG_FRAME_H - 4,
                   false, C_BONE);
    return;
  }
  // ears
  tft.fillRoundRect(x + 5, y - 5, 4, 7, 2, C_PAPER);
  tft.fillRoundRect(x + GG_BUN_W - 9, y - 5, 4, 7, 2, C_PAPER);
  tft.fillRoundRect(x, y, GG_BUN_W, GG_BUN_H, 5, C_PAPER);
  tft.drawRoundRect(x, y, GG_BUN_W, GG_BUN_H, 5, C_INK);
  // dot eyes — the motif
  tft.fillCircle(x + GG_BUN_W / 2 - 4, y + 7, 1, C_INK);
  tft.fillCircle(x + GG_BUN_W / 2 + 4, y + 7, 1, C_INK);
}

static void ggDrawBullet() {
  int x = (int)g_ggBX, y = (int)g_ggBY;
  tft.fillTriangle(x - 2, y + 4, x + 2, y + 4, x, y - 3, C_ORANGE);   // a flung seed
}

// Every erase is CLIPPED to the field interior (Jon 8/12: border clipping) —
// nothing this game rubs out can touch the crayon frame or the soil line.
static void ggEraseRect(float px, float py, int w, int h) {
  int x = (int)px, y = (int)py;
  int x0 = GG_FRAME_X + 4, y0 = GG_FRAME_Y + 4;
  int x1 = GG_FRAME_X + GG_FRAME_W - 4, y1 = GG_GROUND;   // soil line safe too
  if (x < x0) { w -= (x0 - x); x = x0; }
  if (y < y0) { h -= (y0 - y); y = y0; }
  if (x + w > x1) w = x1 - x;
  if (y + h > y1) h = y1 - y;
  if (w > 0 && h > 0) tft.fillRect(x, y, w, h, C_BONE);
}

// Header band y28..44 only — never touches the frame (top at 50 with wobble).
static void ggDrawScore() {
  tft.fillRect(8, 28, 168, 16, C_BONE);
  char sc[16];
  snprintf(sc, sizeof(sc), "%d", g_ggScore > 9999 ? 9999 : g_ggScore);
  tft.setTextColor(C_ORANGE, C_BONE);
  tft.drawString(sc, 8, 28, 2);
  tft.setTextColor(C_HP, C_BONE);
  char wv[16];
  snprintf(wv, sizeof(wv), "wv%d", g_ggWave);
  tft.drawString(wv, 72, 32, 1);
  // hearts: compact at x100-124, clear of the right-aligned "hi N" (Jon 8/12)
  for (int i = 0; i < g_ggHearts && i < 3; i++) {
    int hx = 102 + i * 11, hy = 34;
    tft.fillCircle(hx - 2, hy - 1, 2, C_LOW);
    tft.fillCircle(hx + 2, hy - 1, 2, C_LOW);
    tft.fillTriangle(hx - 4, hy, hx + 4, hy, hx, hy + 5, C_LOW);
  }
  tft.setTextColor(C_INK_SOFT, C_BONE);
  char hi[16];
  snprintf(hi, sizeof(hi), "hi %u", g_ggHigh > 9999 ? 9999 : g_ggHigh);
  tft.drawRightString(hi, 176, 32, 1);
}

// Crayon border, matching the family look; also the self-heal target.
static void ggDrawBorder() {
  int x0 = GG_FRAME_X, y0 = GG_FRAME_Y, x1 = GG_FRAME_X + GG_FRAME_W,
      y1 = GG_FRAME_Y + GG_FRAME_H;
  tttStroke(x0, y0, x1, y0, C_INK, 1, 421);
  tttStroke(x1, y0, x1, y1, C_INK, 1, 422);
  tttStroke(x1, y1, x0, y1, C_INK, 1, 423);
  tttStroke(x0, y1, x0, y0, C_INK, 1, 424);
}

static void ggDrawAcorn(int i) {
  int x = (int)g_ggAX[i], y = (int)g_ggAY[i];
  tft.fillCircle(x, y, 3, C_ORANGE_LO);          // little brown acorn
  tft.drawFastHLine(x - 2, y - 3, 5, C_INK);     // its cap
}

// the magpie: 12x6, strictly inside the top strip so it can never touch the
// frame or the flock (border law: erase band == draw band exactly)
static void ggEraseMag(int px) {
  int x = px - 1;
  if (x < GG_FRAME_X + 3) x = GG_FRAME_X + 3;
  int w = 15;
  if (x + w > GG_FRAME_X + GG_FRAME_W - 3) w = GG_FRAME_X + GG_FRAME_W - 3 - x;
  if (w > 0) tft.fillRect(x, GG_MAG_Y, w, 6, C_BONE);
}

static void ggDrawMag() {
  int x = (int)g_ggMagX, y = GG_MAG_Y;
  tft.fillRoundRect(x, y + 1, 12, 5, 2, C_INK);            // black body
  tft.fillRect(x + 4, y + 3, 4, 2, C_PAPER);               // white belly flash
  int hx = (g_ggMagDir > 0) ? x + 11 : x + 1;              // head leads
  tft.drawPixel(hx, y, C_INK);
  int tx = (g_ggMagDir > 0) ? x - 1 : x + 12;              // tail trails
  tft.drawFastHLine((g_ggMagDir > 0) ? tx : tx - 1, y + 2, 2, C_INK_SOFT);
}

static void ggButton(int x, int w, char kind) {
  int cx = x + w / 2, cy = GG_BTN_Y + GG_BTN_H / 2;
  tft.fillRoundRect(x, GG_BTN_Y, w, GG_BTN_H, 7, C_BONE_LO);
  tft.drawRoundRect(x, GG_BTN_Y, w, GG_BTN_H, 7, C_INK);
  if (kind == 'L')      tft.fillTriangle(cx + 9, cy - 10, cx + 9, cy + 10, cx - 11, cy, C_INK);
  else if (kind == 'R') tft.fillTriangle(cx - 9, cy - 10, cx - 9, cy + 10, cx + 11, cy, C_INK);
  else {                // FIRE: an up seed
    tft.fillTriangle(cx - 8, cy + 8, cx + 8, cy + 8, cx, cy - 10, C_ORANGE);
    tft.drawTriangle(cx - 8, cy + 8, cx + 8, cy + 8, cx, cy - 10, C_INK);
  }
}

static void drawGardenPanel() {
  tft.fillScreen(C_BONE);
  // BACK top-left, title at x64 — the y28..44 stat band keeps its measured
  // columns untouched.
  drawBackChip(GG_DONE_X, GG_DONE_TY, GG_DONE_W, GG_DONE_H, "\x1B SHELF");
  tft.setTextColor(C_INK, C_BONE);
  tft.drawString("GARDEN GUARD", 64, 8, 2);
  ggDrawScore();

  // one border only (Jon 8/12: the separate soil line read as a second,
  // doubled frame) — the bottom of the crayon border IS the soil
  ggDrawBorder();

  for (int r = 0; r < GG_ROWS; r++)
    for (int c = 0; c < GG_COLS; c++)
      if (g_ggAlive[r * GG_COLS + c]) ggDrawPest(c, r, g_ggOffX, g_ggOffY);

  ggDrawBun();
  if (g_ggBullet) ggDrawBullet();
  for (int i = 0; i < GG_MAX_ACORNS; i++)
    if (g_ggAcorn[i]) ggDrawAcorn(i);
  ggButton(GG_BTN_LX, GG_BTN_LW, 'L');
  ggButton(GG_BTN_FX, GG_BTN_FW, 'F');
  ggButton(GG_BTN_RX, GG_BTN_RW, 'R');

  if (g_ggState != 0) {
    int cy = 150;
    tft.fillRoundRect(28, cy - 34, 184, 68, 8, C_PAPER);
    tft.drawRoundRect(28, cy - 34, 184, 68, 8, C_INK);
    tft.setTextColor(C_INK, C_PAPER);
    tft.drawCentreString(g_ggDeathBy == 1 ? "too many acorns!" : "the crows landed!",
                         120, cy - 26, 2);
    tft.setTextColor(C_HP, C_PAPER);
    char wv[28];
    snprintf(wv, sizeof(wv), "held out to wave %d", g_ggWave);
    tft.drawCentreString(wv, 120, cy - 4, 1);
    tft.setTextColor(C_INK_SOFT, C_PAPER);
    tft.drawCentreString("tap to play again", 120, cy + 14, 1);
  }
}

static void gardenOver(bool won) {
  g_ggState = won ? 2 : 1;
  if (g_ggScore > g_ggHigh) { g_ggHigh = g_ggScore; g_ggHighDirty = true; }
  if (won) sfxWin(); else sfxLose();
}

static void ggFire() {
  if (g_ggBullet) return;               // one seed in flight; caller may hold FIRE
  g_ggBullet = true;
  g_ggBX = g_ggBunX + GG_BUN_W / 2;
  g_ggBY = GG_BUN_Y - 4;
  g_ggPrevBX = g_ggBX; g_ggPrevBY = g_ggBY;
  beep(920, 0.04f, SFX_SQUARE, 0.05f);              // pew...
  beep(560, 0.05f, SFX_SQUARE, 0.05f, 0.035f);      // ...ew
}

// march the whole flock one step: erase all alive at the old offset, shift, draw
static void ggMarch() {
  // work out the flock's live horizontal extent to know when to turn
  int minC = GG_COLS, maxC = -1, maxR = -1;
  for (int r = 0; r < GG_ROWS; r++)
    for (int c = 0; c < GG_COLS; c++)
      if (g_ggAlive[r * GG_COLS + c]) {
        if (c < minC) minC = c;
        if (c > maxC) maxC = c;
        if (r > maxR) maxR = r;
      }
  if (maxC < 0) return;

  // THE MARCH HEARTBEAT (panel 8/13, "highest delight-per-line"): the flock's
  // two-note bass step — and because march rate IS crows-alive, the heartbeat
  // accelerates into dread all by itself, exactly like the arcade.
  static bool tock = false;
  beep(tock ? 110 : 90, 0.035f, SFX_SQUARE, 0.045f);
  tock = !tock;

  g_ggPrevOffX = g_ggOffX; g_ggPrevOffY = g_ggOffY;
  int leftEdge = ggPestX(minC, g_ggOffX);
  int rightEdge = ggPestX(maxC, g_ggOffX) + GG_PW;
  bool turn = (g_ggDir > 0 && rightEdge + 4 >= GG_FRAME_X + GG_FRAME_W - 2) ||
              (g_ggDir < 0 && leftEdge - 4 <= GG_FRAME_X + 2);
  if (turn) { g_ggDir = -g_ggDir; g_ggOffY += 12; }
  else      { g_ggOffX += g_ggDir * 6; }

  // repaint: erase every alive pest at the previous offset, draw at the new
  // one — under a hard viewport clip so wing-ticks at the edges can never
  // touch the border (Jon 8/12)
  // TWO PASSES (Jon 8/13: "half their bodies disappear on the down step"):
  // on a 12px drop the next row's OLD footprint overlaps this row's NEW one,
  // so interleaved erase/draw wiped the bottom half of freshly drawn crows.
  // Erase everyone first, then draw everyone.
  tft.setViewport(GG_FRAME_X + 3, GG_FRAME_Y + 3, GG_FRAME_W - 6, GG_FRAME_H - 6, false);
  for (int r = 0; r < GG_ROWS; r++)
    for (int c = 0; c < GG_COLS; c++)
      if (g_ggAlive[r * GG_COLS + c])
        ggErasePest(c, r, g_ggPrevOffX, g_ggPrevOffY);
  for (int r = 0; r < GG_ROWS; r++)
    for (int c = 0; c < GG_COLS; c++)
      if (g_ggAlive[r * GG_COLS + c])
        ggDrawPest(c, r, g_ggOffX, g_ggOffY);
  tft.resetViewport();

  // did the flock reach the soil?
  int lowest = ggPestY(maxR, g_ggOffY) + GG_PH;
  if (lowest >= GG_GROUND) { g_ggDeathBy = 0; gardenOver(false); drawGardenPanel(); }
}

// one fast tick: bunny slide + seed flight + collisions
static void gardenStep(int heldDir, bool fire) {
  if (fire) ggFire();

  g_ggPrevBunX = g_ggBunX;
  if (heldDir) {
    g_ggBunX += heldDir * GG_BUN_SPEED;
    if (g_ggBunX < GG_FRAME_X + 2) g_ggBunX = GG_FRAME_X + 2;
    if (g_ggBunX > GG_FRAME_X + GG_FRAME_W - GG_BUN_W - 2)
      g_ggBunX = GG_FRAME_X + GG_FRAME_W - GG_BUN_W - 2;
  }

  bool bunNicked = false;               // seed erase near the bun bites its head
  if (g_ggBullet) {
    g_ggPrevBX = g_ggBX; g_ggPrevBY = g_ggBY;
    if (g_ggPrevBY > GG_BUN_Y - 14) bunNicked = true;
    g_ggBY -= 8;                        // faster seed = FIRE feels responsive
    if (g_ggBY < GG_FRAME_Y + 2) {
      g_ggBullet = false;
      ggEraseRect(g_ggPrevBX - 3, g_ggPrevBY - 4, 7, 10);
    } else if (g_ggMag && g_ggBY <= GG_MAG_Y + 7 &&
               g_ggBX >= g_ggMagX - 2 && g_ggBX <= g_ggMagX + 14) {
      // MAGPIE DOWN — the mystery bonus, paid on the spot
      ggEraseMag((int)g_ggMagX);
      g_ggMag = false;
      g_ggMagNextAt = millis() + 15000 + (esp_random() % 15000);
      g_ggBullet = false;
      ggEraseRect(g_ggPrevBX - 3, g_ggPrevBY - 4, 7, 10);
      g_ggScore += 50 + (esp_random() % 6) * 50;      // 50..300, like the UFO
      beep(1500, 0.07f, SFX_TRIANGLE, 0.05f);
      beep(1800, 0.07f, SFX_TRIANGLE, 0.05f);
      ggDrawScore();
    } else {
      // hit test against the flock
      for (int r = 0; r < GG_ROWS && g_ggBullet; r++)
        for (int c = 0; c < GG_COLS; c++) {
          int idx = r * GG_COLS + c;
          if (!g_ggAlive[idx]) continue;
          int px = ggPestX(c, g_ggOffX), py = ggPestY(r, g_ggOffY);
          if (g_ggBX >= px - 2 && g_ggBX <= px + GG_PW + 2 &&
              g_ggBY >= py && g_ggBY <= py + GG_PH) {
            g_ggAlive[idx] = false; g_ggLeft--;
            tft.setViewport(GG_FRAME_X + 3, GG_FRAME_Y + 3,
                            GG_FRAME_W - 6, GG_FRAME_H - 6, false);
            ggErasePest(c, r, g_ggOffX, g_ggOffY);
            g_ggBullet = false;
            ggEraseRect(g_ggPrevBX - 3, g_ggPrevBY - 4, 7, 10);
            tft.resetViewport();
            // arcade row scoring: the high crows pay best (30/20/10)
            g_ggScore += (r == 0) ? 30 : (r <= 1) ? 20 : 10;
            sfxOK(); ggDrawScore();
            if (g_ggLeft == 0) {              // WAVE CLEARED: ceremony first
              g_ggScore += 25 * g_ggWave;     // clearance bonus
              if (g_ggScore > g_ggHigh) { g_ggHigh = g_ggScore; g_ggHighDirty = true; }
              sfxWin();
              gameSparkles(GG_FRAME_X + 8, GG_FRAME_Y + 8,
                           GG_FRAME_W - 16, GG_FRAME_H - 16, 700 + g_ggWave);
              delay(450);
              ggLoadWave(g_ggWave + 1);
              drawGardenPanel();
              return;
            }
            break;
          }
        }
    }
  }

  // THE CROWS CAW BACK (Jon 8/12, invaders parity): acorns drop — but only
  // from the LOWEST living crow in a column, like the parent game. (An upper
  // crow's acorn used to fall THROUGH its flockmates, munching their pixels
  // on the way down — Jon: "the bombs are going through things.")
  if (g_ggState == 0 && millis() >= g_ggNextAcornAt) {
    for (int a = 0; a < GG_MAX_ACORNS; a++) {
      if (g_ggAcorn[a]) continue;
      int tries = 12;
      while (tries--) {
        int c = esp_random() % GG_COLS;
        int lowest = -1;
        for (int r = 0; r < GG_ROWS; r++)
          if (g_ggAlive[r * GG_COLS + c]) lowest = r;
        if (lowest < 0) continue;             // empty column, pick again
        g_ggAcorn[a] = true;
        g_ggAX[a] = ggPestX(c, g_ggOffX) + GG_PW / 2;
        g_ggAY[a] = ggPestY(lowest, g_ggOffY) + GG_PH + 3;
        g_ggPrevAX[a] = g_ggAX[a]; g_ggPrevAY[a] = g_ggAY[a];
        break;
      }
      break;                                  // one new acorn per interval
    }
    // waves drop faster — and past the wave cap the SCORE keeps the heat
    // rising (the parent keyed its fire rate to score, so a good player
    // never outlives the pressure — progression review 8/13)
    int gap = 3000 - g_ggWave * 300;
    if (gap < 900) gap = 900;
    gap -= (g_ggScore / 400) * 40;
    if (gap < 600) gap = 600;
    g_ggNextAcornAt = millis() + gap;
  }

  // the magpie crosses its strip; spawns only while the flock is thick
  // (>= 8 alive, like the parent's UFO rule) so the endgame stays pure
  if (g_ggState == 0) {
    if (!g_ggMag && millis() >= g_ggMagNextAt && g_ggLeft >= 8) {
      g_ggMag = true;
      g_ggMagDir = (esp_random() & 1) ? 1 : -1;
      g_ggMagX = (g_ggMagDir > 0) ? GG_FRAME_X + 4
                                  : GG_FRAME_X + GG_FRAME_W - 17;
      g_ggMagPrevX = g_ggMagX;
      beep(1200, 0.05f, SFX_TRIANGLE, 0.04f);          // it announces itself
    }
    if (g_ggMag) {
      g_ggMagPrevX = g_ggMagX;
      g_ggMagX += g_ggMagDir * 1.4f;
      if (g_ggMagX < GG_FRAME_X + 4 || g_ggMagX > GG_FRAME_X + GG_FRAME_W - 17) {
        ggEraseMag((int)g_ggMagPrevX);                 // slipped away unshot
        g_ggMag = false;
        g_ggMagNextAt = millis() + 15000 + (esp_random() % 15000);
      } else {
        ggEraseMag((int)g_ggMagPrevX);
        ggDrawMag();
      }
    }
  }
  bool bunHit = false;
  for (int a = 0; a < GG_MAX_ACORNS; a++) {
    if (!g_ggAcorn[a]) continue;
    // UI-dev audit A2: an acorn that MISSES by 1-2px still drags its erase
    // column through the bunny — flag him for repaint just like the seed does
    if (g_ggPrevAY[a] + 6 >= GG_BUN_Y - 6 && g_ggPrevAY[a] - 5 <= GG_BUN_Y + GG_BUN_H &&
        fabsf(g_ggPrevAX[a] - (g_ggBunX + GG_BUN_W / 2)) < GG_BUN_W / 2 + 6)
      bunNicked = true;
    g_ggPrevAX[a] = g_ggAX[a]; g_ggPrevAY[a] = g_ggAY[a];
    float fall = 2.5f + 0.4f * g_ggWave;
    g_ggAY[a] += (fall > 5.0f ? 5.0f : fall);
    ggEraseRect(g_ggPrevAX[a] - 4, g_ggPrevAY[a] - 5, 9, 11);
    if (g_ggAY[a] > GG_GROUND - 6) {   // reaches the soil — gone BEFORE the
      g_ggAcorn[a] = false;            // border margin, so its clipped erase
      continue;                        // always covers it (no ghost pixels)
    }
    // a well-aimed seed knocks an acorn out of the air (invaders parity:
    // your shot and theirs can meet)
    if (g_ggBullet &&
        fabsf(g_ggAX[a] - g_ggBX) < 6 && fabsf(g_ggAY[a] - g_ggBY) < 8) {
      g_ggAcorn[a] = false;
      g_ggBullet = false;
      ggEraseRect(g_ggPrevBX - 3, g_ggPrevBY - 4, 7, 10);
      g_ggScore += 5 * g_ggWave;   // a real bounty: spending your only seed
      sfxTick();                   // defensively is a trade, not a waste
      ggDrawScore();
      continue;
    }
    // the bunny?
    if (millis() > g_ggGraceUntil &&
        g_ggAY[a] + 3 >= GG_BUN_Y && g_ggAY[a] <= GG_BUN_Y + GG_BUN_H &&
        g_ggAX[a] >= g_ggBunX - 2 && g_ggAX[a] <= g_ggBunX + GG_BUN_W + 2) {
      g_ggAcorn[a] = false;
      bunHit = true;
      continue;
    }
    ggDrawAcorn(a);
  }
  if (bunHit) {
    g_ggHearts--;
    sfxNo();                                  // bonk
    g_ggGraceUntil = millis() + 1500;         // blinking grace
    ggDrawScore();
    if (g_ggHearts <= 0) { g_ggDeathBy = 1; gardenOver(false); drawGardenPanel(); return; }
  }

  // incremental repaint of the moving bits
  if (g_ggBullet) { ggEraseRect(g_ggPrevBX - 3, g_ggPrevBY - 4, 7, 10); }
  bool graceBlink = millis() < g_ggGraceUntil;
  if (bunNicked && (int)g_ggBunX == (int)g_ggPrevBunX && !graceBlink) {
    // the seed's erase just passed through the bun's head — repaint him
    // (Jon 8/12: "when i shoot, part of bunbun's head disappears")
    ggDrawBun();
  }
  if ((int)g_ggBunX != (int)g_ggPrevBunX || graceBlink) {
    // the bunny walks the soil, so its erase is x-clipped to the interior and
    // stops short of the frame bottom; the heal below restores the soil line
    int lo = (int)fminf(g_ggBunX, g_ggPrevBunX) - 1;
    int hi = (int)fmaxf(g_ggBunX, g_ggPrevBunX) + GG_BUN_W + 1;
    if (lo < GG_FRAME_X + 4) lo = GG_FRAME_X + 4;
    if (hi > GG_FRAME_X + GG_FRAME_W - 4) hi = GG_FRAME_X + GG_FRAME_W - 4;
    int bot = GG_FRAME_Y + GG_FRAME_H - 4;
    tft.fillRect(lo, GG_BUN_Y - 6, hi - lo, bot - (GG_BUN_Y - 6), C_BONE);
    // during grace the bunny blinks — visible on the odd beats
    if (!graceBlink || ((millis() / 150) & 1)) ggDrawBun();
  }
  if (g_ggBullet) ggDrawBullet();

  // border insurance: one crayon edge re-stroked every ~quarter second
  static uint8_t healTick = 0, healEdge = 0;
  if (++healTick >= 14) {
    healTick = 0;
    int x0 = GG_FRAME_X, y0 = GG_FRAME_Y, x1 = GG_FRAME_X + GG_FRAME_W,
        y1 = GG_FRAME_Y + GG_FRAME_H;
    switch (healEdge++ & 3) {
      case 0: tttStroke(x0, y0, x1, y0, C_INK, 1, 421); break;
      case 1: tttStroke(x1, y0, x1, y1, C_INK, 1, 422); break;
      case 2: tttStroke(x1, y1, x0, y1, C_INK, 1, 423); break;
      case 3: tttStroke(x0, y1, x0, y0, C_INK, 1, 424); break;
    }
  }
}

static uint32_t ggStepMs() { return 16; }
static uint32_t ggMarchMs() {
  // THE LOCKSTEP RULE (the arcade's real algorithm): the machine moved one
  // alien per frame, so the rack's step interval IS the number left alive.
  // Ours: ~26ms per living crow — a full 32-crow flock shuffles every ~0.8s,
  // and the LAST crow is frantic. The hyperbolic speed-up as you thin them
  // is the whole menace of the parent game; waves shave it further.
  int ms = g_ggLeft * 26 - (g_ggWave - 1) * 15;
  if (ms > 850) ms = 850;
  return (uint32_t)(ms < 70 ? 70 : ms);
}

// 'X' BACK, 'L' 'R' 'F', 0 none
static char ggBtnHit(int tx, int ty) {
  if (tx < GG_DONE_X + GG_DONE_W + 12 && ty <= GG_DONE_TY + GG_DONE_H) return 'X';   // no slop into the score digits (review 8/13 #2)
  if (ty >= GG_BTN_Y && ty <= GG_BTN_Y + GG_BTN_H) {
    if (tx >= GG_BTN_LX && tx <= GG_BTN_LX + GG_BTN_LW) return 'L';
    if (tx >= GG_BTN_FX && tx <= GG_BTN_FX + GG_BTN_FW) return 'F';
    if (tx >= GG_BTN_RX && tx <= GG_BTN_RX + GG_BTN_RW) return 'R';
  }
  return 0;
}
