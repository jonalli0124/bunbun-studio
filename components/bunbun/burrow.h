// Burrow Blocks — bunbun tetris. Root-vegetable blocks tumble into the burrow;
// pack them into full rows to clear them. LEFT/RIGHT slide, ROTATE turns the
// piece, DOWN drops it faster. Clear rows for score; if the burrow fills to the
// top it's over. Same paper-and-ink look and the 8/12 no-full-refresh rule:
// only the falling piece's cells move each tick, and a line-clear repaints just
// the burrow, never the whole screen.
//
// Included into main.cpp after palette, tft, sfx, prefs, esp_random are in
// scope. Loop dispatch + roster hook live in main.cpp beside the other games.
#pragma once

static bool g_bkPanel = false;

// GB proportions (Jon 8/12): the classic 10x18 well, small 11px cells — tall
// and tight, the real Tetris silhouette.
static const int BK_COLS = 10, BK_ROWS = 18, BK_CELL = 11;
static const int BK_X0 = 64, BK_Y0 = 50;          // well 110x198, ends y248 (buttons at 258)

// BACK, top-left (menu redesign P1 — every way out is the same corner).
static const int BK_DONE_X = 4, BK_DONE_TY = 4, BK_DONE_W = 52, BK_DONE_H = 26;
static const int BK_BTN_Y = 258, BK_BTN_H = 54;
// four buttons: LEFT, ROTATE, RIGHT, DOWN
static const int BK_BTN_X[4] = {6, 62, 118, 174};
static const int BK_BTN_W = 52;

// 7 tetrominoes x 4 rotations x 4 blocks (x,y) in a 4x4 box
static const int8_t BK_PIECE[7][4][8] = {
  { {0,1,1,1,2,1,3,1},{2,0,2,1,2,2,2,3},{0,2,1,2,2,2,3,2},{1,0,1,1,1,2,1,3} }, // I
  { {0,0,0,1,1,1,2,1},{1,0,2,0,1,1,1,2},{0,1,1,1,2,1,2,2},{1,0,1,1,0,2,1,2} }, // J
  { {2,0,0,1,1,1,2,1},{1,0,1,1,1,2,2,2},{0,1,1,1,2,1,0,2},{0,0,1,0,1,1,1,2} }, // L
  { {1,0,2,0,1,1,2,1},{1,0,2,0,1,1,2,1},{1,0,2,0,1,1,2,1},{1,0,2,0,1,1,2,1} }, // O
  { {1,0,2,0,0,1,1,1},{1,0,1,1,2,1,2,2},{1,1,2,1,0,2,1,2},{0,0,0,1,1,1,1,2} }, // S
  { {1,0,0,1,1,1,2,1},{1,0,1,1,2,1,1,2},{0,1,1,1,2,1,1,2},{1,0,0,1,1,1,1,2} }, // T
  { {0,0,1,0,1,1,2,1},{2,0,1,1,2,1,1,2},{0,1,1,1,1,2,2,2},{1,0,0,1,1,1,0,2} }, // Z
};

static uint8_t g_bkGrid[BK_ROWS * BK_COLS];       // 0 empty, else piece type+1
static int g_bkType, g_bkRot, g_bkX, g_bkY;       // current piece
static int g_bkNext;
static int g_bkScore, g_bkLines;
static uint8_t g_bkState;                          // 0 run, 1 over, 2 pick-a-level
// START LEVEL (progression review 8/13 — the GB original's level-select was
// its main difficulty dial and we'd dropped it). Gravity/scoring run at
// max(start, lines/10), exactly the parent's rule. Persisted as "bkstart".
static int g_bkStart = 0;
static inline int bkLevelIdx() {
  int l = g_bkLines / 10;
  return (l < g_bkStart) ? g_bkStart : l;
}
static uint32_t g_bkFallAt, g_bkTouchMs;
static bool g_bkSoft;                              // DOWN held: fast fall
static uint16_t g_bkHigh = 0;
static bool g_bkHighDirty = false;

static uint16_t bkColor(int type) {
  switch (type) {
    case 0: return C_HP;          // I - green
    case 1: return C_INK;         // J - dark
    case 2: return C_ORANGE;      // L - carrot
    case 3: return C_ORANGE_LO;   // O - amber
    case 4: return C_HP;          // S - green
    case 5: return C_INK_SOFT;    // T - slate
    default: return C_BONE_EDGE;  // Z
  }
}

static inline int bkPx(int c) { return BK_X0 + c * BK_CELL; }
static inline int bkPy(int r) { return BK_Y0 + r * BK_CELL; }

static void bkFlushHigh() {
  if (!g_bkHighDirty) return;
  g_bkHighDirty = false;
  prefs.begin("bunbun", false);
  prefs.putUShort("bkhi", g_bkHigh);
  prefs.end();
}

static void bkCells(int type, int rot, int px, int py, int out[4][2]) {
  const int8_t *p = BK_PIECE[type][rot & 3];
  for (int i = 0; i < 4; i++) { out[i][0] = px + p[i * 2]; out[i][1] = py + p[i * 2 + 1]; }
}

static bool bkFits(int type, int rot, int px, int py) {
  int cell[4][2]; bkCells(type, rot, px, py, cell);
  for (int i = 0; i < 4; i++) {
    int x = cell[i][0], y = cell[i][1];
    if (x < 0 || x >= BK_COLS || y >= BK_ROWS) return false;
    if (y >= 0 && g_bkGrid[y * BK_COLS + x]) return false;
  }
  return true;
}

static void bkDrawCell(int c, int r, uint16_t col) {
  int x = bkPx(c), y = bkPy(r);
  tft.fillRect(x, y, BK_CELL, BK_CELL, col);
  tft.drawRect(x, y, BK_CELL, BK_CELL, C_BONE);   // thin gap so blocks read apart
}

static void bkEraseCell(int c, int r) {
  int x = bkPx(c), y = bkPy(r);
  tft.fillRect(x, y, BK_CELL, BK_CELL, C_BONE_LO);   // burrow floor tone
  // soil specks (UI-dev P2): a pure function of the cell index, so every
  // erase repaints the identical earth — the flat grey well becomes a burrow
  uint32_t j = tttJit(931 + c * 31 + r, 3);
  tft.drawPixel(x + 2 + (j % 7), y + 3 + ((j >> 3) % 6), C_BONE_EDGE);
  if ((j & 3) == 0)
    tft.drawPixel(x + 7 - (j % 5), y + 8 - ((j >> 4) % 4), C_INK_SOFT);
}

static void bkDrawPiece(bool erase) {
  int cell[4][2]; bkCells(g_bkType, g_bkRot, g_bkX, g_bkY, cell);
  for (int i = 0; i < 4; i++) {
    int x = cell[i][0], y = cell[i][1];
    if (y < 0) continue;
    if (erase) bkEraseCell(x, y);
    else       bkDrawCell(x, y, bkColor(g_bkType));
  }
}

static void bkDrawBoard() {
  // just the burrow interior, from locked cells
  for (int r = 0; r < BK_ROWS; r++)
    for (int c = 0; c < BK_COLS; c++) {
      uint8_t v = g_bkGrid[r * BK_COLS + c];
      if (v) bkDrawCell(c, r, bkColor(v - 1));
      else   bkEraseCell(c, r);
    }
}

static void bkDrawScore() {
  // side column is x4..56 ONLY — the well border starts at ~x59 and a fat
  // font-2 score was crossing it (Jon 8/12). Big scores drop to font 1.
  tft.fillRect(4, 60, 52, 120, C_BONE);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawString("score", 6, 62, 1);
  tft.setTextColor(C_ORANGE, C_BONE);
  char b[16]; snprintf(b, sizeof(b), "%d", g_bkScore);
  tft.drawString(b, 6, 74, g_bkScore > 9999 ? 1 : 2);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawString("rows", 8, 98, 1);
  snprintf(b, sizeof(b), "%d", g_bkLines);
  tft.setTextColor(C_INK, C_BONE);
  tft.drawString(b, 8, 110, 2);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawString("level", 8, 130, 1);
  // 0-based like the GB and the picker chips (review 8/13: tapping "9"
  // must not read back as "level 10")
  snprintf(b, sizeof(b), "%d", bkLevelIdx() > 20 ? 20 : bkLevelIdx());
  tft.setTextColor(C_HP, C_BONE);
  tft.drawString(b, 8, 142, 2);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  // "hi N" capped (audit A5: "best 12400" ran through the well border at x59)
  char hi[16]; snprintf(hi, sizeof(hi), "hi %d", g_bkHigh > 9999 ? 9999 : g_bkHigh);
  tft.drawString(hi, 6, 160, 1);
}

static void bkDrawNext() {
  // little preview in the right margin
  int bx = BK_X0 + BK_COLS * BK_CELL + 8, by = 70;
  tft.fillRect(bx, by, 44, 44, C_BONE);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawString("next", bx, by - 12, 1);
  int cell[4][2]; bkCells(g_bkNext, 0, 0, 0, cell);
  for (int i = 0; i < 4; i++) {
    int x = bx + 4 + cell[i][0] * 9, y = by + 6 + cell[i][1] * 9;
    tft.fillRect(x, y, 8, 8, bkColor(g_bkNext));
    tft.drawRect(x, y, 8, 8, C_BONE);
  }
}

static void bkButton(int i) {
  int x = BK_BTN_X[i], cx = x + BK_BTN_W / 2, cy = BK_BTN_Y + BK_BTN_H / 2;
  tft.fillRoundRect(x, BK_BTN_Y, BK_BTN_W, BK_BTN_H, 7, C_BONE_LO);
  tft.drawRoundRect(x, BK_BTN_Y, BK_BTN_W, BK_BTN_H, 7, C_INK);
  switch (i) {
    case 0: tft.fillTriangle(cx + 7, cy - 9, cx + 7, cy + 9, cx - 9, cy, C_INK); break;   // LEFT
    case 1: // ROTATE: a curved arrow hint (two arcs)
      tft.drawCircle(cx, cy, 8, C_INK); tft.drawCircle(cx, cy, 7, C_INK);
      tft.fillRect(cx, cy - 10, 10, 6, C_BONE_LO);
      tft.fillTriangle(cx + 6, cy - 11, cx + 6, cy - 3, cx + 12, cy - 7, C_INK);
      break;
    case 2: tft.fillTriangle(cx - 9, cy - 7, cx + 9, cy - 7, cx, cy + 9, C_INK); break;   // DOWN
    case 3: tft.fillTriangle(cx - 7, cy - 9, cx - 7, cy + 9, cx + 9, cy, C_INK); break;   // RIGHT (far right - Jon 8/12)
  }
}

static void bkSpawn() {
  g_bkType = g_bkNext;
  g_bkNext = esp_random() % 7;
  // drought mercy (panel): one reroll when the next piece repeats the current
  // — halves the I-piece droughts kids blame, keeps GB-flavored chaos
  if (g_bkNext == g_bkType) g_bkNext = esp_random() % 7;
  g_bkRot = 0; g_bkX = 3; g_bkY = 0;
  bkDrawNext();
  if (!bkFits(g_bkType, 0, g_bkX, g_bkY)) {   // no room to appear = burrow full
    g_bkState = 1;
    if (g_bkScore > g_bkHigh) { g_bkHigh = g_bkScore; g_bkHighDirty = true; }
    sfxLose();
  }
}

static void burrowReset() {
  // lands on the level picker (state 2); bkBeginRun() starts the game
  for (int i = 0; i < BK_ROWS * BK_COLS; i++) g_bkGrid[i] = 0;
  g_bkScore = 0; g_bkLines = 0; g_bkState = 2; g_bkSoft = false;
  g_bkNext = esp_random() % 7;
  g_bkTouchMs = millis();
}

static void bkBeginRun() {
  g_bkState = 0;
  g_bkTouchMs = millis();
  g_bkFallAt = millis() + 500;
  bkSpawn();
}

// the level-picker card: 0-9 chips, two rows of five
static const int BK_PICK_X = 24, BK_PICK_Y = 104, BK_PICK_W = 192, BK_PICK_H = 128;
static void bkDrawPicker() {
  tft.fillRoundRect(BK_PICK_X, BK_PICK_Y, BK_PICK_W, BK_PICK_H, 8, C_PAPER);
  tft.drawRoundRect(BK_PICK_X, BK_PICK_Y, BK_PICK_W, BK_PICK_H, 8, C_INK);
  tft.setTextColor(C_INK, C_PAPER);
  tft.drawCentreString("start level", 120, BK_PICK_Y + 8, 2);
  for (int i = 0; i < 10; i++) {
    int cx = BK_PICK_X + 10 + (i % 5) * 35, cy = BK_PICK_Y + 34 + (i / 5) * 40;
    bool sel = (i == g_bkStart);
    tft.fillRoundRect(cx, cy, 31, 34, 6, sel ? C_ORANGE : C_BONE_LO);
    tft.drawRoundRect(cx, cy, 31, 34, 6, C_INK);
    char d[2] = {(char)('0' + i), 0};
    tft.setTextColor(sel ? C_PAPER : C_INK, sel ? C_ORANGE : C_BONE_LO);
    tft.drawCentreString(d, cx + 15, cy + 9, 2);
  }
  tft.setTextColor(C_INK_SOFT, C_PAPER);
  tft.drawCentreString("tap a number to dig in", 120, BK_PICK_Y + BK_PICK_H - 14, 1);
}

// -1 = no chip at (tx,ty)
static int bkPickHit(int tx, int ty) {
  for (int i = 0; i < 10; i++) {
    int cx = BK_PICK_X + 10 + (i % 5) * 35, cy = BK_PICK_Y + 34 + (i / 5) * 40;
    if (tx >= cx - 2 && tx <= cx + 33 && ty >= cy - 2 && ty <= cy + 36) return i;
  }
  return -1;
}

static void drawBurrowPanel() {
  tft.fillScreen(C_BONE);
  // BACK top-left, title at x64 — the side column and the well keep every
  // measured position they have.
  drawBackChip(BK_DONE_X, BK_DONE_TY, BK_DONE_W, BK_DONE_H, "\x1B SHELF");
  tft.setTextColor(C_INK, C_BONE);
  tft.drawString("BURROW BLOCKS", 64, 8, 2);

  // burrow well — hand-drawn crayon frame, the family look
  {
    int x0 = BK_X0 - 3, y0 = BK_Y0 - 3, x1 = BK_X0 + BK_COLS * BK_CELL + 2,
        y1 = BK_Y0 + BK_ROWS * BK_CELL + 2;
    tttStroke(x0, y0, x1, y0, C_INK, 1, 431);
    tttStroke(x1, y0, x1, y1, C_INK, 1, 432);
    tttStroke(x1, y1, x0, y1, C_INK, 1, 433);
    tttStroke(x0, y1, x0, y0, C_INK, 1, 434);
  }
  bkDrawBoard();
  bkDrawScore();
  bkDrawNext();
  for (int i = 0; i < 4; i++) bkButton(i);
  if (g_bkState != 2) bkDrawPiece(false);   // no piece exists on the picker

  if (g_bkState == 2) {
    bkDrawPicker();
    return;
  }
  if (g_bkState == 1) {
    int cy = 150;
    tft.fillRoundRect(28, cy - 34, 184, 68, 8, C_PAPER);
    tft.drawRoundRect(28, cy - 34, 184, 68, 8, C_INK);
    tft.setTextColor(C_INK, C_PAPER);
    char over[28]; snprintf(over, sizeof(over), "%d rows!", g_bkLines);
    tft.drawCentreString(over, 120, cy - 26, 2);
    tft.setTextColor(C_HP, C_PAPER);
    char lvl[24];
    snprintf(lvl, sizeof(lvl), "reached level %d",
             bkLevelIdx() > 20 ? 20 : bkLevelIdx());
    tft.drawCentreString(lvl, 120, cy - 4, 1);
    tft.setTextColor(C_INK_SOFT, C_PAPER);
    tft.drawCentreString("tap to play again", 120, cy + 14, 1);
  }
}

static void bkLockAndClear() {
  int cell[4][2]; bkCells(g_bkType, g_bkRot, g_bkX, g_bkY, cell);
  for (int i = 0; i < 4; i++) {
    int x = cell[i][0], y = cell[i][1];
    if (y >= 0) g_bkGrid[y * BK_COLS + x] = g_bkType + 1;
  }
  // find the full rows FIRST, so they can flash before they collapse — the
  // classic clear feel (Jon 8/12, GB Tetris as the baseline).
  bool fullRow[BK_ROWS] = {false};
  int cleared = 0;
  for (int r = 0; r < BK_ROWS; r++) {
    bool full = true;
    for (int c = 0; c < BK_COLS; c++) if (!g_bkGrid[r * BK_COLS + c]) { full = false; break; }
    if (full) { fullRow[r] = true; cleared++; }
  }
  if (cleared) {
    // FLASH: the full rows blink twice, then fall. A ~260ms pause in the game
    // loop, felt as the beat the clear deserves.
    for (int blink = 0; blink < 2; blink++) {
      for (int r = 0; r < BK_ROWS; r++)
        if (fullRow[r])
          for (int c = 0; c < BK_COLS; c++) bkDrawCell(c, r, C_PAPER);
      delay(80);
      for (int r = 0; r < BK_ROWS; r++)
        if (fullRow[r])
          for (int c = 0; c < BK_COLS; c++) bkDrawCell(c, r, bkColor(g_bkGrid[r * BK_COLS + c] - 1));
      delay(50);
    }
    // compact: remove the marked rows top-down
    for (int r = BK_ROWS - 1; r >= 0; ) {
      if (fullRow[r]) {
        for (int rr = r; rr > 0; rr--) {
          fullRow[rr] = fullRow[rr - 1];
          for (int c = 0; c < BK_COLS; c++)
            g_bkGrid[rr * BK_COLS + c] = g_bkGrid[(rr - 1) * BK_COLS + c];
        }
        fullRow[0] = false;
        for (int c = 0; c < BK_COLS; c++) g_bkGrid[c] = 0;
      } else {
        r--;
      }
    }
    // GB Tetris scoring: 40 / 100 / 300 / 1200, scaled by level
    static const int lineScore[5] = {0, 40, 100, 300, 1200};
    // multiplier clamps with gravity at 20 (8/13: the UI was advertising
    // levels the game had stopped playing)
    int level = bkLevelIdx();
    if (level > 20) level = 20;
    g_bkScore += lineScore[cleared] * (level + 1);
    int lvlBefore = bkLevelIdx();
    g_bkLines += cleared;
    if (bkLevelIdx() != lvlBefore) sfxGrow();   // a REAL speed step, announced
                                                // (start-9 runs don't chirp at 10 lines)
    if (cleared >= 4) {                            // the TETRIS: full ceremony
      sfxWin();
      gameSparkles(BK_X0, BK_Y0, BK_COLS * BK_CELL, BK_ROWS * BK_CELL,
                   800 + g_bkLines);
      delay(400);
    } else if (cleared >= 2) sfxGrow();
    else sfxOK();
    bkDrawBoard();   // burrow-only repaint after a clear
    bkDrawScore();
  } else {
    beep(150, 0.05f, SFX_SQUARE, 0.05f);          // soft thud when a piece lands
  }
  bkSpawn();
  if (g_bkState == 1) drawBurrowPanel();
  else bkDrawPiece(false);
}

// gravity tick: try to drop one row, else lock
static void burrowGravity() {
  if (bkFits(g_bkType, g_bkRot, g_bkX, g_bkY + 1)) {
    bkDrawPiece(true);
    g_bkY++;
    bkDrawPiece(false);
    if (g_bkSoft) g_bkScore++;        // GB paid a point per soft-dropped row
  } else {
    bkLockAndClear();
  }
}

// button moves (press-edge); each redraws just the piece's cells
static void bkMove(int dx) {
  if (bkFits(g_bkType, g_bkRot, g_bkX + dx, g_bkY)) {
    bkDrawPiece(true); g_bkX += dx; bkDrawPiece(false);
    sfxTick();
  }
}
static void bkRotate() {
  int nr = (g_bkRot + 1) & 3;
  if (bkFits(g_bkType, nr, g_bkX, g_bkY)) {
    bkDrawPiece(true); g_bkRot = nr; bkDrawPiece(false);
    beep(720, 0.04f, SFX_SQUARE, 0.05f);   // a distinct twist blip
  }
}

static uint32_t bkFallMs() {
  if (g_bkSoft) return 45;                       // soft drop while DOWN held
  // THE REAL GB GRAVITY TABLE (frames-per-row at 60fps, converted to ms).
  // The famous cliff at levels 8->9->10 (17f -> 11f -> 10f) is the panic
  // ramp that makes the original addictive — copied, not approximated.
  static const uint16_t GB_MS[21] = {883, 817, 750, 683, 617, 550, 467, 367,
                                     283, 183, 167, 150, 133, 117, 100, 100,
                                     83,  83,  67,  67,  50};
  int level = bkLevelIdx();
  if (level > 20) level = 20;
  return GB_MS[level];
}

// 'X' BACK, 'L' 'T'(rotate) 'R' 'D', 0 none
static char bkBtnHit(int tx, int ty) {
  if (tx < BK_DONE_X + BK_DONE_W + 12 && ty <= BK_DONE_TY + BK_DONE_H + 4) return 'X';
  if (ty >= BK_BTN_Y && ty <= BK_BTN_Y + BK_BTN_H) {
    static const char k[4] = {'L', 'T', 'D', 'R'};   // RIGHT on the right (Jon 8/12)
    for (int i = 0; i < 4; i++)
      if (tx >= BK_BTN_X[i] && tx <= BK_BTN_X[i] + BK_BTN_W) return k[i];
  }
  return 0;
}
