// bunbun — UI constants taken directly from bunbun_adult_test.html's stylesheet and markup,
// not eyeballed. Everything is the HTML's own geometry scaled by UI_S (240/320 = 0.75), the
// same factor the room art is packed at, so the proportions stay faithful on a 240x320 panel.
//
// HTML structure being reproduced:
//   .screen   canvas 320x240, with .menu overlaying the top and .stats overlaying the bottom
//   .ticker   22px ink bar directly beneath the canvas
//   .controls round UP / MENU / DOWN keys
//   .foot     PAUSE / MUSIC ON / SFX ON / RESET pins
#pragma once
#include <Arduino.h>

static const float UI_S = 0.75f;

// ---- palette: CSS values put through the SAME correction the art gets ----
// The converter bakes saturation +30% and a white point of 246 into every sprite and room,
// to compensate for this panel rendering muted and warm. Leaving the UI uncorrected meant
// the chrome and the art were being treated differently on the same screen. These are the
// CSS colours through the identical transform:
//     luma = .299r + .587g + .114b;  c' = (luma + (c-luma) * 1.30) * 255/246
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_INK       = RGB565( 53,  52,  44);   // --ink       #33322c
static const uint16_t C_INK_SOFT  = RGB565(115, 108,  89);   // --ink-soft  #6d685a
static const uint16_t C_PAPER     = RGB565(255, 250, 232);   // --paper     #f6f1e4
static const uint16_t C_BONE      = RGB565(244, 234, 213);   // --bone      #e9e2d2
static const uint16_t C_BONE_LO   = RGB565(217, 205, 176);   // --bone-lo   #cfc6b0
static const uint16_t C_BONE_EDGE = RGB565(189, 175, 139);   // --bone-edge #b3a98e
// The ACCENTS keep their authored values. The correction above exists to lift muted,
// warm-shifted art; run over an already-saturated accent it just pushes it further —
// #e8501e (232,80,30) became (255,71,3), which is why the orange kept reading as too orange.
static const uint16_t C_ORANGE    = RGB565(0xe8, 0x50, 0x1e);   // --orange
static const uint16_t C_ORANGE_LO = RGB565(0xc2, 0x3c, 0x11);   // --orange-lo
static const uint16_t C_DISC      = RGB565(0x8a, 0x6e, 0xe6);   // #b_disc i
static const uint16_t C_HP        = RGB565(0x27, 0xae, 0x60);   // #b_hp i
static const uint16_t C_LOW       = RGB565(0xc0, 0x39, 0x2b);   // .bar.low i
// A moon is yellow (Jon 8/14). The only new colour the redesign adds, and it exists because
// the SLEEP tab's crescent drawn in ink read as a hole rather than a moon. Warm gold, chosen
// to sit inside the existing paper/orange family rather than beside it.
static const uint16_t C_MOON      = RGB565(0xf0, 0xc8, 0x4a);

// ---- layout, HTML px * UI_S ----
static const int UI_W = 240, UI_H = 320;
static const int SCENE_W = 240, SCENE_H = 180;          // canvas 320x240 * 0.75

// In the HTML the menu and stats float OVER the canvas, because there the canvas is only
// part of a much larger page. Here the screen is the whole device, and overlaying them hid
// the top and bottom of the room — including the clock and the rug. There is spare height
// below the scene, so everything lives under it and the room stays completely unobstructed.
//
// MENU REDESIGN P3 — the icon row, the UP/OK/DOWN keys and the PAUSE/MUSIC/NAP pins are
// GONE. Three rows of chrome served one job badly: a cursor nobody used to drive an icon
// strip too small to read, plus a pin row that hid PAUSE in 14px of paper. In their place:
// four mitten-sized TABS (CARE · PLAY · MUSIC · SLEEP), with PAUSE promoted to a chip in
// the room itself beside DANCE and the gear. The freed 90px is what pays for the tabs.
//
// 8 menu indices survive because runMenu()'s cases ARE the care verbs — the CARE sheet's
// cards dispatch straight into them (0 FEED · 2 BATH · 3 SWEEP · 4 MEDS/TREAT · 5 CUDDLE ·
// 7 WORK/SCHOOL), the PLAY tab into case 1 (so the care-first refusal stays at the door),
// and the SLEEP sheet carries what case 6 used to open. Not one behaviour moved.
static const int N_MENU = 8;

// .stats — the same 7 columns, directly under the scene now that the icon row is gone
static const int STATS_Y = SCENE_H + 2;               // 182
static const int STATS_H = 24;                        // 182..205
static const int STAT_COLS = 7;   // +LOVE (Jon, launch night)
static const int BAR_H = 5;

static const int TICKER_Y = STATS_Y + STATS_H + 4;    // 210..223
static const int TICKER_H = 14;

// ---- the tab bar (menu redesign P3) ----
// Four doors, 55x88, 4px gaps: x 4 / 63 / 122 / 181, right edge 236 with 4px of panel to
// spare. 88 tall is more than double the old pin's 14 and clears the 44px mitten floor
// twice over. Tabs are DOORS, not modes (charter R5): no persistent highlight, no badge,
// no pulse — the only state any tab carries is the MUSIC seat's small "off" dot, which is
// the lowercase `snd` cue the pin row used to own (R4a).
static const int N_TABS = 4;
static const int TAB_X0 = 4, TAB_W = 55, TAB_GAP = 4;
static const int TAB_Y = 228, TAB_H = 88;             // 228..315, room state
// Under an open sheet the bar shrinks to its short form so the sheet gets the height: still
// 48 tall, still over the 44px floor, still the same four doors in the same four columns.
static const int TAB_SHEET_Y = 268, TAB_SHEET_H = 48; // 268..315
// Both forms put the LABEL on the same baseline, so a sheet opening or closing never makes
// the four words jump — only the icons move.
static const int TAB_LABEL_DY = 66;                   // label y = bar y + h - 22

// ---- the sheet (menu redesign P3) ----
// A sheet is not a screen: the room above SHEET_TOP keeps composing and pushing at full
// rate through a clipped push, and the pet HOPS UP to sit above his own sheet. 112 is the
// number the pet's own geometry chose — he stands at roughly y130..176, so anything higher
// would bury the thing the sheet claims to be about.
static const int SHEET_TOP = 112;
// The crayon rule sits 3px INSIDE the sheet, not on its edge. tttStroke wobbles +-2px and
// its 1px pen adds another, so a rule drawn ON y112 would put ink on rows 109..115 — and
// rows below 112 are the clipped push's territory, which would erase and repaint the top of
// the crayon every frame. At y115 the whole 3px wobble budget lands on 112..118: the line
// still reads as the sheet's top edge, and not one pixel of it is ever eaten.
static const int SHEET_RULE_Y   = 115;
// THE STAT BAND RIDES THE CARE SHEET (Jon 8/14: "when the care button is pressed the
// status bars should be at the top"). The bars normally live at y182, which a sheet
// covers — so opening CARE moves them to the top of the sheet, where they belong: you
// see the needs in the same glance as the buttons that answer them. 120..143, the exact
// STATS_H band, so drawStats() draws its real self here and not a lookalike.
static const int SHEET_STATS_Y = 120;
// The grab handle is retired: the stat band now occupies its row, the crayon rule already
// says "sheet", and on the SLEEP sheet the handle was painted under row 1 anyway.
static const int SHEET_TITLE_Y  = 128;                          // (unused; kept for SLEEP)
// The CARE grid: 4 columns x 2 rows, shortened 52->48 to seat the stat band above them.
// 48 still clears the 44px mitten minimum. Right edge 235 with 5px of panel spare;
// row 2 bottoms out at 246, 6px clear of the message strip.
static const int CARE_CARD_W = 53, CARE_CARD_H = 48;
static const int CARE_COL_X[4] = {8, 66, 124, 182};
static const int CARE_ROW_Y[2] = {147, 199};
// The message strip: the ticker, pushed here instead of y210 while the CARE sheet is up.
static const int SHEET_STRIP_Y = 252;                           // 252..265, 3px above the bar
// The SLEEP sheet's three rows keep the exact geometry the P2 SCREEN gave them — same x,
// same width, same height, same three y's. Converting a screen to a sheet moved no pixel.
static const int SL_X = 12, SL_W = 216, SL_H = 44;
static const int SL_Y1 = 120, SL_Y2 = 168, SL_Y3 = 216;         // row 3 bottoms at 259
