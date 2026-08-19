// bunbun â€” ESP32-2432S028R, portrait 240x320.
//
// This is a port of bunbun_adult_test.html, not an approximation of it. The animation table,
// its playback modes and frame rates, the mood ordering, the menu contents and its
// UP/DOWN/MENU navigation, the stats row, ticker and palette all come from that file.
//
// Layout mirrors the HTML device shell, scaled 0.75 (320 -> 240 wide):
//   y 0..180    scene (the 320x240 room, point-sampled 4->3), with .menu over the top
//               and .stats over the bottom
//   y 180..195  .ticker
//   y 201..269  .controls  UP / MENU / DOWN
//   y 274..292  .foot      PAUSE / MUSIC / SFX / RESET
//
// Measured on this board during bring-up:
//   display ILI9341 BGR+inversion (platformio.ini) | backlight GPIO21 driven hard, on LEDC
//   touch XPT2046 VSPI 25/39/32/33, PENIRQ dead so pressure is the press signal
//   colour saturation +30% / white 246, baked into the art by the converter

#include <Arduino.h>
#include <time.h>       // W-029: weekday from the system epoch
#include <SPI.h>
#include <TFT_eSPI.h>
#include <esp_partition.h>
#include <esp_mac.h>
#include "FS.h"
#include "SD_MMC.h"
using fs::File;
#include <Preferences.h>
#include "game.h"
// Declared before scene.h so the scene parser can stamp the panic breadcrumb too — that parse
// does a malloc and a cJSON recursion from inside the render path, which makes it a suspect
// worth being able to rule in or out. Defined with the rest of the breadcrumb, further down.
static void bcMark(int where);
#include "scene.h"   // a room read from /spiffs/scene.json, if one is there

// EVERY EMOTION ANNOUNCES ITSELF on the ticker (Jon: "if he is doing an emote the
// scroll text should say so regardless of what it is" / "scroll text not notating
// all emotions"). Stock keys map to stock lines; a CUSTOM clip standing in for an
// emotion resolves through the scene table - the child's own ticker line wins,
// else its act joins the stock lines. Called from setAnim on every clip change.
static const char *emoteLineFor(const char *key) {
  const char *b2 = key;
  if (!strncmp(b2, "baby_", 5) || !strncmp(b2, "teen_", 5)) b2 += 5;
  if (b2[0] == 'c' && b2[1] == '_') {
    for (int i = 0; i < g_scAnimN; i++)
      if (!strcmp(g_scAnim[i].key, b2)) {
        if (g_scAnim[i].txt[0]) return g_scAnim[i].txt;
        b2 = g_scAnim[i].act;            // announce by what it counts as
        break;
      }
  }
  return !strcmp(b2, "angry")  ? "bunbun is not pleased"            :
       !strcmp(b2, "love")   ? "bunbun's heart is full"           :
       !strcmp(b2, "bored")  ? "bunbun has run out of ideas"      :
       !strcmp(b2, "hungry") ? "bunbun could really eat"          :
       !strcmp(b2, "sick")   ? "bunbun feels a bit rotten"        :
       !strcmp(b2, "tired")  ? "bunbun's eyes are getting heavy"  :
       !strcmp(b2, "bath")   ? "bunbun is having a lovely soak"   :
       !strcmp(b2, "wash")   ? "bunbun is scrubbing up"           :
       !strcmp(b2, "eat")    ? "bunbun is tucking in"             :
       !strcmp(b2, "sleep")  ? "bunbun is fast asleep... zzz"     :
       !strcmp(b2, "play")   ? "bunbun is up to something"        :
       !strcmp(b2, "jump")   ? "bunbun cannot keep still"         :
       !strcmp(b2, "work")   ? "bunbun is being very industrious" : nullptr;
}
#include "ui.h"
#include "music.h"
#include "beat.h"
#include "sfx.h"
#include "net.h"

TFT_eSPI tft = TFT_eSPI();
// The scene is composited off-screen and pushed in one go. Drawing the menu and stats
// straight to the panel after the room meant every frame wiped and redrew them, which read
// as heavy flicker at 10fps. In the HTML these are DOM layers over the canvas; here they
// have to go into the same buffer before it's sent. 240x180x2 = 86KB, affordable.
static TFT_eSprite scene = TFT_eSprite(&tft);
static const int BAND_H = 30;
static const float VIEW = 0.75f;
static inline int gx2s(int g) { return (int)(g * VIEW + 0.5f); }
static inline int gy2s(int g) { return (int)(g * VIEW + 0.5f); }

// ---------------- backlight ----------------
static const int PIN_BL = 45, BL_CH = 0;      // Freenove S3: backlight on GPIO45
// LEDC is free here — audio is a real codec on I2S, not PWM — so the backlight can be
// dimmed, which the design wants after dark.
static void backlightBegin() {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  // 300Hz, NOT 5kHz. The panel's backlight runs off a constant-current LED driver, not a bare
  // transistor, and at 5kHz its output capacitor smooths the PWM into near-DC — so 6/255 (2.4%
  // duty) still lit the screen almost fully and, worse, drew almost full current. Slow enough
  // that the driver actually switches off between pulses, fast enough not to flicker.
  // Arduino core 3.x replaced the channel API (ledcSetup + ledcAttachPin) with a pin-based one:
  // ledcAttach(pin, freq, resolution), and ledcWrite() now takes the PIN, not the channel. This
  // is the single API change most likely to bite silently — passing a channel number where a pin
  // is expected compiles cleanly and drives the wrong GPIO, or nothing at all.
  ledcAttach(PIN_BL, 300, 8);
  ledcWrite(PIN_BL, 255);
}
// When the backlight last moved. The charge detector waits for this to settle before believing
// any voltage trend — see batteryUpdate().
static uint32_t g_blChangedMs = 0;
static void backlightSet(uint8_t v) { ledcWrite(PIN_BL, v); g_blChangedMs = millis(); }

// Adaptive brightness. The backlight is by far the largest single load on this board, and an
// ambient pet spends nearly all its time unwatched — so it runs bright only just after you
// touch it and fades back down to a low glow. On the charger there is nothing to save, so it
// stays full. The ramp is gradual because a hard step reads as a fault rather than a feature.
static uint8_t g_blNow = 255, g_blTarget = 255;
static uint32_t g_lastTouchMs = 0;
// True while any game surface owns the glass (defined below the panel flags).
// Ambient audio consults it: rain hiss and idle peeps stay out of the arcade
// (Jon 8/13: "it should pause all bunbun activity including the rain").
static bool gamesLive();
static const uint8_t BL_BRIGHT = 255, BL_MID = 70, BL_LOW = 18;
// Set by the 'b' sweep command so a level can be inspected by eye. Without it the tick would
// drag the brightness straight back to BL_BRIGHT on the next loop, since charging forces it.
static bool g_blManual = false;
// Brightness depends ONLY on how long since you last touched it — deliberately NOT on whether
// it is charging.
//
// It used to force full brightness while charging, which reads well on paper ("nothing to save
// on the charger") but closes a control loop through the battery: brightness changes the load,
// the load changes the terminal voltage, and the terminal voltage is the only thing the charge
// detector has to work from. So dimming looked like a charger being plugged in, which forced
// the screen bright, which reloaded the cell, which looked like unplugging. The screen visibly
// hunted and the charge icon flickered with it. Widening the bands and lengthening the settling
// gate only changed how FAST it hunted; nothing fixes it while the actuator feeds its own
// sensor. Cutting the dependency fixes it outright, and idle-dimming on the charger is a mild
// good in its own right for a panel that sits on all day.
// The pet's name, and the AirPlay identity built from it. Kept in NVS as its own key rather
// than inside GameState: that struct is length- and version-checked on load, so growing it by
// even one field would make every existing save fail the size test and hatch a new egg.
static char g_petName[13] = "";
static char g_airName[24] = "bunbun";
static bool g_nameAsk = false;          // the naming screen is up
// Whether it has actually been PAINTED. Setting g_nameAsk only routes the touches; without
// this the screen was never drawn at all, so the previous contents stayed on the panel and the
// keyboard only appeared once a tap happened to land on a key and trigger a redraw.
static bool g_namePainted = false;

// "<name>-XXXX", the last two MAC bytes in hex. The suffix is what keeps two of these
// distinguishable on the same network — AirPlay shows the name alone, so without it a second
// bunbun would be indistinguishable in Control Centre.
static void buildAirName() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  const char *n = g_petName[0] ? g_petName : "bunbun";
  snprintf(g_airName, sizeof(g_airName), "%s-%02X%02X", n, mac[4], mac[5]);
}

// Declared up here, well above the disco module that owns them, because backlightTick(),
// think() and simulate() all sit above that module and this file is one translation unit
// compiled top to bottom.
static bool  g_danceMode = false;
static float g_discoDrop = 0.0f;    // 0 = stowed above the ceiling, 1 = fully lowered

// "Ball is down and settled", the condition for bunbun to actually dance and for decay to
// pause — as opposed to merely having the toggle on while it is still on its way down.
static inline bool discoDown() { return g_danceMode && g_discoDrop > 0.9f; }

static void backlightTick(bool /*charging*/) {
  if (g_blManual) return;
  uint32_t idle = millis() - g_lastTouchMs;
  // Dance mode pins the screen bright. The idle timer would otherwise dim a light show nobody
  // is touching precisely BECAUSE they are watching it rather than poking at it — the one case
  // where "no input" is the strongest evidence that the screen is being looked at.
  if (g_danceMode)       g_blTarget = BL_BRIGHT;
  else if (idle < 20000) g_blTarget = BL_BRIGHT;
  else if (idle < 90000) g_blTarget = BL_MID;
  else                   g_blTarget = BL_LOW;
  if (g_blNow == g_blTarget) return;
  int step = (g_blNow < g_blTarget) ? 4 : -2;      // brighten fast, dim gently
  int v = (int)g_blNow + step;
  if ((step > 0 && v > g_blTarget) || (step < 0 && v < g_blTarget)) v = g_blTarget;
  g_blNow = (uint8_t)v;
  backlightSet(g_blNow);
}

// ---------------- touch: FT6336U, capacitive, on I2C ----------------
// A completely separate bus from the audio codec's I2S, so the conflict that made audio and
// touch mutually exclusive on the 2432S028R cannot happen here.
// Mapping confirmed by the corner-tap probe: mode 3, sx = 319-tx, sy = 239-ty in landscape.
#include "hostwire.h"   // Wire-shaped view of the host's I2C bus; see the header

// bunbun's diagnostics go to the USB-JTAG console, the same place the host's ESP_LOG output
// lands and the only one reachable over the USB cable. Arduino's plain `Serial` is UART0 on
// GPIO43/44, which on this board goes nowhere you can read — so every one of bunbun's prints
// (the i2c scan, the RTC probe, the power log, the trace ring) was being written into the void.
// (ARDUINO_USB_CDC_ON_BOOT in the top-level CMakeLists makes `Serial` the USB-JTAG port.)
static const int PIN_SDA = 16, PIN_SCL = 15, PIN_TP_RST = 18;
static const uint8_t FT_ADDR = 0x38;

// Touch diagnostics. "no touch" and "the read failed" both leave g_rawX at -1, so from outside
// they are indistinguishable — and that ambiguity is exactly what makes a dead panel look like a
// panel nobody is pressing. Counted separately so the log answers which one it is.
static volatile uint32_t g_ftOk = 0, g_ftFail1 = 0, g_ftFail2 = 0;
static volatile uint8_t  g_ftLastStatus = 0xFF, g_ftLastErr = 0;
// Worst single cost per diagnostic window, in us. iter is the whole loop() pass, draw is
// drawScene alone, i2c is one touch read. Whichever of these owns the 100ms explains the 10Hz.
static volatile uint32_t g_ftMaxUs = 0, g_drawMaxUs = 0, g_iterMaxUs = 0;
// STAGE PROFILE. "it is running really slow" / "disco is even slow" — and disco asks for a
// 50ms frame while one compose was measuring 62ms, so the budget was never reachable. Rather
// than guess at the hot loop again, these split the compose into the three things it does:
// read the room's rows out of the mapped pak, run the per-pixel grade, and hand the finished
// band to the scene sprite. Peak micros over the same window as g_drawMaxUs.
static volatile uint32_t g_tPakUs = 0, g_tPixUs = 0, g_tPushUs = 0;
static volatile uint32_t g_tPakLast = 0, g_tPixLast = 0, g_tPushLast = 0;
static uint32_t g_tPakAcc = 0, g_tPixAcc = 0, g_tPushAcc = 0;
static volatile uint32_t g_drawCount = 0;   // draws in the current diagnostic window → real fps
// Per-stage attribution inside a draw, because the whole-draw number has now supported two
// wrong theories in a row. discoUsFrame accumulates across all bands of one frame.
static volatile uint32_t g_discoUsMax = 0, g_discoUsFrame = 0;
static inline void ftOk(uint8_t st)             { g_ftOk++;   g_ftLastStatus = st; }
static inline void ftFail(int which, uint8_t e) { if (which == 1) g_ftFail1++; else g_ftFail2++;
                                                  g_ftLastErr = e; }

static bool ftReadRaw(uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(FT_ADDR);
  Wire.write(reg);
  uint8_t e = Wire.endTransmission(false);
  if (e != 0) { ftFail(1, e); return false; }
  uint8_t got = Wire.requestFrom((int)FT_ADDR, (int)n);
  if (got != (uint8_t)n) { ftFail(2, got); return false; }
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  ftOk(buf[0]);
  return true;
}
static bool ftRead(uint8_t reg, uint8_t *buf, size_t n) {
  uint32_t t0 = micros();
  bool ok = ftReadRaw(reg, buf, n);
  uint32_t d = micros() - t0;
  if (d > g_ftMaxUs) g_ftMaxUs = d;
  return ok;
}
static void touchBegin() {
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(20);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(300);                       // the FT6x36 needs ~300ms after reset before it answers
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
}
// Raw controller values, exposed so calibration can be derived from measurement rather than
// inferred. The earlier mapping came from a probe run in LANDSCAPE and doesn't transfer
// cleanly to the portrait layout.
static int g_rawX = -1, g_rawY = -1;

// A capacitive panel reports absolute position, so this is a pure orientation transform —
// no range calibration needed, unlike the resistive panel on the old board.
static const bool TS_SWAP = false;   // swap the two axes
static const bool TS_FLIPX = false;  // mirror horizontally
static const bool TS_FLIPY = false;  // mirror vertically

// W-023: phantom-touch hygiene. Ghost taps on this panel have pressed REAL buttons — walked
// the set-time prompt into a poisoned timezone, flipped music on mid-gate-run, dropped the
// disco ball with nobody in the room. The per-widget guard (W-019's prompt armor) doesn't
// scale; this is the layer every widget reads through, so the filter lives here. A contact
// only counts when the frame itself is sane — a point count the FT6336 can actually produce,
// coords inside the panel matrix (constrain() used to fold 0xFFF garbage back INTO the
// screen, which is exactly how ghosts reached buttons) — AND the same finger appears in two
// consecutive loop passes within GHOST_STAB px. Passes are ~5ms apart (delay(4) loop); no
// human tap is short enough to lose, and a bus glitch almost never lies the same way twice.
// Rejects are counted per cause: a gift unit with a noisy panel should be visible from the
// bench (serial stats) and from a phone (/api/system/info) BEFORE it ships to a kid.
static volatile uint32_t g_ghostBad = 0, g_ghostRange = 0, g_ghostJump = 0;
static const int GHOST_STAB = 16;    // px a finger may wander between confirming reads
static const int GHOST_MARGIN = 8;   // raw slack past the matrix edge before it's garbage

static bool touchRead(int *px, int *py) {
  static bool confirmed = false, havePend = false;
  static int pendX = 0, pendY = 0;
  uint8_t st = 0;
  if (!ftRead(0x02, &st, 1)) { confirmed = havePend = false; return false; }
  uint8_t n = st & 0x0F;
  if (n == 0) { g_rawX = g_rawY = -1; confirmed = havePend = false; return false; }
  if (n > 2) {                       // this controller tracks at most two points; 0xFF's
    g_ghostBad++;                    // low nibble says "15 fingers" and means a lying bus
    confirmed = havePend = false; return false;
  }
  uint8_t p[4];
  if (!ftRead(0x03, p, 4)) { confirmed = havePend = false; return false; }
  int tx = ((int)(p[0] & 0x0F) << 8) | p[1];
  int ty = ((int)(p[2] & 0x0F) << 8) | p[3];
  g_rawX = tx; g_rawY = ty;
  if (tx > UI_W - 1 + GHOST_MARGIN || ty > UI_H - 1 + GHOST_MARGIN) {
    g_ghostRange++;
    confirmed = havePend = false; return false;
  }
  if (!confirmed) {
    if (havePend && abs(tx - pendX) <= GHOST_STAB && abs(ty - pendY) <= GHOST_STAB) {
      confirmed = true;              // second sighting agrees with the first: a finger
    } else {
      if (havePend) g_ghostJump++;   // sane-looking frame that teleported — not a finger
      pendX = tx; pendY = ty; havePend = true;
      // Jon (8/12): "people are consistently missing touches, pressing harder."
      // The two-sighting ghost guard is right, but WAITING a whole loop pass for
      // the second sighting is what made light taps miss: under streaming a pass
      // is 70-100ms, so a quick tap only ever lands in ONE pass and is dropped.
      // Take the confirming read RIGHT NOW, a few ms later, instead of next pass.
      // Still two agreeing reads (ghost protection intact) — just not a slow
      // frame apart — so a normal-firmness tap registers first time.
      delay(3);
      uint8_t st2 = 0, p2[4];
      if (ftRead(0x02, &st2, 1) && (st2 & 0x0F) >= 1 && (st2 & 0x0F) <= 2 &&
          ftRead(0x03, p2, 4)) {
        int tx2 = ((int)(p2[0] & 0x0F) << 8) | p2[1];
        int ty2 = ((int)(p2[2] & 0x0F) << 8) | p2[3];
        if (abs(tx2 - pendX) <= GHOST_STAB && abs(ty2 - pendY) <= GHOST_STAB &&
            tx2 <= UI_W - 1 + GHOST_MARGIN && ty2 <= UI_H - 1 + GHOST_MARGIN) {
          confirmed = true;
          tx = tx2; ty = ty2; g_rawX = tx; g_rawY = ty;   // use the fresher reading
        } else {
          return false;              // disagreed: leave the candidate for next pass
        }
      } else {
        return false;                // finger already gone or bad read
      }
    }
  }
  int a = TS_SWAP ? ty : tx;
  int b = TS_SWAP ? tx : ty;
  if (TS_FLIPX) a = (UI_W - 1) - a;
  if (TS_FLIPY) b = (UI_H - 1) - b;
  *px = constrain(a, 0, UI_W - 1);
  *py = constrain(b, 0, UI_H - 1);
  return true;
}

// The fleet-facing tally (W-023): web_server folds these into /api/system/info so a noisy
// panel is a number on a phone, not a hunch on a bench.
// The wish recorder's self-hearing probe beep (8/12): a fixed, known tone the
// recorder plays through the speaker and expects to HEAR through the mic at a
// sane level. 990Hz for 120ms — long enough to span the probe window.
// Returns 1 if the beep actually plays; 0 in silent mode (fx off), so the
// recorder knows NOT to require hearing a beep that never sounded — the
// difference between a self-check and self-inflicted stuck ears.
extern "C" int bunbun_probe_beep(void) {
  if (!g_sfxEnabled || g_fxLevel <= 0) return 0;
  beep(990, 0.12f, SFX_SQUARE, 0.05f);
  return 1;
}

extern "C" void bunbun_touch_stats(unsigned *ok, unsigned *fails, unsigned *ghosts) {
  *ok = g_ftOk;
  *fails = g_ftFail1 + g_ftFail2;
  *ghosts = g_ghostBad + g_ghostRange + g_ghostJump;
}

// W-044: declared here, above the pak readers that gate on it (the rest of
// the host's fw_update surface is declared much further down, next to the
// wish recorder, but this one is needed by spriteLoad/roomLoad).
extern "C" bool fw_assets_writing(void);

// ---------------- pak ----------------
#pragma pack(push, 1)
struct PakEntry { char name[32]; uint32_t offset, size; uint16_t origW, origH, w, h;
                  int16_t offX, offY; uint8_t fmt; uint8_t rsv[3]; };
#pragma pack(pop)
static_assert(sizeof(PakEntry) == 56, "must match converter");
static const esp_partition_t *g_assets = nullptr;
static uint16_t g_count = 0;
static PakEntry *g_index = nullptr;

// The pak, MEMORY-MAPPED. esp_partition_read goes through the flash driver, and the driver
// takes a global flash lock — the same lock NVS writes, SPIFFS reads and the WiFi stack use.
// Standalone, bunbun had that lock to itself and per-frame reads were harmless; inside this app
// the character sprite is re-read EVERY FRAME (see spriteLoad) while the host is persisting
// volume, serving its web UI and logging to SPIFFS, so band and sprite reads kept blocking
// behind other flash users. Mapped, a read is cached memcpy from XIP hardware: no driver, no
// lock, no one to wait for.
static const uint8_t *g_pakMap = nullptr;

static inline void pakRead(uint32_t off, void *dst, size_t n) {
  if (g_pakMap) { memcpy(dst, g_pakMap + off, n); return; }
  esp_partition_read(g_assets, off, dst, n);        // fallback if the mmap ever fails
}

static bool pakBegin() {
  g_assets = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "assets");
  if (!g_assets) return false;
  esp_partition_mmap_handle_t mh;
  if (esp_partition_mmap(g_assets, 0, g_assets->size, ESP_PARTITION_MMAP_DATA,
                         (const void **)&g_pakMap, &mh) != ESP_OK) {
    g_pakMap = nullptr;                             // partition reads still work, just slower
    Serial.println("pak: mmap failed, falling back to driver reads");
  }
  char m[4]; pakRead(0, m, 4);
  if (memcmp(m, "BUNP", 4)) return false;
  pakRead(6, &g_count, 2);
  g_index = (PakEntry *)malloc((size_t)g_count * sizeof(PakEntry));
  if (!g_index) return false;
  pakRead(8, g_index, (size_t)g_count * sizeof(PakEntry));
  Serial.printf("pak: %u entries\n", g_count);
  return true;
}
static const PakEntry *pakFind(const char *n) {
  for (uint16_t i = 0; i < g_count; i++)
    if (!strncmp(g_index[i].name, n, 32)) return &g_index[i];
  return nullptr;
}

// ---------------- room ----------------
static uint16_t g_pal[256];
static uint32_t g_roomPix = 0;
static uint16_t g_roomW = 0;
static char g_roomName[32] = {0};
// Which palette entries are sky. Clouds are drawn only where the room pixel underneath is
// one of these, so they stay behind the window frame and curtains without needing a
// separate mask image â€” the same trick the HTML uses, but done against palette indices.
static bool g_isSky[256];
static bool g_isOutside[256];      // sky + clouds + the hills seen through the window
static int  g_dimApplied = -1;     // cache key for the dimmed palette; -1 forces a rebuild
static int  g_skyX0, g_skyX1, g_skyY0, g_skyY1;
static bool g_haveSky = false;
// The authored weather box overrides the detected window: clouds and rain live exactly
// where the kid drew the box, palette be damned (sunset skies are not blue, and outdoor
// rooms have no window for the heuristics to find).
static bool g_skyAuthored = false;
static void applySceneSkyBox();
static int  g_skyR = 12, g_skyG = 40, g_skyB = 28;   // average daytime glass colour, RGB565 components
static int  g_perchX = 0, g_perchY = 0;              // centre of the bottom-right pane
// Per-pixel "this is glass" mask. The HTML draws its visitor into a sky buffer and masks the
// whole buffer against the glass, so the approach flight is invisible until it reaches the
// window. Without the equivalent here the firefly flew across the middle of the room to get
// to its perch. One bit per pixel, in PSRAM.
static uint8_t *g_skyMask = nullptr;
static inline bool isGlass(int x, int y) {
  if (!g_skyMask || x < 0 || y < 0 || x >= SCENE_W || y >= SCENE_H) return false;
  uint32_t i = (uint32_t)y * SCENE_W + x;
  return g_skyMask[i >> 3] & (1 << (i & 7));
}
// THE HILLS BEYOND THE GLASS, AS PIXELS RATHER THAN AS COLOURS.
// g_skyMask covers only the blue; the green field below the horizon is just as much "outside"
// and had no mask of its own, so it kept its daylight at midnight — measured on 6D1C at 23:59,
// the greenest through-glass pixel was (40,132,0) against an ungraded daylight of ~(60,160,70).
// The obvious fix, extending the sky BOUNDING BOX down to reach the grass, is recorded as wrong
// a hundred lines below: g_isOutside keys off the PALETTE, the furniture shares entries with the
// hills, and the chair, both tables, the shelves and the picture were all painted into the night.
// A per-pixel plane cannot do that, and the reason is structural rather than lucky: this mask is
// built from the ROOM IMAGE ALONE, and every prop is a sprite blitted over the finished band
// afterwards, never looked up in a palette. Props are therefore immune by construction — which
// is exactly why the builder gets away with a plain colour test in skyMask().
static uint8_t *g_gndMask = nullptr;
static inline bool isGlassGnd(int x, int y) {
  if (!g_gndMask || x < 0 || y < 0 || x >= SCENE_W || y >= SCENE_H) return false;
  uint32_t i = (uint32_t)y * SCENE_W + x;
  return g_gndMask[i >> 3] & (1 << (i & 7));
}

static bool roomLoad(const char *n) {
  if (fw_assets_writing()) return false;    // same standdown as spriteLoad
  if (!strncmp(g_roomName, n, 32)) return true;
  const PakEntry *e = pakFind(n); if (!e) return false;
  uint16_t c = 0; pakRead(e->offset, &c, 2);
  if (c > 256) return false;
  pakRead(e->offset + 2, g_pal, c * 2);
  g_roomPix = e->offset + 2 + c * 2; g_roomW = e->w;
  strncpy(g_roomName, n, 32);
  // The dimmed palette is cached against the TIME only, so swapping rooms left the previous
  // room's dimmed colours in place, indexed by this room's palette entries — which is why the
  // farm came out full of blues. A new palette must always force a rebuild.
  g_dimApplied = -1;

  // classify sky: strongly blue, mid-to-bright green, weak red
  memset(g_isSky, 0, sizeof(g_isSky));
  for (int i = 0; i < c; i++) {
    uint16_t v = g_pal[i];
    int r = ((v >> 11) & 0x1F) << 3, g = ((v >> 5) & 0x3F) << 2, b = (v & 0x1F) << 3;
    // Also require green well above red. Without it the crib's mauves (144,140,208) and
    // (128,128,208) matched, so the "sky" box covered most of the room and clouds and rain
    // were drawn far outside the window.
    g_isSky[i] = (b > 170 && b > r + 80 && g > r + 30 && g > 90 && g < 235 && r < 150);
  }
  // Bounding box of the sky, so clouds only get considered where they can show.
  //
  // A plain bounding box is NOT enough, and skipping this step is what put the firefly
  // indoors: other things in the room are sky-blue too, so the box ran from x88 all the way
  // to x197 while the actual window ends around x131. Anything positioned as a fraction
  // across that box — the perch above all — landed in the middle of the room.
  // The HTML solves it by keeping only the largest contiguous run of columns; do the same.
  static uint8_t row[240];
  static uint16_t col[240];
  static uint32_t skyR = 0, skyG = 0, skyB = 0, skyN = 0;
  memset(col, 0, sizeof(col));
  skyR = skyG = skyB = skyN = 0;
  // Only look for the window in the UPPER part of the room, as the HTML does (it scans
  // y10-140 of 240). Windows are always up the wall; furniture is not. Without this the teen
  // room's blue-and-white striped duvet classified as sky, and because the bed (x0-152) is
  // WIDER than the window (x120-176) the largest-column-run picked the bed — so the sill
  // firefly perched on the duvet, south-west of the actual window.
  const int SCAN_Y0 = 7, SCAN_Y1 = 105;          // 240x180 space; ~y10-140 of the 320x240 art
  for (int y = SCAN_Y0; y <= SCAN_Y1; y++) {
    pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
    for (int x = 0; x < g_roomW; x++)
      if (g_isSky[row[x]]) {
        col[x]++;
        uint16_t v = g_pal[row[x]];
        skyR += (v >> 11) & 0x1F; skyG += (v >> 5) & 0x3F; skyB += v & 0x1F; skyN++;
      }
  }
  // widest column run, tolerating small gaps so a mullion doesn't split the window in two
  int bestX0 = 0, bestX1 = -1; long bestTot = -1;
  int runStart = -1, gap = 0; long tot = 0;
  for (int x = 0; x <= g_roomW; x++) {
    if (x < g_roomW && col[x] > 0) {
      if (runStart < 0) runStart = x;
      tot += col[x]; gap = 0;
    } else if (runStart >= 0) {
      if (++gap > 8 || x == g_roomW) {
        int end = x - gap;
        if (tot > bestTot) { bestTot = tot; bestX0 = runStart; bestX1 = end - 1; }
        runStart = -1; gap = 0; tot = 0;
      }
    }
  }
  g_skyX0 = bestX0; g_skyX1 = bestX1;
  g_skyY0 = SCENE_H; g_skyY1 = -1;
  if (bestX1 >= bestX0) {
    for (int y = SCAN_Y0; y <= SCAN_Y1; y++) {
      pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
      for (int x = g_skyX0; x <= g_skyX1; x++)
        if (g_isSky[row[x]]) { if (y < g_skyY0) g_skyY0 = y; if (y > g_skyY1) g_skyY1 = y; break; }
    }
  }
  g_haveSky = (g_skyX1 > g_skyX0 && g_skyY1 > g_skyY0);
  applySceneSkyBox();   // an authored box outranks whatever the detection just decided

  // GROW the glass vertically with a much looser test, row by row from known glass.
  //
  // The strict classifier has to avoid false positives across the whole room, which makes it
  // miss the paler part of a pane. Measured in the browser: the teen window's lower half is
  // rgb(152,236,244) and fails BOTH `r < 150` and `g < 235`, so at night the top pane went
  // dark while the bottom stayed broad daylight. Inside columns already established as the
  // window we can afford to be permissive — but only by growing contiguously, so it stops at
  // the frame and can never jump to something blue elsewhere in those columns (the teen room's
  // bed sits directly under this window and its duvet is blue).
  if (g_haveSky) {
    static bool loose[256];
    for (int i = 0; i < c; i++) {
      uint16_t v = g_pal[i];
      int r = ((v >> 11) & 0x1F) << 3, g = ((v >> 5) & 0x3F) << 2, b = (v & 0x1F) << 3;
      loose[i] = (b > 190 && b >= r && b >= g - 20 && r < 200);
    }
    for (int y = g_skyY1 + 1; y < 180; y++) {
      pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
      int n = 0;
      for (int x = g_skyX0; x <= g_skyX1; x++) if (g_isSky[row[x]] || loose[row[x]]) n++;
      if (n < 3) break;
      for (int x = g_skyX0; x <= g_skyX1; x++) if (loose[row[x]]) g_isSky[row[x]] = true;
      g_skyY1 = y;
    }
    for (int y = g_skyY0 - 1; y >= 0; y--) {
      pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
      int n = 0;
      for (int x = g_skyX0; x <= g_skyX1; x++) if (g_isSky[row[x]] || loose[row[x]]) n++;
      if (n < 3) break;
      for (int x = g_skyX0; x <= g_skyX1; x++) if (loose[row[x]]) g_isSky[row[x]] = true;
      g_skyY0 = y;
    }
  }
  if (skyN) { g_skyR = skyR / skyN; g_skyG = skyG / skyN; g_skyB = skyB / skyN; }

  // Find the individual panes so the visitor sits INSIDE the bottom-right one rather than
  // on a guessed 3/4 point. Rows of glass separated by gaps are the crossbars; columns
  // separated by gaps are the mullions.
  g_perchX = (g_skyX0 + g_skyX1) / 2; g_perchY = g_skyY1 - 1;
  if (g_haveSky) {
    static uint8_t band[180];
    memset(band, 0, sizeof(band));
    for (int y = g_skyY0; y <= g_skyY1; y++) {
      pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
      int n2 = 0;
      for (int x = g_skyX0; x <= g_skyX1; x++) if (g_isSky[row[x]]) n2++;
      band[y] = (n2 >= 3);
    }
    int bY0 = -1, bY1 = -1, curStart = -1;      // keep the LAST band = bottom row of panes
    for (int y = g_skyY0; y <= g_skyY1 + 1; y++) {
      bool on = (y <= g_skyY1) && band[y];
      if (on) { if (curStart < 0) curStart = y; }
      else if (curStart >= 0) { bY0 = curStart; bY1 = y - 1; curStart = -1; }
    }
    if (bY1 >= bY0 && bY0 >= 0) {
      memset(col, 0, sizeof(col));
      for (int y = bY0; y <= bY1; y++) {
        pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
        for (int x = g_skyX0; x <= g_skyX1; x++) if (g_isSky[row[x]]) col[x]++;
      }
      int gX0 = -1, gX1 = -1, cs = -1;          // keep the LAST group = right-most pane
      for (int x = g_skyX0; x <= g_skyX1 + 1; x++) {
        bool on = (x <= g_skyX1) && col[x] >= 2;
        if (on) { if (cs < 0) cs = x; }
        else if (cs >= 0) { gX0 = cs; gX1 = x - 1; cs = -1; }
      }
      if (gX1 >= gX0 && gX0 >= 0) { g_perchX = (gX0 + gX1) / 2; g_perchY = bY1 - 1; }
    }
  }

  // glass mask, so anything "outside" is clipped to the panes exactly as the HTML's is
  if (!g_skyMask) g_skyMask = (uint8_t *)ps_calloc((SCENE_W * SCENE_H + 7) / 8, 1);
  if (g_skyMask) {
    memset(g_skyMask, 0, (SCENE_W * SCENE_H + 7) / 8);
    for (int y = 0; y < 180; y++) {
      pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
      for (int x = 0; x < g_roomW && x < SCENE_W; x++)
        if (g_isSky[row[x]]) {
          uint32_t i = (uint32_t)y * SCENE_W + x;
          g_skyMask[i >> 3] |= (1 << (i & 7));
        }
    }
  }

  // ...and the same again for the ground beyond it. The test is the builder's, transcribed from
  // skyMask(): "the daylit green of the field", G clear of both R and B and bright with it.
  // One deliberate narrowing on top of the builder's version, because the builder scans a room
  // image that is only ever a room, while this has to survive whatever art a child imports: a
  // pixel must be REACHABLE FROM THE SKY through green to count, so a green rug on the far side
  // of the floor can never be mistaken for a hillside. See the grow below for why.
  if (!g_gndMask) g_gndMask = (uint8_t *)ps_calloc((SCENE_W * SCENE_H + 7) / 8, 1);
  if (g_gndMask) {
    memset(g_gndMask, 0, (SCENE_W * SCENE_H + 7) / 8);
    if (g_haveSky && g_skyMask) {   // the grow is seeded from the sky plane, so it must exist
      bool fld[256];
      for (int i = 0; i < 256; i++) {
        const uint16_t v = g_pal[i];
        const int R = ((v >> 11) & 0x1F) * 255 / 31, G = ((v >> 5) & 0x3F) * 255 / 63,
                  B = (v & 0x1F) * 255 / 31;
        fld[i] = !g_isSky[i] && G > R + 12 && G > B + 8 && G > 90;
      }
      // GROWN DOWN FROM THE SKY, NOT BOXED. The first version of this confined the search to
      // the sky's own column span and left a bright green wedge along the bottom-right of the
      // pane: the field OVERHANGS the sky box, because g_skyX1 is where the blue reaches and
      // the ground carries on past it. Measured — the graded region stopped dead at x153 while
      // ungraded daylight green (40,132,0) sat at x154.
      // A box was the wrong shape for the question. What is actually true of the view is that
      // it is CONNECTED: the field meets the sky at the horizon and is bounded by the window
      // frame, whose brown fails the green test on every side. So each row grows from the row
      // above it — seeded by the sky itself — and then spreads sideways within its own row.
      // Nothing can be marked that is not reachable from the sky through green, which is a far
      // stronger guarantee than any margin around a bounding box, and it needs no constant.
      // SEEDED BY THE BOX, THEN GROWN OUT OF IT. Two versions of this are worth recording,
      // because each failed in the opposite direction and the answer is both together.
      // A plain box over the sky's column span graded the field but stopped dead at x153,
      // leaving ungraded daylight green (40,132,0) at x154: the ground OVERHANGS the sky,
      // since g_skyX1 is only as wide as the blue reaches.
      // Growing purely by connectivity from the sky plane instead marked NOTHING — measured,
      // zero pixels changed on the hill rows. g_isSky is a strict blue (b>170, b>r+80), and
      // the horizon is a paler band that satisfies neither that nor the green test, so sky and
      // field are not actually touching and the grow never started.
      // So: seed inside the box, where the answer is known to be right, and then let the field
      // extend itself sideways and downwards through its own colour. The overhang is reached
      // because it is connected to the seed; a green rug across the room is not reached because
      // nothing connects it to the window. No margins, no constants.
      const int GB = SCENE_W / 8;
      const int W = (g_roomW < SCENE_W) ? g_roomW : SCENE_W;
      int n = 0;
      for (int y = g_skyY0; y < SCENE_H; y++) {
        pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
        uint8_t *cur = g_gndMask + y * GB;
        for (int x = g_skyX0; x <= g_skyX1 && x < W; x++)        // seed
          if (fld[row[x]]) cur[x >> 3] |= (1 << (x & 7));
        if (y > g_skyY0) {                                       // grow down
          const uint8_t *up = g_gndMask + (y - 1) * GB;
          for (int x = 0; x < W; x++)
            if (fld[row[x]] && (up[x >> 3] & (1 << (x & 7)))) cur[x >> 3] |= (1 << (x & 7));
        }
        for (int x = 1; x < W; x++)                              // spread, both ways
          if (fld[row[x]] && (cur[(x - 1) >> 3] & (1 << ((x - 1) & 7))))
            cur[x >> 3] |= (1 << (x & 7));
        for (int x = W - 2; x >= 0; x--)
          if (fld[row[x]] && (cur[(x + 1) >> 3] & (1 << ((x + 1) & 7))))
            cur[x >> 3] |= (1 << (x & 7));
        for (int x = 0; x < W; x++)
          if (cur[x >> 3] & (1 << (x & 7))) n++;
      }
      Serial.printf("glass ground: %d px seeded x%d..%d from y%d\n", n, g_skyX0, g_skyX1, g_skyY0);
    }
  }

  // Which palette entries are OUTSIDE the window — sky, clouds, and the hills below the
  // horizon. Tinting only the blue left the landscape lit like daytime, just dimmer.
  // Decided by WHERE a colour appears rather than what colour it is: anything that occurs
  // almost exclusively inside the window box is part of the view. The frame's brown also
  // appears on the floor and furniture, so it correctly stays indoors.
  memset(g_isOutside, 0, sizeof(g_isOutside));
  if (g_haveSky) {
    static uint32_t inBox[256], total[256];
    memset(inBox, 0, sizeof(inBox)); memset(total, 0, sizeof(total));
    for (int y = 0; y < 180; y++) {
      pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
      // NOTE, and do not "fix" this the obvious way. The hills below the horizon stay daylit at
      // night because g_skyY1 is the bottom of the SKY pixels, so every grass row falls outside
      // this box and never gets marked outside. Extending the box downward to reach them was
      // tried and is WRONG: g_isOutside is a PALETTE classification, and the furniture shares
      // palette entries with the hills — so the chair, both tables, the shelves and the picture
      // were all reclassified as sky and painted into the night. Measured on hardware.
      // The real fix is per-pixel, using the g_skyMask/isGlass() mask that already exists, not a
      // bounding box over shared colours.
      bool yIn = (y >= g_skyY0 && y <= g_skyY1);
      for (int x = 0; x < g_roomW; x++) {
        total[row[x]]++;
        if (yIn && x >= g_skyX0 && x <= g_skyX1) inBox[row[x]]++;
      }
    }
    int n = 0;
    for (int i = 0; i < 256; i++)
      if (inBox[i] > 4 && inBox[i] * 10 >= total[i] * 8) { g_isOutside[i] = true; n++; }
    // THE TEEN DUVET IS NOT THE SKY (Jon 8/14: "portions of the bed are the same as outside
    // so when it rains the bed changes colors"). g_isSky is decided by COLOUR, and the teen
    // room's blue-and-white bedding is the same blue as the window — the comment up at the
    // column scan already records that it fooled the window finder once. The night tint was
    // safe because it asks g_isOutside, which is decided by WHERE a colour appears; but the
    // weather paths (overcast, cloud, rain, rainbow, stars) still asked g_isSky alone, so a
    // shower repainted the bed along with the view. Intersecting the two here fixes every
    // one of those consumers at once: sky now means "blue AND actually in the window".
    for (int i = 0; i < 256; i++)
      if (g_isSky[i] && !g_isOutside[i]) g_isSky[i] = false;
    Serial.printf("outside colours: %d ->", n);
    for (int i = 0; i < 256; i++)
      if (g_isOutside[i]) {
        uint16_t v = g_pal[i];
        Serial.printf(" [%d](%d,%d,%d)%s", i, (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F,
                      g_isSky[i] ? "sky" : "gnd");
      }
    Serial.println();
  }
  Serial.printf("sky: %s box %d,%d..%d,%d\n", g_haveSky ? "found" : "none",
                g_skyX0, g_skyY0, g_skyX1, g_skyY1);
  return true;
}

// soft blobs drifting right, wrapping around the sky box
// Speeds were 1.1-2.4 px/sec across a window only ~60px wide, so a crossing took over half
// a minute and read as static. Fast enough now to be visibly drifting.
struct Cloud { float x, y, w, h, sp; };
// The five cloud styles the builder offers, in ITS order (placer.html CLOUD_STYLES). Row 0 is the
// firmware's own table, verbatim, so a scene that says nothing draws exactly what it always drew.
// w is a HALF-width here - the draw below spans cl.x-half .. cl.x+half.
static const Cloud CLOUD_STYLE_TAB[SCENE_CLOUD_STYLES][3] = {
  {{0,0,14,4, 7.0f},{0,0,10,3, 9.5f},{0,0,18,5, 5.0f}},   // 0 shipped
  {{0,0,26,2, 9.0f},{0,0,20,2,11.0f},{0,0,32,3, 7.0f}},   // 1 wisps
  {{0,0, 9,6, 6.0f},{0,0, 7,5, 8.0f},{0,0,12,7, 5.0f}},   // 2 puffs
  {{0,0,44,7, 3.0f},{0,0,36,6, 3.5f},{0,0,52,8, 2.5f}},   // 3 bank
  {{0,0, 6,2,12.0f},{0,0, 4,2,14.0f},{0,0, 8,3,10.0f}},   // 4 scatter
};
static Cloud g_clouds[SCENE_MAX_CLOUDS] = {{0,0,14,4,7.0f},{0,0,10,3,9.5f},{0,0,18,5,5.0f}};
static int   g_cloudN       = 3;
static float g_cloudSpeedK  = 1.0f;
static int   g_cloudAlphaK  = 153;    // (60% of 255) == the shipped 3/5 blend, exactly
static bool  g_cloudsInit   = false;
static bool  g_cloudSceneDone = false;

// The scene's sky, applied once it turns up. Deliberately lazy rather than done at boot: SPIFFS is
// mounted by the host component and sceneEnsure() keeps retrying for as long as it takes (see the
// 6D1C note in scene.h), so the scene can arrive minutes after the first frame.
static bool g_skyBoxMasked = false;   // room art occludes inside the box (window frames)
// even-odd point-in-polygon against the scene's sky shape; with no polygon (bbox-only
// scenes) everything inside the bbox counts
static bool skyPolyHit(int x, int y) {
  if (g_scSkyN < 3) return true;
  bool in = false;
  for (int i = 0, j = g_scSkyN - 1; i < g_scSkyN; j = i++) {
    int yi = g_scSkyPts[i][1], yj = g_scSkyPts[j][1];
    if ((yi > y) == (yj > y)) continue;
    float xx = g_scSkyPts[i][0] +
               (float)(g_scSkyPts[j][0] - g_scSkyPts[i][0]) * (y - yi) / (float)(yj - yi);
    if (x < xx) in = !in;
  }
  return in;
}
// ENVIRONMENT-SPEC layer 3, stars: tiny pixels that twinkle inside the sky shape after
// dark. Seeded once per room+scene onto pixels the sky rules allow (polygon, and the
// art's own sky where the art occludes); the band renderer fades them with the same
// daylight() curve the room grading uses, so they appear as the light goes.
static struct { uint8_t x, y, ph, big; } g_envStars[40];
static uint8_t g_envStarN = 0;
static void seedSceneStars() {
  g_envStarN = 0;
  const SceneEnv *e = sceneEnv();
  if (!e || e->stars <= 0 || !g_haveSky || !g_roomPix || !g_roomW) return;
  static uint8_t srow[240];
  int want = e->stars > 40 ? 40 : e->stars;
  for (int i = 0; i < want; i++) {
    for (int t = 0; t < 12; t++) {
      int x = g_skyX0 + (int)(esp_random() % (uint32_t)max(1, g_skyX1 - g_skyX0 + 1));
      int y = g_skyY0 + (int)(esp_random() % (uint32_t)max(1, g_skyY1 - g_skyY0 + 1));
      if (!skyPolyHit(x, y)) continue;
      if (!g_skyAuthored || g_skyBoxMasked) {   // the art gates wherever it can
        pakRead(g_roomPix + (uint32_t)y * g_roomW, srow, g_roomW);
        if (x >= g_roomW || !g_isSky[srow[x]]) continue;
      }
      g_envStars[g_envStarN].x = (uint8_t)x;
      g_envStars[g_envStarN].y = (uint8_t)y;
      g_envStars[g_envStarN].ph = (uint8_t)(esp_random() & 255);
      g_envStars[g_envStarN].big = (esp_random() % 4) == 0;
      g_envStarN++;
      break;
    }
  }
}

static void applySceneSkyBox() {
  const SceneEnv *e = sceneEnv();
  if (!e || e->skyW <= 0) { g_skyAuthored = false; seedSceneStars(); return; }
  g_skyX0 = e->skyX; g_skyX1 = e->skyX + e->skyW - 1;
  g_skyY0 = e->skyY; g_skyY1 = e->skyY + e->skyH - 1;
  g_haveSky = true; g_skyAuthored = true;
  // Jon 8/18: "can they occur behind the scene? like in case there is a window frame?"
  // If ANY pixel inside the box classifies as sky, the room's own art keeps occluding
  // (frames, curtains, trees win). If none does - a sunset the classifier cannot see -
  // the box wins raw. Decided once per room+box, not per frame.
  g_skyBoxMasked = false;
  if (g_roomPix && g_roomW) {
    static uint8_t row[240];
    for (int y = g_skyY0; y <= g_skyY1 && !g_skyBoxMasked; y++) {
      pakRead(g_roomPix + (uint32_t)y * g_roomW, row, g_roomW);
      for (int x = g_skyX0; x <= g_skyX1; x++)
        if (g_isSky[row[x]] && skyPolyHit(x, y)) { g_skyBoxMasked = true; break; }
    }
  }
  seedSceneStars();
}

static void cloudsApplyScene() {
  const SceneEnv *e = sceneEnv();
  if (!e) return;
  applySceneSkyBox();
  const Cloud *base = CLOUD_STYLE_TAB[constrain((int)e->cloudT, 0, SCENE_CLOUD_STYLES - 1)];
  int n = (e->cloudN >= 0) ? e->cloudN : 3;
  if (n > SCENE_MAX_CLOUDS) n = SCENE_MAX_CLOUDS;
  float k = e->cloudW / 100.0f;
  for (int i = 0; i < n; i++) {
    g_clouds[i] = base[i % 3];                       // beyond three it cycles the same shapes,
    g_clouds[i].w = max(1.0f, base[i % 3].w * k);    // exactly as the builder's rebuildClouds()
    g_clouds[i].h = max(1.0f, base[i % 3].h * k);
  }
  g_cloudN      = n;
  g_cloudSpeedK = e->cloudS / 100.0f;
  g_cloudAlphaK = (e->cloudA * 255) / 100;
  g_cloudsInit  = false;                             // re-seed positions across the sky box
  Serial.printf("scene sky: %d cloud(s) style %d, %d%% size, %d%% drift, %d%% opacity\n",
                n, (int)e->cloudT, (int)e->cloudW, (int)e->cloudS, (int)e->cloudA);
}

static void cloudsUpdate(float dt) {
  if (!g_cloudSceneDone && sceneActive()) { cloudsApplyScene(); g_cloudSceneDone = true; }
  if (!g_haveSky || g_cloudN <= 0) return;             // cn:0 - a clear sky
  int sw = g_skyX1 - g_skyX0 + 1, sh = g_skyY1 - g_skyY0 + 1;
  if (!g_cloudsInit) {
    for (int i = 0; i < g_cloudN; i++) {
      g_clouds[i].x = g_skyX0 + (esp_random() % max(1, sw));
      g_clouds[i].y = g_skyY0 + 2 + (esp_random() % max(1, sh / 2));
    }
    g_cloudsInit = true;
  }
  for (int i = 0; i < g_cloudN; i++) {
    g_clouds[i].x += g_clouds[i].sp * g_cloudSpeedK * dt;
    if (g_clouds[i].x - g_clouds[i].w > g_skyX1) {
      g_clouds[i].x = g_skyX0 - g_clouds[i].w;
      g_clouds[i].y = g_skyY0 + 2 + (esp_random() % max(1, sh / 2));
    }
  }
}

// ---------------- sprites ----------------
// PSRAM, not internal .bss (8/13 fleet regression): this 24KB buffer was the
// single biggest internal-RAM resident. On 0.1.19x the fleet units bottomed
// out ~7.7KB free internal — under the fw_selfcheck 14K validation floor, so
// the anti-rollback watchdog reboot-looped them, WiFi hit "mem fail", SNTP
// never synced (which is ALSO why pet age under-read after reboots). Decode
// is plain CPU reads; the scene sprite already lives in PSRAM at 16fps.
#define SPR_BUF_SZ (24 * 1024)
static uint8_t *g_spr = nullptr;
static uint32_t g_rowOff[128];
static PakEntry g_meta;
// The child's composed frames use a FIXED FEET ROW: canvas row (origH - 6) is the assembler's
// y=90 feet line, with 6 rows beneath for below-feet pixels (a lying sleep's belly). The blit
// anchors that row at footY - so every pose lands exactly where drawChar puts it, including
// content that hangs below the feet ("he is sitting lower in the device").
static bool g_canimAnchor = false;
static bool g_sprOK = false;
static char g_sprName[32] = {0};
static uint32_t g_sprGen = 0;

static bool spriteLoad(const char *n) {
  // W-044: while the art partition is being rewritten, the mapping under
  // this read is being erased. Draw nothing rather than decode 0xFF as a
  // sprite header — that walks the row table off the end of g_spr.
  if (fw_assets_writing()) { g_sprOK = false; g_sprName[0] = 0; return false; }
  if (!g_spr) {
    g_spr = (uint8_t *)heap_caps_malloc(SPR_BUF_SZ, MALLOC_CAP_SPIRAM);
    if (!g_spr) { g_sprOK = false; return false; }   // vector fallbacks carry the day
  }
  if (!strncmp(g_sprName, n, 32)) return g_sprOK;
  const PakEntry *e = pakFind(n);
  // WIDTH as well as height. Every scanline blitter decodes into line[128]/cov[128] and then
  // indexes with an si derived from g_meta.w — clamped on the write, not on the read — so a
  // sprite wider than 128 reads past both arrays. Only the height was ever checked, and a
  // child-imported prop is exactly the thing that can be 200px wide.
  if (!e || e->size > SPR_BUF_SZ || e->h > 128 || e->w > 128) {
    g_sprOK = false; g_sprName[0] = 0; return false;
  }
  pakRead(e->offset, g_spr, e->size);
  g_meta = *e;
  uint32_t p = 0;
  for (int y = 0; y < e->h; y++) {
    g_rowOff[y] = p; uint8_t s = g_spr[p++];
    for (int i = 0; i < s; i++) { p++; uint8_t l = g_spr[p++]; p += l * 2; }
  }
  strncpy(g_sprName, n, 32); g_sprGen++; g_sprOK = true;
  return true;
}

// ---- THE PET'S STENCIL ----
// Jon, on live hardware: "bunbun is behind objects instead of in front".
//
// The pet is blitted INSIDE the band pipeline (spriteBlit, below, called from composeRoom), and an
// uploaded scene's furniture is drawn by sceneDrawProps() AFTER that whole loop has finished,
// straight into the scene sprite — so every table, chair and rug painted over the top of him. It
// hid for this long because the room's own compiled-in fittings are a wall clock, a wall sconce
// and a shelf radio: all high on the wall, none of them ever over his head. Scene props are floor
// furniture, and floor furniture is exactly what he stands in front of.
//
// He is NOT moved out of the band pipeline to fix it. Two things are deliberately stamped over him
// in there — the disco beams and the ball — and his anchor carries the ANIM-13 trim correction
// that only spriteBlit knows about; moving him would cost the first and duplicate the second.
// Instead the furniture simply does not paint where he already is: spriteBlit records the pixels
// it covers in a 1-bit mask, and a prop on a layer BEHIND him skips them. Because his pixels are
// opaque either way, the result on screen is identical to drawing him last, for none of the cost.
//
// The mask is 240x180 bits = 5.4KB and lives in PSRAM — internal RAM is the scarce one here, and
// the whole mask fits in cache anyway. It is optional: if the allocation ever fails the stencil
// turns itself off and the room draws exactly as it did before.
static const int STEN_STRIDE = UI_W / 8;          // 240 px -> 30 bytes a row
static uint8_t  *g_petSten   = nullptr;
static bool      g_petStenOK = false;   // a mask was recorded for THIS frame
static bool      g_stenApply = false;   // true only while props BEHIND the pet are being drawn

static void petStenBegin() {
  // Only the scene's props are ever held out of him, so a unit with no scene.json neither
  // allocates the mask nor pays a single instruction for it: the marking, the clearing and the
  // testing all hang off g_petStenOK. The compiled-in room draws bit for bit as it always has.
  if (!sceneActive()) { g_petStenOK = false; return; }
  if (!g_petSten)
    g_petSten = (uint8_t *)heap_caps_malloc(STEN_STRIDE * SCENE_H, MALLOC_CAP_SPIRAM);
  g_petStenOK = (g_petSten != nullptr);
  if (g_petStenOK) memset(g_petSten, 0, STEN_STRIDE * SCENE_H);
}
static inline void petStenMark(int x, int y) {
  if (!g_petStenOK) return;
  if ((unsigned)x >= (unsigned)UI_W || (unsigned)y >= (unsigned)SCENE_H) return;
  g_petSten[y * STEN_STRIDE + (x >> 3)] |= (uint8_t)(1 << (x & 7));
}
// True when this pixel is the pet's and the caller is drawing something that belongs behind him.
// While a "behind" performance runs (the editor's depth control - bathing IN the tub), the
// pet's stencil stands down and the furniture paints over him, exactly as the assembler
// composes it. ("he... is sitting on top not behind the bathtub")
static bool g_performBehind = false;
static inline bool petStenHides(int x, int y) {
  if (g_performBehind) return false;
  if (!g_stenApply || !g_petStenOK) return false;
  if ((unsigned)x >= (unsigned)UI_W || (unsigned)y >= (unsigned)SCENE_H) return false;
  return ((g_petSten[y * STEN_STRIDE + (x >> 3)] >> (x & 7)) & 1) != 0;
}

// Coverage is tracked separately from colour: value 0 is legitimate black (his outline), so
// using it as the transparency marker dropped his linework and washed him out.
// How much to take a SPRITE down, matching the room's own dim. 1.0 in full daylight.
// SPRITES ARE NOT DIMMED. THE BUILDER IS THE GOLDEN RECORD AND IT DOES NOT DIM THEM.
// There was a dimRun() here that took every decoded sprite run down by the room's own dim
// factor, on the reasoning that a rug measuring byte-for-byte identical at noon and midnight
// is a full-daylight sticker on a graded backdrop. That reasoning was mine, not the builder's,
// and on the panel it read as Jon called it: "the lighting is really screwed up ... its also
// all really dark", with the left sconce reading as switched OFF while still casting a cone
// (its brightest pixel measured 250 in daylight, 100 dimmed).
// The builder's litRoom() takes ONE argument, the room's name, and grades ONLY
// ROOMS[name].img. Every prop, the cat and bunbun are then drawn over that graded backdrop
// with a plain drawImage at full value - no alpha, no filter, and no exemption needed for
// lamps because nothing is dimmed in the first place. Backing the dim off to 72%, and then
// holding lamps out of it, were both patches on a feature the golden record does not have.
// If sprite grading is ever wanted it belongs in the BUILDER first, where Jon can see it.

static void spriteBlit(uint16_t *band, int bandY, int footX, int footY, float scale) {
  if (!g_sprOK || !g_meta.w) return;
  int dw = (int)(g_meta.w * scale + 0.5f), dh = (int)(g_meta.h * scale + 0.5f);
  if (dw <= 0 || dh <= 0) return;
  // ANIM-13 (council 2026-08-15, addendum 2). Every frame in the pak is trimmed to its opaque
  // box at pack time and carries offX/offY to put it back; the firmware had never read them, so
  // each frame was re-centred on its OWN trimmed box. A clip whose silhouette changes width then
  // slides under a body the artist never moved. Measured over 57 frames: his neck sits at x
  // 47.5..49.5 in every one of them, so the drawings are registered and the placement was
  // throwing it away. play swung 13.2 screen px (a 7px jump between consecutive frames) as the
  // ball left and re-entered the box; tired's frame 0 jumped 5px off the "zzz" droplets; and
  // hungry/eat carried a CONSTANT bias — the speech bubble stretches the box right, so bunbun
  // was drawn 8px left of his own foot line, which is why he teleported sideways the moment he
  // got hungry and teleported back when he was fed. Anchoring the UNTRIMMED frame's centre on
  // footX instead removes all of it: worst residual drift anywhere in the pack is 0.98 px, at
  // every phase scale.
  //
  // HORIZONTAL ONLY, and that is a finding, not timidity. Condition 3 of the ruling also asks
  // for `top = footY - origH*s + offY*s`, which assumes the art is bottom-flush in its canvas.
  // It is not: every standing clip leaves ~25 fully transparent rows under his feet
  // (offY + h = 71 of 96), while the egg leaves 4 — there is no shared canvas baseline in the
  // pak to correct against. Applying it literally would lift him 25 * 1.0875 = 27px off the
  // floor at adult scale while S.y stayed put, desynchronising him from BLOCKS_*, bounds() and
  // the sheet-lift ear clamp, all of which read footY as his FEET. The five clips ANIM-13 names
  // (play, tired, hungry, eat, work-drive) are lateral without exception. The vertical anchor
  // stays the silhouette's bottom, exactly as it has always been.
  //
  // ONE CALL SITE. This is the pet and only the pet (main.cpp:4455). Props, furniture and every
  // exported scene go through drawItemAtXY/drawItemRot, whose coordinates — BLOCKS_*, the chair
  // sit point, catChairSpot() — were all measured by eye against trim-centred placement. Do not
  // propagate this to that path.
  int left = footX - (int)floorf((g_meta.origW * 0.5f - g_meta.offX) * scale + 0.5f);
  int top  = g_canimAnchor
           ? footY - (int)floorf(((g_meta.origH - 6) - g_meta.offY) * scale + 0.5f)
           : footY - dh;
  uint32_t xs = ((uint32_t)g_meta.w << 16) / dw, ysp = ((uint32_t)g_meta.h << 16) / dh;
  static uint16_t line[128]; static uint8_t cov[128]; static uint32_t lineFor = 0xFFFFFFFF;
  for (int dy = 0; dy < dh; dy++) {
    int r = (top + dy) - bandY;
    if (r < 0 || r >= BAND_H) continue;
    int sy = (int)((uint32_t)dy * ysp >> 16); if (sy >= g_meta.h) sy = g_meta.h - 1;
    uint16_t *dst = band + r * UI_W;
    uint32_t key = (g_sprGen << 8) | (uint32_t)sy;
    if (lineFor != key) {
      memset(cov, 0, sizeof(cov));
      uint32_t p = g_rowOff[sy]; uint8_t segs = g_spr[p++]; int x = 0;
      for (int s = 0; s < segs; s++) {
        x += g_spr[p++]; uint8_t l = g_spr[p++];
        for (int k = 0; k < l && x + k < 128; k++) {
          line[x + k] = (uint16_t)(g_spr[p + k * 2] | (g_spr[p + k * 2 + 1] << 8));
          cov[x + k] = 1;
        }
        p += l * 2; x += l;
      }
      lineFor = key;
    }
    for (int dx = 0; dx < dw; dx++) {
      int X = left + dx; if (X < 0 || X >= UI_W) continue;
      int si = (int)((uint32_t)dx * xs >> 16);
      // ...and the same pixel goes into the stencil, so the scene's furniture knows to leave it
      // alone. One bit-set per covered pixel of his, and only his — see the note above.
      if (cov[si]) { dst[X] = line[si]; petStenMark(X, top + dy); }
    }
  }
}
// the same blit, but straight to the panel â€” for UI that lives outside the scene sprite
static void spriteBlitPanel(int cx, int cy, float scale) {
  if (!g_sprOK || !g_meta.w) return;
  int dw = (int)(g_meta.w * scale + 0.5f), dh = (int)(g_meta.h * scale + 0.5f);
  if (dw <= 0 || dh <= 0) return;
  uint32_t xs = ((uint32_t)g_meta.w << 16) / dw, ysp = ((uint32_t)g_meta.h << 16) / dh;
  for (int dy = 0; dy < dh; dy++) {
    int sy = (int)((uint32_t)dy * ysp >> 16); if (sy >= g_meta.h) sy = g_meta.h - 1;
    uint16_t line[128]; uint8_t cov[128]; memset(cov, 0, sizeof(cov));
    uint32_t p = g_rowOff[sy]; uint8_t segs = g_spr[p++]; int x = 0;
    for (int s = 0; s < segs; s++) {
      x += g_spr[p++]; uint8_t l = g_spr[p++];
      for (int k = 0; k < l && x + k < 128; k++)
          line[x + k] = (uint16_t)(g_spr[p + k * 2] | (g_spr[p + k * 2 + 1] << 8)), cov[x + k] = 1;
      p += l * 2; x += l;
    }
    for (int dx = 0; dx < dw; dx++) {
      int si = (int)((uint32_t)dx * xs >> 16);
      if (cov[si]) tft.drawPixel(cx - dw / 2 + dx, cy - dh / 2 + dy, line[si]);
    }
  }
}

// draw the currently loaded sprite centred at (cx, cy) inside the scene sprite
// Colour of a single pixel of the currently loaded sprite, walking the row's RLE segments.
// Used to paint the cat clock's drawn-on tail in the cat's OWN colour.
static bool spritePixel(int sx, int sy, uint16_t *out) {
  if (!g_sprOK || sy < 0 || sy >= g_meta.h) return false;
  uint32_t p = g_rowOff[sy]; uint8_t segs = g_spr[p++]; int x = 0;
  for (int s = 0; s < segs; s++) {
    x += g_spr[p++]; uint8_t l = g_spr[p++];
    if (sx >= x && sx < x + l) {
      int k = sx - x;
      *out = (uint16_t)(g_spr[p + k * 2] | (g_spr[p + k * 2 + 1] << 8));
      return true;
    }
    p += l * 2; x += l;
  }
  return false;
}

static bool g_clipGlass = false;    // set while drawing things that live outside the window
// `flip` mirrors about the sprite's own centre. It exists because the cat's art all faces WEST —
// the builder says so in CAT_ART and the frames themselves confirm it — so walking her east is
// that same clip mirrored, and one swat clip gives both a west and an east swipe.
static bool g_blitFlip = false;
static void spriteBlitDirect(int cx, int cy, float scale) {
  if (!g_sprOK || !g_meta.w) return;
  int dw = (int)(g_meta.w * scale + 0.5f), dh = (int)(g_meta.h * scale + 0.5f);
  if (dw <= 0 || dh <= 0) return;   // its three siblings all guard this; on Xtensa /0 is a panic
  uint32_t xs = ((uint32_t)g_meta.w << 16) / dw, ysp = ((uint32_t)g_meta.h << 16) / dh;
  for (int dy = 0; dy < dh; dy++) {
    int sy = (int)((uint32_t)dy * ysp >> 16); if (sy >= g_meta.h) sy = g_meta.h - 1;
    uint16_t line[128]; uint8_t cov[128]; memset(cov, 0, sizeof(cov));
    uint32_t p = g_rowOff[sy]; uint8_t segs = g_spr[p++]; int x = 0;
    for (int s = 0; s < segs; s++) {
      x += g_spr[p++]; uint8_t l = g_spr[p++];
      for (int k = 0; k < l && x + k < 128; k++)
          line[x + k] = (uint16_t)(g_spr[p + k * 2] | (g_spr[p + k * 2 + 1] << 8)), cov[x + k] = 1;
      p += l * 2; x += l;
    }
    for (int dx = 0; dx < dw; dx++) {
      int si = (int)((uint32_t)(g_blitFlip ? dw - 1 - dx : dx) * xs >> 16);
      // No manual byte swap here: TFT_eSprite::drawPixel already does
      // `color = (color >> 8) | (color << 8)` internally. Swapping again cancelled it out and
      // rendered the near-black cat clock as green.
      int px = cx - dw / 2 + dx, py = cy - dh / 2 + dy;
      // g_clipGlass restricts a sprite to the window panes, which is how the bird stays
      // genuinely outside — occluded by the frame, and invisible on its approach flight.
      if (g_clipGlass && !isGlass(px, py)) continue;
      // The pet's silhouette wins when the caller has armed the stencil — the same mechanism
      // that puts him in front of the furniture. petStenHides() is false unless g_stenApply is
      // set, so every other caller of this function is untouched.
      if (petStenHides(px, py)) continue;
      if (cov[si]) scene.drawPixel(px, py, line[si]);
    }
  }
}

// ---------------- animation table (from the HTML's ANIM + loader) ----------------
enum { M_LOOP, M_PINGPONG, M_ONCE };
struct AnimDef { const char *key, *folder; uint8_t frames; float fps; uint8_t mode; };
static const AnimDef ANIMS[] = {
  {"idle","idle-anim",5,6,M_LOOP},
  {"walk_down","walk-south/anim",8,9,M_LOOP}, {"walk_up","walk-north/anim",8,9,M_LOOP},
  {"walk_left","walk-west/anim",8,9,M_LOOP},  {"walk_right","walk-east/anim",8,9,M_LOOP},
  {"jump","jump/anim",5,8,M_PINGPONG}, {"eat","eat/anim",5,5,M_PINGPONG},
  {"bath","bath/anim",5,4,M_LOOP}, {"sleep","sleep/anim",5,1.5f,M_LOOP},
  {"tired","tired-anim",5,4,M_LOOP}, {"bored","bored-anim",5,4,M_LOOP},
  {"hungry","hungry-anim",5,4,M_LOOP}, {"sick","sick-anim",5,4,M_LOOP},
  {"play","play/anim",9,6,M_PINGPONG}, {"angry","angry-anim",5,5,M_LOOP},
  {"love","love-anim",5,4,M_LOOP}, {"idle_n","adult-idle-n",1,1,M_LOOP},
  // 9 frames, not 5: regenerated front-on (the old art faced south-east, inherited from the
  // Baby state's own three-quarter `south` rotation). fps dropped 5 -> 4 so the longer cycle
  // still reads as unhurried rather than busier than before.
  {"baby_idle","baby-idle",9,4,M_LOOP}, {"baby_jump","baby-jump",5,8,M_PINGPONG},
  {"baby_walk_down","baby-walk-south",5,7,M_LOOP}, {"baby_walk_up","baby-walk-north",5,7,M_LOOP},
  {"baby_walk_left","baby-walk-west",5,7,M_LOOP},  {"baby_walk_right","baby-walk-east",5,7,M_LOOP},
  {"baby_crawl_down","baby-crawl-south",5,6,M_LOOP}, {"baby_crawl_up","baby-crawl-north",5,6,M_LOOP},
  {"baby_crawl_left","baby-crawl-west",5,6,M_LOOP},  {"baby_crawl_right","baby-crawl-east",5,6,M_LOOP},
  {"baby_fall_left","baby-fall-west",5,6,M_ONCE},  {"baby_fall_right","baby-fall-east",5,6,M_ONCE},
  {"baby_eat","baby-eat",5,5,M_PINGPONG}, {"baby_bath","baby-bath",5,4,M_LOOP},
  {"baby_sleep","baby-sleep",5,2,M_LOOP}, {"baby_tired","baby-tired",5,4,M_LOOP},
  {"baby_bored","baby-bored",5,4,M_LOOP}, {"baby_hungry","baby-hungry",5,4,M_LOOP},
  {"baby_love","baby-love",5,4,M_LOOP}, {"baby_angry","baby-angry",5,5,M_LOOP},
  {"baby_cuddle","baby-cuddle",5,4,M_LOOP}, {"baby_play","baby-play",5,5,M_PINGPONG},
  // Grown-up cuddles (Jon's credits, launch night; art landed in the pak
  // 2026-08-09): nine frames of snuggling in a blanket, one clip per age.
  // Keyed so pa("cuddle") resolves teen_cuddle -> cuddle -> (baby handled
  // by its own key above). cuddleReady() below still checks the PAK, so a
  // unit whose art has not arrived yet falls back to the love clip rather
  // than drawing nothing.
  {"teen_cuddle","teen-cuddle",9,5,M_LOOP}, {"cuddle","adult-cuddle",9,5,M_LOOP},
  {"baby_sick","baby-sick",4,4,M_LOOP}, {"baby_sit","baby-sit",5,4,M_LOOP},
  {"baby_sit_n","baby-sit-n",1,1,M_LOOP},
  // The adult job. The art was in the pack all along; only the sequence was missing.
  {"work_basket","work-basket/anim-west",5,5,M_LOOP},
  {"work_drive","work-drive/anim-east",7,5,M_LOOP},
  {"work_dig","work-dig/anim",5,5,M_LOOP},
  {"work_carrot","work-carrot/anim",5,4,M_LOOP},
  // Teen. Only the animations the budget covered exist here; pa() silently falls back to the
  // adult art for anything absent, so adding more later needs no code change.
  // Slow: an idle at 5fps read as fidgeting. 3fps keeps it as gentle breathing.
  {"teen_idle","teen-idle",5,3,M_LOOP},
  // 5 frames, not 4: v3 keeps the reference frame as frame 0. Slower than the adult's 9fps —
  // this pet ambles. Regenerated with v3 after the walking-4-frames template mangled them.
  {"teen_walk_down","teen-walk-south",5,5,M_LOOP}, {"teen_walk_up","teen-walk-north",5,5,M_LOOP},
  {"teen_walk_left","teen-walk-west",5,5,M_LOOP},  {"teen_walk_right","teen-walk-east",5,5,M_LOOP},
  // Each of these is its own generated STATE, animated — the same way the baby set is built,
  // not an action description played over the base pose.
  {"teen_tired","teen-tired",5,4,M_LOOP},   {"teen_hungry","teen-hungry",5,4,M_LOOP},
  {"teen_bored","teen-bored",5,4,M_LOOP},   {"teen_eat","teen-eat",5,5,M_PINGPONG},
  {"teen_bath","teen-bath",5,4,M_LOOP},     {"teen_sleep","teen-sleep",5,2,M_LOOP},
  {"teen_angry","teen-angry",5,5,M_LOOP},   {"teen_love","teen-love",5,4,M_LOOP},
  {"teen_play","teen-play",5,5,M_PINGPONG}, {"teen_sick","teen-sick",5,4,M_LOOP},
  // Headphones on, dancing. Faster fps than the other moods because it needs to read as
  // dancing rather than swaying, and 9 frames so the loop does not look like a twitch.
  {"teen_dance","teen-dance",9,8,M_LOOP},
  // Kids' revision (2026-08-07): pulls his phone out between wanders. The
  // trigger also pakFind-gates on teen-text/0 so an OTA'd firmware without
  // the USB pak flash simply never fires it.
  {"teen_text","teen-text",5,5,M_LOOP},
  // School — the teen's answer to the adult's work sequence.
  // 7 frames: regenerated at 6 (+1 reference) because the 4-frame version read as shifting
  // weight on the spot rather than a walk cycle.
  {"school_bag","school-bag/anim-west",7,6,M_LOOP},
  {"school_desk","school-desk/anim",5,5,M_LOOP},
  {"school_star","school-star/anim",5,4,M_LOOP},
};
static const int N_ANIMS = sizeof(ANIMS) / sizeof(ANIMS[0]);
static const char *EGG_F[5] = {"egg-1-whole/south","egg-2-crack/south","egg-3-wiggle/south",
                               "egg-4-baby-visible/south","egg-5-just-hatched/south"};
// The HTML treats the egg as an ANIMATION â€” egg_hatch, 5 frames, 1fps, mode "once" â€” that
// plays through on its own. I was indexing the frame by tap count instead, so the sequence
// only advanced when you poked it and jumped straight to the last frame after a reset.
static const AnimDef EGG_ANIM = {"egg_hatch", "", 5, 1.0f, M_ONCE};

// THE CHILD'S OWN ANIMATIONS, from scene.json. The scene table (g_scAnim) owns the string
// storage; these AnimDef mirrors just point into it so the rest of the render path never
// learns a second shape. Re-mirrored whenever a scene loads (g_scGen moves) — and note that
// g_anim may be POINTING at one of these entries when that happens: the strings mutate in
// place, currentFrame() clamps/wraps against the new counts, and spriteLoad's %s/0 fallback
// covers a folder that vanished. Worst case is one odd frame, never a crash.
static AnimDef  g_customAnim[SCENE_MAX_ANIMS];
static uint16_t g_customGen = 0xFFFF;
static void syncCustomAnims() {
  if (g_customGen == g_scGen) return;
  g_customGen = g_scGen;
  for (int i = 0; i < g_scAnimN; i++) {
    g_customAnim[i].key    = g_scAnim[i].key;
    g_customAnim[i].folder = g_scAnim[i].folder;
    g_customAnim[i].frames = g_scAnim[i].frames;
    g_customAnim[i].fps    = g_scAnim[i].fps;
    g_customAnim[i].mode   = g_scAnim[i].mode;   // scene.h documents 0/1/2 = LOOP/PINGPONG/ONCE
  }
}
static const AnimDef *findAnim(const char *k) {
  for (int i = 0; i < N_ANIMS; i++) if (!strcmp(ANIMS[i].key, k)) return &ANIMS[i];
  syncCustomAnims();
  for (int i = 0; i < g_scAnimN; i++) if (!strcmp(g_customAnim[i].key, k)) return &g_customAnim[i];
  return &ANIMS[0];
}
static bool hasAnim(const char *k) {
  for (int i = 0; i < N_ANIMS; i++) if (!strcmp(ANIMS[i].key, k)) return true;
  syncCustomAnims();
  for (int i = 0; i < g_scAnimN; i++) if (!strcmp(g_customAnim[i].key, k)) return true;
  return false;
}
static const AnimDef *g_anim = &ANIMS[0];
static float g_animT = 0;
// While set, the animation clock is frozen — used for the beat of stillness on arriving
// somewhere, so a walk resolves into a stop before the idle cycle starts.
static uint32_t g_holdUntil = 0;
// He has a target but has not set off yet — the pause before departure.
static uint32_t g_departAt = 0;
// Counts down to the next "linger": a multi-beat freeze on a single idle frame.
static float g_lingerT = 0;

static void say(const char *t);
static bool tickerAwake();   // defined after S - the announce stays quiet over a sleeper
static const char *emoteLineFor(const char *key);   // defined after scene.h - it reads the scene table
static void setAnim(const char *k) {
  const AnimDef *a = findAnim(k);
  // Changing clip ALWAYS releases the freeze. g_holdUntil stops the global animation clock, so
  // a linger that was holding an idle pose used to carry straight over into whatever came
  // next: press BATH or GAME mid-linger and the new animation sat on frame 0 for the rest of
  // the hold — up to 8 beats, longer than the action itself, which looked like being stuck in
  // the tub. A hold is meant to still ONE pose, so it cannot outlive that pose.
  // Anything that wants a new clip to start held must set g_holdUntil AFTER calling this.
  if (a != g_anim) {
    g_anim = a; g_animT = 0; g_holdUntil = 0;
    // EVERY EMOTE ANNOUNCES ITSELF (Jon: "if he is doing an emote the scroll text
    // should say so regardless of what it is") - the moment one of the feeling clips
    // goes up, the ticker says which, whatever put it there.
    const char *ln = emoteLineFor(a->key);   // acquitted with the lamps and restored
    if (ln && tickerAwake()) {
      static const char *lastLn = nullptr;
      static uint32_t lastLnMs = 0;
      if (ln != lastLn || millis() - lastLnMs > 15000) {
        lastLn = ln; lastLnMs = millis();
        say(ln);
      }
    }
  }
}
// frame sequencing honouring loop / pingpong / once, as the HTML's frameSeq does
static int currentFrame() {
  int n = g_anim->frames;
  if (n <= 1) return 0;
  int t = (int)(g_animT * g_anim->fps);
  switch (g_anim->mode) {
    case M_PINGPONG: { int p = 2 * n - 2; int i = t % p; return i < n ? i : p - i; }
    case M_ONCE:     return t >= n ? n - 1 : t;
    default:         return t % n;
  }
}

// ---------------- state ----------------
static GameState S;
static bool tickerAwake() { return S.lights != 0; }

// The editor's breathing, at last a device behaviour: while a custom animation shows, the
// whole character pulses about his feet - 1 + br% * sin(2pi * frame / period). The blit
// anchors bottom-centre, so scaling IS feet-pinned for free. Base clips never breathe.
static float customBreathe() {
  for (int i = 0; i < g_scAnimN; i++) {
    if (g_anim != &g_customAnim[i]) continue;
    float br = g_scAnim[i].breathe;
    if (br <= 0.01f) return 1.0f;
    // THE ASSEMBLER'S OWN FORMULA, verbatim ("the breathing animation isnt the same" - the
    // golden rule outranks my earlier floors and clamps): pulse = br% * sin(2pi * step /
    // period), sampled at the INTEGER sequence step, exactly as drawChar computes it. If a
    // breath is too subtle or too slow, the child turns the dial in the editor and both
    // sides move together.
    float per = g_scAnim[i].bper > 1 ? (float)g_scAnim[i].bper : 8.0f;
    float fps = g_anim->fps > 0.1f ? g_anim->fps : 7.0f;
    float step = floorf(g_animT * fps);
    return 1.0f + (br / 100.0f) * sinf(2.0f * (float)M_PI * step / per);
  }
  // A SPECIES PET AT REST MUST STILL BE ALIVE ("hes not breathing at idle"). The bunny's
  // idle is five drawn frames; a character-pack idle is one held rotation, and a held frame
  // is a statue. So the stand-in clips get a gentle built-in breath - 2.5% about the feet,
  // one breath every ~3.5s, the editor's own motion at the editor's usual numbers.
  if (S.species_idx > 0) {
    const char *k = g_anim->key;
    if (!strcmp(k, "idle") || !strcmp(k, "idle_n") || !strcmp(k, "sleep") ||
        !strcmp(k, "jump") || !strcmp(k, "baby_idle") || !strcmp(k, "baby_sleep") ||
        !strcmp(k, "baby_sit"))
      return 1.0f + 0.025f * sinf(2.0f * (float)M_PI * g_animT / 3.5f);
  }
  return 1.0f;
}

// ---------------- character packs (CHARACTER-PACKS.md §4, §5) ----------------
typedef enum {
  INHERIT_UPRIGHT = 0,   // sits upright, mammal proportions — may inherit from the base pack
  INHERIT_NONE    = 1,   // body plan too different — must supply every frame itself
} InheritClass;

typedef struct {
  const char  *id;        // "croc", 8 chars max, [a-z0-9]; "" is the base pack
  const char  *display;   // ticker/menu name
  InheritClass inherit;
  bool         can_hold;  // false -> a held object goes on the floor beside him, never dropped
  int8_t       carryDX;   // where a carried thing rides, relative to the classic
  int8_t       carryDY;   // in-front-of-him point (0,0 = hands; the penguin's is his BEAK)
} CharacterDef;

// Index 0 MUST be the base pack with an EMPTY id: that is what makes the override a no-op for
// the default pet, so none of this can move a pixel until art exists. No second entry until
// there is a pak to point it at — an id with no frames behind it would miss on every sprite
// and cost a wasted pak scan per draw.
static const CharacterDef CHARACTERS[] = {
  { "", "Bunny", INHERIT_UPRIGHT, true, 0, 0 },
  // The first real species (2026-08-17, "i still see the rabbit on the device"). The frames
  // come from the assembler-era capybara pack, mapped onto ANIMS[] folders by
  // tools/mkspecies.py: full adult set; baby rides its crawl art for the walk keys; teen and
  // the work/school sequences deliberately fall back to the base pack until clips exist.
  { "capybara", "Capybara", INHERIT_UPRIGHT, true, 0, 0 },
  // The five-pack cast (2026-08-18, adult-only, owner-curated bases). Work/school/teen
  // keys fall back to the base pack until such clips exist, exactly as the capybara does.
  { "bunny",   "Bunny",   INHERIT_UPRIGHT, true, 0, 0 },
  { "cat",     "Cat",     INHERIT_UPRIGHT, true, 0, 0 },
  { "dog",     "Dog",     INHERIT_UPRIGHT, true, 0, 0 },
  { "frog",    "Frog",    INHERIT_UPRIGHT, true, 0, 0 },
  // THE BEAK CARRY (Jon: "I'm fine with the beak"): flippers cannot hug a jar, so a
  // carried thing rides at his beak - higher and a touch further forward than hands.
  { "penguin", "Penguin", INHERIT_UPRIGHT, true, 2, -12 },
  // the style reference joins the cast ("let's finish the croc") - hands, like the others
  { "croc",    "Croc",    INHERIT_UPRIGHT, true, 0, 0 },
};
static const int CHARACTERS_N = (int)(sizeof(CHARACTERS) / sizeof(CHARACTERS[0]));

// ---- body width, and why the walkable model needs one ----
//
// BOUNDS_* and BLOCKS_* describe where a POINT may stand — clearOfBlocks() tests the feet and
// only ever pushes down in y. Nothing in the model knows how wide the animal is. That worked
// only because the constants were hand-fitted to ONE animal: measured off the shipped pak, the
// bunny fills 27 of his 96px canvas, and BOUNDS_ADULT.x1 = 298 puts his right edge at 319.8 on
// a 320px screen. Tuned to within a pixel.
//
// A shared body with a species head and tail changes exactly this: the overhang. So the width
// is measured, never declared — see charBodyW(), derived from the widest trimmed frame the pak
// actually contains. Hand-entering it is how it would silently stop matching the art.
//
// The bunny's 27 is the CALIBRATION, not a limit: every inset below is computed relative to it,
// so index 0 insets by zero and the hand-tuned constants keep their exact present meaning.
static const int BASE_BODY_W = 27;

// Filled at pakBegin() from the index. [0] is pinned to BASE_BODY_W: the base pack's character
// frames cannot be told apart from items/, icons/, games/ and rooms/ by name alone, and
// guessing would be worse than the calibration we measured.
static int g_charBodyW[CHARACTERS_N];

static int charBodyW() {
  int i = (S.species_idx < CHARACTERS_N) ? S.species_idx : 0;
  return g_charBodyW[i] > 0 ? g_charBodyW[i] : BASE_BODY_W;
}

// How much wider than the bunny this species is, in SCREEN px each side, at the current phase
// scale. Zero for the base pack, by construction.
static float charOverhang(float scale) {
  float d = (float)(charBodyW() - BASE_BODY_W) * 0.5f * scale;
  return d > 0 ? d : 0;
}

// Derive each species' body width from the pak — the widest trimmed frame it actually carries,
// measured as the reach either side of the canvas centre that spriteBlit() anchors on.
//
// MEASURED AT MOUNT, not baked in at compile time, and that is deliberate. The art lives in its
// own partition and updates on its own path: a firmware build and an art build are separate
// downloads (approved.json names both), so a compiled-in width would go stale the first time
// the art shipped without the firmware — and it would go stale SILENTLY, as a character that
// walks through the bed. The converter computes the same number at build time and asserts it;
// this is the consumer, so the two cannot disagree without the assert firing.
static void charMeasureBodies() {
  g_charBodyW[0] = BASE_BODY_W;          // the calibration; see BASE_BODY_W
  for (int c = 1; c < CHARACTERS_N; c++) {
    const char *id = CHARACTERS[c].id;
    g_charBodyW[c] = 0;
    if (!id || !*id) continue;
    size_t n = strlen(id);
    // THE STANDING IDLE IS THE BODY, not the widest frame. Widest-of-everything charged a
    // species for its lying-down sleep pose (capybara: sleep spans the full 96px canvas ->
    // a 50px walkable inset each side, nowhere left to stand) while the bunny stays pinned
    // to his idle's 27. Everyone is measured the same way now: the frame he STANDS in.
    // Widest-of-all remains the fallback for a pack with no idle override.
    char idleKey[48];
    snprintf(idleKey, sizeof(idleKey), "%s/idle-anim/0", id);
    int widest = 0;
    for (uint16_t i = 0; i < g_count; i++) {
      const PakEntry &e = g_index[i];
      if (strncmp(e.name, id, n) || e.name[n] != '/') continue;
      int left  = -(int)e.origW / 2 + e.offX;      // opaque box, relative to the feet
      int right = left + (int)e.w;
      int half  = (-left > right ? -left : right);  // whichever side reaches further
      if (!strncmp(e.name, idleKey, sizeof(e.name))) { widest = half * 2; break; }
      if (half * 2 > widest) widest = half * 2;
    }
    g_charBodyW[c] = widest;
    Serial.printf("char: %-8s body_w=%d (base %d)%s\n", id, widest, BASE_BODY_W,
                  widest == 0 ? "  <-- NO FRAMES, falls back to base" : "");
  }
}

// key -> "<species>/key" when the pak really carries that frame, else key untouched.
//
// The pakFind() test is the point: a species that overrides only some states still gets base
// frames for the rest, and a species whose art is missing degrades to the bunny rather than to
// a blank sprite. Caller owns the buffer, so this stays allocation-free on the draw path.
//
// It belongs at the CALL SITES, never inside spriteLoad(). That loader also serves items/,
// icons/ and games/ keys; prefixing down there would let a species silently shadow a shared
// object and would double the pak scan for every sprite drawn.
static const char *charSpriteKey(const char *key, char *buf, size_t bufLen) {
  if (S.species_idx == 0 || S.species_idx >= CHARACTERS_N) return key;
  const char *id = CHARACTERS[S.species_idx].id;
  if (!id || !*id) return key;
  snprintf(buf, bufLen, "%s/%s", id, key);
  if (pakFind(buf)) return buf;
  // a CUSTOM animation's frames (canim/) are the child's own art, not species art -
  // the idle fallback below must never eat them ("he is no longer doing my bath
  // animation": the first version swapped every canim frame for the penguin's idle)
  if (!strncmp(key, "canim/", 6)) return key;
  // DEFAULT TO IDLE, NEVER TO ANOTHER ANIMAL (Jon: "he became a bunny and was walking
  // north for a bit"): a clip this species pack lacks used to fall through to the base
  // pack's BUNNY frames mid-move. The species' own idle is the honest stand-in - same
  // frame number when it exists so the beat still reads, else its first frame.
  const char *slash = strrchr(key, '/');
  int fr = slash ? atoi(slash + 1) : 0;
  const char *idleFolder = (S.phase == PH_BABY) ? "baby-idle"
                         : (S.phase == PH_TEEN) ? "teen-idle" : "idle-anim";
  snprintf(buf, bufLen, "%s/%s/%d", id, idleFolder, fr);
  if (pakFind(buf)) return buf;
  snprintf(buf, bufLen, "%s/%s/0", id, idleFolder);
  if (pakFind(buf)) return buf;
  return key;   // this species has no idle either: the base pack carries on
}

// The pet's single still frame for a phase, species-resolved and ready for spriteLoad().
//
// THIS IS THE ONLY PHASE->FOLDER MAP. Carrot Chase, Garden Guard and Bunny Hop each carried
// their own copy, and the three were required to agree with the render path forever. They
// cannot be allowed to drift, because the naming is ASYMMETRIC — adult idle is `idle-anim`
// with no prefix, while baby and teen are `baby-idle`/`teen-idle` — and the folder names are
// DICTATED by ANIMS[], not chosen, since the override key is species + "/" + folder. Three
// copies of an asymmetric mapping is precisely how one copy ends up wrong.
//
// The returned pointer stays valid until the next call and is meant to be handed straight to
// spriteLoad(). That is safe for the same reason spriteLoad() itself is: everything that draws
// runs in the bunbun task, and spriteLoad already owns one shared g_spr/g_meta/g_sprName slot.
static const char *petFrameKey(uint8_t phase) {
  const char *base = phase == PH_BABY ? "baby-idle/0"
                   : phase == PH_TEEN ? "teen-idle/0"
                                      : "idle-anim/0";
  static char buf[64];
  return charSpriteKey(base, buf, sizeof(buf));
}

static float g_fx = 160, g_fy = FLOOR_Y;
static Preferences prefs;
static Preferences &whatsNewPrefs() { return prefs; }
static char g_ticker[64] = "";
static uint32_t g_tickUntil = 0;
// Scroll position, declared here because say() resets it so a new message shows immediately.
static int g_tickX = UI_W;
// Reset the scroll position so the message appears IMMEDIATELY rather than waiting to travel
// in from the right — that lag is what made the ticker feel out of step with the action.
// What is playing right now, for the ticker. Set from AirPlay metadata when the sender gives
// it, and from the filename for SD tracks. Kept separate from say() because it is a STANDING
// fact rather than a 4-second announcement — say() would show it once and lose it.
static char g_nowPlaying[80] = "";
static void danceBtnInvite();   // W-024 invitation window; defined with dance
static void setNowPlaying(const char *t) {
  if (!t || !*t) { g_nowPlaying[0] = 0; return; }
  // W-024: a song starting or changing is exactly the moment the DANCE
  // invitation should appear (both AirPlay metadata and SD announce land
  // here). Only on a real change, so re-sent metadata doesn't re-invite.
  if (strncmp(g_nowPlaying, t, sizeof(g_nowPlaying) - 1) != 0) {
    danceBtnInvite();
    // W-047: a new song excites him - but at most once per ten minutes,
    // or an album becomes a trill metronome (Ivy's sound-fatigue rule).
    static uint32_t exciteAt = 0;
    if (millis() - exciteAt > 600000UL) { exciteAt = millis(); sfxExcited(); }
  }
  strncpy(g_nowPlaying, t, sizeof(g_nowPlaying) - 1);
  g_nowPlaying[sizeof(g_nowPlaying) - 1] = 0;
}

// Every line the pet says passes through here, which is the one place worth
// teaching his NAME (the kids, 2026-08-09: they want to see the name they
// picked, not the species). Dozens of messages were written with "bunbun"
// baked in — "bunbun ate a meal", "bunbun is asleep" — so instead of
// rewriting each one, the token is swapped on the way to the ticker. A pet
// still called bunbun reads exactly as before; a pet called Clover hears
// her own name everywhere, forever, with no message left behind.
// W-050 (Jon, 8/10: "I thought when it says bunbun is happy or loved or
// something it would make the sound"): the mood listener. Every spoken
// line passes through here, so this is where words become audible
// feelings — love purrs, joy hops. Manners: never speaks over a voice
// that just fired at the call site (sfxNo/sfxOK etc. — if any voice is
// active, the mood yields), and at most one mood sound per minute so the
// ticker can't turn into a jingle machine.
static void sayMood(const char *t) {
  static uint32_t moodAt = 0;
  if (g_fxLevel <= 0 || millis() - moodAt < 60000UL) return;
  for (int i = 0; i < SFX_VOICES; i++)
    if (g_voice[i].active) return;      // the site already spoke
  if (strstr(t, "love") || strstr(t, "cuddle") || strstr(t, "missed you")) {
    sfxPurr();  moodAt = millis();
  } else if (strstr(t, "fun") || strstr(t, "happy") || strstr(t, "hooray") ||
             strstr(t, "good morning")) {
    sfxHomeAgain();  moodAt = millis();
  }
}

static void say(const char *t) {
  sayMood(t);
  const char *nm = g_petName;
  const char *tok = nm[0] ? strstr(t, "bunbun") : nullptr;
  if (tok && strcasecmp(nm, "bunbun") != 0) {
    size_t pre = (size_t)(tok - t);
    if (pre > 62) pre = 62;
    size_t n = pre;
    memcpy(g_ticker, t, n);
    // Keep the sentence's own capitalisation: a line that opened with
    // "bunbun" gets the name as the kid typed it either way, but a
    // mid-sentence swap must not shout.
    for (size_t i = 0; nm[i] && n < 63; i++) g_ticker[n++] = nm[i];
    const char *rest = tok + 6;
    while (*rest && n < 63) g_ticker[n++] = *rest++;
    g_ticker[n] = 0;
  } else {
    strncpy(g_ticker, t, 63); g_ticker[63] = 0;
  }
  g_tickUntil = millis() + 4000;
  g_tickX = 4;
}

// ---- THE NAME RULE (the kids' order, menu redesign P1) ----
// say() renames every ticker line, but a handful of screens print "bunbun"
// straight onto the glass — the nap face, the reset confirm, the game status
// lines, the boot splash. These helpers are the one door those screens go
// through now: the name exactly as the kid typed it, a shouting copy for
// BUTTON CAPS, and a formatter for the printf-shaped sites. fitFont() is the
// other half of the order: a kid's name is NEVER truncated — the font steps
// down 4 -> 2 -> 1 until the whole line fits its budget instead.
static const char *petName() { return g_petName[0] ? g_petName : "bunbun"; }
static size_t petNameUpper(char *dst, size_t n) {
  const char *s = petName();
  size_t i = 0;
  for (; s[i] && i + 1 < n; i++) {
    char c = s[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    dst[i] = c;
  }
  dst[i] = 0;
  return i;
}
static void fmtPet(char *d, size_t n, const char *fmt) { snprintf(d, n, fmt, petName()); }
static void say(const char *t);
// say() with the pet's name folded in — the ticker's own substitution only fires on the
// literal word "bunbun", and these lines are built from the kid's chosen name instead.
static void fmtPetSay(const char *fmt) {
  char b[80];
  fmtPet(b, sizeof(b), fmt);
  say(b);
}
// An empty house is quiet (Jon 8/14: "music should turn off as well" — "him leaving should
// be lonely"). Only the pet's OWN music stops; a phone streaming over AirPlay is a guest's
// audio and is never touched by the pet's mood. Defined down with g_musicOn.
static void awayHush();
static void awayUnhush();
// maxFont caps the ladder: a small button asks for 2 so a short name doesn't
// come back font-4 huge inside a 40px chip.
static int fitFont(const char *s, int maxW, int maxFont = 4) {
  if (maxFont >= 4 && tft.textWidth(s, 4) <= maxW) return 4;
  if (maxFont >= 2 && tft.textWidth(s, 2) <= maxW) return 2;
  return 1;
}

static long ageMin() { return (long)(S.ageMs / 60000); }
// TEST_ADULT forces the adult phase WITHOUT touching ageMs, so the real save is untouched and
// flashing the normal build back restores the actual pet at its actual age. Waiting out 24h
// is not a practical way to check the adult room, animations and bounds.
// The room he lives in. Teen falls back to the adult room until room-teen is in the pack, so
// the phase is playable the moment the code lands rather than waiting on the art.
static const char *phaseRoom() {
  // a scene may bring its own room; the pet's phase decides only when it does not
  const char *sr = sceneRoom();
  if (sr && pakFind(sr)) return sr;
  if (S.phase == PH_BABY) return "rooms/room-baby";
  if (S.phase == PH_TEEN && pakFind("rooms/room-teen")) return "rooms/room-teen";
  return "rooms/room-adult";
}

// Maps a bare animation name onto the current phase: "walk_left" becomes "baby_walk_left",
// "teen_walk_left", or stays "walk_left" for an adult.
//
// The teen deliberately FALLS BACK to the adult art when a teen_ variant does not exist. The
// generation budget does not cover a full teen set, so the teen has his own idle, walk and a
// few moods and borrows the rest — which reads fine, since he is the same rabbit at a size
// between the two. Adding art later needs no code change: the lookup picks it up.
static const char *pa(const char *base) {
  static char buf[40];
  // The baby falls back exactly like the teen below. It used to return "baby_<x>" unconditionally,
  // so a MISSING baby clip did not fall back to the adult equivalent - it failed the lookup and
  // landed on ANIMS[0], which is the adult IDLE. A baby then walked, ate, bathed and slept by
  // standing still. Harmless for this rabbit, whose baby set is complete; fatal for the first
  // creature a child makes with one clip set, because baby is the phase every new pet starts in
  // and standing still is the first thing they would ever see it do.
  if (S.phase == PH_BABY) {
    snprintf(buf, sizeof(buf), "baby_%s", base);
    if (hasAnim(buf)) return buf;
  }
  if (S.phase == PH_TEEN) {
    snprintf(buf, sizeof(buf), "teen_%s", base);
    if (hasAnim(buf)) return buf;
  }
  return base;
}

static Phase phaseOf() {
  // ADULT-ONLY (Jon 8/18: "he stays an adult since we got rid of the aging") - a
  // fresh egg, a restored save, a clock hiccup: none of them may show a baby again.
  // ageMin() keeps counting; it is the OTA pet-preservation signal, not a life stage.
  return PH_ADULT;
}
static bool babyCanStand() { return ageMin() >= TODDLER_AT; }
static bool babyWalksOnly() { return ageMin() >= WALKER_AT; }
static const PhaseRates &rates() {
  return S.phase == PH_BABY ? RATES_BABY : S.phase == PH_TEEN ? RATES_TEEN : RATES_ADULT;
}
static float spriteScale() {
  float s = S.phase == PH_BABY ? SCALE_BABY : S.phase == PH_TEEN ? SCALE_TEEN : SCALE_ADULT;
  return s * VIEW;
}
// The assembler's master size dial, applied at DRAW time: species frames are baked at a
// fixed 0.5 of the source art, and the scene says how big he travels (ts). Composed
// animation frames already carry their authored size, so they never take this factor.
// ("the new scaler on the device doesnt seem to adjust walking")
static float travelFactor() {
  if (S.species_idx == 0 || g_scTravel <= 0) return 1.0f;
  return g_scTravel / 0.5f;
}
static Bounds bounds() {
  // an uploaded scene describes the floor it drew; with no scene, the room is the built-in one
  Bounds b;
  if (!(sceneActive() && sceneBounds(&b)))
    b = S.phase == PH_BABY ? BOUNDS_BABY : S.phase == PH_TEEN ? BOUNDS_TEEN : BOUNDS_ADULT;
  // Inset by however much wider than the bunny this species is, so a head or a tail stops at
  // the wall instead of leaving the room. These constants were fitted to the bunny with about a
  // pixel to spare on the right at adult scale, so there is no slack to borrow. Zero inset for
  // index 0: the rectangle keeps exactly the meaning it has always had.
  int o = (int)(charOverhang(spriteScale() * travelFactor()) + 0.5f);
  if (o > 0) {
    b.x0 += o;
    b.x1 -= o;
    // A species wide enough to close the lane gets one standing spot rather than an inverted
    // rectangle, which every rejection-sampling loop in here would spin on forever.
    if (b.x1 < b.x0) b.x0 = b.x1 = (b.x0 + b.x1) / 2;
  }
  return b;
}
static bool alive() { return S.stage == STAGE_ALIVE; }

// ---- behaviour state (declared before freshState, which clears it) ----
// W-028 Lou's clause: when bunbun last woke (lights 0->1). 0 = this boot,
// which makes uptime itself the grace period — no 7am (or power-on) ambush.
static uint32_t g_wokeSickMs = 0;
// The LOVE meter (Jon, launch night): fills from cuddles and petting,
// drains slowly through the day, sleeps untouched at night. Lives OUTSIDE
// GameState on purpose — growing that struct invalidates every existing
// save. Persisted to NVS on cuddle/pet events, never on a timer (the
// flash-freeze lesson).
static float g_love = 70.0f;
static uint32_t g_loveSavedMs = 0;
// W-036 consequence mode ("BRAVE"), chosen once on a new game right after
// naming (Jon's ruling). In brave mode, TRULY sustained neglect — already
// sick AND still starving/filthy for hours more — sends bunbun on a
// run-away day: the room sits empty, care buttons explain, and he always
// comes home. The ghost stays sealed (council, 10-0). Cozy mode = today's
// behaviour exactly. NVS-stored, never in GameState.
static bool g_modeBrave = false;
static bool g_modeAsk = false;         // the chooser screen is up
static bool g_modePainted = false;
static uint32_t g_awayUntil = 0;       // millis deadline while away, else 0
// Kid-council revision (launch week, from the actual kids): coming home is
// EARNED — while bunbun is away the MEDS button becomes TREATS, and putting
// treats out is what draws him back, ten minutes on the dot. The sealed rule
// survives as a failsafe: with no treats at all he still comes home after
// 24 h, because he must never be permanently gone on a kid who didn't find
// the button.
static uint32_t g_treatsOutMs = 0;     // when treats were put out, else 0
static bool bunAway() { return g_awayUntil && millis() < g_awayUntil; }
// THE HOMECOMING SCENE (Jon 8/14). Coming back used to be instant: one tick he was gone,
// the next he was standing in the middle of the room. Now it plays out — he walks in from
// the doorway, crosses to the basket, the basket goes, and he eats. 0 = not happening,
// 1 = walking to the basket, 2 = eating. The room is his again either way; the scene only
// owns where he walks, and his stats stay exactly where the neglect left them.
static uint8_t g_homeStage = 0;
static uint32_t g_homeAt = 0;
// Where the basket sits: the middle of the floor, in the room's own 320x240 coordinates.
// One place, so the drawing and the walk target can never disagree about where it is.
static const int TREAT_X = 160, TREAT_Y = FLOOR_Y + 6;
static inline bool bunComingHome() { return g_homeStage != 0; }
static uint32_t g_sleepAtMs = 0;       // when lights last went out (evening hold)
static void loveSave() {
  if (millis() - g_loveSavedMs < 30000) return;
  g_loveSavedMs = millis();
  prefs.begin("bunbun", false);
  prefs.putFloat("love", g_love);
  prefs.end();
}
static int g_tx = -1, g_ty = -1;
static float g_wanderT = 0, g_emoteT = 9;
static int      g_hopsLeft = -1;      // hops left in this flurry; -1 = start a fresh one
static uint32_t g_restUntil = 0;      // flopped until this, and nothing performs
// Passive furniture use, from the HTML's pickVisit/arriveAtSpot: he wanders to the rug or
// the crib and settles into it for a while. This is what makes him sit down and PLAY rather
// than only ever walking and idling.
static uint32_t g_settleUntil = 0, g_nextVisitOK = 0;
static int g_visit = -1;                       // 0 = rug, 1 = crib, 2 = radio, 3 = a placed mark
static char g_markAnim[16] = "";               // the pose the child put at that mark
// the assembler's rhythm: activity -> one wander -> activity, never the same mark twice
// in a row when there is a choice
static int  g_lastMark = -1;
static bool g_lastWasMark = false;
// The EXACT authored spot of the mark being visited. The assembler walks to legal ground
// NEAR a thing and then settles ONTO the authored point - which may be up on the beanbag or
// inside the tub's footprint. Walking straight at such a point fought clearOfBlocks forever
// and hit the 9s give-up, which is why bathing never showed: the trip targets the front
// doorstep (clearOfBlocks's own push-out), and arrival snaps onto this.
static int g_markX = 0, g_markY = 0;
// Lights-out/tuck-in taken AT THE SLEEP SPOT: the button starts the walk, and this says what
// to apply when he arrives (1 = lights out, 2 = the night hold). Applying it at press time
// would trip the asleep-guard and cancel the very walk to bed.
static uint8_t g_sleepPending = 0;
// Jon's model, final form: "idle and sit are the only animations that arent driven by
// something and are essentially the fillers". Ambient wandering may visit FILLER marks;
// eat, bath and sleep happen only when something DRIVES them - a button, a need, bedtime.
static bool markIsFiller(int m) {
  for (int i = 0; i < g_scAnimN; i++)
    if (!strcmp(g_scAnim[i].key, g_scBun[m].anim)) {
      const char *a = g_scAnim[i].act;
      return !(!strcmp(a, "bath") || !strcmp(a, "sleep") || !strcmp(a, "eat"));
    }
  return true;    // built-in pose marks (play, love...) stay ambient, exactly as before
}
static const int RUG_X = 170, RUG_Y = FLOOR_Y + 4;
static const int CRIB_X = 86, CRIB_Y = FLOOR_Y - 6;
// Where the teen's radio sits, and where he stands to dance next to it. Declared up here with
// the other destinations because think() picks targets long before the drawing code runs.
// x236 put it ON TOP of the painted desk chair — measured off room-teen.png, the clear floor
// is the strip in front of the bed, so it sits there and he dances to its right.
static const int RADIO_X = 96, RADIO_Y = FLOOR_Y + 6;
// The stand spot is pushed clear of the bed block at runtime, so this y is only a hint.
static const int RADIO_STAND_X = 140, RADIO_STAND_Y = FLOOR_Y + 22;
static bool g_action = false;
static uint32_t g_actionEnd = 0;
static int g_eggTaps = 0;
static uint32_t g_poopDue = 0;             // a mess arrives a while after eating
static bool g_nightSleep = false;          // SLEEP double-tapped in the evening: down until morning
// W-059: the family hours. Bedtime start gates the auto-nap window; the
// morning number moves BOTH wakes together (Grim's clause — the screen and
// the pet must never disagree about when morning is). Defaults are the
// owner's original spec so no unit changes behavior silently.
static int g_bedStartMin = 22 * 60;        // nap-window start: 8pm / 9pm / 10pm
static int g_bedEndMin   = 6 * 60;         // morning: 6:00 / 6:30 / 7:00
// W-059: quiet until greeted — after any night sleep bunbun wakes SILENT
// (no peeps, need-voices, or motor) until the day's first touch, which
// answers with the purr. Manners, not a mute (Ivy): ticker text still
// speaks, human-started music is never gated.
static bool g_quietGreet = false;
#include "haptics.h"                       // W-022 — needs prefs, S, g_nightSleep, g_lastTouchMs above
static float nightAmount();                // defined with the clock, below
static float daylight();                   // ditto
static int clockNowMin();                  // ditto — night sleep needs the wall clock
static bool schoolHoursNow();              // W-029 — simulate() sits above the clock section
static float rainAmount();                 // defined with the weather, below
// visitor state, declared here because the movement code reacts to it
static int g_birdPhase = 0;                // 0 none, 1 arriving, 2 perched, 3 leaving
static bool g_birdReacted = false, g_watching = false;
static uint32_t g_birdLeaveAt = 0;
// The 6-9h toddler window: he tries to walk, gets part way, then tumbles back into a crawl.
// crawlFrac is the fraction of the trip walked upright before falling; >1 means no fall.
static float g_crawlFrac = 2.0f, g_tripLen = 0;
static bool  g_crawling = false;

// RESET must clear the RUNTIME state too, not just the saved struct. Leaving the tap count
// behind meant a new egg opened already cracked and hatched on the first press; leaving the
// wander/settle timers behind meant the new bunbun inherited the old one's plans.
static void saveName();   // defined just below, next to the other NVS writers

static void freshState() {
  // A new egg is a new pet, so it gets a new name. Clearing this is what raises the naming
  // screen again on the next loop.
  g_petName[0] = 0;
  saveName();
  g_nameAsk = true;
  g_namePainted = false;
  memset(&S, 0, sizeof(S));
  S.magic = SAVE_MAGIC; S.version = SAVE_VERSION;
  S.stage = STAGE_EGG; S.phase = PH_BABY;
  S.food = S.fun = S.clean = S.energy = S.health = S.disc = 100;
  S.lights = 1; S.x = 160; S.y = FLOOR_Y;
  g_fx = 160; g_fy = FLOOR_Y;
  g_eggTaps = 0;
  g_tx = g_ty = -1; g_visit = -1;
  g_settleUntil = 0; g_nextVisitOK = 0;
  g_action = false; g_wanderT = 0; g_emoteT = 9;
  g_anim = &EGG_ANIM; g_animT = 0;
}
static void loadName() {
  prefs.begin("bunbun", true);
  String n = prefs.getString("petname", "");
  prefs.end();
  strncpy(g_petName, n.c_str(), sizeof(g_petName) - 1);
  g_petName[sizeof(g_petName) - 1] = 0;
  buildAirName();
}

static void saveName() {
  prefs.begin("bunbun", false);
  prefs.putString("petname", g_petName);
  prefs.end();
  buildAirName();
}

static void saveState() { prefs.begin("bunbun", false); prefs.putBytes("state", &S, sizeof(S)); prefs.end(); }

// Sleep-state persistence (Jon, 8/11: "upon update the game needs to return
// to its state like screen off, nap, or sleeping through the night"). An OTA
// reboots the unit; without this a bunny updated at 2am wakes wide awake at
// 2am. The byte is written only on the (infrequent) sleep transitions - the
// event-only-persist rule that keeps flash writes off the render path - and
// restored on the next boot IF that boot was a software reset (an OTA or a
// crash), never a power-on: pulling the plug is a human choosing a fresh
// start. 0=awake 1=screen-off 2=auto-nap 3=night-sleep.
static void saveSleepState(uint8_t s) {
  prefs.begin("bunbun", false);
  prefs.putUChar("sleepst", s);
  prefs.end();
}
static uint8_t g_pendingSleepRestore = 0;   // read at boot, applied in loop()

// A backup copy of the save, written on a slower cadence than the 20s primary
// (below), and NEVER written by freshState. loadState falls back to it if the
// primary key is unreadable. Cheap insurance against a single-key mishap.
static void saveStateBackup() {
  prefs.begin("bunbun", false);
  prefs.putBytes("statebk", &S, sizeof(S));
  prefs.end();
}

// Parse a save from one NVS key into S. SIZE-TOLERANT by design: it keys only
// on the magic + version, never on the blob's LENGTH. The old loader demanded
// getBytesLength == sizeof(GameState), so the day the struct grew a field (or a
// unit carried an older-lineage save), every existing pet was rejected and
// hatched as a fresh egg on update — exactly the "OTA reset my pet to baby"
// non-negotiable (5DC0, 8/12). Now a shorter blob reads into a zero-initialised
// struct (new fields default to zero, then get sane values); a longer blob is
// read up to our struct size. Only a wrong magic or an incompatible version is
// grounds to refuse — and refusing here NEVER wipes the save, it just means we
// try the backup, and only if that also fails do we hatch a new egg.
// prefs must already be begun (read scope) by the caller.
static bool loadStateFrom(const char *key) {
  size_t n = prefs.getBytesLength(key);
  if (n == 0) return false;
  GameState t; memset(&t, 0, sizeof(t));
  prefs.getBytes(key, &t, sizeof(t));   // reads min(n, sizeof(t)); any tail stays zero
  if (t.magic != SAVE_MAGIC) return false;
  if (!(t.version == SAVE_VERSION || t.version == 3)) return false;
  S = t;
  // A blob shorter than today's struct leaves the new tail fields zeroed; give
  // the ones that must not be zero a home so a migrated pet isn't dead-at-(0,0).
  if (S.x == 0 && S.y == 0) { S.x = 160; S.y = FLOOR_Y; }
  g_fx = S.x; g_fy = S.y;
  // v3 saves are structurally compatible but were accumulated under decay rates
  // 8x faster than these, so they arrive with every need on the floor. Top up.
  if (t.version < SAVE_VERSION) {
    S.food   = max(S.food,   70.0f);
    S.fun    = max(S.fun,    70.0f);
    S.clean  = max(S.clean,  70.0f);
    S.energy = max(S.energy, 70.0f);
    S.health = max(S.health, 80.0f);
    S.version = SAVE_VERSION;
  }
  return true;
}
static bool loadState() {
  prefs.begin("bunbun", true);
  bool ok = loadStateFrom("state") || loadStateFrom("statebk");
  prefs.end();
  return ok;
}
// THE UNDO FOR "start over?" (Jon, 8/18: "i just restarted him and he became a baby" -
// the start-over pin sits one thumb-width from RESTART). statebk is written on a slow
// cadence and NEVER by freshState, so for ~5 minutes after an accidental reset the real
// pet is still in it. This copies it back over the fresh egg and re-saves both keys.
extern "C" bool bunbun_restore_backup(void) {
  prefs.begin("bunbun", true);
  bool ok = loadStateFrom("statebk");
  prefs.end();
  if (ok) { saveState(); saveStateBackup(); }
  return ok;
}

// Pet snapshot for /api/system/info — so a fleet update can be verified to have
// PRESERVED the pet (stage/phase/age unchanged), not silently hatched a fresh
// egg. Added the night the strict-length loader wiped 5DC0 to baby (8/12).
extern "C" void bunbun_pet_snapshot(int *stage, int *phase, int *age_min) {
  *stage = S.stage;
  *phase = S.phase;
  *age_min = (int)(S.ageMs / 60000);
}
// Character pack, published next to the pet snapshot (CHARACTER-PACKS.md §8.1). Deliberately
// its OWN export rather than widening bunbun_pet_snapshot(): that signature is what the fleet
// preservation check already calls, and the point of §0.1 is that adding species changes
// nothing about how stage/phase/age are read.
extern "C" void bunbun_species_snapshot(int *idx, const char **id) {
  int i = (S.species_idx < CHARACTERS_N) ? (int)S.species_idx : 0;
  if (idx) *idx = i;
  if (id)  *id  = CHARACTERS[i].id;
}
// Where both actors are, in room coordinates, so "is the drawn floor actually honoured" can be
// answered by sampling and testing against the polygon instead of squinting at screenshots.
// g_fx/g_fy are the live float feet; S.x/S.y are the rounded copy the rest of the game reads.
extern "C" void bunbun_actor_snapshot(int *bx, int *by, int *cx, int *cy, int *cphase);
// AGE JUMP, for reviewing a build at every life stage without waiting days for one
// (Jon 8/14: "can we test the different ages?"). Sets the clock the phase is derived from
// and lets simulate() do the rest — phaseOf() picks the phase on the next tick, drawScene()
// reloads the room from it, and the growing-up chime plays exactly as it would have.
// Nothing else about the pet is touched: same name, same stats, same wishes. It is a
// TIME MACHINE, not a reset — which is the whole reason it is safe to expose.
// Hatches an egg first if one is still sitting there, since a phase means nothing to an egg.
// SEND HIM OFF, for reviewing the away/treat cycle on demand (Jon 8/14: "I have yet to see
// the process of bunbun leaving"). No wonder — the real trigger is a deep neglect chain:
// BRAVE mode, already sick, AND food and clean both under 10 held for two game-hours. A
// cared-for pet can never reach it, and a cozy-mode pet never can at all. This runs the
// SAME code the neglect path runs, so what you watch is the real thing: the 24h failsafe,
// the empty room, and TREATS pulling him home ten minutes later.
extern "C" void bunbun_send_away(void) {
  g_awayUntil = millis() + 24UL * 3600UL * 1000UL;
  g_treatsOutMs = 0;
  S.lights = 0;                      // same darkened room the real path leaves
  awayHush();                        // ...and the same silence (missed on the first pass)
  sfxLose();
  say("bunbun hopped away... put treats out to call him home");
  saveState();
  Serial.println("away: forced (debug) - TREATS on the care sheet brings him back");
}

// Switching species (CHARACTER-PACKS.md §8.4): never reloads the pet, never touches age or
// needs — one byte of pet state and a save. The sprite path re-resolves every frame through
// charSpriteKey(), so the change is visible on the next draw with no cache to invalidate;
// body widths for every species were measured at mount. Same task-boundary pragmatism as
// bunbun_set_age_min above: a rare, deliberate act, and the write is a single byte.
extern "C" int bunbun_set_species(const char *id) {
  for (int i = 0; i < CHARACTERS_N; i++) {
    const char *cid = CHARACTERS[i].id[0] ? CHARACTERS[i].id : "bunny";
    if (!strcasecmp(cid, id)) {
      if (S.species_idx != (uint8_t)i) {
        S.species_idx = (uint8_t)i;
        saveState();
        char line[64];
        snprintf(line, sizeof(line), "bunbun is a %s now!", CHARACTERS[i].display);
        say(line);
      }
      Serial.printf("species: %s (idx %d)\n", CHARACTERS[i].display, i);
      return i;
    }
  }
  return -1;
}
extern "C" const char *bunbun_species_id(void) {
  int i = (S.species_idx < CHARACTERS_N) ? S.species_idx : 0;
  return CHARACTERS[i].id[0] ? CHARACTERS[i].id : "bunny";
}

extern "C" void bunbun_set_age_min(int m) {
  if (m < 0) m = 0;
  if (S.stage == STAGE_EGG) { S.stage = STAGE_ALIVE; }
  S.ageMs = (int64_t)m * 60000LL;
  saveState();
  Serial.printf("age jump: %d min -> phase follows on the next tick\n", m);
}
// Every destination MUST be pushed clear of the scenery before it is used. A target that sits
// inside a block is unreachable: clearOfBlocks shoves his feet back out every frame, the
// distance never closes, and he walks on the spot forever. That is exactly what the baby
// room's WINDOW_WATCH (145,196) did in the teen room, where a bed occupies x0-152.
static void clearOfBlocks(int *x, int *y);
static void avoidChairFront(int *x, int *y);

// Where he stands to watch the window, and to visit furniture — per phase, because the rooms
// have completely different layouts.
// EVERY ERRAND DESTINATION HAS TO BE SOMEWHERE HE CAN STAND.
// The window-watch spot, the pet-the-cat spot and the furniture spots are all compiled-in
// constants measured off the SHIPPED room. A child's floor is a different shape: Jon's polygon
// starts at y=206 across the middle, while windowWatchSpot() asks for y=196. That target is in
// the WALL — so the walker pushes him up to 196, keepLegal() drags him back onto the boards
// every frame, `d < 4` never becomes true and he is stuck at the back wall for good.
// Clamping the target is the fix; the alternative is these constants travelling in the scene,
// which is the right answer and a bigger one.
static void clampErrandToFloor(int *x, int *y) {
  if (!sceneActive()) return;
  int top, bot;
  if (!sceneFloorLane(*x, &top, &bot)) return;
  if (*y < top + 2) *y = top + 2;
  if (*y > bot - 1) *y = bot - 1;
}

static void windowWatchSpot(int *x, int *y) {
  if (S.phase == PH_TEEN) { *x = 170; *y = FLOOR_Y + 6; }   // gap between bed and beanbag (browser-checked)
  else                    { *x = 145; *y = FLOOR_Y - 4; }
  clearOfBlocks(x, y);
  clampErrandToFloor(x, y);   // a child's floor may not reach the compiled-in spot
  // ...and not in front of the chair. The teen's spot is 170, and in the farmhouse the chair's
  // block runs 172-234, so the keep-out span is 165-241 — the shipped watch spot lands INSIDE
  // it and he then sits there for up to a minute, which is the longest passive thing he does
  // anywhere. Measured on Jon's scene; a child's chair lands somewhere else and this is a no-op.
  avoidChairFront(x, y);
}
static void furnitureSpot(int which, int *x, int *y) {
  if (S.phase == PH_TEEN) {
    if (which == 1) { *x = 104; *y = FLOOR_Y + 22; }        // in front of the bed
    else            { *x = 182; *y = FLOOR_Y + 20; }        // the oval rug
  } else {
    *x = (which == 1) ? CRIB_X : RUG_X;
    *y = (which == 1) ? CRIB_Y : RUG_Y;
    // ...unless the child's room actually HAS one, in which case go to theirs. RUG_X and CRIB_X
    // were measured off the shipped room; Jon's rug is at 209 and he has no crib at all, so half
    // of every wander was a trip to furniture that is not in the room — which is most of what
    // read as him "walking to random places that dont make sense".
    const SceneProp *p = sceneFindPropLike((which == 1) ? "crib" : "rug");
    if (p) {
      *x = (int)p->x;
      *y = (int)(p->y + 8);            // stand just in front of it, not on its back edge
    }
  }
  clearOfBlocks(x, y);
  clampErrandToFloor(x, y);            // and it must be somewhere he can actually stand
}

// THE CHAIR'S FRONTAGE IS NOT A PLACE HE STOPS.
// Jon: "i dont want bunbun to do any passive actions in front of the chair" — "he sometimes
// does something in front of the chair while the cat is there and it takes away from it all".
// The keep-out zone was making this WORSE rather than preventing it: clearOfBlocks() resolves a
// point inside a block by setting y to that block's yFront, which is the front edge of the
// furniture — so any destination that landed on the chair was moved to directly in front of it,
// which is the one place in the room he must not loiter. The zone says "not ON the chair"; it
// never said "not in front of it", and those are different sentences.
// Only the CHAIR, and only for passive destinations. The tables keep their frontage because the
// jar errand has to reach them, and an errand that cannot reach its landing is a worse bug than
// a badly placed idle. Identified through the chair PROP rather than by block index, because a
// child's blocks are in whatever order they drew them.
// The span itself, so the "is he in it" test and the "push it out" fix can never drift apart.
static bool chairFrontSpan(int *lo, int *hi) {
  if (!sceneActive()) return false;
  const SceneProp *p = sceneFindPropLike("chair");
  if (!p) return false;
  const Block *b; int n;
  if (!sceneBlocks(&b, &n)) return false;
  for (int i = 0; i < n; i++) {
    if (p->x < b[i].x0 - 4 || p->x > b[i].x1 + 4) continue;   // the block holding the chair
    *lo = b[i].x0 - 7; *hi = b[i].x1 + 7;
    return true;
  }
  return false;
}
static void avoidChairFront(int *x, int *y) {
  (void)y;
  int lo, hi;
  if (!chairFrontSpan(&lo, &hi)) return;
  if (*x < lo || *x > hi) return;                             // already clear of it
  const bool goLeft = (*x - lo) < (hi - *x);
  // CLEAR OF THE EDGE BY MORE THAN THE WALKER'S ARRIVAL TOLERANCE. Landing exactly on lo/hi
  // still tests as inside the zone — but a 2px margin is not enough either, and that mistake
  // was measured on the device rather than reasoned about: the walker calls it arrived at
  // `d < 4`, so with the target at lo-2 he stopped at 165 (= lo, still inside), the standing
  // rule re-fired, re-issued the same target, and he idled on the boundary line for good.
  // CHAIR_CLEAR must stay > 4 for that reason.
  const int CHAIR_CLEAR = 8;
  int nx = goLeft ? lo - CHAIR_CLEAR : hi + CHAIR_CLEAR;
  if (nx < 14 || nx > 306) nx = goLeft ? hi + CHAIR_CLEAR : lo - CHAIR_CLEAR;
  if (nx >= 14 && nx <= 306) *x = nx;
}
static void clearOfBlocks(int *x, int *y) {
  const Block *b; int n;
  const Block *sb; int sn;
  if (sceneActive() && sceneBlocks(&sb, &sn)) { b = sb; n = sn; }
  else if (S.phase == PH_BABY) { b = BLOCKS_BABY; n = 1; }
  else if (S.phase == PH_TEEN) { b = BLOCKS_TEEN; n = 3; }
  else                         { b = BLOCKS_ADULT; n = 3; }
  // Widen every footprint by this species' overhang, so the test is "does his BODY overlap the
  // furniture" rather than "do his feet". The bunny clears BLOCKS_TEEN's bed (x1 = 152) by one
  // pixel standing at x = 170; an animal with a snout would be inside it while the feet read
  // clear. Zero for index 0, so the tuned numbers are untouched.
  int o = (int)(charOverhang(spriteScale() * travelFactor()) + 0.5f);
  for (int i = 0; i < n; i++)
    if (*x >= b[i].x0 - o && *x <= b[i].x1 + o && *y < b[i].yFront) *y = b[i].yFront;
}

// ---------------- behaviour ----------------
static void startAction(const char *a, float secs) {
  setAnim(a); g_action = true; g_actionEnd = millis() + (uint32_t)(secs * 1000);
  g_tx = g_ty = -1;
  // Something the player asked for ALWAYS animates. setAnim clears the hold when the clip
  // changes, but not when the requested action happens to already be on screen, and a button
  // press that produced a motionless character would read as the game having hung.
  g_holdUntil = 0;
}
static bool contentMood() { return !S.sick && S.food >= 35 && S.energy >= 35 && S.fun >= 35; }

// mood ordering copied from the HTML's moodAnim()
static const char *moodCustom(const char *act, const char *fb) {
  const char *k = sceneActAnim(act);
  return k ? k : fb;
}
static const char *moodAnim() {
  bool b = (S.phase == PH_BABY);
  // Jon's split, honored: an EMOTION (hungry, tired, sick, bored, angry) is a state shown
  // wherever he stands - and the child's own emotion animation wins when the scene has one.
  if (S.sick)        return moodCustom("sick",   pa("sick"));
  if (S.food < 35)   return moodCustom("hungry", pa("hungry"));
  if (S.energy < 35) return moodCustom("tired",  pa("tired"));
  if (S.fun < 35)    return moodCustom("bored",  pa("bored"));
  // Neglected affection reads as sulking. Threshold is lower than the others (25 vs 35) and
  // it sits at the bottom of the ladder on purpose: hunger and tiredness are more urgent, and
  // a permanently cross bunbun would be miserable rather than expressive.
  if (S.disc < 25)   return moodCustom("angry", pa("angry"));
  // Only a baby sits rather than stands, and only before he can stand up.
  if (b && !babyCanStand()) return "baby_sit";
  return moodCustom("idle", pa("idle"));
}

// A COMMANDED ACTION GOES TO ITS PLACE ("when i hit sleep he walks over to the sleep
// location and does the sleep animation. same concept for the bath"): find the mark whose
// animation carries this act, walk to its doorstep, settle onto the authored spot, perform.
// False = the scene has no such place; the caller keeps its old in-place behaviour.
static void catDismissIfAway();
// THE DOOR IS THE SIDE OF THE SCREEN. An act whose room exists somewhere else sends him
// to the edge, the room swaps, he walks in from the mirror edge and does the thing.
// After the act (or a work session) he comes home the same way. If a room is not
// defined: eat and bath simply happen here (the main-room fallback), work does not
// happen at all - both by the owner's rule.
static uint8_t  g_doorTrip = 0;      // 1 = walking to the exit edge
static uint8_t  g_whyDoor = 0;       // the reason that started this crossing
static uint32_t g_visitHomeAt = 0;   // a fruitless side-room visit walks home at this time
static uint32_t g_roomMinStay = 0;   // Jon: "at least 10 seconds" in a side room, whatever happens
static uint32_t g_workNextAt = 0;    // the stroll between work reps ends, and the next one starts
static bool     g_actIsWork  = false;   // the running startAction() is a work performance
static int8_t   g_workReps   = 0;       // visit mode: work performances still owed (-1 = a wk session governs)
static uint8_t  g_doorRole = 0;
static char     g_doorAct[8] = "";
static uint32_t g_workUntil = 0;     // the scene work session deadline; 0 = no session
// HIS WORK METER GOES UP AT WORK (Jon: "his work meter should go up while he is at
// work its not doing that") - only the retired farm script ever paid discipline. A
// one-visit job pays what the script paid; a session pays per rep, so the meter
// visibly climbs while he is there.
static void creditWork() {
  S.disc   = min(100.0f, S.disc + (g_workUntil ? 6.0f : 14.0f));
  S.energy = max(0.0f, S.energy - (g_workUntil ? 2.0f : 5.0f));
  if (g_workReps > 0) g_workReps--;
  saveState();
}
// THE POTTY ROUTINE (Jon: "he should walk to bathroom and do a defined sit action and
// then wash hands before going back to main room"). Reads the bathroom scene's own
// tags: the sit act is the toilet, the bath act is the sink. 1 = sitting, 2 = washing.
static uint8_t g_pottySeq  = 0;
static bool    g_pottyLock = false;  // routine-internal dispatches don't cancel it
// ---- "WHY DID YOU DO THAT?" (WHYFEATURE.md) ------------------------------------
// The reason is written in the same breath as the decision. Nothing here ever inspects
// current state and guesses backwards - by the time a child asks, the meter that started
// the trip has already been refilled by the meal it caused.
enum WhyCause : uint8_t {
  WHY_NONE = 0, WHY_METER, WHY_BUTTON, WHY_ROLL, WHY_SCHEDULE,
  WHY_ROUTINE, WHY_FILLER, WHY_INTERRUPTED, WHY_BLOCKED
};
struct Because {
  uint8_t  cause;
  char     act[12];
  char     meter[12];
  uint8_t  roomFrom, roomTo;
  uint32_t atMs;
};
static Because g_why[4];
static uint8_t g_whyN = 0;          // how many are real
static uint8_t g_whyAt = 0;         // next slot
static const char *ROOM_WORD[4] = {"the main room", "the kitchen", "the bathroom", "work"};
static void say(const char *t);
static void whyNote(uint8_t cause, const char *act, const char *meter) {
  Because &b = g_why[g_whyAt];
  b.cause = cause;
  snprintf(b.act, sizeof(b.act), "%s", act ? act : "");
  snprintf(b.meter, sizeof(b.meter), "%s", meter ? meter : "");
  b.roomFrom = g_scCurRole;
  b.roomTo = g_scCurRole;           // a door trip stamps the real destination later
  b.atMs = millis();
  g_whyAt = (uint8_t)((g_whyAt + 1) % 4);
  if (g_whyN < 4) g_whyN++;
  // THE ONE THAT MUST BE HEARD, not merely available if asked
  if (cause == WHY_BLOCKED && g_scOK) {
    static uint32_t lastBlk = 0;
    if (millis() - lastBlk > 15000) {
      lastBlk = millis();
      char line[96];
      const char *what = !strcmp(b.act, "eat")   ? "a snack"
                       : !strcmp(b.act, "bath")  ? "a bath"
                       : !strcmp(b.act, "wash")  ? "a wash"
                       : !strcmp(b.act, "toilet")? "the potty"
                       : !strcmp(b.act, "work")  ? "a job to do"
                       : !strcmp(b.act, "sleep") ? "a bed" : b.act;
      snprintf(line, sizeof(line), "bunbun wanted %s - there is nowhere for it yet", what);
      say(line);
    }
  }
}
static Because *whyLast() { return g_whyN ? &g_why[(g_whyAt + 3) % 4] : nullptr; }
// the sentence, in the same voice as the ticker: kid words, name the actual thing
static const char *whySentence(char *buf, size_t n) {
  Because *b = whyLast();
  if (!b || !b->cause) { snprintf(buf, n, "I am just being me"); return buf; }
  const char *room = (b->roomTo < 4) ? ROOM_WORD[b->roomTo] : "";
  const bool moved = (b->roomTo != b->roomFrom);
  switch (b->cause) {
    case WHY_METER:
      if (!strcmp(b->meter, "food"))   snprintf(buf, n, "my tummy was rumbling%s%s",
                                                moved ? ", so I went to " : "", moved ? room : "");
      else if (!strcmp(b->meter, "clean")) snprintf(buf, n, "I was getting grubby%s%s",
                                                moved ? ", so I went to " : "", moved ? room : "");
      else if (!strcmp(b->meter, "energy")) snprintf(buf, n, "my eyes were getting heavy");
      else snprintf(buf, n, "I was feeling %s", b->meter);
      break;
    case WHY_BUTTON:
      if (moved) snprintf(buf, n, "you asked me to, so I went to %s", room);
      else       snprintf(buf, n, "you asked me to!");
      break;
    case WHY_ROLL:     snprintf(buf, n, "I really needed the potty"); break;
    case WHY_SCHEDULE:
      if (!strcmp(b->act, "sleep")) snprintf(buf, n, "it is night time, so I am off to bed");
      else if (moved)               snprintf(buf, n, "it was time for %s", room);
      else                          snprintf(buf, n, "it was time to get on with it");
      break;
    case WHY_ROUTINE:  snprintf(buf, n, "I always wash my hands after"); break;
    case WHY_FILLER:   snprintf(buf, n, "nothing much - just pottering about"); break;
    case WHY_INTERRUPTED: snprintf(buf, n, "I was busy, but something needed me"); break;
    case WHY_BLOCKED: {
      const char *what = !strcmp(b->act, "eat")   ? "a snack"
                       : !strcmp(b->act, "bath")  ? "a bath"
                       : !strcmp(b->act, "wash")  ? "a wash"
                       : !strcmp(b->act, "toilet")? "the potty"
                       : !strcmp(b->act, "work")  ? "to do my job"
                       : !strcmp(b->act, "sleep") ? "my bed" : nullptr;
      if (what) snprintf(buf, n, "I wanted %s, but there is nowhere to have one", what);
      else      snprintf(buf, n, "I wanted to %s, but there is nowhere for that", b->act);
      break; }
    default: snprintf(buf, n, "I am just being me");
  }
  return buf;
}
extern "C" void bunbun_why(char *buf, int len) { whySentence(buf, (size_t)len); }
// He thinks out loud when what he is doing would look arbitrary from outside - the
// dice, the clock, and any trip to another room. Said in HIS voice ("I really needed
// the potty"), which is what marks a reason apart from the usual status line.
static void whySpeakIfInteresting() {
  Because *b = whyLast();
  if (!b || !g_scOK) return;
  const bool travelling = (b->roomTo != b->roomFrom);
  const bool worth = travelling || b->cause == WHY_ROLL ||
                     (b->cause == WHY_SCHEDULE && !strcmp(b->act, "sleep"));
  if (!worth) return;
  static uint32_t lastSaid = 0;
  static uint32_t lastAt = 0;
  if (b->atMs == lastAt) return;                 // one reason, said once
  if (millis() - lastSaid < 20000) return;       // and never a stream of them
  lastSaid = millis(); lastAt = b->atMs;
  char line[96];
  whySentence(line, sizeof(line));
  say(line);
}
// the reason a COMMAND carries, set by the caller just before it asks for the act
static uint8_t g_whyNext = WHY_NONE;
static char    g_whyNextMeter[12] = "";
static void whyFor(uint8_t cause, const char *meter) {
  g_whyNext = cause;
  snprintf(g_whyNextMeter, sizeof(g_whyNextMeter), "%s", meter ? meter : "");
}

static bool sceneErrandTo(const char *act);
static bool sceneDoorTo(uint8_t tgt, const char *act) {
  // no side-to-side teleports: work to kitchen goes THROUGH the main room. The act
  // rides along; the errand re-runs at main's middle and routes the second leg.
  if (g_scCurRole != SCENE_ROLE_MAIN && tgt != SCENE_ROLE_MAIN) tgt = SCENE_ROLE_MAIN;
  g_performBehind = false; g_settleUntil = 0; g_watching = false;
  // kitchen is left of the main room; bathroom and work are right. Leaving a side room
  // always heads back toward the main room's side.
  // Jon: "kitchen and bathroom are off the left of the main room screen, work is off
  // to the right"
  bool exitLeft;
  if (g_scCurRole == SCENE_ROLE_MAIN)
    exitLeft = (tgt == SCENE_ROLE_KITCHEN || tgt == SCENE_ROLE_BATH);
  else
    exitLeft = (g_scCurRole == SCENE_ROLE_WORK);   // side rooms exit toward the main room
  // ROOM SPACE, not panel space: the builder's rooms are 320 wide (SCENE_W is the
  // 240px panel the room is drawn onto at 0.75). The doors live at the room's edges.
  const int ROOMSPACE_W = 320;
  int ex = exitLeft ? 6 : ROOMSPACE_W - 6;
  int ey = (int)g_fy;
  if (g_scHasBounds) {
    if (ey < g_scBounds.y0 + 4) ey = g_scBounds.y0 + 4;   // above the floor: come down first
    if (ey > g_scBounds.y1 - 2) ey = g_scBounds.y1 - 2;
  }
  clampErrandToFloor(&ex, &ey);
  clearOfBlocks(&ex, &ey);
  clampErrandToFloor(&ex, &ey);
  g_tx = ex; g_ty = ey;
  g_visit = 4;
  g_doorTrip = 1; g_doorRole = tgt; g_visitHomeAt = 0;
  { Because *b = whyLast(); g_whyDoor = b ? b->cause : (uint8_t)WHY_NONE; }
  whySpeakIfInteresting();          // he says why he is off, in his own words
  strncpy(g_doorAct, act ? act : "", sizeof(g_doorAct) - 1);
  g_doorAct[sizeof(g_doorAct) - 1] = 0;
  g_tripLen = sqrtf((g_tx - g_fx) * (g_tx - g_fx) + (g_ty - g_fy) * (g_ty - g_fy));
  g_crawling = !babyCanStand(); g_crawlFrac = babyCanStand() ? 2 : 0;
  g_holdUntil = millis() + 300;
  g_departAt = g_holdUntil;
  g_wanderT = 18.0f;
  g_action = false;
  Serial.printf("door: heading %s for role %d act '%s'\n", exitLeft ? "left" : "right",
                (int)tgt, g_doorAct);
  return true;
}
// The brain's live state, for debugging worlds over HTTP instead of squinting at
// screenshots: which room, which rooms exist, whether the lights are on, and what
// he is currently trying to do ("he is just staying still doing the idle pose").
extern "C" void bunbun_brain_snapshot(char *buf, int len) {
  snprintf(buf, len,
    "{\"role\":%d,\"avail\":%d,\"lights\":%d,\"tx\":%d,\"ty\":%d,"
    "\"visit\":%d,\"door\":%d,\"action\":%d,\"settle_ms\":%ld,"
    "\"x\":%d,\"y\":%d,\"anim\":\"%s\",\"potty\":%d,\"work\":%d,"
    "\"food\":%d,\"fun\":%d,\"energy\":%d,\"clean\":%d,"
    "\"floorN\":%d,\"props\":%d,\"anims\":%d,\"sick\":%d}",
    (int)g_scCurRole,
    (g_scRoleAvail[0]?1:0)|(g_scRoleAvail[1]?2:0)|(g_scRoleAvail[2]?4:0)|(g_scRoleAvail[3]?8:0),
    S.lights?1:0, g_tx, g_ty, (int)g_visit, (int)g_doorTrip, g_action?1:0,
    (long)(millis()<g_settleUntil ? (g_settleUntil-millis()) : 0),
    (int)S.x, (int)S.y, g_anim?g_anim->key:"?", (int)g_pottySeq, g_workUntil?1:0,
    (int)S.food, (int)S.fun, (int)S.energy, (int)S.clean,
    (int)g_scFloorN, (int)g_scPropN, (int)g_scAnimN, (int)S.sick);
}
// /api/debug/act - the remote lever the door-walk tests (and future layers) need.
// Set from the web task, consumed on the game task the next tick.
static char g_dbgAct[8] = "";
extern "C" void bunbun_debug_act(const char *a) {
  // stat pokes apply IMMEDIATELY (the queued version raced the next meal - "it says
  // he is full"); everything that moves him still goes through think()'s consumption
  if (a && !strcmp(a, "hungry")) { S.food = 20.0f; return; }
  // THE LEVERS THAT UNSTICK HIM (rehearsal M10): while sick, away or asleep every act
  // was swallowed with ok:true and nothing happened, with no route to the MEDS button.
  if (a && !strcmp(a, "meds")) { S.sick = 0; S.health = min(100.0f, S.health + 35); return; }
  if (a && !strcmp(a, "wake")) { S.lights = 1; g_sleepPending = 0; g_nightSleep = false; return; }
  strncpy(g_dbgAct, a ? a : "", sizeof(g_dbgAct) - 1);
  g_dbgAct[sizeof(g_dbgAct) - 1] = 0;
}
// THE WORLD'S OWN TRAVEL KIT (Jon: "can the bunbun package not also provide the walk
// animation and default to idle if something is missing?"): a package can ship walk
// clips under the reserved acts walk_e / walk_w (act[8] caps at 7 chars). Moving prefers them, falls back
// to the scene's idle clip, and only then to the installed species art - so a penguin
// world walks like a penguin even before the penguin species is installed.
static const char *sceneTravelAnim(float dx, bool horiz) {
  // no kit shipped at all = the species pack's real walks beat an idle glide; the
  // idle fallback is only for a kit with a piece missing (and for vertical travel,
  // which a side-on walk clip cannot face)
  if (!sceneActAnim("walk_e") && !sceneActAnim("walk_w")) return nullptr;
  const char *k = horiz ? sceneActAnim(dx > 0 ? "walk_e" : "walk_w") : nullptr;
  if (!k) k = sceneActAnim("idle");
  return k;
}
// Can this act happen anywhere in this world at all? (rehearsal S10) - the same
// availability the errand router uses, asked before a button changes a meter.
static bool sceneCanDo(const char *act) {
  const int bit = sceneActBit(act);
  if (bit) for (int r = 0; r < 4; r++)
    if (g_scRoleAvail[r] && (g_scRoleActs[r] & bit)) return true;
  return sceneActMark(act) >= 0 || sceneActAnim(act) != nullptr;
}
static bool sceneWorkAvail() {
  return g_scRoleAvail[SCENE_ROLE_WORK] || sceneActMark("work") >= 0 ||
         sceneActAnim("work") != nullptr;
}

static bool sceneErrandTo(const char *act) {
  // WHY: whoever asked set g_whyNext; an unattributed errand is the pet's own pottering
  const uint8_t why = g_whyNext ? g_whyNext : (uint8_t)WHY_FILLER;
  char whyMeter[12]; snprintf(whyMeter, sizeof(whyMeter), "%s", g_whyNextMeter);
  g_whyNext = WHY_NONE; g_whyNextMeter[0] = 0;
  if (!g_pottyLock) g_pottySeq = 0;  // any real command outranks the routine
  // ...and it ends the WORK commitment the same way ("i had him go to work and then
  // go eat and he went back to work after eating"): the menu's eat/bath never cleared
  // the session, so the owed reps and the stroll tick re-summoned him after the meal.
  // Only work itself, and the potty's own internal legs, keep the session alive.
  if (strcmp(act, "work") != 0 && !g_pottyLock) {
    g_workUntil = 0; g_workReps = 0; g_workNextAt = 0;
  }
  // WHICH ROOM does this act live in? ("if someone hits bathe... he can go to the
  // bathroom, if he hits eat, he can go to the kitchen")
  // THE ROOM HE IS IN WINS WHEN IT OFFERS THE ACT (Jon: "if eat is available in the
  // bathroom or the kitchen and he is in the bathroom, he can eat. if not, he walks
  // to the main room and to the kitchen"). The roles scan read every room's acts, so
  // this asks the world instead of guessing by role: here first, then the act's home
  // room, then any room that has it. Sits and the emotions stay local, as ever.
  const int bit = sceneActBit(act);
  uint8_t tgt = g_scCurRole;
  if (bit && !(g_scRoleActs[g_scCurRole] & bit)) {
    const uint8_t home = !strcmp(act, "eat")  ? SCENE_ROLE_KITCHEN
                       : !strcmp(act, "work") ? SCENE_ROLE_WORK
                       : (!strcmp(act, "bath") || !strcmp(act, "wash") ||
                          !strcmp(act, "toilet")) ? SCENE_ROLE_BATH
                       : SCENE_ROLE_MAIN;
    const uint8_t tryOrder[5] = {home, SCENE_ROLE_MAIN, SCENE_ROLE_KITCHEN,
                                 SCENE_ROLE_BATH, SCENE_ROLE_WORK};
    bool found = false;
    for (int i = 0; i < 5 && !found; i++)
      if (g_scRoleAvail[tryOrder[i]] && (g_scRoleActs[tryOrder[i]] & bit)) {
        tgt = tryOrder[i];
        found = true;
      }
    if (!found) {
      if (!strcmp(act, "work")) {
        whyNote(WHY_BLOCKED, act, "");
        return false;                           // no workplace anywhere = he does not work
      }
      // his bed is home even unmarked; anything else tries its canonical room
      tgt = !strcmp(act, "sleep") ? SCENE_ROLE_MAIN
          : (g_scRoleAvail[home] ? home : g_scCurRole);
    }
  }
  if (tgt != g_scCurRole) {
    whyNote(why, act, whyMeter);
    { Because *b = whyLast(); if (b) b->roomTo = tgt; }   // he is going THERE
    return sceneDoorTo(tgt, act);
  }
  if (!strcmp(act, "work") && !g_workUntil && sceneWorkMin() > 0)
    g_workUntil = millis() + (uint32_t)sceneWorkMin() * 60000UL;
  int m = sceneActMark(act);
  if (m < 0) {
    // no PLACE for this act, but maybe the child's ANIMATION exists with an "anywhere"
    // rule - then it happens right where he stands, exactly as the assembler plays it
    const char *k = sceneActAnim(act);
    if (!k) { whyNote(WHY_BLOCKED, act, ""); return false; }
    const AnimDef *ad = findAnim(k);
    float loopS = ad->frames / (ad->fps > 0.1f ? ad->fps : 7.0f);
    float secs = 2.0f * loopS + 2.0f;
    for (int i = 0; i < g_scAnimN; i++)
      if (!strcmp(g_scAnim[i].key, k) && g_scAnim[i].dur > 0.1f) { secs = g_scAnim[i].dur; break; }
    g_actIsWork = !strcmp(act, "work");
    whyNote(why, act, whyMeter);
    startAction(k, secs);
    return true;
  }
  whyNote(why, act, whyMeter);
  g_performBehind = false;   // he steps OUT of whatever he was in ("he goes behind the rug")
  g_settleUntil = 0;         // and stops the current performance to go ("hit sleeping should
                             // cause him to stop his current animation and then go to sleep")
  g_watching = false;        // a command outranks the window ("actions need to override
                             // watching the bird")
  int mx = g_scBun[m].x, my = g_scBun[m].y;
  g_markX = mx; g_markY = my;
  clearOfBlocks(&mx, &my);
  clampErrandToFloor(&mx, &my);
  g_tx = mx; g_ty = my;
  g_visit = 3;
  strncpy(g_markAnim, g_scBun[m].anim, sizeof(g_markAnim) - 1);
  g_markAnim[sizeof(g_markAnim) - 1] = 0;
  g_lastMark = m; g_lastWasMark = true;
  g_tripLen = sqrtf((g_tx - g_fx) * (g_tx - g_fx) + (g_ty - g_fy) * (g_ty - g_fy));
  g_crawling = !babyCanStand(); g_crawlFrac = babyCanStand() ? 2 : 0;
  g_holdUntil = millis() + 300;      // a commanded errand sets off promptly
  g_departAt = g_holdUntil;
  g_wanderT = 18.0f;
  g_action = false;                  // the walk and the performance ARE the action
  return true;
}

static int g_workStage;      // the adult job's stage; defined with the sequence, below

// ---------------- dancing ----------------
// Position in BEATS, free-running at the detected tempo and nudged toward each onset. A pure
// "frames since the last beat" scheme jumps every time an onset lands slightly early or late,
// which reads as a stumble; a free-running phase that is gently pulled into line stays smooth
// and still tracks the music. This is the same job a PLL does, and for the same reason.
static float    g_dancePos  = 0.0f;
static uint32_t g_danceSeen = 0;      // last onset count acted on
static int      g_danceHop  = 0;      // px lifted off the floor, applied in drawScene
static int      g_danceSway = 0;      // px sideways, ditto

// The routine: bounce on the spot for a few bars, cross the room, bounce again. Both halves are
// measured in BEATS rather than seconds, so the whole thing stays married to the music — and
// the walk is timed to ARRIVE on a beat, so the first bounce of the next burst lands exactly
// where the step did.
enum { DSTG_JUMP = 0, DSTG_WALK = 1 };
static uint8_t g_dStage    = DSTG_JUMP;
static float   g_dStageEnd = 0.0f;    // the g_dancePos at which this stage hands over
static float   g_dTx = -1, g_dTy = -1;

// ---- THE PET LIFT (menu redesign P3) ----
// The mockup wanted the sheet to SLIDE up over the room. At 10fps a real slide is five
// tearing frames, and repainting a 240x208 body every one of them is exactly the per-frame
// cost the whole sheet design exists to avoid. So the sheet doesn't move — HE does. The
// blit already takes a vertical offset (g_danceHop); g_sheetLift rides the same hook, eased
// over 250ms, and the pet visibly hops up to sit above his sheet. Same read, zero repaints.
//
// Not a new buffer, not a new sprite, not one byte of .bss beyond these three ints (law 7).
static int      g_sheetLift = 0;        // px the pet is currently lifted off the floor
static int      g_sheetLiftFrom = 0, g_sheetLiftTo = 0;
static uint32_t g_sheetLiftT0 = 0;

// Called the moment dance mode is switched on. Anything already running has to be cleared here
// rather than waited out: an action, a furniture settle or a held frame would otherwise keep
// think() returning early for several seconds, so the ball would be down and bunbun would carry
// on eating or sitting until the old animation happened to finish.
static void danceBegin() {
  g_action = false;
  g_actionEnd = 0;
  g_settleUntil = 0;
  g_holdUntil = 0;
  g_watching = false;
  g_visit = -1;
  g_tx = g_ty = -1;
  g_dancePos = 0.0f;
  g_dStage = DSTG_JUMP;
  g_dStageEnd = 0.0f;      // forces a fresh stage decision on the first danceStep
}

static void danceStep(float dt) {
  // Dancing owns movement outright. The ordinary walker must not also be steering, or the two
  // fight over position every frame.
  g_tx = g_ty = -1;
  g_visit = -1;

  uint32_t per = g_beatTempo ? g_beatTempo : 800;
  float beatSec = per / 1000.0f;
  g_dancePos += dt / beatSec;

  // Pull toward the nearest beat boundary on each new onset, by a fraction rather than
  // snapping. 30% converges within a couple of beats and never visibly jerks.
  if (g_beatCount != g_danceSeen) {
    g_danceSeen = g_beatCount;
    float frac = g_dancePos - floorf(g_dancePos);
    float err  = (frac < 0.5f) ? -frac : (1.0f - frac);
    // Only correct on onsets that land NEAR a predicted beat. Off-beat onsets — the snares,
    // arriving at double time — used to yank the hop sideways by up to 0.15 of a beat twice
    // per beat, which is most of why the dance was smooth in silence and twitchy with music.
    // A gated, smaller nudge keeps the lock without the limp.
    if (fabsf(err) < 0.25f) g_dancePos += err * 0.20f;
  }

  // ---- stage changes, always on a beat boundary ----
  if (g_dancePos >= g_dStageEnd) {
    if (g_dStage == DSTG_JUMP) {
      Bounds b = bounds();
      int tries = 0, tx, ty;
      do {
        tx = b.x0 + esp_random() % (b.x1 - b.x0);
        ty = b.y0 + esp_random() % (b.y1 - b.y0);
        clearOfBlocks(&tx, &ty);
      } while ((abs(tx - S.x) + abs(ty - S.y) < 40 || !sceneFloorHas(tx, ty)) && ++tries < 12);
      sceneFloorClamp(&tx, ty);            // dancing off the edge of the floor is still off it
      g_dTx = tx; g_dTy = ty;

      // Whole number of beats, from the distance at roughly the walker's usual pace. Rounding
      // UP is what puts the arrival on a beat instead of somewhere inside one.
      float dx = g_dTx - g_fx, dy = g_dTy - g_fy;
      float dist = sqrtf(dx * dx + dy * dy);
      int beats = (int)ceilf(dist / (44.0f * beatSec));
      if (beats < 2) beats = 2;
      if (beats > 8) beats = 8;
      g_dStage = DSTG_WALK;
      g_dStageEnd = floorf(g_dancePos) + beats;
    } else {
      g_dTx = g_dTy = -1;
      g_dStage = DSTG_JUMP;
      // 4 or 8 beats — one or two bars. Anything shorter reads as a twitch between walks.
      g_dStageEnd = floorf(g_dancePos) + ((esp_random() & 1) ? 4 : 8);
    }
  }

  float ph = g_dancePos - floorf(g_dancePos);   // 0..1 within the current beat
  const char *clip;

  if (g_dStage == DSTG_WALK && g_dTx >= 0) {
    // Cover the REMAINING distance in the REMAINING beats. Recomputed every frame, so a tempo
    // change mid-walk simply adjusts the pace rather than causing an overshoot or a stall, and
    // arrival lands on the beat by construction instead of by a speed constant that happens to
    // be about right.
    float remain = g_dStageEnd - g_dancePos;
    float dx = g_dTx - g_fx, dy = g_dTy - g_fy;
    if (remain > 0.02f) {
      float frac = (dt / beatSec) / remain;
      if (frac > 1.0f) frac = 1.0f;
      g_fx += dx * frac;
      g_fy += dy * frac;
    } else {
      g_fx = g_dTx; g_fy = g_dTy;
    }
    int nx = (int)g_fx, ny = (int)g_fy;
    clearOfBlocks(&nx, &ny);
    g_fy = ny;
    S.x = nx; S.y = ny;

    bool horiz = fabsf(dx) > fabsf(dy);
    bool baby  = (S.phase == PH_BABY);
    // The baby CRAWLS between bursts rather than walking. Unconditionally, unlike ordinary
    // movement — that picks walk or crawl from babyCanStand() and how far into the trip it is,
    // so a baby old enough to walk would never crawl again. Scuttling between bounces is the
    // funnier read, and it is the one thing a baby can do that the other phases cannot.
    if (horiz) clip = baby ? (dx > 0 ? "baby_crawl_right" : "baby_crawl_left")
                           : pa(dx > 0 ? "walk_right" : "walk_left");
    else       clip = baby ? (dy > 0 ? "baby_crawl_down" : "baby_crawl_up")
                           : pa(dy > 0 ? "walk_down" : "walk_up");
    // Still bobbing while crossing, just lower — it is a strut, not a march.
    g_danceHop  = (int)(3.0f * sinf(ph * 3.14159265f));
    g_danceSway = 0;
  } else {
    clip = pa("jump");
    // The hop that made this read as dancing in the first place: a full arc every beat.
    g_danceHop  = (int)(7.0f * sinf(ph * 3.14159265f));
    g_danceSway = (int)(4.0f * sinf(g_dancePos * 3.14159265f));
  }

  setAnim(clip);
  // Drive the clip from the musical phase instead of the wall clock. currentFrame() derives the
  // frame from g_animT * fps, so writing g_animT is how the beat gets the steering wheel.
  // The period differs by mode — a pingpong clip runs 2n-2 steps per cycle, not n — so this
  // asks for the right one rather than assuming a plain loop and stuttering on the jump.
  int n = g_anim->frames > 0 ? g_anim->frames : 1;
  int period = (g_anim->mode == M_PINGPONG) ? (2 * n - 2) : n;
  if (period < 1) period = 1;
  float t = ph * period;
  g_animT = t / (g_anim->fps > 0 ? g_anim->fps : 8);
  g_holdUntil = 0;
}

// ================= THE FLOOR IS AN INVARIANT, NOT A SUGGESTION =================
// Straight out of the builder, which is the golden record here and which already learned this
// the expensive way (placer.html keepLegal, and the note above it):
//
//   "nothing WALKED through a zone, but bunbun STOOD in one for 13,667 frames. Arriving is not
//    walking: his destination was checked, but the lane he actually finished on could be a
//    different one, and after that nothing ever re-checked where he was standing."
//
// Testing the chosen target — which is all the first cut of the floor-polygon support did — is
// the approach that comment is about. So this runs ONCE A FRAME, after every state machine has
// had its say, and pulls him back onto the boards. One place, one rule, and the next piece of
// behaviour added here cannot forget it.
//
// The cat is deliberately NOT clamped: she sleeps on marks that are up on shelves and tables,
// and the builder skips a perched actor for exactly that reason — the floor is not hers.
static void keepLegal() {
  // NOT during work or school. Those sequences load a DIFFERENT room and drive his feet to that
  // room's floor line; the scene's polygon describes the living room. Clamping him to it while he
  // is at the farm holds him just short of the desk, and updateWork only advances when he gets
  // within 4px — so the whole sequence stalls with no give-up timer to rescue it. think() is
  // already stood down for g_workStage for the same reason.
  if (!alive() || g_workStage || bunAway()) return;
  if (!sceneActive()) return;
  int top, bot;
  if (!sceneFloorLane((int)g_fx, &top, &bot)) return;   // doorway, or no floor drawn: leave him
  if (g_fy >= (float)top && g_fy <= (float)bot) return;
  g_fy = (g_fy < (float)top) ? (float)top : (float)bot;
  S.y = (int)g_fy;
}

// ================= THINGS THAT GET KNOCKED OVER =================
// The loop Jon wrote out twice: she clears whatever is standing on the spot she wants, sleeps
// there, leaves — and then bunbun puts it back where it lived. The yarn is the exception he
// stated in the same breath: it is a TOY, it rolls along the floor, and it stays there.
//
// The scene says which props are loose and where each lands (see SceneLoose). This is only the
// live state: where the thing is right now, and whether it is over.
struct LooseRt {
  float x, y;          // live canvas-top position; starts at the prop's placed position
  float roll;          // px per second along the floor, rollers only
  float aim;           // roll toward this x and stop there; NAN for a free roll
  bool  down;          // knocked over / batted, and not yet back home
  bool  held;          // in bunbun's arms
};
static LooseRt g_lrt[SCENE_MAX_LOOSE];
static uint16_t g_lrtGen = 0xffff;          // which scene generation the state was seeded from
// SOMETHING OF HIS WENT OVER, AND HE MINDS. Jon: "can we have bunbun be frustrated every time
// the jar gets knocked down". The builder sets S.huffAt in the swat and only for a non-roller —
// the yarn is a toy, being batted about is what it is FOR, and he has no opinion about it.
static int   g_huffAt = -1;
static float g_huffT = 0;

// Seed every loose thing where the child placed it. Re-seeded whenever a new scene arrives, so a
// push does not leave a jar lying on the floor of a room that no longer has one.
static void looseSeed() {
  for (int i = 0; i < g_scLooseN; i++) {
    const SceneProp *p = &g_scProp[g_scLoose[i].prop];
    g_lrt[i].x = p->x; g_lrt[i].y = p->y;
    g_lrt[i].roll = 0; g_lrt[i].aim = NAN;
    g_lrt[i].down = false; g_lrt[i].held = false;
  }
  g_lrtGen = g_scGen;
}
static inline void looseEnsure() { if (g_lrtGen != g_scGen) looseSeed(); }

static int looseOf(int propIndex) {
  for (int i = 0; i < g_scLooseN; i++) if (g_scLoose[i].prop == propIndex) return i;
  return -1;
}

static bool looseDrawOverride(int propIndex, float *x, float *y, const char **name) {
  if (!g_scLooseN) return false;
  looseEnsure();
  const int i = looseOf(propIndex);
  if (i < 0) return false;
  // BELT AND BRACES. This is the doorway between the sim and the renderer, and a non-finite
  // coordinate reaching drawItemAtXY is an out-of-bounds index, not a wrong-looking pixel.
  if (!isfinite(g_lrt[i].x) || !isfinite(g_lrt[i].y)) return false;
  *x = g_lrt[i].x; *y = g_lrt[i].y;
  // once it is over it wears its down pose — a jar-up becomes a jar-down — and keeps it until
  // bunbun stands it up again
  if (g_lrt[i].down && g_scLoose[i].down[0]) *name = g_scLoose[i].down;
  return g_lrt[i].down || g_lrt[i].held;
}

// The builder's stepRolls(), constants and all. Its own comment explains the decay: "0.88 per
// frame stops a ball dead in about 12px, which is a nudge, not 'hit across the floor'. At 0.975
// a swat carries it 60-90px, which is a room's width of travel and reads as the thing Jon
// described." The walls stop it at 16 and 304; keep-out zones do not stop it at all.
static void looseStep(float dt) {
  if (!g_scLooseN) return;
  looseEnsure();
  // CLAMP dt. The panic breadcrumb named this stage, and the signature was always 10-12s after
  // boot — which is exactly when sceneEnsure() finally succeeds and does a blocking SPIFFS read
  // plus a cJSON parse. The frame after that stall carries a huge dt, and here that feeds both
  // `roll * dt` and `powf(0.975, 60*dt)`. A wild or NaN result lands in o->x, flows out through
  // looseDrawOverride into drawItemAtXY, and `(int)(NaN * VIEW)` is undefined — it indexes a
  // 128-entry line buffer with garbage. Nothing downstream was ever going to survive that.
  if (!(dt > 0.0f)) return;              // NaN or negative: skip the frame entirely
  if (dt > 0.25f) dt = 0.25f;
  for (int i = 0; i < g_scLooseN; i++) {
    LooseRt *o = &g_lrt[i];
    if (o->roll == 0.0f || o->held) continue;
    if (!isfinite(o->x) || !isfinite(o->roll)) {   // already poisoned: put it back where it lives
      const SceneProp *p = &g_scProp[g_scLoose[i].prop];
      o->x = p->x; o->y = p->y; o->roll = 0; o->aim = NAN;
      continue;
    }
    const float nx = o->x + o->roll * dt;
    if (nx < 16.0f) { o->x = 16.0f; o->roll = 0; continue; }
    if (nx > 304.0f) { o->x = 304.0f; o->roll = 0; continue; }
    if (!isnan(o->aim) && ((o->aim - o->x) * (o->aim - nx) < 0)) {   // arrived at the landing
      o->x = o->aim; o->roll = 0; o->aim = NAN; continue;
    }
    o->x = nx;
    o->roll *= powf(0.975f, 60.0f * dt);
    if (fabsf(o->roll) < 4.0f) o->roll = 0;
  }
}

// Is one of the loose things standing on this spot? The builder's occupantOf(): within 26 across
// and 22 down, nearest by x, and rollers do not count — a ball on the floor is not in her way.
static int looseOccupant(float sx, float sy) {
  if (!g_scLooseN) return -1;
  looseEnsure();
  int best = -1;
  float bestd = 1e9f;
  for (int i = 0; i < g_scLooseN; i++) {
    if (g_scLoose[i].roller || g_lrt[i].down || g_lrt[i].held) continue;
    const float d = fabsf(g_lrt[i].x - sx);
    if (d < 26.0f && fabsf(g_lrt[i].y - sy) < 22.0f && d < bestd) { bestd = d; best = i; }
  }
  return best;
}

// A ball she could have a game with (loosePlaything): a roller that is not already moving.
static int looseToy() {
  if (!g_scLooseN) return -1;
  looseEnsure();
  for (int i = 0; i < g_scLooseN; i++)
    if (g_scLoose[i].roller && !g_lrt[i].held && fabsf(g_lrt[i].roll) <= 4.0f) return i;
  return -1;
}

// Knock it over. `fromX` is where she is standing, which decides which way it goes when there is
// no landing to aim at.
static void looseKnock(int i, float fromX) {
  LooseRt *o = &g_lrt[i];
  const SceneLoose *L = &g_scLoose[i];
  const bool haveLand = (L->lx != INT16_MIN);
  o->down = true;
  if (L->roller) {
    const float dir = haveLand ? ((L->lx > o->x) ? 1.0f : -1.0f) : ((fromX < o->x) ? 1.0f : -1.0f);
    const float dist = haveLand ? fabsf(L->lx - o->x) : 0.0f;
    // an AIMED roll is hit hard enough to arrive: with a per-frame decay f the total travel is
    // v0/60/(1-f), so the speed that reaches a given distance is dist*(1-f)*60
    float v = haveLand ? dist * (1.0f - 0.975f) * 60.0f * 1.02f
                       : (90.0f + (float)(esp_random() % 70));
    if (v < 90.0f) v = 90.0f;
    if (v > 420.0f) v = 420.0f;
    o->roll = dir * v;
    o->aim = haveLand ? (float)L->lx : NAN;
    return;                          // it stays on the floor: never lifted, never shelved
  }
  g_huffAt = i;                    // not a toy: he will have something to say about this
  // WHERE YOU PUT jar-down IS WHERE IT ENDS UP. With no landing it scatters +-26px instead.
  if (haveLand) { o->x = L->lx; o->y = L->ly; }
  else {
    o->x += (esp_random() & 1) ? 26.0f : -26.0f;
    if (o->x < 20.0f) o->x = 20.0f;
    if (o->x > 300.0f) o->x = 300.0f;
  }
}

// The thing bunbun should go and tidy: knocked over, not a toy, not already in his arms.
// IS SHE SITTING ON ITS SPOT? Jon: "the jar should only be put back when the cat isnt on the
// table". Defined down with her state machine; declared here because the chores are written
// long before it.
static bool catOnItsSpot(int i);

static int looseNeedsTidy() {
  if (!g_scLooseN) return -1;
  looseEnsure();
  for (int i = 0; i < g_scLooseN; i++) {
    if (!g_lrt[i].down || g_lrt[i].held) continue;
    if (g_scLoose[i].roller) continue;               // a toy is not a chore; he leaves it
    if (g_lrt[i].roll != 0.0f) continue;             // still moving; let it come to rest
    if (catOnItsSpot(i)) continue;                   // she is up there; it can wait
    return i;
  }
  return -1;
}
// True while the cat is mid-visit and still doing something with the room — defined down with
// her state machine, declared here because the tidy errand is written long before it.
static bool catStillBusy();

// ---- HE PUTS IT BACK WHERE IT LIVED ----
// The other half of Jon's loop: "it wakes up and leaves then bunbun goes and places the object
// back where it was". Two rules come with it, both his:
//   - he waits until she is DONE with the spot. Tidying it out from under a sleeping cat is not
//     the beat; the builder gates its totidy on her having settled.
//   - THE YARN IS A TOY AND HE LEAVES IT ALONE. looseNeedsTidy() skips rollers, and that is the
//     whole of "the yarn stays tied to the ground".
static int      g_tidy = -1;         // which loose thing he is dealing with
static uint8_t  g_tidyStage = 0;     // 0 to it, 1 lifting, 2 carrying home, 3 setting down
static uint32_t g_tidyUntil = 0;
static bool tidyStep(float dt) {
  if (!g_scLooseN) return false;
  // AN ERRAND IN FLIGHT BEATS THE CHORE - but only for TAKING a new job. A tidy
  // already underway (g_tidy >= 0) finishes; a loose thing waits for him to be free.
  if (g_tidy < 0 && (g_doorTrip || g_visit >= 0 || g_tx >= 0 || g_action || g_sleepPending))
    return false;
  if (g_tidy < 0) {
    // The chores do not wait on the dice or the timer (the builder says so in as many words).
    // Which object he may touch is decided per object by looseNeedsTidy(), which keeps him off
    // whichever surface the cat is currently on.
    const int i = looseNeedsTidy();
    if (i < 0) return false;
    g_tidy = i; g_tidyStage = 0;
    g_tx = g_ty = -1;
  }
  LooseRt *o = &g_lrt[g_tidy];
  const SceneProp *home = &g_scProp[g_scLoose[g_tidy].prop];

  if (g_tidyStage == 0) {                      // walk to it
    const float dx = o->x - g_fx;
    if (fabsf(dx) > 8.0f) {
      g_fx += (dx > 0 ? 42.0f : -42.0f) * dt;
      S.x = (int)g_fx;
      { const char *tk = sceneTravelAnim(dx, true);
        setAnim(tk ? tk : pa(dx > 0 ? "walk_right" : "walk_left")); }
      return true;
    }
    g_tidyStage = 1; g_tidyUntil = millis() + 800;   // the builder's 0.8s lift
    setAnim(pa("idle"));
    return true;
  }
  if (g_tidyStage == 1) {                      // lifting
    setAnim(pa("idle"));
    if (millis() >= g_tidyUntil) { o->held = true; g_tidyStage = 2; sfxTick(); }
    return true;
  }
  if (g_tidyStage == 2) {                      // carrying it home
    const float dx = home->x - g_fx;
    // species carry point: hands by default, the penguin's beak (Jon's ruling) - the
    // same tidy walk, the jar just rides where this animal actually carries things
    {
      const int ci = (S.species_idx < CHARACTERS_N) ? S.species_idx : 0;
      o->x = g_fx + 11.0f + CHARACTERS[ci].carryDX;
      o->y = (float)(FLOOR_Y - 26 + CHARACTERS[ci].carryDY);
    }
    if (fabsf(dx) > 6.0f) {
      g_fx += (dx > 0 ? 42.0f : -42.0f) * dt;
      S.x = (int)g_fx;
      { const char *tk = sceneTravelAnim(dx, true);
        setAnim(tk ? tk : pa(dx > 0 ? "walk_right" : "walk_left")); }
      return true;
    }
    g_tidyStage = 3; g_tidyUntil = millis() + 500;
    setAnim(pa("idle"));
    return true;
  }
  // setting it down, the right way up, exactly where it was —
  // BUT NOT UNDER A SLEEPING CAT. The gate below is checked when he PICKS THE JOB UP, and that
  // was the whole of it, so nothing stopped her arriving while he was already carrying it: she
  // settles on the table, he finishes the errand, and the jar goes down beside her. Jon, twice:
  // "he needs to wait until the cat is off the table or shelf", "i saw him put the jar on while
  // the cat was sleeping". He stands and holds it until she moves.
  setAnim(pa("idle"));
  if (catOnItsSpot(g_tidy)) { g_tidyUntil = millis() + 400; return true; }
  if (millis() >= g_tidyUntil) {
    o->x = home->x; o->y = home->y;
    o->down = false; o->held = false; o->roll = 0; o->aim = NAN;
    g_tidy = -1;
    say("put back where it was");
    // AND THEN HE GOES. Clearing the timer was not enough — the wander roll only sends him
    // somewhere a quarter of the time, so he stood at the table for beat after beat (Jon: "he
    // also isnt moving away from the table after placing the can on top"). The builder gives
    // him an explicit goal instead: "away from what he has just put right, rather than looming
    // over it" — it tries the far side first, at 70, 55, 90, 110 then 45 px.
    const int first = (g_fx < 160.0f) ? 1 : -1;
    const int dists[5] = {70, 55, 90, 110, 45};
    int gx = -1;
    for (int s = 0; s < 2 && gx < 0; s++) {
      const int side = s ? -first : first;
      for (int d = 0; d < 5; d++) {
        const int cand = (int)g_fx + side * dists[d];
        int top, bot;
        if (cand < 14 || cand > 306) continue;
        if (sceneActive() && !sceneFloorLane(cand, &top, &bot)) continue;
        gx = cand; break;
      }
    }
    if (gx < 0) gx = (int)g_fx + (first * 60);
    int gy = (int)g_fy;
    avoidChairFront(&gx, &gy);           // "away from what he put right" must not mean the chair
    clampErrandToFloor(&gx, &gy);
    g_tx = gx; g_ty = gy;
    g_wanderT = 6.0f;          // and he stays gone for a bit rather than drifting straight back
    g_departAt = 0;
    g_settleUntil = 0;
  }
  return true;
}

static void think(float dt) {
  // The job drives his position and animation itself, so ordinary behaviour must stand down
  // for the duration — otherwise setAnim() runs every frame and the sequence never shows.
  if (g_workStage) return;
  // A RUNNING action gets the stage even during dance mode — this is what lets a mid-party
  // cuddle actually show its animation instead of being overwritten by danceStep every frame.
  // danceBegin() clears stale actions when the party starts, so the only actions that reach
  // here during dance are ones deliberately allowed through the menu (cuddles).
  if (g_action) {
    if (millis() >= g_actionEnd) {
      g_action = false;
      if (g_actIsWork) { g_actIsWork = false; creditWork(); }
      if (g_pottySeq == 1) {
        g_pottySeq = 2;
        const char *washAct = (sceneActMark("wash") >= 0 || sceneActAnim("wash")) ? "wash"
                            : (sceneActMark("bath") >= 0 || sceneActAnim("bath")) ? "bath"
                            : nullptr;
        if (washAct) {
          g_pottyLock = true;
          whyFor(WHY_ROUTINE, "");
          bool ok = sceneErrandTo(washAct);
          g_pottyLock = false;
          if (ok) return;
        }
        g_pottySeq = 0;
      } else if (g_pottySeq == 2) {
        g_pottySeq = 0;
      }
      if (g_workUntil || g_workReps > 0) {
        // the child hears about a room that was too big to load (rehearsal S7)
  if (g_scTooBig) {
    static uint32_t saidBig = 0;
    if (!saidBig || millis() - saidBig > 60000) {
      saidBig = millis();
      say("that room is too big for bunbun - take a few things out and send it again");
    }
    g_scTooBig = false;
  }
  if (g_dbgAct[0]) { g_workUntil = 0; g_workReps = 0; }  // a command outranks work
        else if ((g_workUntil ? millis() < g_workUntil : g_workReps > 0) &&
            (sceneActMark("work") >= 0 || sceneActAnim("work"))) {
          // HE WALKS HIS ROUNDS (Jon: "shouldnt he walk around?"): back-to-back reps of
          // a 5s work clip read as standing still. Each rep ends in a short stroll; the
          // tick below starts the next rep from wherever the stroll took him.
          g_workNextAt = millis() + 2500 + esp_random() % 3500;
          g_wanderT = 0.5f;
          return;
        }
        if (g_workUntil) { g_workUntil = 0; say("work is all done!"); }
      }
      if (g_scCurRole != SCENE_ROLE_MAIN && !g_doorTrip) {
        if (millis() < g_roomMinStay) {          // the visit's minimum stay first
          if (!g_visitHomeAt) g_visitHomeAt = g_roomMinStay;
          g_wanderT = 2.0f;
        } else { sceneDoorTo(SCENE_ROLE_MAIN, ""); return; }
      }
    } else return;
  }

  // the party never steals an errand either - he walks his trip plainly and the
  // dance takes him back the moment he is free
  if (discoDown() && S.lights && !S.sick &&
      !g_doorTrip && g_visit < 0 && g_tx < 0 && !g_action && !g_sleepPending) {
    danceStep(dt); return;
  }
  if (!S.lights) {
    // A COMMAND OUTRANKS BEDTIME. This return used to fire before the errand code and
    // wipe g_tx every tick, so at night BATHE/EAT/WORK started a door walk that died
    // the same instant ("i hit take a bath and its not walking to the next room - a
    // reboot didnt fix it": nothing was broken, it was 9pm). While a debug act waits,
    // an errand walks, or a settle performs, he stays up; the moment the errand is
    // done and he is back to no-target, this branch reclaims him and he sleeps.
    if (!g_dbgAct[0] && g_visit < 0 && !g_doorTrip && millis() >= g_settleUntil) {
      if (g_scCurRole != SCENE_ROLE_MAIN) {
        // his bed is at home: the working day ends and he walks back before sleeping
        g_workUntil = 0; g_workReps = 0; g_workNextAt = 0; g_visitHomeAt = 0;
        sceneDoorTo(SCENE_ROLE_MAIN, "");
        return;
      }
      // AT HOME, THE BED IS THE BED ("uhh he just went and slept on the chair"):
      // lights-out used to bed him wherever it caught him. If the room has a sleep
      // spot and he is not on it, he walks there first; sleep takes him on arrival.
      {
        int sm = sceneActMark("sleep");
        if (sm >= 0) {
          const int dx = g_scBun[sm].x - (int)g_fx, dy = g_scBun[sm].y - (int)g_fy;
          whyFor(WHY_SCHEDULE, "energy");
          if (dx * dx + dy * dy > 144 && sceneErrandTo("sleep")) return;
        }
      }
      const char *cs = sceneActAnim("sleep");   // the child's sleep, if the scene brought one
      setAnim(cs ? cs : pa("sleep"));
      g_tx = g_ty = -1;
      return;
    }
  }
  if (S.sick) {
    // A SICK PET CANNOT BE ON AN ERRAND (rehearsal S9): this used to wipe the target
    // every frame while the bedtime branch kept re-issuing one, so he stood frozen
    // for ever with the reason board cheerfully saying he was off to bed. Being ill
    // ends the trip honestly instead of fighting it.
    if (g_visit >= 0 || g_doorTrip || g_sleepPending) {
      g_visit = -1; g_doorTrip = 0; g_sleepPending = 0;
      g_workUntil = 0; g_workReps = 0; g_workNextAt = 0; g_pottySeq = 0;
      say("bunbun is too poorly for that - he needs medicine");
    }
    setAnim(moodAnim()); g_tx = g_ty = -1; return;
  }

  // HE MINDS FIRST, THEN HE TIDIES. The builder runs the huff at the very top of stepBun, ahead
  // of the chores, so the reaction lands while the thing is still rolling rather than after he
  // has quietly put it right — which would read as him not having noticed.
  if (g_huffT > 0.0f) {
    g_huffT -= dt;
    if (g_visit >= 0 || g_doorTrip || g_tx >= 0) {
      // mid-errand: the huff waits its turn instead of freezing the walk to sulk
    } else {
      setAnim(pa("angry"));
      return;
    }
  }
  if (g_huffAt >= 0) {
    const int who = g_huffAt;
    g_huffAt = -1;
    g_huffT = 0.8f + (esp_random() % 600) / 1000.0f;     // R(0.8,1.4)
    (void)who;              // the builder also turns him toward it; the device's facing lives in
                            // the anim name and `angry` is drawn front-on, so there is nothing
                            // to turn. Left here so the intent is not lost if a side pose lands.
    setAnim(pa("angry"));
    sfxNo();
    say("he huffs - that was where it lived");
    return;
  }

  if (tidyStep(dt)) return;   // something of his is on the floor; that is his whole turn

  // HE DOES NOT LOITER IN FRONT OF THE CHAIR. Jon, twice: "i dont want bunbun to do any passive
  // actions in front of the chair" / "never idles or does a passive emotion in front of the
  // chair". Filtering DESTINATIONS was only ever half the sentence — a destination filter cannot
  // see where he actually is. He arrives from a tidy errand that drove g_fx directly, or walks
  // through the span on the way somewhere else, or the window-watch spot puts him there, and
  // then the emote timer fires wherever he happens to be standing. So the rule is about his
  // POSITION, checked every frame, and it is the one place that has to be right.
  //
  // Two different situations, deliberately treated differently:
  //   PASSING THROUGH (he has a target beyond the span) — leave the trip alone, just stay quiet
  //     until he is out. Cancelling a furniture visit here would kill the pose at the far end.
  //   STOPPED IN IT (no target, or one that keeps him inside) — he leaves, on his feet, using the
  //     ordinary walker. Nothing teleports; "all actions should be procedural".
  bool chairZone = false;
  if (!bunComingHome() && !g_doorTrip && g_visit < 0) {
    // keep-out means DON'T LOITER, never don't walk (Jon's own rule): an errand or
    // door trip crossing the chair front is a walker, and stealing its target sent
    // every trip to the span's rim instead of the door.
    int clo, chi;
    if (chairFrontSpan(&clo, &chi) && g_fx >= (float)clo && g_fx <= (float)chi) {
      chairZone = true;
      if (g_tx < 0 || (g_tx >= clo && g_tx <= chi)) {
        g_settleUntil = 0;              // whatever he had settled into, he is not doing it here
        g_watching = false;
        g_visit = -1;
        int nx = (int)g_fx, ny = (int)g_fy;
        avoidChairFront(&nx, &ny);
        // If neither side of the span is on the floor there is nowhere to send him, and setting
        // a target equal to where he stands would arrive instantly and re-fire this every frame.
        // A chair that wide is a scene problem, not something to thrash over.
        if (nx != (int)g_fx) {
          clampErrandToFloor(&nx, &ny);
          g_tx = nx; g_ty = ny;
          g_departAt = 0; g_holdUntil = 0; // no thoughtful pause first — he just steps aside
          g_tripLen = sqrtf((g_tx - g_fx) * (g_tx - g_fx) + (g_ty - g_fy) * (g_ty - g_fy));
        }
      }
    }
  }

  g_danceHop = g_danceSway = 0;

  // spontaneous joy, as in the HTML: a well-cared-for bunbun shows it
  g_emoteT -= dt;
  // The joy-hop, the love moment and the teen's phone all fire from here, wherever he is stood.
  // Held just short of firing while he is in the chair's frontage — including while merely
  // walking through it — so it lands half a second after he is clear instead of never.
  if (chairZone && g_emoteT < 0.5f) g_emoteT = 0.5f;
  if (g_emoteT <= 0) {
    // WISH (2026-08-05, spoken into the mic, transcribed by the nightly pipeline):
    // "bunbun needs to be able to jump more frequently." The joy-jump existed but hid
    // behind a 9-22s timer and a near-perfect mood gate, so it almost never showed.
    // Faster timer, friendlier gate, jump favoured over love.
    g_emoteT = 5 + (esp_random() % 800) / 100.0f;
    // Not while he is settled into something. This ran BEFORE the settle check, so a jump
    // could cut the radio dance (or a furniture pose) short partway through.
    // NOTE: no dance here either. Dancing belongs to the radio visit and nowhere else — he
    // must be stood beside the radio with headphones on, never miming it mid-floor. The only
    // place teen_dance is set is on arriving at RADIO_STAND.
    if (g_tx < 0 && millis() >= g_settleUntil
        && S.food > 50 && S.fun > 50 && S.energy > 35) {
      // Kids' revision (launch week): the teen sometimes pulls out his
      // phone and texts for a bit between wanders. Art-gated on teen_text
      // so the event stays invisible until that state lands in the pak.
      if (S.phase == PH_TEEN && hasAnim("teen_text") &&
          pakFind("teen-text/0") && (esp_random() % 100) < 30) {
        startAction("teen_text", 4.5f);
        return;
      }
      // THE FLURRY AND THE FLOP — the part that makes his liveliness his own rather than a
      // metronome. placer.html: "how well he is kept sets the size of the flurry and the length
      // of the flop". A well-kept bunbun bursts five hops and then goes down for 45-60s; a
      // neglected one manages one or two and is flat out for two minutes. The device had the
      // same emote CLOCK as the builder but neither the burst nor the collapse, so his rate was
      // identical whatever his stats said — busy forever, in a way no animal is.
      if (millis() < g_restUntil) return;                 // spent: nothing performs
      const float care = (S.food + S.fun + S.energy) / 3.0f;
      int bandHops; uint32_t restLo, restSpan;
      if      (care >= 70) { bandHops = 5;                     restLo = 45000; restSpan = 15000; }
      else if (care >= 40) { bandHops = 3;                     restLo = 45000; restSpan = 30000; }
      else                 { bandHops = 1 + (esp_random() % 2); restLo = 60000; restSpan = 60000; }
      if (g_hopsLeft < 0) g_hopsLeft = bandHops;

      // DIRECTOR, 2026-08-15: "60s of emote in 300s is a pet performing at you." But the WISH
      // above is specifically about JUMPING, so the hop keeps its fast timer and everything
      // that is NOT a hop waits at least 25s between showings. He jumps as often as the kids
      // asked; the room gets its quiet back from the rest.
      static uint32_t lastSoft = 0;
      bool love = (esp_random() % 100) < 35;
      if (love && millis() - lastSoft < 25000UL) return;
      if (love) lastSoft = millis();
      else {
        // a HOP spends one of the flurry; the soft emotes keep their own slow clock and never
        // eat into it, exactly as the builder's `if (pick.a!=='jump')` split does
        if (--g_hopsLeft <= 0) {
          g_hopsLeft = -1;
          g_restUntil = millis() + restLo + (esp_random() % restSpan);
        }
      }
      // W-051 (Jon, twice: "it just did a little jump and I didn't hear
      // anything"): spontaneous joy is audible now - the happiest thing
      // he does unprompted finally sounds like it. Hops boing, love
      // moments purr. Five-minute cooldown: delight, not a doorbell.
      {
        // THE FIRST ONE ALWAYS SOUNDS. `joyAt = 0` plus `millis() - joyAt > 300000` is false for
        // the whole first five minutes of every boot, so the first joy-hop a child sees after
        // switching on was guaranteed silent — and every OTA is a boot. That is W-051 exactly,
        // which Jon raised twice: "it just did a little jump and I didn't hear anything".
        static uint32_t joyAt = 0;
        static bool joyFirst = true;
        if (g_fxLevel > 0 && (joyFirst || millis() - joyAt > 300000UL)) {
          joyFirst = false;
          joyAt = millis();
          if (love) sfxPurr(); else sfxHomeAgain();
        }
      }
      startAction(love ? pa("love") : pa("jump"), 2.0f);
      return;
    }
  }
  if (g_dbgAct[0]) {
    char a[8]; strncpy(a, g_dbgAct, sizeof(a)); a[7] = 0; g_dbgAct[0] = 0;
    Serial.printf("debug act: %s\n", a);
    if (!strcmp(a, "potty")) g_poopDue = millis() + 1500;
    else { whyFor(WHY_BUTTON, ""); sceneErrandTo(a); }
  }
  // settled into a piece of furniture â€” hold the pose until it times out
  if (millis() < g_settleUntil) return;
  if (g_visit >= 0 && millis() >= g_settleUntil && g_tx < 0) {
    g_visit = -1;
    if (g_pottySeq == 1) {
      g_pottySeq = 2;
      // the SINK is act 'wash' ("counts as: washing up"); only if the bathroom has no
      // sink does the routine settle for the tub - so the BATHE button's bath/shower
      // and the potty's hand-wash never fight over the same tag
      const char *washAct = (sceneActMark("wash") >= 0 || sceneActAnim("wash")) ? "wash"
                          : (sceneActMark("bath") >= 0 || sceneActAnim("bath")) ? "bath"
                          : nullptr;
      if (washAct) {
        g_pottyLock = true;
        whyFor(WHY_ROUTINE, "");
        bool ok = sceneErrandTo(washAct);  // wash those hands
        g_pottyLock = false;
        if (ok) return;
      }
      g_pottySeq = 0;                      // no sink here: straight home
    } else if (g_pottySeq == 2) {
      g_pottySeq = 0;                      // hands washed; the home walk below takes over
    }
    // a placed work performance just finished: the meter moves
    for (int i = 0; i < g_scAnimN; i++)
      if (!strcmp(g_scAnim[i].key, g_markAnim) && !strcmp(g_scAnim[i].act, "work")) {
        creditWork(); break;
      }
    // A WORK SESSION OWNS THE CLOCK (Jon: the bug collector must not stop early): while
    // it runs, the performance ending just means the next work animation begins.
    if (g_workUntil || g_workReps > 0) {
      if (g_dbgAct[0]) { g_workUntil = 0; g_workReps = 0; }    // a command outranks work
      else if ((g_workUntil ? millis() < g_workUntil : g_workReps > 0) &&
          (sceneActMark("work") >= 0 || sceneActAnim("work"))) {
        g_workNextAt = millis() + 2500 + esp_random() % 3500;   // the same stroll
        g_wanderT = 0.5f;
        return;
      }
      if (g_workUntil) { g_workUntil = 0; say("work is all done!"); }
    }
    // an act in another room is over: walk home - after the visit's minimum stay
    if (g_scCurRole != SCENE_ROLE_MAIN && !g_doorTrip) {
      if (millis() < g_roomMinStay) {
        if (!g_visitHomeAt) g_visitHomeAt = g_roomMinStay;
        g_wanderT = 2.0f;
      } else { sceneDoorTo(SCENE_ROLE_MAIN, ""); return; }
    }
    // the performance is OVER: drop to his mood/idle right now, exactly as the preview's
    // rest does. Leaving the clip up made a 5-second duration look like 20 ("its going
    // longer than 5" - the animation kept playing while he waited to decide what was next).
    setAnim(moodAnim());
    g_performBehind = false;
    // 35-60s between furniture visits, down from 60-100s. This cooldown was the real limit on
    // how often he reached the radio — the per-visit chance barely mattered while this was
    // throttling every furniture trip.
    g_nextVisitOK = millis() + 35000 + esp_random() % 25000;
    g_wanderT = 10.0f + (esp_random() % 1000) / 100.0f;   // settle for a good while after a visit
  }

  // A SIDE ROOM IS FOR THE ERRAND, HOME IS FOR LIVING (Jon: "he is in the bathroom
  // with no way home other than sleep"): free in a side room, nothing owed, minimum
  // stay served - he walks home. The action-end and settle-end paths already do
  // this, but a stay that ends in a wander had NO exit; this tick is the guarantee
  // that no state whatsoever can strand him in a side room.
  if (g_scCurRole != SCENE_ROLE_MAIN && !g_doorTrip && g_visit < 0 && g_tx < 0 &&
      !g_action && !g_workUntil && g_workReps <= 0 && !g_sleepPending &&
      g_pottySeq == 0 && millis() >= g_roomMinStay) {
    sceneDoorTo(SCENE_ROLE_MAIN, "");
    return;
  }

  // a walk-to-bed that died (backstop, interruption) is re-issued the moment he is
  // free - g_sleepPending must never dangle with the lights still on
  if (g_sleepPending && g_tx < 0 && g_visit < 0 && !g_doorTrip && !g_action && S.lights) {
    whyFor(WHY_SCHEDULE, "energy");
    if (!sceneErrandTo("sleep")) {
      S.lights = 0; g_sleepAtMs = millis();
      if (g_sleepPending == 2) { g_nightSleep = true; saveSleepState(3); }
      g_sleepPending = 0;
      saveState();
    }
    return;
  }

  // the stroll between work reps is over: the next rep starts where he stands
  if (g_workNextAt && millis() >= g_workNextAt) {
    if (!g_workUntil && g_workReps <= 0) g_workNextAt = 0;   // nothing owed: drop it
    else if (g_visit < 0 && !g_doorTrip && !g_action) {
      g_workNextAt = 0;
      whyFor(WHY_SCHEDULE, "");
      sceneErrandTo("work");
      return;
    }
  }
  // the fruitless-visit linger is over: head home (cleared by any new door trip)
  if (g_visitHomeAt && millis() >= g_visitHomeAt) {
    g_visitHomeAt = 0;
    if (g_scCurRole != SCENE_ROLE_MAIN && !g_doorTrip) { sceneDoorTo(SCENE_ROLE_MAIN, ""); return; }
  }

  if (g_tx >= 0) {
    // Backstop: give up on a target he cannot reach. Even with every destination pushed clear
    // of the scenery, one bad coordinate should degrade to "wanders off again" rather than
    // "walks on the spot until reset" — which is how the window-watch bug presented.
    static int lastTx = -1, lastTy = -1;
    static uint32_t targetAt = 0;
    if (g_tx != lastTx || g_ty != lastTy) { lastTx = g_tx; lastTy = g_ty; targetAt = millis(); }
    // ...but NOT on the walk home. That one is deliberately half speed (18px/s, "he
    // needs to slowly move towards it") and the doorway-to-basket run is 134px — 7.4s
    // against a 9s giveup, which is no margin at all on a panel that has been seen at
    // 10fps with 70ms draw spikes. The homecoming has its own 45s deadline below.
    if (g_homeStage != 1 && millis() - targetAt > 9000) {
      // a side-room visit whose walk died still goes home eventually
      if (g_scCurRole != SCENE_ROLE_MAIN && !g_visitHomeAt)
        g_visitHomeAt = millis() + 12000;
      g_tx = g_ty = -1; g_visit = -1; g_wanderT = 1.0f;
      g_performBehind = false;
      g_doorTrip = 0; g_workUntil = 0; g_pottySeq = 0;
      if (g_scCurRole != SCENE_ROLE_MAIN) { sceneDoorTo(SCENE_ROLE_MAIN, ""); return; }
      if (g_sleepPending) {          // bed unreachable: sleep where he stands, never strand it
        S.lights = 0; g_sleepAtMs = millis();
        if (g_sleepPending == 2) { g_nightSleep = true; saveSleepState(3); }
        g_sleepPending = 0; saveState();
      }
      Serial.println("gave up on an unreachable target");
      return;
    }
    // Stand still until the departure pause is over — he has decided where to go but has not
    // started walking yet. Keeps the idle pose rather than snapping into a walk cycle.
    if (millis() < g_departAt) { setAnim(moodAnim()); return; }

    float dx = g_tx - g_fx, dy = g_ty - g_fy, d = sqrtf(dx * dx + dy * dy);
    // arrived at the window while something is out there: sit with his back to us and watch
    if (d < 4 && g_visit < 0 && g_birdReacted && g_birdPhase == 2) {
      // g_visit < 0: only an aimless arrival turns into birdwatching. This used to hijack
      // ANY trip that happened to arrive while a bird was out - including a commanded
      // errand to the tub, which then sat down and watched the window instead of bathing.
      g_tx = g_ty = -1;
      g_watching = true;
      g_settleUntil = millis() + 60000;                 // ended when the visitor leaves
      g_birdLeaveAt = millis() + 3000 + esp_random() % 2000;   // 3-5s of watching
      setAnim(S.phase == PH_BABY ? "baby_sit_n" : "idle_n");
      return;
    }
    // The door target is CLAMPED into the floor polygon, and on the farmhouse the
    // clamp can land a stride inside the wall - at night he stood at x~25 against a
    // target the d<4 test missed by inches until the 9s backstop shot the trip. A
    // door is an edge, not a mark: being NEAR it is being through it.
    if (d < 12 && g_visit == 4 && g_doorTrip) {
      // AT THE DOOR: swap the room, walk in from the mirror edge, then do the errand
      g_doorTrip = 0; g_tx = g_ty = -1; g_visit = -1;
      bool exitedLeft = (g_fx < 160);              // the ROOM's centre, not the panel's
      if (!sceneLoadRole(g_doorRole)) { g_wanderT = 1.0f; return; }
      catDismissIfAway();                  // the cat is a main-room guest, always
      g_cloudSceneDone = false;              // the new room's sky rules apply fresh
      int ex2 = exitedLeft ? 320 - 8 : 8;          // mirror edge in room space
      int ey2 = (int)g_fy;
      // the NEW room's floor may not reach this height - clamp the height first, or
      // clampErrandToFloor yanks the X and he materialises mid-room instead of at
      // the edge ("when he enters the main room from work he isnt on the side")
      if (g_scHasBounds) {
        if (ey2 < g_scBounds.y0 + 4) ey2 = g_scBounds.y0 + 4;
        if (ey2 > g_scBounds.y1 - 2) ey2 = g_scBounds.y1 - 2;
      }
      clampErrandToFloor(&ex2, &ey2);
      g_fx = (float)ex2; g_fy = (float)ey2;
      S.x = (int16_t)ex2; S.y = (int16_t)ey2;
      Serial.printf("door: now in role %d\n", (int)g_scCurRole);
      // A VISIT IS A VISIT (Jon: "when he goes to work or kitchen, he needs to stay in
      // there for at least 10 seconds"): even a 3-second meal keeps him in the room a
      // human-visible while before the walk home.
      g_roomMinStay = (g_scCurRole != SCENE_ROLE_MAIN) ? millis() + 10000 + esp_random() % 4000 : 0;
      // THROUGH THE MIDDLE (Jon: "when he transitions from room to room he needs to
      // go to the middle of each room and then do the action. the same for coming
      // back"): entering at the edge, he first walks to the room's middle - the act
      // he came for waits in g_doorAct and runs from there, so every crossing reads
      // as a real entrance instead of a teleport-and-shuffle along the wall.
      { int mx = 160, my = (int)g_fy;              // the room's true middle
        clampErrandToFloor(&mx, &my);
        clearOfBlocks(&mx, &my);
        g_tx = mx; g_ty = my;
        g_visit = 5;                         // 5 = the entrance walk to the middle
      }
      return;
    }
    if (d < 12 && g_visit == 5) {
      // MADE IT TO THE MIDDLE: now the act he crossed for (or, coming home with no
      // act, his own rhythm). This is the exact consumption the door arrival used to
      // run at the edge - only the starting point moved.
      g_tx = g_ty = -1; g_visit = -1;
      if (g_doorAct[0]) {
        if (!strcmp(g_doorAct, "work")) {
          if (!g_workUntil && sceneWorkMin() > 0)
            g_workUntil = millis() + (uint32_t)sceneWorkMin() * 60000UL;
          g_workReps = g_workUntil ? -1 : 2;   // a visit owes two jobs; a session owes the clock
        }
        g_pottyLock = (g_pottySeq != 0);
        whyFor(g_whyDoor ? g_whyDoor : (uint8_t)WHY_BUTTON, "");   // the reason travelled too
        bool ok = sceneErrandTo(g_doorAct);
        // a scene without a "the toilet" animation still has its plain sit
        if (!ok && !strcmp(g_doorAct, "toilet")) ok = sceneErrandTo("sit");
        // BATHE accepts the sink and washing accepts the tub - Jon's washroom tub
        // counts-as "wash", and without this the bath errand crossed the whole
        // world just to shrug and walk home
        if (!ok && !strcmp(g_doorAct, "bath")) ok = sceneErrandTo("wash");
        if (!ok && !strcmp(g_doorAct, "wash")) ok = sceneErrandTo("bath");
        g_pottyLock = false;
        if (!ok) {
          // NO INSTANT BOUNCE (Jon: "it flashed for a second to work scene then almost
          // instantly went back"): the room he was sent to has nothing for this act, but
          // he still WENT there - so he looks around for a while like anyone would, and
          // the linger deadline below walks him home.
          g_pottySeq = 0;
          g_visitHomeAt = millis() + 20000 + esp_random() % 15000;
          g_wanderT = 3.0f;
        }
      } else {
        g_wanderT = 6.0f;                    // home again, back to his own rhythm
      }
      return;
    }
    // d<4 could be unreachable by inches when a mark hugs the floor's edge - Jon's
    // sleep rug at y=238 clamped to the boundary and he "walks in place and then
    // eventually falls asleep" (the 9s backstop, not an arrival). Being NEAR the
    // doorstep is enough: the settle below snaps him onto the authored spot anyway,
    // exactly as it always has.
    if (d < 9 && g_visit >= 0 && g_visit != 5) {
      // arrived at the furniture: settle in
      g_tx = g_ty = -1;
      g_settleUntil = millis() + 4000 + esp_random() % 6000;
      bool b = (S.phase == PH_BABY);
      if (g_visit == 3) {
        // SETTLE ONTO THE AUTHORED SPOT - up on the beanbag, into the tub - exactly as the
        // assembler's sim settles onto (obj + place offset). The walk only ever came to the
        // doorstep; the performance happens where the child put it ("activity based
        // animations occur in the correct spot").
        S.x = (int16_t)g_markX; S.y = (int16_t)g_markY;
        g_fx = (float)g_markX; g_fy = (float)g_markY;
        // depth resolves FIRST, whatever else this arrival does: the sleep-errand return
        // below used to skip it, leaving a bath's "behind" armed - and every prop painted
        // over the sleeping pet ("he is sleeping under the rug").
        g_performBehind = false;
        for (int i = 0; i < g_scAnimN; i++)
          if (!strcmp(g_scAnim[i].key, g_markAnim)) { g_performBehind = g_scAnim[i].behind; break; }
        if (g_sleepPending) {
          // he walked to bed; NOW the lights go out, right here on the child's sleep spot
          S.lights = 0; g_sleepAtMs = millis();
          if (g_sleepPending == 2) { g_nightSleep = true; saveSleepState(3); }
          g_sleepPending = 0;
          // ASLEEP THIS TICK, not after a settle of moonwalking ("he still walking in
          // place before sleeping"): the arrival above armed a 4-10s settle, which
          // blocked the asleep branch while the WALK clip kept playing on the rug.
          // Nothing here needs waiting out - drop the settle, clear the visit, and put
          // the sleep clip up ourselves.
          g_settleUntil = 0;
          g_visit = -1;
          { const char *cs = sceneActAnim("sleep");
            setAnim(cs ? cs : pa("sleep")); }
          saveState();
          return;                    // the asleep branch owns him from the next tick
        }
        // AT THE MARK: the ASSEMBLER's timing, kept (Jon: "sleep should walk over to the
        // location and sleep like the assembler"). Its sim performs TWO FULL LOOPS of the
        // animation, then rests AT the spot with the animation still showing for its normal
        // 18-42s between-journeys wait - which is why a sleep in the preview reads as a real
        // nap. The old 3-7s came from the earlier builder's atmark and cut every sleep short.
        setAnim(pa(g_markAnim));
        {
          const AnimDef *ad = findAnim(pa(g_markAnim));
          uint32_t loopMs = (uint32_t)(1000.0f * ad->frames /
                                       (ad->fps > 0.1f ? ad->fps : 7.0f));
          float dur = 0;
          for (int i = 0; i < g_scAnimN; i++)
            if (!strcmp(g_scAnim[i].key, g_markAnim)) { dur = g_scAnim[i].dur; break; }
          // an authored duration replaces the whole default (2 loops + the 18-42s rest)
          g_settleUntil = millis() + (dur > 0.1f ? (uint32_t)(dur * 1000)
                                     : 2 * loopMs + 18000 + esp_random() % 24000);
        }
        g_nextVisitOK = millis() + 35000 + esp_random() % 25000;
        // the line matches the ACTION ("make it more in line with the action and game") -
        // say() swaps "bunbun" for the pet's chosen name on the way to the ticker
        {
          const char *ln = "bunbun found something fun to do";
          for (int i = 0; i < g_scAnimN; i++)
            if (!strcmp(g_scAnim[i].key, g_markAnim)) {
              if (g_scAnim[i].txt[0]) { ln = g_scAnim[i].txt; break; }   // the child's words
              const char *ac = g_scAnim[i].act;
              if      (!strcmp(ac, "sit"))   ln = "bunbun found a comfy spot";
              else if (!strcmp(ac, "sleep")) ln = "bunbun is getting sleepy... zzz";
              else if (!strcmp(ac, "bath"))  ln = "splish splash! bunbun is in the bath";
              else if (!strcmp(ac, "eat"))   ln = "bunbun is munching away";
              else if (!strcmp(ac, "idle"))  ln = "bunbun is just hanging out";
              else if (!strcmp(ac, "love"))  ln = "bunbun feels the love";
              else if (!strcmp(ac, "play"))  ln = "bunbun is playing!";
              break;
            }
          say(ln);
        }
      }
      else if (g_visit == 2) {
        // reached the radio — put the headphones on and dance for a good while
        g_settleUntil = millis() + 9000 + esp_random() % 5000;
        setAnim("teen_dance");
      }
      else if (g_visit == 1) setAnim(pa("tired"));
      else setAnim((esp_random() % 100) < 60 ? pa("play")
                                             : pa("bored"));
      return;
    }
    // Long pause after arriving. This is a lofi, passive pet — it should mostly be still, and
    // movement should read as an occasional event rather than constant pacing. Was 3.2-11s.
    // g_settleHold freezes the first idle frame for a beat so he visibly STOPS on arrival
    // rather than snapping from walk straight into the idle cycle.
    if (d < 4) {
      g_tx = g_ty = -1;
      g_performBehind = false;       // he has arrived somewhere plain; nothing covers him
      // 18-42s between journeys. The ANIMATION speeds are separate (see the fps column in
      // ANIMS) — this is only how often he decides to go somewhere, which is what read as
      // restless. He should look like he is mostly just there.
      g_wanderT = 18.0f + (esp_random() % 2400) / 100.0f;
      // Arrival-freeze KILLED (Jon 8/12: "it keeps stopping on the wrong
      // animation. should we just kill that?" — yes). When the mood clip was
      // already showing, setAnim didn't reset the frame, so the hold froze a
      // random mid-stride frame and read as a glitch. He now just settles
      // into the mood clip and lets it play.
      // THE ASSEMBLER'S "ANYWHERE" RULE, kept as a rule: an animation the child bound to
      // "anywhere he walks" plays wherever he happens to settle - the same dice roll the
      // assembler's own sim makes - instead of at one fake pinned point mid-floor.
      const char *aw = sceneAnywhereAnim();
      if (aw && contentMood() && (esp_random() % 100) < 35) setAnim(aw);
      else setAnim(moodAnim());
    }
    else {
      // Half speed on the way home (Jon 8/14: "he needs to slowly move towards it") — a
      // bunny who has been out all night does not bound back in.
      float sp = (g_homeStage == 1 ? 18.0f : 42.0f) * dt;
      g_fx += dx / d * sp; g_fy += dy / d * sp;
      // WALK AROUND, NOT THROUGH: when a step lands inside a footprint the push-out slides
      // him along its FRONT - with 4px of daylight, so his sprite skirts the furniture
      // instead of ploughing across its art on the way to the window.
      int nx = (int)g_fx, ny = (int)g_fy; clearOfBlocks(&nx, &ny);
      if (ny != (int)g_fy) ny += 4;
      g_fy = ny;
      S.x = nx; S.y = ny;
      // face the way he's travelling â€” all four directions, as the HTML does
      bool horiz = fabsf(dx) > fabsf(dy);
      bool b = (S.phase == PH_BABY);
      const char *a;
      if (b) {
        // learning to walk: once he's covered crawlFrac of the trip he tumbles, plays the
        // fall once, and crawls the rest â€” the wobble you specified for the 6-9h window
        float done = g_tripLen > 0 ? 1.0f - (d / g_tripLen) : 1.0f;
        if (!g_crawling && done >= g_crawlFrac) {
          g_crawling = true;
          startAction(dx > 0 ? "baby_fall_right" : "baby_fall_left", 0.85f);
          return;
        }
        bool walk = babyCanStand() && !g_crawling;
        if (horiz) a = walk ? (dx > 0 ? "baby_walk_right" : "baby_walk_left")
                            : (dx > 0 ? "baby_crawl_right" : "baby_crawl_left");
        else       a = walk ? (dy > 0 ? "baby_walk_down" : "baby_walk_up")
                            : (dy > 0 ? "baby_crawl_down" : "baby_crawl_up");
      } else {
        const char *tk = sceneTravelAnim(dx, horiz);
        if (tk)         a = tk;
        else if (horiz) a = pa(dx > 0 ? "walk_right" : "walk_left");
        else            a = pa(dy > 0 ? "walk_down" : "walk_up");
      }
      setAnim(a);
    }
    return;
  }
  setAnim(moodAnim());

  // LINGER. Every so often, freeze the idle on a single frame for a few beats. Without this
  // the idle cycles forever and even a slow loop reads as perpetual motion — the stillness has
  // to actually stop, not just slow down. Holding one frame is what makes him look like he is
  // simply sitting in the room rather than being animated at it.
  g_lingerT -= dt;
  if (g_lingerT <= 0) {
    g_lingerT = 5.0f + (esp_random() % 700) / 100.0f;        // consider one every 5-12s
    if ((esp_random() % 100) < 65) {
      float beats = 2.0f + (esp_random() % 300) / 100.0f;    // hold for 2-5 beats
      // Clamp in WALL CLOCK as well as beats. The length is beat-derived so the stillness sits
      // with the music, but beatSecs() runs to 1.3s on a slow track and 8 beats of that was a
      // 10s motionless character — past "calm" and into "it has crashed". Whatever the tempo,
      // a linger never outstays this.
      uint32_t ms = (uint32_t)(beatSecs() * 1000 * beats);
      g_holdUntil = millis() + min(ms, (uint32_t)4500);
    }
  }

  g_wanderT -= dt;
  if (g_wanderT <= 0) {
    // THE ASSEMBLER'S CADENCE STANDS in a room a child built (Jon: "the bath animation isnt
    // showing up"). Its sim never rolls "shall I bother?" dice: after every 18-42s rest it
    // PICKS AN ACTIVITY, walks there, performs, and takes one wander between activities -
    // that alternation IS the pacing. Under the old gates (10-25% to move at all, then 50%
    // to pick a mark) a specific animation surfaced every 10-30 minutes; in the preview it
    // surfaces every minute or two. A room with no marks keeps the old, calmer dice.
    int fillers = 0;
    if (sceneActive())
      for (int m = 0; m < g_scBunN; m++)
        if (markIsFiller(m)) fillers++;
    bool sceneActs = fillers > 0;
    if (sceneActs ||
        (esp_random() % 1000) / 1000.0f < (contentMood() ? 0.25f : 0.10f)) {
      // sometimes head for a piece of furniture instead of a random spot
      // A MARK THE CHILD PLACED WINS. The builder takes a bun mark half the time it is free to
      // go anywhere: `if (S.bunMarks.length && S.t > b.nextVisitOK && rnd()<0.5) b.st='tomark'`.
      // The compiled-in furniture spots below are the fallback for a room that has none.
      {
        int mx, my; const char *ma = nullptr;
        // activity -> wander -> activity, preferring a different mark than last time - the
        // assembler's fresh-pick rule, in its alternation
        bool takeMark;
        if (sceneActs) takeMark = !g_lastWasMark;
        else takeMark = millis() > g_nextVisitOK && (esp_random() % 100) < 50;
        int mi = -1;
        if (takeMark && fillers > 0) {
          // pick among the FILLER marks only, still preferring a different one than last
          int pick = (int)(esp_random() % (uint32_t)fillers), seen = 0;
          for (int m = 0; m < g_scBunN; m++) {
            if (!markIsFiller(m)) continue;
            if (seen++ == pick) { mi = m; break; }
          }
          if (fillers > 1 && mi == g_lastMark) {
            int m2 = mi;
            do { m2 = (m2 + 1) % g_scBunN; } while (!markIsFiller(m2) || m2 == mi);
            mi = m2;
          }
          if (mi >= 0) { mx = g_scBun[mi].x; my = g_scBun[mi].y; ma = g_scBun[mi].anim; }
        }
        if (mi >= 0) {
          g_lastMark = mi; g_lastWasMark = true;
          g_performBehind = false;     // stepping out to travel: nothing covers a walker
          g_markX = mx; g_markY = my;              // where the performance actually happens
          // walk to the front doorstep: clearOfBlocks pushes a point in a footprint DOWN to
          // the front edge - the assembler's reachableApproach, by the device's own physics
          clearOfBlocks(&mx, &my);
          clampErrandToFloor(&mx, &my);
          g_tx = mx; g_ty = my;
          g_visit = 3;                                  // "at a mark" — perform what was placed
          strncpy(g_markAnim, ma, sizeof(g_markAnim) - 1);
          g_markAnim[sizeof(g_markAnim) - 1] = 0;
          g_tripLen = sqrtf((g_tx - g_fx) * (g_tx - g_fx) + (g_ty - g_fy) * (g_ty - g_fy));
          g_crawling = !babyCanStand(); g_crawlFrac = babyCanStand() ? 2 : 0;
          g_holdUntil = millis() + (uint32_t)(beatSecs() * 2000);
          g_departAt = g_holdUntil;
          g_wanderT = 18.0f;
          return;
        }
      }
      g_lastWasMark = false;        // this trip is a wander/furniture visit, so next acts
      g_performBehind = false;
      if (millis() > g_nextVisitOK && (esp_random() % 100) < 50) {
        // A happy teen sometimes goes over to the radio instead. Tired wins over dancing, so
        // this sits after the energy check — nobody dances on an empty tank.
        // 70%, up from 45. The radio is the teen's most characterful behaviour and it was
        // landing only every 3-4 minutes, which is rare enough that you could watch for a
        // while and never see it. The energy gate below matters more than this number now
        // that stats decay twice as fast — he simply qualifies less often than he used to.
        bool radio = (S.phase == PH_TEEN && S.energy >= 45 && contentMood()
                      && hasAnim("teen_dance") && pakFind("items/radio")
                      && (esp_random() % 100) < 70);
        g_visit = radio ? 2 : (S.energy < 45) ? 1 : 0;
        if (g_visit == 2) { g_tx = RADIO_STAND_X; g_ty = RADIO_STAND_Y; clearOfBlocks(&g_tx, &g_ty); }
        else              { furnitureSpot(g_visit, &g_tx, &g_ty); }
        avoidChairFront(&g_tx, &g_ty);       // never settle in front of the cat's chair
        clampErrandToFloor(&g_tx, &g_ty);    // these are the shipped room's numbers too
      } else {
        // PICK X, THEN PICK Y FROM THE FLOOR AT THAT X — the builder's stepBun:
        //     gx = R(26,296); const sp = floorSpanAt(gx);
        //     gy = sp ? R(sp.top+3, sp.bot-1) : 216;
        // The rectangle from bounds() is INSCRIBED, so on a trapezoid it is the narrowest lane
        // in the room applied everywhere. In Jon's farmhouse that is y 216..237 while the floor
        // at mid-room runs 206..238 — measured: he was using 85% of the boards and never the
        // back strip. "bunbun doesnt seem to be wondering the whole room like the builder."
        Bounds b = bounds(); int tries = 0, tx, ty;
        do {
          tx = b.x0 + esp_random() % (b.x1 - b.x0);
          int top, bot;
          if (sceneFloorLane(tx, &top, &bot) && bot - top > 6)
            ty = (top + 3) + (int)(esp_random() % (uint32_t)((bot - 1) - (top + 3) + 1));
          else
            ty = b.y0 + esp_random() % (b.y1 - b.y0);      // no polygon: the rectangle, as before
          clearOfBlocks(&tx, &ty);
          // the assembler's keep-out semantics, kept: a keep-out never blocks the WALK -
          // only the choice to end a walk inside one (sceneCanLoiter)
        } while ((abs(tx - S.x) + abs(ty - S.y) < 60 || !sceneFloorHas(tx, ty) ||
                  !sceneCanLoiter(tx, ty)) && ++tries < 12);
        sceneFloorClamp(&tx, ty);          // 12 tries up: put it on the floor rather than accept it
        avoidChairFront(&tx, &ty);         // and not in front of the chair, whatever it picked
        g_tx = tx; g_ty = ty;
      }
      // A beat of stillness BEFORE setting off, whichever destination was chosen. The rhythm
      // is  move -> arrive -> pause -> idle -> pause -> move , so every journey is bracketed
      // by stillness rather than starting the instant the timer expires, which read as him
      // twitching into motion. The walk itself does not begin until the hold expires.
      g_holdUntil = millis() + (uint32_t)(beatSecs() * 2000);
      g_departAt = g_holdUntil;
      // decide up front how this trip goes, as the HTML's babyMove does
      g_tripLen = sqrtf((g_tx - g_fx) * (g_tx - g_fx) + (g_ty - g_fy) * (g_ty - g_fy));
      if (!babyCanStand())      { g_crawling = true;  g_crawlFrac = 0; }
      else if (babyWalksOnly()) { g_crawling = false; g_crawlFrac = 2; }
      else {
        float p = (float)(ageMin() - TODDLER_AT) / (WALKER_AT - TODDLER_AT);
        p = constrain(p, 0.0f, 1.0f);
        bool tryWalk = (esp_random() % 100) < (35 + 50 * p);
        g_crawling = !tryWalk;
        g_crawlFrac = tryWalk ? (0.30f + 0.45f * p + (esp_random() % 10) / 100.0f) : 0.0f;
      }
    } else g_wanderT = 1.2f + (esp_random() % 180) / 100.0f;
  }
}

static void simulate(float dt) {
  S.ageMs += (int64_t)(dt * 1000);
  // the crack sequence is 5 frames at 1fps; when it finishes, he's born
  if (S.stage == STAGE_HATCHING && g_animT >= 5.0f) {
    S.stage = STAGE_ALIVE; S.ageMs = 0;
    S.x = 160; S.y = FLOOR_Y; g_fx = 160; g_fy = FLOOR_Y;
    g_anim = findAnim("baby_sit"); g_animT = 0;
    sfxHatch();
    say("bunbun hatched!");
    saveState();
  }
  if (!alive()) return;
  // Not while asleep. The mess is DEFERRED rather than cancelled — g_poopDue is left standing,
  // so it arrives shortly after the lights come back on. Cancelling would mean a fed bunbun put
  // straight to bed never made a mess at all, which turns sleep into a way of dodging the
  // consequence; waiting just means it happens when he is up.
  // Nor mid-dance. Same DEFERRAL as sleep rather than a cancellation: the mess still arrives
  // once the party stops, so dance mode is not a way to opt out of cleaning up.
  if (!g_poopDue)
    g_poopDue = millis() + 480000 + esp_random() % 420000;   // 8-15 min: nature's own clock
  if (g_poopDue && millis() >= g_poopDue && S.lights && !discoDown()) {
    // THE ONLY PASSIVE ROOM TRIP (Jon: "the if he needs to go to the bathroom is the
    // only thing that is passive"): with a bathroom defined he takes himself there
    // instead of leaving a mess where he stands. Kitchen and work never move on their
    // own - buttons only.
    if (g_scRoleAvail[SCENE_ROLE_BATH]) {
      if (!g_action && !g_doorTrip && !g_sleepPending && g_visit < 0 && g_tx < 0 &&
          g_scCurRole == SCENE_ROLE_MAIN) {
        g_poopDue = 0;
        Serial.println("nature calls: off to the bathroom");
        g_pottySeq = 1;
        g_pottyLock = true;
        whyNote(WHY_ROLL, "toilet", "");
        { Because *b = whyLast(); if (b) b->roomTo = SCENE_ROLE_BATH; }
        whySpeakIfInteresting();
        g_whyDoor = WHY_ROLL;
        sceneDoorTo(SCENE_ROLE_BATH, "toilet");   // the toilet type first; sit is the fallback
        g_pottyLock = false;
        return;
      }
      // BUSY OR AWAY: the need KEEPS - the timer stays standing and he goes the
      // moment he is free. The first version fell through to g_poopDue = 0 and
      // CONSUMED the trip instead of postponing it ("he didnt go potty" - the
      // timer ripened mid-settle and died right there). No return: the rest of
      // think() must keep running or the very trip being waited on would freeze.
    } else {
      g_poopDue = 0;
      if (false && S.poopN < 4) {   // retired: nothing drops on the floor any more
        S.poopX[S.poopN] = S.x; S.poopY[S.poopN] = S.y; S.poopN++;
        S.clean = max(0.0f, S.clean - 8.0f);
        sfxPlop();                       // W-046, per Piper: "PLOP. hehehehe."
        say("bunbun made a mess");
      }
    }
  }
  Phase np = phaseOf();
  if (np != S.phase) {
    Phase was = (Phase)S.phase;
    S.phase = np;
    // Two milestones now, and neither should fire when a test build flips the phase backwards.
    // W-046: growing up finally makes a sound — Maya was scandalized that
    // "the BIGGEST thing that ever happens to him" was silent.
    if (was == PH_BABY && np == PH_TEEN)      { sfxGrow(); say("bunbun is growing up fast!"); }
    else if (was == PH_TEEN && np == PH_ADULT) { sfxGrow(); say("bunbun is all grown up!"); }
    else if (was == PH_BABY && np == PH_ADULT) { sfxGrow(); say("bunbun is all grown up!"); }
  }
  float m = dt / 60.0f;
  const PhaseRates &r = rates();
  // Nothing moves during the party — no drain, and no sleep recovery either. Gated on the ball
  // actually being DOWN rather than on the toggle, so the couple of seconds it spends dropping
  // still tick normally. Age still advances above; this stops the needs changing, not time.
  if (discoDown()) m = 0.0f;
  // W-036, Tempo's clause (council 8/7, adopted 10-0): the day he leaves,
  // drains FREEZE — the lesson is the empty room, not a deeper hole. This
  // also keeps the passive cues honest: an empty room must never say his
  // tummy is rumbling. Time and age still advance; the meters hold.
  if (bunAway()) m = 0.0f;
  if (!S.lights) {
    S.energy = min(100.0f, S.energy + r.sleepGain * m);
    S.food = max(0.0f, S.food - 0.6f * m);
    S.health = min(100.0f, S.health + 5.0f * m);
    // Overnight sleep holds through the energy-full wake and ends at the CLOCK, not the
    // meter: 6am on the room clock, with the same window test as bedtime so a clock set
    // backwards past midnight can't strand him asleep.
    if (g_nightSleep) {
      int cm = clockNowMin();
      if (cm >= g_bedEndMin && cm < 18 * 60) {
        g_nightSleep = false; S.lights = 1; saveSleepState(0);
        g_quietGreet = true;               // W-059: silent until someone says hello
        say("good morning! bunbun slept the whole night");
      }
    } else if (S.energy >= 100) {
      // Evening sleeps HOLD (Jon, launch night): after 6pm, a bunbun put
      // to bed stays down at least 30 minutes even if his energy tops out
      // — an evening nap that ends in ninety seconds reads as a broken
      // button, and bedtime is bedtime. Daytime naps still wake on full.
      int cmw = clockNowMin();
      bool evening = (cmw >= 18 * 60 || cmw < g_bedEndMin);
      if (!(evening && millis() - g_sleepAtMs < 1800000UL)) {
        S.lights = 1;
        say("bunbun woke up refreshed!");
      }
    }
  } else {
    // Needs drift far slower once the sun is down, as in the HTML. DECAY_NIGHT was declared
    // in game.h but never actually applied here, so overnight was as demanding as midday.
    m *= 1.0f - nightAmount() * (1.0f - DECAY_NIGHT);
    // W-029: school-hours mercy — see schoolHoursNow().
    if (schoolHoursNow()) m *= 0.5f;
    S.food = max(0.0f, S.food - r.food * m);
    S.fun = max(0.0f, S.fun - r.fun * m);
    // each mess on the floor drags cleanliness down faster, as in the HTML
    S.clean = max(0.0f, S.clean - (r.clean + S.poopN * 0.3f) * m);
    S.energy = max(0.0f, S.energy - r.energy * m);
    float discWas = S.disc;
    S.disc = max(0.0f, S.disc - r.meterDecay * m);
    // Call it out ONCE on the way down, so the sulk reads as something that happened rather
    // than a pose that quietly appeared. Only on the crossing — not every frame below it.
    if (discWas >= 25.0f && S.disc < 25.0f) {
      sfxNo();
      say(S.phase == PH_BABY ? "bunbun wants some attention..."
                             : "bunbun is behind on work...");
    }
    // The LOVE meter drains gently through the waking day (100 -> 0 in
    // roughly a day of neglect); cuddles and petting refill it. Nights
    // leave it alone — absence while asleep is not neglect.
    g_love = max(0.0f, g_love - 0.50f * m);   // Jon 8/12: 0.20 still read as "not going down at all" - now ~30/waking-hour, visible across a morning
    // W-022 addendum (Jon): passive-emotion cues — bunbun EXPRESSING
    // himself when a need crosses low, never notifying. One soft chirp
    // (FX-gated) + one gentle pulse (motor-gated), per need, at most once
    // per THREE minutes (Jon raised the frequency, launch night), daytime
    // only (this branch), never stacked: the first crossing this frame
    // wins and the others wait their cooldown.
    {
      static uint32_t needCueAt[4] = {0, 0, 0, 0};   // food, energy, clean, love
      static float prevN[4] = {100, 100, 100, 100};
      float nowN[4] = {S.food, S.energy, S.clean, g_love};
      bool spoke = false;
      for (int i = 0; i < 4; i++) {
        if (!spoke && prevN[i] >= 25.0f && nowN[i] < 25.0f &&
            millis() - needCueAt[i] > 180000UL) {
          needCueAt[i] = millis();
          spoke = true;
          // W-046 (sound session 8/10): each need speaks in its own voice
          // now — Piper and Maya's sound book replaced the one-chirp-fits-
          // all sfxCall. Same cooldowns, same daytime-only branch, same
          // FX gate; only the character changed.
          // W-059: pre-greeting mornings keep the voice and motor still —
          // the ticker line below still tells the truth silently.
          if (g_fxLevel > 0 && !g_quietGreet) {
            if (i == 0)      sfxTummy();
            else if (i == 1) sfxYawn();
            else if (i == 2) sfxStinky();
            else             sfxLonely();
          }
          if (!g_quietGreet) hapticPulse();
          say(i == 0 ? "bunbun's tummy is rumbling..."
              : i == 1 ? "bunbun is getting sleepy..."
              : i == 2 ? "bunbun could use a bath..."
                       : "bunbun misses you...");
        }
        prevN[i] = nowN[i];
      }
    }
  }
  float st = 0;
  if (S.food < 15) st += 1;
  if (S.clean < 15) st += 1;
  if (S.sick) st += 1.5f;
  if (S.disc < 15) st += 0.5f;
  S.health += (st > 0 ? -st * 0.6f : 1.2f) * m;
  S.health = constrain(S.health, HEALTH_FLOOR, 100.0f);

  // W-028: sickness becomes REAL. Until now S.sick was checked in ten places
  // — art for every age, the MEDS cure, the ticker line — and set in none:
  // the trigger never made it over from the HTML, so MEDS could only ever
  // say "bunbun feels fine". Doctrine (council 2026-08-06, kids ruling
  // double-weighted): rare and EARNED — only sustained visible neglect (food
  // or cleanliness pinned under 15) brings it on; a loved bunbun never gets
  // sick. Never as an ambush: not while asleep (this branch), and not in the
  // first hour after waking or booting (Lou's clause — g_wokeSickMs starts
  // at 0, so uptime itself must clear an hour first). Neglect heals off
  // twice as fast as it accrues, and the ghost stays unreachable: MEDS fixes
  // this on the first tap, always.
  {
    static float neglectMin = 0;
    static uint8_t prevLights = 1;
    if (!prevLights && S.lights) g_wokeSickMs = millis();   // just woke up
    if (prevLights && !S.lights) g_sleepAtMs = millis();    // just fell asleep
    prevLights = S.lights;
    if (S.lights && !S.sick) {
      if (S.food < 15 || S.clean < 15) neglectMin += m;
      else neglectMin = max(0.0f, neglectMin - 2.0f * m);
      if (neglectMin >= 120.0f && millis() - g_wokeSickMs > 3600000UL) {
        S.sick = 1;
        neglectMin = 0;
        sfxDroop();                  // W-046, per Maya: "a balloon going
                                     // sad. Not scary."
        say("bunbun caught a sniffle - MEDS will fix it");
      }
    } else if (S.sick) {
      neglectMin = 0;                       // the cure starts the clock over
    }
    // W-036 BRAVE mode: the run-away day. Only past the sniffle — already
    // sick AND still starving/filthy for two more game-hours. He packs a
    // tiny bag, the room goes quiet, and he ALWAYS comes home (4 hours),
    // more in love than he left. Cozy mode never enters this block.
    if (g_modeBrave && !bunAway() && S.sick && S.lights) {
      static float awayMin = 0;
      if (S.food < 10 && S.clean < 10) awayMin += m;
      else awayMin = max(0.0f, awayMin - 2.0f * m);
      if (awayMin >= 120.0f) {
        awayMin = 0;
        // 24 h failsafe only — treats (the away-mode MEDS button) are the
        // real way home, pulling the deadline in to 20-40 min.
        g_awayUntil = millis() + 24UL * 3600UL * 1000UL;
        g_treatsOutMs = 0;
        // Lights out AND the music off with him (Jon 8/14: "the lights need to
        // also be off to indicate an issue" / "music should turn off as well").
        // A bright room playing songs to nobody reads as a rendering glitch; a
        // dark quiet one reads as an empty house, which is the truth. Leaving
        // should feel lonely. The homecoming brings both back.
        S.lights = 0;
        awayHush();
        sfxLose();
        say("bunbun hopped away... put treats out to call him home");
      }
    }
  }
  // After 6pm a worn-out bunbun tucks HIMSELF in for the whole night (Jon,
  // launch night) — no double-tap required. Same night-sleep the button
  // gives: holds to 6am on the room clock, energy refills, world quiets.
  // Only from a settled state: not mid-action, not mid-party, not away.
  if (S.lights && !g_nightSleep && !g_action && !g_danceMode && !bunAway() &&
      S.energy < 20.0f) {
    int cmv = clockNowMin();
    if (cmv >= 18 * 60 || cmv < g_bedEndMin) {
      S.lights = 0;
      g_sleepAtMs = millis();
      g_nightSleep = true;
      saveSleepState(3);
      sfxTuckIn();                     // W-046, per Maya: "a music box - the
                                       // last note takes forever." The
                                       // night's LAST sound (Ivy's rule).
      say("bunbun tucked himself in for the night");
    }
  }
  // W-036: the away screen's help text — Lou's grandmother sentence,
  // shipped verbatim per the 8/7 minutes. Every few minutes the empty room
  // reassures whoever is looking at it; a kid who missed the leaving line
  // must never be left to wonder. First line ~4 min in (the leaving line
  // already spoke), then gently, never stacked.
  {
    static uint32_t louSaidMs = 0;
    if (!bunAway()) louSaidMs = millis();
    else if (millis() - louSaidMs > 240000UL) {
      louSaidMs = millis();
      say("he went exploring, he'll be back tomorrow");
    }
  }
  // The homecoming — checked every tick so it happens even mid-nap.
  if (g_awayUntil && millis() >= g_awayUntil) {
    bool treated = g_treatsOutMs != 0;
    g_awayUntil = 0;
    S.lights = 1;                    // he is home: the lights come back on with him
    g_nightSleep = false;
    if (treated) {
      // THE SCENE (Jon 8/14): he appears at the doorway and WALKS to the basket
      // rather than materialising mid-room. Stats are deliberately left where
      // the neglect put them — the reunion is the reward, not a free reset;
      // you still have to feed him back up. Only the sickness lifts, because a
      // bunny who found his way home is well enough to be hungry.
      g_homeStage = 1;
      // THE SICKNESS LIFTS HERE, NOT AT THE END OF THE WALK. The comment above has
      // said "only the sickness lifts" since 8/14, but the line only existed in the
      // untreated branch below and in stage 2 — and stage 2 is on the far side of a
      // walk that S.sick itself forbids. think() opens with
      //     if (S.sick) { setAnim(moodAnim()); g_tx = g_ty = -1; return; }
      // so the very next frame threw away the target he was just given. Running away
      // REQUIRES S.sick (see the BRAVE gate), and nothing can cure it while he is
      // gone — the MEDS slot is the TREAT button — so this was not an edge case:
      // every treated homecoming stalled him at the doorway playing `sick` on the
      // spot, with the basket he came home for sat uneaten in the middle of the room.
      // Jon, 8/16: "he never walked over and ate it but stayed on the far left of the
      // screen almost stuck." The 24h failsafe path was healthy, which is why this
      // survived: only the path a child actually uses was broken.
      S.sick = 0;
      g_treatsOutMs = g_treatsOutMs ? g_treatsOutMs : millis();   // basket stays out
      // Movement runs on g_fx/g_fy — setting only S.x/S.y left him standing still,
      // which is why the first version teleported instead of walking (8/14).
      g_fx = 4; g_fy = FLOOR_Y;      // the doorway, hard left, off the rug
      S.x = (int)g_fx; S.y = (int)g_fy;
      g_tx = TREAT_X - 22; g_ty = TREAT_Y;   // stop just short, beside the basket
      g_visit = -1;
      g_departAt = 0;
      g_settleUntil = 0;
      g_wanderT = 600.0f;            // nothing else may pick a target during the walk
      // ...and a deadline, because "nothing else may pick a target" was never true.
      // tidyStep() clears g_tx the moment it takes a job, the lights-out and sick
      // branches of think() clear it too, and any of them stranded g_homeStage at 1
      // for good: the arrival test below measures against g_tx == -1, which can never
      // be within 8px, so stage 2 never ran and the basket was orphaned on the floor.
      g_homeAt = millis() + 45000;
      setAnim(pa("walk_right"));
      sfxHomeAgain();                // W-046, per Piper: "BOING BOING!"
      fmtPetSay("%s is coming back...");
    } else {
      g_treatsOutMs = 0;
      S.sick = 0;
      S.health = max(S.health, 40.0f);
      g_love = min(100.0f, g_love + 30);
      loveSave();
      sfxHomeAgain();
      say("bunbun came home! he missed you so much");
    }
  }
  // The walk in, and the meal at the end of it.
  if (g_homeStage == 1) {
    // SOMETHING TOOK HIS TARGET. tidyStep(), lights-out and the sick branch all clear
    // g_tx, and each of them used to end the homecoming permanently. Hand it back: the
    // chore or the nap still gets its turn (they run inside think(), which stands down
    // for them), he simply remembers where he was going afterwards.
    if (g_tx < 0) { g_tx = TREAT_X - 22; g_ty = TREAT_Y; g_wanderT = 600.0f; }
    // And a floor under the whole thing. If he still has not reached the basket after
    // 45s — pinned by scenery, asleep, whatever comes next — he gets it anyway. A treat
    // a child left out is never allowed to sit there forever, which is the failure Jon
    // actually saw; better a slightly abrupt arrival than a room that stays broken.
    if (millis() >= g_homeAt) {
      g_fx = (float)(TREAT_X - 22); g_fy = (float)TREAT_Y;
      S.x = (int)g_fx; S.y = (int)g_fy;
      Serial.println("homecoming: walk timed out, closing it out at the basket");
    }
    float dx = g_tx - g_fx, dy = g_ty - g_fy;
    if (dx * dx + dy * dy < 64.0f) {          // he made it to the basket
      g_treatsOutMs = 0;                      // the basket is gone: he ate it
      g_homeStage = 2;
      g_homeAt = millis() + 6000;
      // The LOVE animation, not a play bounce (Jon 8/14) — the hearts are the
      // whole point of the reunion, and they are what he has been saving up.
      setAnim("love");
      sfxHomeAgain();
      fmtPetSay("%s found the treat - he missed you!");
    }
  } else if (g_homeStage == 2 && millis() >= g_homeAt) {
    g_homeStage = 0;
    awayUnhush();                             // the house has someone in it again
    S.sick = 0;
    S.health = max(S.health, 40.0f);
    S.food = min(100.0f, S.food + 25.0f);     // one basket's worth, not a full meal
    g_love = min(100.0f, g_love + 30);
    loveSave();
    g_wanderT = 6.0f;
    fmtPetSay("%s is home - he missed you so much");
  }
}

// ---------------- menu (HTML order, navigation and dimming) ----------------
// Launch-night restructure (Jon's ruling): cuddles are for ALL AGES, so
// CUDL keeps slot 5 forever and the day's occupation gets its OWN slot to
// the right. The disc meter still relabels with age (cuddle-need early,
// work later) — only the BUTTONS stopped sharing a seat, because a button
// that means affection on Monday and chores on Tuesday was never honest.
// icons/work may not exist in the pak yet (art session queued): the slot
// draws label-only until the icon lands.
// W-061: the quick-play seat is labelled PLAY now — both evergreen docs (the
// behavior registry and the HOW-TO) always called it that, and once a real
// PLAY (seat 1) is the games door now: it opens the roster whose first pick
// is the quick-play toy (Free Play). No standalone GAME seat.
static const char *MID[N_MENU]  = {"feed","play","bath","sweep","meds","care","light","work"};
static const char *MLBL[N_MENU] = {"FEED","PLAY","BATH","SWEEP","MEDS","CUDL","ZZZ","WORK"};
static const char *menuLabel(int i) {
  if (i == 7 && S.phase == PH_BABY) return "";   // the seat isn't there yet
  if (i == 7 && S.phase == PH_TEEN) return "SCHL";
  if (i == 4 && bunAway()) return "TREAT";   // meds slot doubles as the way home
  return MLBL[i];
}
// W-061 games — the GAME seat opens a ROSTER (Jon, 8/11: a game is "a
// selection under the play game, not a standalone button"), and each game is
// a pick inside it. Ivy's rule made law: every future game lives behind this
// one door, never a new menu seat.
static bool g_gameRoster = false;
static bool g_gamePanel = false;
static void tttReset();
static void drawGamePanel();
static void drawGameRoster();
// Menu redesign P2: the SLEEP surface. Three rest verbs used to live behind
// three different doors — the ZZZ seat's single tap, the NAP pin, and a
// double-tap nobody could discover — and now they are three labeled buttons
// on one screen. The seat is the door; every old code path survives behind it.
//
// P3 turns it from a full SCREEN into a SHEET: same three rows, same three code
// paths, but the room stays alive above it instead of being wiped away. Which
// is why it LEAVES the motor gate's offMain list and the panelOwnsScreen list —
// a sheet lives on the main-loop path by design (spec law 6), so hapticTick()
// keeps running under it and the scene keeps composing above it.
static bool g_sleepSheet = false;
// The CARE sheet: the eight care verbs as kid-sized cards, over a live room.
static bool g_careSheet  = false;
static void drawSleepSheet();
static void drawCareSheet();
static inline bool sheetOpen() { return g_careSheet || g_sleepSheet; }
// THE WISH SCREEN (P3, spec 1.8) — a full ink screen that IS the input lock.
static bool g_wishScreen = false;
static bool g_wishPrevPaused = false;
static void drawWishScreen();

static uint32_t g_menuUntil = 0;
static bool g_paused = false, g_musicOn = true;
// The empty-house hush (see the forward decl). g_musicOn alone only tells the player to
// stop starting NEW tracks — the one already in flight kept going (Jon 8/14: "music didnt
// stop"), so the mixer is muted here too. g_musicLevel is untouched, so the family's chosen
// volume is exactly what comes back when he does.
static void hostSetMusicVolume(int level);
static void awayHush() {
  g_musicOn = false;
  hostSetMusicVolume(0);
}
static void awayUnhush() { g_musicOn = (g_musicLevel > 0); hostSetMusicVolume(g_musicLevel); }
// The icon row it used to raise is gone (P3), but wakeMenu() is NOT (R4c): it is also the
// firmware panel's hold-open, so a network round-trip that outlives a menu timeout still
// gets to put its verdict on the screen the parent tapped CHECK on. Kept alive deliberately.
static void wakeMenu() { g_menuUntil = millis() + 4200; }
static bool itemDim(int i) {
  // Away: the meds slot is TREATS — lit until they're out, dim after.
  if (!strcmp(MID[i], "meds") && bunAway()) return g_treatsOutMs != 0;
  if (!strcmp(MID[i], "meds")) return !S.sick;
  if (!strcmp(MID[i], "sweep")) return !S.poopN;
  // Cuddles NEVER dim at any age — affection is not a chore with a "done"
  // state. WORK dims for babies (too little) and when the day's work is
  // already caught up, which is what a dimmed icon usefully says.
  if (!strcmp(MID[i], "work")) return (S.phase == PH_BABY) || S.disc >= 96;
  return false;
}

// ---- the day's occupation ----
// One staged sequence serving both phases, because the beats are identical: shoulder
// something, carry it left, travel back, do the task, hold up the result. A teen goes to
// SCHOOL and comes back with a gold star; an adult goes to WORK and comes back with a carrot.
// The `disc` meter is CUDL as a baby, SCHOOL as a teen, WORK as an adult — one number, three
// lives, exactly the way the HTML relabels it.
// g_workStage: 0 none, 1 shoulder, 2 walk, 3 travel, 4 task, 5 result (declared above think())
static float g_workT = 0;
static const int WORK_LEFT_X = 90, WORK_MID_X = 160;
// The classroom has its own furniture, measured off room-school.png: painted desks at x88-140
// and x198-255, with a clear gap between them. Reusing the farm's x90 walked him straight into
// the left desk, and x160 sat his sprite's own desk half inside it. He now starts clear of the
// chalkboard wall and settles in the gap, where his desk has room of its own.
// Browser-checked against room-school.png: x52 clipped the chalkboard's corner and FLOOR_Y+4
// stood him against the back wall. x72 / +14 puts him clear and properly on the floorboards,
// and the mid spot at 168 sits his desk in the gap between the two painted ones.
static const int SCHOOL_LEFT_X = 72, SCHOOL_MID_X = 168;
static const int SCHOOL_FLOOR_Y = FLOOR_Y + 14;
static const int SCHOOL_START_X = 238;   // right of the desk, so the walk to it heads west
static bool teenSchool() { return S.phase == PH_TEEN; }

static void startWork() {
  g_workStage = 1; g_workT = 0;
  g_tx = g_ty = -1;
  // School starts him RIGHT of the desk so the single walk is always westward, matching the
  // direction school_bag is drawn facing. The adult keeps its own staging.
  if (teenSchool()) {
    g_fx = SCHOOL_START_X; g_fy = SCHOOL_FLOOR_Y;
    S.x = (int)g_fx; S.y = (int)g_fy;
  }
  setAnim(teenSchool() ? "school_bag" : "work_basket");
  // school_bag is a WALK cycle, so playing it during the shoulder-the-satchel beat looked
  // like walking on the spot. Freeze it on one frame until he actually sets off. This must
  // come AFTER setAnim, which now clears any hold when the clip changes.
  if (teenSchool()) g_holdUntil = millis() + (uint32_t)(beatSecs() * 1500);
}
static void updateWork(float dt) {
  if (!g_workStage) return;
  g_workT += dt;
  // Each scene has its own furniture, so the staging positions differ.
  const bool sc = teenSchool();
  const int leftX = sc ? SCHOOL_LEFT_X : WORK_LEFT_X;
  const int midX  = sc ? SCHOOL_MID_X  : WORK_MID_X;
  const int wy    = sc ? SCHOOL_FLOOR_Y : FLOOR_Y - 4;
  // School runs slower than the farm AND on the music's clock. Each beat is measured from the
  // track by the mixer (see beatSecs() in sfx.h), so the stages land on bar lines instead of
  // arbitrary seconds — and it falls back to 75 BPM when the music is off, so the pacing is
  // the same either way. A bar is 4 beats; at 75 BPM that is 3.2s.
  const float BEAT = beatSecs(), BAR = barSecs();
  const float tShoulder = sc ? BEAT * 1.5f : 0.8f; // a beat and a half to shoulder the satchel
  const float tTask     = sc ? BAR : 1.1f;         // a bar of writing
  const float tResult   = sc ? BEAT * 3 : 1.6f;    // three beats holding up the gold star
  const float walkSpeed = sc ? 42.0f : 60.0f;      // ambles rather than marches
  switch (g_workStage) {
    case 1:                                                   // shoulder the basket
      // Held on a single frame for school (see startWork) — release it the moment the walk
      // begins so the cycle starts exactly when he starts moving, not before.
      if (g_workT > tShoulder) { g_workStage = 2; g_workT = 0; g_holdUntil = 0; }
      break;
    case 2: {                                                 // carry it west
      // SCHOOL walks straight to the desk and sits down — one journey, one direction. It used
      // to inherit the farm's shape (walk LEFT to fetch the tractor, then DRIVE back to the
      // middle), which is why he tracked left and then doubled back for no reason.
      int destX = sc ? midX : leftX;
      float dx = destX - g_fx, dy = wy - g_fy;
      float d = sqrtf(dx * dx + dy * dy);
      if (d < 4) { g_fx = destX; g_fy = wy; S.x = (int)g_fx; S.y = (int)g_fy;
                   g_workT = 0;
                   if (sc) { g_workStage = 4; setAnim("school_desk"); }   // straight to the desk
                   else    { g_workStage = 3; setAnim("work_drive"); }
                 }
      else { float sp = walkSpeed * dt;
             g_fx += dx / d * sp; g_fy += dy / d * sp;
             S.x = (int)g_fx; S.y = (int)g_fy; }
      break;
    }
    case 3: {                                                 // drive back to the middle
      float p = min(1.0f, g_workT / 2.4f);
      g_fx = leftX + (midX - leftX) * p;
      g_fy = wy;
      S.x = (int)g_fx; S.y = (int)g_fy;
      if (p >= 1) { g_workStage = 4; g_workT = 0;
                    setAnim(sc ? "school_desk" : "work_dig"); }
      break;
    }
    case 4:
      if (g_workT > tTask) {
        g_workStage = 5; g_workT = 0;
        setAnim(teenSchool() ? "school_star" : "work_carrot");
      }
      break;
    case 5:
      if (g_workT > tResult) {
        g_workStage = 0;
        S.disc = min(100.0f, S.disc + 28);
        S.energy = max(0.0f, S.energy - 10);
        say(teenSchool() ? "bunbun got a gold star" : "bunbun finished some work");
        setAnim(moodAnim());
        saveState();
      }
      break;
  }
}

// Did the last runMenu() actually DO something, or did it refuse? The CARE sheet needs the
// answer and nothing else does: a success closes the sheet and hands the eye back to the
// room (the charter's whole posture — the room is the reward), while a refusal keeps the
// sheet up with its line in the message strip, where the finger already is. Set in exactly
// the two places a care verb succeeds; every refusal path returns before them.
static bool g_menuActed = false;

static void runMenu(int i) {
  g_menuActed = false;
  // Refuse everything while paused, and SAY so. Paused froze the animation clock and think(),
  // but the menu still accepted taps — so an action would change the sprite and then sit there
  // motionless. That reads as the game having crashed, not as it being paused, and it cost a
  // lot of time chasing a frozen-animation bug that was this. The only visible cue was the
  // first pin button reading PLAY instead of PAUSE, which is far too subtle to notice.
  if (g_paused) { sfxNo(); say("paused - press PLAY to carry on"); return; }
  // One thing at a time. Starting an action mid-action cut the previous one off partway, so a
  // quick run along the menu played a second of each and finished none — and because the stat
  // changes apply immediately on the tap, you could stack up all the benefits while seeing
  // almost none of the animation. Let whatever is running finish first.
  //
  // The work/school sequence counts too: it drives position and animation itself for several
  // seconds and interrupting it strands him mid-errand.
  //
  // EXCEPT CUDDLES, which outrank everything (Jon's ruling, launch morning:
  // "if I ever press cuddle or love no matter what it should always work" —
  // this after five presses bounced off a love-hold's rolling action tail).
  // A cuddle mid-errand just ends the errand (the stat change already
  // landed on the tap); a cuddle mid-work calls him home. The busy gate
  // still protects errands from EACH OTHER — just never from affection.
  if (i == 5) {
    g_workStage = 0;                     // affection outranks the errand
  } else if (i != 1 && (g_action || g_workStage)) {
    if (i == 7 && g_workUntil) { /* WORK during a work session falls through: it recalls him */ }
    else { sfxNo();
      // name what he is doing instead of a bare "busy"
      const char *d = g_workUntil ? "bunbun is at work - press work to call him home"
                    : (g_anim ? emoteLineFor(g_anim->key) : nullptr);
      say(d ? d : "bunbun is busy");
      return; }
  }
  // i == 1 (PLAY) is exempt from the busy gate (Jon 8/11: "the play button
  // shouldn't have to wait for a passive to finish") - opening the games
  // roster is navigation, not an action that would fight a running clip.
  // Dance mode locks the action menu too. Everything in here drives position and animation for
  // several seconds, and danceStep() re-sets the clip every frame, so an action taken now would
  // apply its stat change and then be invisibly overwritten — the tap would look broken while
  // silently having worked, which is worse than refusing it.
  // Cuddles (menu 5) are exempt from the dance lockout — you can hug a dancing bunbun. The
  // action briefly takes the stage (see the gate order in think()) and the party resumes.
  // W-036: an away bunbun can't be cared for — the empty room is the point.
  // EXCEPT the meds slot, which becomes TREATS while he's away (the kids'
  // revision): putting treats out is what calls him home.
  if (bunAway()) {
    if (i == 4) {
      if (g_treatsOutMs) { sfxNo(); say("the treat is already out"); return; }
      g_treatsOutMs = millis();
      // Ten seconds (Jon 8/14). Ten MINUTES was long enough that nobody ever saw
      // the homecoming at all; thirty seconds was still long enough to look away.
      // The walk itself is the slow part — this is just the wait before it starts.
      uint32_t soon = millis() + 10UL * 1000UL;
      if (soon < g_awayUntil) g_awayUntil = soon;   // treats pull him home
      sfxOK();
      // No porch (Jon 8/14): there is no porch anywhere in this house, and the
      // basket is set down in the room the kid is looking at.
      fmtPetSay("you left a treat out - %s will be back soon...");
      g_menuActed = true;                 // a success: the CARE sheet steps aside for it
      return;
    }
    sfxNo(); say("bunbun is away - try putting TREATS out"); return;
  }
  if (discoDown() && i != 5) { sfxNo(); say("dance mode - tap DANCE to stop"); return; }
  // Asking for anything wakes him. While the lights are off think() forces the sleep animation
  // and returns, so an action would set its clip and then be overwritten on the very next frame
  // — the tap appeared to do nothing at all. Waking first makes the action visible AND matches
  // what you meant by tapping it. ZZZ (back at index 6 — Jon kept sleep in its old seat and
  // WORK took the end) is excluded: that IS the lights control, and waking on it would make
  // the button unable to ever turn them off.
  if (!S.lights && i != 6) {
    S.lights = 1;
    say("bunbun woke up");
  }
  bool b = (S.phase == PH_BABY);
  switch (i) {
    case 0: if (S.food >= 98) { sfxNo(); say("bunbun is full"); return; }
            // NOTHING IS EATEN IF THERE IS NOWHERE TO EAT (rehearsal S10): the meter
            // used to fill first, so a world with no meal fed him anyway - and the
            // blocked line that would have told the child was overwritten one line
            // later by "bunbun ate a meal".
            if (sceneActive() && !sceneCanDo("eat")) {
              sfxNo(); whyNote(WHY_BLOCKED, "eat", "");
              return;
            }
            // W-047: a meal when he's genuinely hungry is THRILLING - the
            // trill rides in right behind the menu's OK chirp.
            if (S.food < 30) sfxExcited();
            S.food = min(100.0f, S.food + 35); S.fun = min(100.0f, S.fun + 2);
            whyFor(WHY_BUTTON, "food");
            if (!sceneErrandTo("eat")) startAction(pa("eat"), 3.4f);
            say("bunbun ate a meal");
            // Meals are followed by a mess a while later (babies more often). "A while" was
            // 15-35 SECONDS, so every feed produced a mess almost immediately and the floor
            // was never clean — it's minutes now, and less of a certainty.
            if ((esp_random() % 100) < (b ? 55 : 35)) {
              // back to the shipped pace (the 20s test setting is retired) - and a
              // meal only ever HASTENS the standing baseline, never postpones it
              uint32_t mealDue = millis() + 240000 + esp_random() % 300000;   // 4-9 min
              if (!g_poopDue || mealDue < g_poopDue) g_poopDue = mealDue;
            }
            break;
    case 1:                                                   // PLAY opens the games roster
      // Jon 8/11: no games until the pet is reasonably cared for - play is
      // earned, not the escape from a hungry/dirty/sick bunny. Fun is the
      // one meter NOT gated (it is what play refills).
      if (S.sick || S.food < 35 || S.clean < 35 || S.energy < 30 || g_love < 30) {
        sfxNo(); say("take care of bunbun first, then play"); return;
      }
      g_gameRoster = true; drawGameRoster(); return;
    case 2: if (sceneActive() && !sceneCanDo("bath") && !sceneCanDo("wash")) {
              sfxNo(); whyNote(WHY_BLOCKED, "bath", "");
              return;
            }
            S.clean = min(100.0f, S.clean + 45); S.fun = min(100.0f, S.fun + 6);
            whyFor(WHY_BUTTON, "clean");
            if (!sceneErrandTo("bath")) startAction(pa("bath"), 4.2f);
            say("bunbun took a bath"); break;
    case 3: if (!S.poopN) { sfxNo(); say("nothing to sweep"); return; }
            S.poopN = 0; S.clean = min(100.0f, S.clean + 12);
            say(S.phase == PH_TEEN  ? "you picked up the laundry"
              : S.phase == PH_ADULT ? "you washed the dishes"
                                    : "you swept up the mess");
            break;
    case 4: if (!S.sick) { sfxNo(); say("bunbun feels fine"); return; }
            S.sick = 0; S.health = min(100.0f, S.health + 35);
            startAction(pa("love"), 2.0f); say("bunbun feels better"); break;
    case 5: {
              // CUDL, every age, no refusal ever — "bunbun is all snuggled"
              // turned affection away once, and the one thing a cuddle
              // button must never do is say no. Babies bank it on the disc
              // meter (their disc IS cuddle-need); grown bunbuns keep their
              // work meter untouched and just feel loved. Per-age cuddle
              // animations are queued for the art session — pa() serves the
              // best clip each phase has until then.
              S.fun = min(100.0f, S.fun + 6);
              g_love = min(100.0f, g_love + 12);   // the LOVE meter feeds on this
              loveSave();
              if (b) {
                S.disc = min(100.0f, S.disc + 22);
                startAction("baby_cuddle", 2.4f);
              } else {
                // Teens and adults get their OWN cuddle clip now. Art-gated
                // on the pak (the anim table can name a clip the installed
                // pak has never heard of), falling back to the love clip
                // exactly as before — so an old pak still cuddles.
                const char *ck = pa("cuddle");
                const AnimDef *cd = findAnim(ck);
                char f0[48];
                snprintf(f0, sizeof(f0), "%s/0", cd->folder);
                startAction((hasAnim(ck) && pakFind(f0)) ? ck : pa("love"), 2.4f);
              }
              say("cuddles!");
              sfxPurr();                        // W-047: the love is audible now
              hapticPurrStart(2400);            // W-022: the headline behaviour
              break;
            }
    case 7: {
              // The day's occupation, in its own seat now (launch night —
              // it used to squat in the cuddle slot for grown bunbuns;
              // Jon then kept ZZZ in its familiar place, so WORK holds the
              // end of the row).
              // PRESS WORK AGAIN TO CALL HIM HOME ("hes now stuck at work. it just
              // says he is busy"): the session is Jon's own dont-stop-early rule doing
              // its job for the full work length - what was missing was the recall.
              if (g_workUntil || g_workReps > 0) {
                g_workUntil = 0; g_workReps = 0; g_workNextAt = 0;
                say("work is done for today - heading home");
                if (g_scCurRole != SCENE_ROLE_MAIN && !g_doorTrip) sceneDoorTo(SCENE_ROLE_MAIN, "");
                break;
              }
              if (b) { sfxNo(); say("too little for chores - cuddles instead!"); return; }
              bool sc = teenSchool();
              if (S.disc >= 96)  { sfxNo();
                                   say(sc ? "homework is all done" : "work is all caught up");
                                   return; }
              if (S.energy < 15) { sfxNo();
                                   say(sc ? "bunbun is too tired for school"
                                          : "bunbun is too tired to work");
                                   return; }
              // A WORLD WITH A WORK ROOM NEVER FALLS INTO THE FARM SCRIPT: pressing
              // WORK while already standing in a work room that has no work animation
              // used to fail the errand and start the scripted farm sequence instead -
              // minutes of "bunbun is busy" with no way out ("hes now stuck at work").
              if (sceneWorkAvail()) {
                whyFor(WHY_BUTTON, "");
                if (!sceneErrandTo("work"))
                  say("nothing to do at work yet - add a 'his job' animation");
                break;
              }
              startWork();
              break;
            }
    case 6:
      // ZZZ opens the SLEEP surface now (menu redesign P2). The single-tap
      // toggle and the hidden evening double-tap retired together — their
      // exact code paths live on behind the panel's three labeled buttons
      // (LIGHTS OUT / WAKE UP, NAP THE SCREEN, TUCK IN FOR THE NIGHT), so a
      // kid can finally SEE the night option nobody ever discovered by
      // accident. Opening a door is navigation, same as PLAY: no chirp, no
      // stat change, and the earlier gates (paused/busy/away/dance) all
      // still stand between the seat and the panel.
      g_sleepSheet = true; drawSleepSheet(); return;
  }
  sfxOK();
  saveState();
  g_menuActed = true;
}

// ---------------- music ----------------
// Confirmed by probe: the amplifier is on GPIO26 (DAC2), and it responds to both PWM and a
// DAC sine â€” so a real sample stream works, and there's no clash with touch (which would
// have been a problem had it turned out to be GPIO25, the touch clock).
//
// The track is raw 8-bit unsigned PCM at 11025Hz rather than MP3. No decoder means no
// libhelix, no ~30KB of decode buffers, and no CPU contention with the scene compositor;
// the trade is 102 seconds before it loops.
//
// I2S built-in DAC mode is used instead of a timer ISR so DMA owns the sample clock. The
// render loop can stall for a frame without the audio stuttering.
// ---------------- audio: hosted by the AirPlay 2 app ----------------
// Everything from here to the wall clock used to be bunbun's own audio stack: SD_MMC, the
// ESP32-audioI2S decoder, direct ES8311 setup and an AirPlay 1 receiver. In this build the host
// application owns all of it — I2S, the codec, volume and the AirPlay session — so what remains
// is a compatibility layer that keeps the old names alive for the ~3,000 lines below.
//
// SD-CARD MP3 PLAYBACK IS DELIBERATELY GONE. It needs ESP32-audioI2S, which would fight the
// host's audio pipeline for ownership of the I2S peripheral — and this project's own history
// says loudly that two owners of one audio resource is how you get silence nobody can explain.
// AirPlay is the audio source now.
#include "esp_err.h"

// ================= WHERE IT DIED =================
// Two panics tonight and neither reproducible on demand; seven in the log before that, none ever
// root-caused. Guessing has now cost more than instrumenting will.
//
// fw_update.c already has an RTC-noinit crumb, but it has NO VALIDITY MARKER — so when it reads
// back 24, or 72, values nothing in the firmware ever writes, there is no way to tell a real
// stamp from uninitialised RTC memory. That is the whole reason it could not be trusted tonight.
// This one carries a magic word and a sequence counter, so "the marker is genuine" is a question
// with an answer. RTC noinit is internal SoC memory that survives a warm reset; it is NOT the
// external RTC part, which this board no longer has.
//
// Stamped at the top of each stage of a frame. The LAST value standing is where it died.
RTC_NOINIT_ATTR struct { uint32_t magic, where, seq; } g_bc;
static uint32_t g_bcPrevWhere = 0, g_bcPrevSeq = 0;
static bool     g_bcPrevValid = false;
#define BC_MAGIC 0xB0FFB0FFu
#define BC(n) do { g_bc.magic = BC_MAGIC; g_bc.where = (n); g_bc.seq++; } while (0)
enum {
  BC_IDLE = 1, BC_SIMULATE, BC_THINK, BC_TIDY, BC_KEEPLEGAL, BC_CLOUDS, BC_RAIN, BC_BIRD,
  BC_CAT, BC_LOOSE, BC_EVENTS, BC_COMPOSE, BC_LIGHTMAP, BC_SCENEPROPS, BC_CATCLOCK,
  BC_DRAWCAT, BC_SCENELOAD, BC_PAKREAD, BC_TOUCH, BC_UI
};
static void bcMark(int where) { BC(where); }

// Taken once at boot, before anything stamps over it.
static void bcSnapshot() {
  g_bcPrevValid = (g_bc.magic == BC_MAGIC);
  g_bcPrevWhere = g_bcPrevValid ? g_bc.where : 0;
  g_bcPrevSeq   = g_bcPrevValid ? g_bc.seq   : 0;
  g_bc.magic = BC_MAGIC; g_bc.where = 0;      // seq deliberately keeps counting across boots
}
extern "C" void bunbun_bc_snapshot(int *valid, int *where, unsigned *seq) {
  *valid = g_bcPrevValid ? 1 : 0;
  *where = (int)g_bcPrevWhere;
  *seq = (unsigned)g_bcPrevSeq;
}
extern "C" {
#include "dac.h"
bool audio_receiver_is_playing(void);   // host AirPlay stream state (W-072 wake)
uint32_t audio_output_last_stream_ms(void);  // last real stream sample -> the uploader's own gate
esp_err_t wish_recorder_start(void (*done_cb)(bool ok));
bool wish_recorder_active(void);
bool wish_recorder_saving(void);
int wish_recorder_seconds(void);
void wish_recorder_stop(void);
// mirror of wish_recorder.h's wish_fail_t (kept in C to avoid dragging the
// header's transitive includes into the Arduino build)
typedef enum { WISH_FAIL_NONE = 0, WISH_FAIL_MIC, WISH_FAIL_MEMORY,
               WISH_FAIL_TOO_SHORT, WISH_FAIL_STORAGE } wish_fail_t;
wish_fail_t wish_recorder_fail_reason(void);
// firmware self-update from the wish repo (main/network/fw_update.c)
typedef enum { FW_IDLE = 0, FW_CHECKING, FW_DOWNLOADING, FW_UP_TO_DATE,
               FW_FAILED, FW_SUCCESS } fw_update_state_t;
esp_err_t fw_update_start(void);
// W-054: hop to the next remembered wifi network (main/network/wifi.c)
extern "C" int wifi_switch_next_known(char *out_ssid, size_t out_len);
fw_update_state_t fw_update_state(void);
int fw_update_pct(void);
const char *fw_update_reason(void);
void fw_update_ack(void);
// W-044: art-pack OTA. The game reports a pak that would not mount so the
// next update check re-fetches it (an interrupted art write lands exactly
// here by design — the magic sector is written last).
void fw_assets_report_missing(void);
bool fw_assets_repair_wanted(void);
bool wish_uploader_busy(void);
int wish_uploader_pct(void);
int wish_uploader_take_event(void);
bool wish_uploader_online(void);
int wish_uploader_pending(void);
// W-020: arm the USB card-reader boot mode (main/usb_msc_mode.c; stubbed on
// targets without OTG). C linkage — an in-function extern mangled and broke
// the link once already.
void usb_msc_mode_set_flag(bool on);
bool usb_msc_mode_available(void);

// 8/13 panic investigation: the RTC-noinit breadcrumb fw_update.c owns. It
// survives a panic reboot, so a field unit's crash report can say WHERE the
// firmware was without a serial cable. bunbun stamps the game surfaces into
// it (see the arcade block in loop()); fw_update.c reads it back on boot.
extern uint32_t g_fw_crumb;
}

// The wish recorder's done callback fires on the recorder task (core 1);
// crossing cores to poke the ticker directly would race, so it only sets this
// flag and the bunbun loop does the talking.
static volatile int g_wishDone = 0; // 1 saved, -1 failed/discarded
static void wishDoneCb(bool ok) { g_wishDone = ok ? 1 : -1; }

#include "whatsnew.h"
#include "esp_app_desc.h"
static void say(const char *t);
static Preferences &whatsNewPrefs();
// Once, ~8s after the first boot of a NEW firmware version: bunbun announces
// its own overnight change on the ticker. The delay clears boot chatter and
// the one NVS write (remembering the version) lands before render-critical
// play begins, not during it.
static void whatsNewTick() {
  static bool done = false;
  if (done || millis() < 8000) return;
  done = true;
  // Restart confession (field debugging, 2026-08-07): when the last reset
  // was NOT a normal power-on/soft-restart, say why. This is how a crash in
  // the car gets diagnosed without a serial cable — panic vs watchdog vs
  // power dip point at completely different culprits.
  {
    esp_reset_reason_t rr = esp_reset_reason();
    const char *why = NULL;
    if (rr == ESP_RST_PANIC)        why = "bunbun restarted: crash (panic)";
    else if (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT ||
             rr == ESP_RST_WDT)     why = "bunbun restarted: watchdog";
    else if (rr == ESP_RST_BROWNOUT) why = "bunbun restarted: power dip";
    if (why) say(why);
  }
  if (!BUNBUN_WHATSNEW[0]) return;
  const esp_app_desc_t *ad = esp_app_get_description();
  Preferences &p = whatsNewPrefs();
  p.begin("bunbun", false);
  String seen = p.getString("seenver", "");
  if (seen != ad->version) {
    p.putString("seenver", ad->version);
    say(BUNBUN_WHATSNEW);
  }
  p.end();
}

static const bool AUDIO_ENABLED = true;

// Kept so the SND panel and the track screen still compile. There is no card to scan.
static const int MAX_TRACKS = 12;
static char g_tracks[MAX_TRACKS][96];
static int  g_trackN = 0, g_trackSel = 0;
static volatile int g_trackReq = -1;
static bool g_sdOk = false;
static String g_musicPath;
static uint32_t g_musicLen = 0;
static bool g_musicReady = false;
static bool g_rotate = false;

// The amp pin is the host's now (CONFIG_MUTE_GPIO=1, active-high mute), but bunbun's trace and
// its sound panel still read this mirror.
static bool g_ampOn = true;
static void ampEnable(bool on) { g_ampOn = on; }

// The codec is configured by the host at boot for whatever AirPlay is streaming, so re-clocking
// from here would fight it. Recorded only.
static int  g_codecRate = 44100;
static void codecSetRate(int hz) { g_codecRate = hz; }
static void *g_codec = nullptr;

static const float MUSIC_MAX_GAIN = 1.0f;
static const int   MUSIC_OFFSET   = 0;

// Real implementations follow sdplayer.h below — the stubs died when SD playback came back.
static void scanTracks();
static void playTrack(int i);
static void musicBegin(uint32_t);

#include "sdplayer.h"
// btaudio.h (the analog-capture attempt) is retired but kept on disk as the
// post-mortem; btbridge.h is the digital I2S listener that replaced it,
// exposing the SAME three names (g_btActive/btRingAvail/btRingPull) so the
// silence-branch routing below did not have to change at all.
#include "btbridge.h"

static void scanTracks() { g_sdRescan = true; }        // performed by the sd task, never here
static void playTrack(int i) {
  if (i >= 0 && i < g_trackN) g_trackReq = i;          // the sd task picks it up
}
// W-049 (the trapped fleet, 8/10): btBridgeBegin unconditionally claimed
// ~13KB of INTERNAL RAM (I2S DMA + task stack) on every unit, wired to a
// D1 or not — and that was exactly the margin the TLS self-updater's
// memory gate needed. Boot baseline fell to ~16KB internal and every
// 0.1.105+ unit failed "low mem to install" on repo updates; only the
// LAN direct-push (no TLS) still worked. The listener is opt-in now,
// same honest-hardware pattern as the vibration motor: NVS-declared
// ("btbr"), bench keys 'k' declare / 'K' undeclare, dormant otherwise.
static void musicBegin(uint32_t) {
  sdPlayerBegin();
  if (prefs.getUChar("btbr", 0)) btBridgeBegin();
  else Serial.println("btbridge: not declared wired - dormant, RAM saved ('k' declares)");
}

// AirPlay state, read from the host instead of run by us.
static bool airPlayOn() { return true; }
static const char *airStatus() { return "airplay 2"; }
static void airReport() {}
// The host advertises and tears down the AirPlay service itself, for the life of the device, so
// these are no-ops rather than a second thing starting and stopping the same session.
static bool airPlayBegin() { return true; }
static void airPlayEnd()   {}
static void netGoingOffline() {}

// bunbun's SND panel used to set the MP3 decoder's volume. With the decoder gone it now drives
// the HOST's codec, which turns that slider into an on-device volume control for AirPlay — worth
// having, since otherwise the only control is the phone's.
//
// W-031: the linear -30..0 dB spread put level 1 at -22.5 dB — "background",
// not "bedtime" (field report: quietest setting too loud in a sleeping room).
// Perceptual steps instead: 1 is genuinely hushed, each step up roughly
// doubles apparent loudness, 4 is full. dac_set_volume() takes the same dB
// scale AirPlay uses, so the on-device slider still matches the phone's.
// W-048 (Jon, 8/10: "does the music volume affect the effects and rain?
// they should be independent"): it DID — this function used to move the
// DAC's MASTER volume, the analog knob after the mix, so music level 1
// (-40dB) crushed every chirp on its way out. That is precisely why a
// fresh 0.1.107 sounded mute: the sound book was playing into a strangled
// master. The music level is now a DIGITAL gain applied to the music
// samples at their source (bunbun_local_pcm, below) — the DAC master
// stays wherever the system (boot default / AirPlay) puts it, and the
// effects finally ride at their own true loudness, as the sfx.h header
// promised all along.
static volatile float g_musicGain = 0.063f;   // level 2 default
static void hostSetMusicVolume(int level) {
  // Same perceived steps as the old dB table, now digital:
  // 1 = -40dB (barely there), 2 = -24dB, 3 = -12dB, 4 = 0dB.
  // Level 0's silence still comes from g_musicOn stopping the player.
  static const float LVL_GAIN[5] = {0.0f, 0.01f, 0.063f, 0.25f, 1.0f};
  if (level < 0) level = 0;
  if (level > 4) level = 4;
  g_musicGain = LVL_GAIN[level];
}

// ---------------- wall clock ----------------
// The time is advanced from millis() off a base set at boot. Only presentation reads it (clock
// hands, day/night, lamp); nothing that measures a DURATION does, so a wrong or re-entered time
// can never corrupt his age or stats.
//
// The base comes from the best source available, in order:
//   1. a DS3231 on the I2C bus  — survives everything, never asks
//   2. the RTC domain            — survives a nap and a reset, not a power cut
//   3. NVS                       — survives a power cut, but is stale by the time it was off
//   4. asking                    — last resort
static int  g_clockBaseMin = 9 * 60;          // wall-clock minutes at boot
static uint32_t g_clockBaseMs = 0;
static bool g_clockSet = false;
// W-015: the set-time screen is a FALLBACK now, not the boot default. On a
// cold boot the pet lives on a provisional (NVS-seeded) clock while the
// internet gets 30s to answer; only silence opens the prompt. g_clockPrompt
// says the prompt is actually on screen — !g_clockSet alone no longer means
// that.
static bool g_clockPrompt = false;
static uint32_t g_clockWaitUntil = 0;
static uint32_t g_clockPromptAt = 0;   // when the prompt opened (W-019 guard)
// Timezone, LEARNED not picked (council A9: never default UTC): any manual
// set while internet time is known stores (local - UTC), and from then on
// every boot syncs silently. NVS key "tzOffMin"; sentinel = never learned.
static const int TZ_UNSET = -100000;
// W-019: on chipless units every non-human clock source is PROVISIONAL —
// good enough to live by, never good enough to stop the internet from
// correcting it. While provisional and online, sync retries every 10
// minutes instead of settling for the daily cadence.
static bool g_clockProvisional = false;

// real SNTP lives in the host (main/network/wall_clock.c) — see ntpTask
extern "C" esp_err_t settings_get_device_name(char *name, size_t len);
extern "C" void wall_clock_start(void);
extern "C" void wall_clock_stop(void);
extern "C" int wall_clock_utc(int *sec);
extern "C" int wall_clock_wait_fresh(int timeout_ms);

static volatile uint32_t g_utcFreshMs = 0;   // when a REAL sync landed this boot

static void tzStore(int localMin) {
  // Learning is shared by every manual-set path (touch AND bench serial —
  // the canary was corrected three times tonight through a path that never
  // learned, which kept a poisoned offset alive). Rounded to 15 min; real
  // timezones live in [-12h, +14h].
  // Only learn against a clock that got a GENUINE server answer this boot:
  // the epoch survives soft reboots, and computing an offset against a
  // stale carry would poison the timezone through the honest front door.
  if (!g_utcFreshMs) {
    Serial.println("clock: not learning timezone - no fresh sync this boot");
    return;
  }
  int s = 0, um = wall_clock_utc(&s);
  if (um < 0) {
    return;
  }
  int off = ((localMin - um) % 1440 + 1440) % 1440;
  off = ((off + 7) / 15) * 15 % 1440;
  if (off > 840) off -= 1440;                  // canonical -11:45..+14:00
  if (off < -720 || off > 840) {
    return;                                    // not a timezone on this Earth
  }
  prefs.begin("bunbun", false);
  prefs.putInt("tzOffMin", off);
  prefs.end();
  Serial.printf("clock: timezone learned (utc%+d min)\n", off);
}

static int clockNowMin() {
  return (g_clockBaseMin + (int)((millis() - g_clockBaseMs) / 60000)) % 1440;
}

// W-030: one tiny voice for navigation taps (panel buttons, never the room
// itself — touching bunbun's world should feel like touching, not typing).
// Rides the FX level: fx 0 keeps a silent bunbun silent.
static void uiTick() { if (g_fxLevel > 0) sfxTick(); }

// W-029 "bunbun packs a lunch": weekday school hours (8am-3pm) run the
// drains at half rate, so a bunbun left at 8am is peckish at pickup, not
// desperate by ten with hours of unseen sad-screen behind it. Weekday comes
// from the system epoch when it is plausible (SNTP has set it; it survives
// soft reboots and the exact day is not safety-critical here). If the epoch
// was never set — hand-set clock, offline forever — every day is a school
// day: mercy over rigor.
static bool schoolHoursNow() {
  int cm = clockNowMin();
  if (cm < 8 * 60 || cm >= 15 * 60) return false;
  time_t t = time(NULL);
  if (t > 1672531200) {                       // epoch is real (post-2023)
    // tz cached: this runs every simulate() frame and an NVS read per frame
    // would lock the flash cache 10x/second for a value taught twice a year.
    static int offCache = -100000;
    static uint32_t offReadMs = 0;
    if (offCache == -100000 || millis() - offReadMs > 600000UL) {
      prefs.begin("bunbun", true);
      offCache = prefs.getInt("tzOffMin", TZ_UNSET);
      prefs.end();
      offReadMs = millis();
    }
    if (offCache != TZ_UNSET) {
      long days = (long)((t + offCache * 60) / 86400);
      int wd = (int)((days + 4) % 7);         // 0=Sun .. 6=Sat
      if (wd == 0 || wd == 6) return false;   // weekends belong to the kid
    }
  }
  return true;
}

// ---- DS3231 (ZS-042 module) ----
// Optional: probed at boot and simply absent if not fitted, so the same firmware runs with or
// without the module. Only hours and minutes are used — the date is not part of the game.
static const uint8_t DS3231_ADDR = 0x68;
static bool g_rtcChip = false;
static inline uint8_t bcd2dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }
static inline uint8_t dec2bcd(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

// Reads minutes-past-midnight. Returns false if the chip is missing OR if its oscillator has
// stopped (OSF, status bit 7) — a fresh module or a dead backup cell reports a plausible-looking
// but meaningless time, and silently trusting that is worse than asking.
static bool ds3231Read(int *minOfDay, int *secOut = nullptr) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x0F);                                   // status register
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)DS3231_ADDR, 1) != 1) return false;
  if (Wire.read() & 0x80) return false;               // oscillator stopped: time is not valid
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)DS3231_ADDR, 3) != 3) return false;
  uint8_t s = Wire.read(), m = Wire.read(), h = Wire.read();
  if (secOut) *secOut = bcd2dec(s & 0x7F);
  int hh = (h & 0x40) ? (bcd2dec(h & 0x1F) % 12) + ((h & 0x20) ? 12 : 0)   // 12h mode
                      : bcd2dec(h & 0x3F);                                 // 24h mode
  *minOfDay = (hh * 60 + bcd2dec(m & 0x7F)) % 1440;
  return true;
}

// `sec` matters more than it looks: the display only shows HH:MM, so what you actually notice
// is WHEN the minute rolls over. Setting seconds to 0 while the real clock is at :40 leaves it
// looking a minute out for most of every minute, which reads as a wrong clock rather than a
// clock set at the wrong moment.
static void ds3231Write(int minOfDay, int sec = 0) {
  if (!g_rtcChip) return;
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  Wire.write(dec2bcd((uint8_t)(sec % 60)));
  Wire.write(dec2bcd(minOfDay % 60));
  Wire.write(dec2bcd(minOfDay / 60));                 // bit6 clear = 24-hour mode
  Wire.endTransmission();
  Wire.beginTransmission(DS3231_ADDR);                // clear OSF: the time is meaningful now
  Wire.write(0x0F);
  Wire.write(0x00);
  Wire.endTransmission();
}

// The CLOCK row's commit, callable over HTTP (/api/debug/clock?min=N): base set,
// chip written, minute persisted, timezone learned - one call and internet syncs
// keep it right forever after ("its online but the time isnt updated": tzOffMin was
// never taught on this unit, so NTP answers were ignored by design).
extern "C" void bunbun_set_clock(int localMin) {
  localMin = ((localMin % 1440) + 1440) % 1440;
  g_clockBaseMin = localMin;
  g_clockBaseMs = millis();
  g_clockSet = true;
  g_clockProvisional = false;
  ds3231Write(g_clockBaseMin);
  prefs.begin("bunbun", false);
  prefs.putInt("clkMin", g_clockBaseMin);
  prefs.end();
  tzStore(g_clockBaseMin);
}

// ---- NTP, when there is a network ----
// Belt and braces for the DS3231: the chip holds time to ~5s/month but only from wherever it was
// SET, and a hand-set clock starts up to a minute out (measured: 50s). A network sync fixes the
// starting point rather than the drift, which is the error that actually shows.
//
// Runs on its OWN TASK because every part of it blocks: the geolocation lookup is a few seconds
// of HTTP, and waiting for SNTP is several more. Doing that on the game loop would freeze the
// pet — the same lesson the SD probe taught. The task does the slow work, publishes a result,
// and exits; the loop picks it up and writes the chip.
static volatile bool g_ntpDone = false;
static volatile int  g_ntpMin = -1, g_ntpSec = 0;

// W-015: real internet time. The comment that used to live here believed the
// host ran an SNTP client in ntp_clock.c — it never did (that file is
// AirPlay-session timing against the SENDER). main/network/wall_clock.c is
// the actual SNTP client now; this task starts it and waits for the first
// sync, reporting UTC through the same g_ntp* channel the loop always read.
static void ntpTask(void *) {
  wall_clock_start();
  // Up to ~25s for a FRESH server answer; with the 3s post-online delay this
  // lands inside Jon's 30s ask-before-prompting budget. wait_fresh, not
  // "time() looks plausible": the epoch survives soft reboots, and trusting
  // the carried value re-blessed an 80-minute drift as a sync (2026-08-05).
  if (wall_clock_wait_fresh(25000) == 0) {
    int s = 0, m = wall_clock_utc(&s);
    g_ntpMin = m;
    g_ntpSec = s;
    g_utcFreshMs = millis();   // tz learning may trust the epoch this boot
  }
  // One answer a day is all the clock needs; a resident SNTP service was
  // ~300 bytes of permanent internal RAM on a 40KB floor (the regression
  // gate failed 0.1.32 by 57 bytes over exactly this). time() stays valid
  // after deinit, so timezone learning still works between syncs.
  wall_clock_stop();
  g_ntpDone = true;
  vTaskDelete(nullptr);
}

static void ntpStart() {
  g_ntpDone = false; g_ntpMin = -1;
  xTaskCreatePinnedToCore(ntpTask, "bunbun-ntp", 4096, nullptr, 1, nullptr, 1);
}

// Adopt the chip's time as the base. Also called periodically so millis() drift never shows.
static bool clockSyncFromChip() {
  int m, s = 0;
  if (!g_rtcChip || !ds3231Read(&m, &s)) return false;
  g_clockBaseMin = m;
  // Back-date the base by the seconds already elapsed in this minute, so the NEXT rollover
  // lands where the chip says it should. Re-basing to `millis()` and dropping the seconds — as
  // this used to — restarted the minute from zero on every sync, shifting the rollover by up to
  // 59 seconds each time. That looks exactly like a clock losing time, while the chip itself is
  // accurate to about 5 seconds a MONTH. The seconds arrive in the same I2C read either way, so
  // this costs nothing.
  g_clockBaseMs = millis() - (uint32_t)s * 1000;
  g_clockSet = true;
  return true;
}
// daylight(), matching the HTML: 1 = full day, 0 = deep night, soft ramps at dawn and dusk
static float daylight() {
  float h = clockNowMin() / 60.0f;
  if (h >= 8 && h < 17) return 1.0f;
  if (h >= 6 && h < 8)  return (h - 6) / 2.0f;
  if (h >= 17 && h < 20) return 1.0f - (h - 17) / 3.0f;
  return 0.0f;
}
static float nightAmount() { return 1.0f - daylight(); }
static float lampLevel() {
  float h = clockNowMin() / 60.0f;
  if (h >= 18) return min(1.0f, (h - 18) / 0.25f);
  if (h < 6)   return 1.0f;
  if (h < 6.25f) return max(0.0f, 1.0f - (h - 6) / 0.25f);
  return 0.0f;
}

// ---------------- night dimming ----------------
// The room has to darken after dark, otherwise the lamp has nothing to stand out against â€”
// which is exactly what you get with a lit beam over a fully-lit room.
// Done on the 256-entry PALETTE rather than per pixel: 43,200 pixels a frame would be
// wasteful when only 62 distinct colours exist. Warm dark, never a blue regrade.
static uint16_t g_palDim[256];
static uint16_t g_palGnd[256];   // the same palette graded as ground seen through the window
// (declared up with the room loader, which invalidates it whenever the palette changes)
static void updateDimPalette() {
  // Dance mode drops the house lights. Done HERE, at the palette, rather than as a pass over
  // the finished frame: this is 256 entries recomputed only when the level actually changes,
  // where a full-screen darkening pass would be 43,200 read-modify-writes every frame at 16fps.
  // The coloured beams are additive, so a darker room is what makes them read as light instead
  // of as tinted stickers — which is most of why they looked subtle.
  // Rain gloom (by request): a daytime shower takes the room down a touch. Scaled by
  // daylight so it never stacks onto night, and it rides `dim`, so the existing cache key
  // (dim*32 in the low bits of q) already invalidates the palette as the shower fades in/out.
  // AWAY DARKENS THE WHOLE ROOM (Jon 8/14: "the lights need to also be off to indicate an
  // issue"). Switching S.lights off only kills the LAMP, and in daylight the window carries
  // the room — so a bunny could be missing at noon and the house still looked cheerful. An
  // away term takes it down to dusk regardless of the hour, and lifts the moment he is back.
  float dim = nightAmount() * 0.60f + rainAmount() * daylight() * 0.14f +
              (S.lights ? 0.0f : 0.22f) + (g_danceMode ? 0.50f : 0.0f) +
              (bunAway() ? 0.42f : 0.0f);
  float cap = g_danceMode ? 0.84f : 0.78f;
  if (dim > cap) dim = cap;
  int q = (int)(dim * 32) | ((int)(nightAmount() * 8) << 8) | (g_danceMode ? (1 << 16) : 0) |
          (bunAway() ? (1 << 17) : 0);
  if (q == g_dimApplied) return;
  g_dimApplied = q;
  // k must come from `dim`, NOT from q. q is a composite cache key with the night level
  // packed into the high bits; dividing it by 32 made k hugely negative and crushed the
  // entire room palette to black.
  float k = 1.0f - dim;
  float night = nightAmount();
  // The ground-beyond-the-glass curve, as a palette of its own. The grade itself already knew
  // how to take the hills to night — it was only ever asked about palette entries, and the
  // hills' entries are shared with the furniture so they could never be marked. Precomputing
  // the answer for EVERY entry lets the band loop pick per pixel, off g_gndMask, for the cost
  // of one extra table and no per-pixel arithmetic at all.
  for (int i = 0; i < 256; i++) {
    const uint16_t v = g_pal[i];
    int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    const int nr = 3, ng = 7, nb = 9;        // (24,28,74) in 8-bit — the builder's own target
    const float amt = night * 0.88f;         // and its 0.88, so the field lags the sky a little
    r = (int)(r + (nr - r) * amt);
    g = (int)(g + (ng - g) * amt);
    b = (int)(b + (nb - b) * amt);
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    if (r > 31) r = 31; if (g > 63) g = 63; if (b > 31) b = 31;
    g_palGnd[i] = (uint16_t)((r << 11) | (g << 5) | b);
  }
  for (int i = 0; i < 256; i++) {
    uint16_t v = g_pal[i];
    int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    if (g_isOutside[i]) {
      // Everything beyond the glass goes to night together — sky, clouds AND the hills.
      // Tinting only the blue left a daylit landscape under a night sky.
      // Sky goes deep navy; the ground keeps a hint of its own hue but much darker.
      bool sky = g_isSky[i];
      int nr = sky ? 2 : 3, ng = sky ? 5 : 7, nb = sky ? 11 : 9;
      float amt = night * (sky ? 1.0f : 0.88f);
      r = (int)(r + (nr - r) * amt);
      g = (int)(g + (ng - g) * amt);
      b = (int)(b + (nb - b) * amt);
    } else {
      // keep a touch more red than blue so the indoor dark stays warm
      r = (int)(r * (k + 0.06f * (1 - k)));
      g = (int)(g * k);
      b = (int)(b * (k - 0.04f * (1 - k)));
    }
    if (r > 31) r = 31; if (g > 63) g = 63; if (b < 0) b = 0; if (b > 31) b = 31;
    if (r < 0) r = 0; if (g < 0) g = 0;
    g_palDim[i] = (uint16_t)((r << 11) | (g << 5) | b);
  }
}

// ---------------- room dressing ----------------
// Positions are the HTML's, in its 320x240 coordinate space, scaled by VIEW at draw time.
struct ItemSpot { float x, y, s; bool flip; };
// Both nudged down 10 (Jon 8/14: "shift the time and cat down just a bit so there isn't any
// overlap") — the pause pad wraps the digits now, and at the adult's original y28 that pad
// reached y7, close enough to the top trim and the chip row to read as crowded. The cat and
// its time move together, so the clock face still sits where the pendulum hangs.
// The adult's shelf goes down furthest: its digits sat at y18, which is inside the y4..20
// pause pad now that the pad is pinned to the chip row. At y44 the digits land at y23, clear
// of the pad by 3px. Baby (y46 -> digits 24) and teen (y60 -> digits 35) already clear it.
static const ItemSpot CATCLOCK_BABY = {70, 46, 0.55f, false};
// x88 -> x72 (Jon 8/14): at 88 the pause pad ran x47..85 and the DANCE chip
// starts at x81 — a 4px overlap. At 72 the pad sits x35..73, a clear 8px short
// of DANCE. The cat and its time move with the pad; they are one cluster.
static const ItemSpot CATCLOCK_ADULT = {72, 44, 0.45f, false};
// teen: browser-checked. Centred in the open wall between the small framed picture (x~20) and
// the curtain (x~110), low enough to clear the ceiling trim and leave room for the tail.
static const ItemSpot CATCLOCK_TEEN = {62, 60, 0.48f, false};
static const ItemSpot LAMP_BABY  = {297, 32, 0.74f, true};
static const ItemSpot LAMP_ADULT = {297, 29, 0.72f, true};
static const ItemSpot LAMP_TEEN  = {297, 30, 0.72f, true};   // same right wall, above the desk
// The teen's radio. Sits on the floor to the right, clear of the desk, so he has somewhere to
// walk TO — the dance is a destination rather than something he does on the spot.
static const ItemSpot RADIO_TEEN = {RADIO_X, RADIO_Y, 0.42f, false};
// dial centre + radius, measured off the 64x64 catclock sprite
static const float DIAL_X = 31.4f, DIAL_Y = 47.6f, DIAL_R = 9.5f;

static void drawItemAtXY(const char *name, float gx, float gy, float sx, float sy, bool flip);

// scratch for rotated draws: the largest sprite the pak allows is 128x128
#define ROT_MAX 128
static uint16_t *g_rotPix = nullptr;
static uint8_t  *g_rotCov = nullptr;

// Decodes the loaded sprite's RLE into flat pixel + coverage buffers so it can be sampled in any
// order. Returns false if the scratch cannot be had, and the caller falls back to an upright draw.
static bool spriteFlatten() {
  // Guard on BOTH, and free whichever one did come back. The gate used to be `if (!g_rotPix)`
  // alone, so when the 32KB succeeded and the 16KB did not — or the reverse — the block re-ran on
  // the next call and overwrote the surviving pointer without freeing it. drawItemRot() calls
  // this for every rotated prop every frame, so that is 16KB of PSRAM gone per prop per frame.
  if (!g_rotPix || !g_rotCov) {
    if (g_rotPix) { free(g_rotPix); g_rotPix = nullptr; }
    if (g_rotCov) { free(g_rotCov); g_rotCov = nullptr; }
    g_rotPix = (uint16_t *)heap_caps_malloc(ROT_MAX * ROT_MAX * 2, MALLOC_CAP_SPIRAM);
    g_rotCov = (uint8_t *)heap_caps_malloc(ROT_MAX * ROT_MAX, MALLOC_CAP_SPIRAM);
    if (!g_rotPix || !g_rotCov) {
      if (g_rotPix) { free(g_rotPix); g_rotPix = nullptr; }
      if (g_rotCov) { free(g_rotCov); g_rotCov = nullptr; }
      return false;                    // caller falls back to an upright draw
    }
  }
  if (g_meta.w > ROT_MAX || g_meta.h > ROT_MAX) return false;
  // Clear ROW BY ROW, because this buffer is WRITTEN with a stride of ROT_MAX, not of w. A single
  // memset of w*h clears w*h CONTIGUOUS bytes — for a 20x30 sprite that is 600 bytes, which at a
  // stride of 128 is the first four and a bit rows and nothing else. Rows below that kept the
  // coverage bits of whichever sprite was flattened last, so drawItemRot sampled colours out of
  // g_rotPix that belonged to the previous prop: the halo of unrelated magenta/green/red pixels
  // around the right sconce, the only rotated wall prop in Jon's scene. The left sconce is the
  // same art with no rotation and is clean, which is what pinned it here.
  // Only [0,w) x [0,h) is ever read back — drawItemRot bounds su and sv before indexing — so
  // clearing w bytes per row is exactly enough, and cheaper than clearing the full 16 KB.
  for (int y = 0; y < g_meta.h; y++) memset(g_rotCov + (size_t)y * ROT_MAX, 0, g_meta.w);
  for (int y = 0; y < g_meta.h; y++) {
    uint32_t p = g_rowOff[y];
    uint8_t segs = g_spr[p++];
    int x = 0;
    for (int i = 0; i < segs; i++) {
      x += g_spr[p++];
      uint8_t l = g_spr[p++];
      for (int k = 0; k < l; k++) {
        if (x + k < g_meta.w) {
          g_rotPix[y * ROT_MAX + x + k] =
              (uint16_t)(g_spr[p + k * 2] | (g_spr[p + k * 2 + 1] << 8));
          g_rotCov[y * ROT_MAX + x + k] = 1;
        }
      }
      p += l * 2;
      x += l;
    }
  }
  return true;
}

// ---- SCENE PROPS ARE PLACED BY THEIR WHOLE CANVAS, NOT BY THEIR INK ----
//
// Jon, on the live shelf: "it needs to be the exact same as the builder". It was not, and this is
// why. The builder draws a sprite's ENTIRE w x h canvas and anchors it by the feet. The pak stores
// only the trimmed ink rectangle plus the offX/offY it was cut from, and drawItemAtXY() draws that
// rectangle with its top at `gy` — but `gy` is the top of the FULL canvas, because that is what
// tools/scene_push.py computes and sends. So every prop rode exactly offY*scale too high, and any
// prop whose trim was not horizontally symmetric slid sideways as well:
//
//     shelf   offY 34, scale 1.0  ->  34 px high      chair  offY 17, scale 1.02  ->  17 px high
//     rug     offY 12, scale 1.3  ->  16 px high              offX 27 of 96 wide  ->  10 px left
//     sconce  offY  9, scale 1.0  ->   9 px high      table  offY  7, scale 0.70  ->   5 px high
//
// Nothing looked broken, which is why it survived: every prop was wrong in the same direction, so
// the room stayed internally plausible while matching the builder nowhere.
//
// This lives in its own entry point rather than inside drawItemAtXY because drawItemAtXY is also
// how the firmware's OWN fittings are drawn — the cat clock, the sconce, the radio — and those
// call sites were positioned by eye against the trimmed behaviour. Scenes get the exact rule;
// the compiled-in room keeps the one it was tuned to.
//
// Returns the ink's top-left in ROOM space. Must be called with the sprite already loaded.
static void scenePropInk(float gx, float gy, float sx, float sy, bool flip,
                         float *inkLeft, float *inkTop) {
  const float ow = g_meta.origW ? g_meta.origW : g_meta.w;
  // Mirroring happens about the FULL canvas, so a flipped sprite's ink sits at the distance from
  // the right edge that it had from the left. Using the ink box alone would pin it in place.
  *inkLeft = flip ? gx + ow * sx * 0.5f - (float)(g_meta.offX + g_meta.w) * sx
                  : gx - ow * sx * 0.5f + (float)g_meta.offX * sx;
  *inkTop = gy + (float)g_meta.offY * sy;
}

static void drawScenePropAtXY(const char *name, float gx, float gy, float sx, float sy, bool flip) {
  if (!spriteLoad(name)) return;
  float il, it;
  scenePropInk(gx, gy, sx, sy, flip, &il, &it);
  // drawItemAtXY takes the ink's CENTRE x and its TOP y; spriteLoad above is cached, so the
  // second call inside it costs nothing.
  drawItemAtXY(name, il + g_meta.w * sx * 0.5f, it, sx, sy, flip);
}

// A rotated prop: same anchor, same scale, turned clockwise by `deg`. Scene-only, so it takes the
// same whole-canvas anchor as drawScenePropAtXY and turns about the CANVAS centre — which is what
// the builder turns about. Turning about the ink's centre instead would walk a trimmed sprite
// sideways as it rotated.
static void drawItemRot(const char *name, float gx, float gy, float sx, float sy,
                        bool flip, float deg) {
  if (!spriteLoad(name)) return;
  if (!spriteFlatten()) { drawScenePropAtXY(name, gx, gy, sx, sy, flip); return; }
  const float iw = g_meta.w * sx * VIEW, ih = g_meta.h * sy * VIEW;
  if (iw <= 0 || ih <= 0) return;
  const float oh = g_meta.origH ? g_meta.origH : g_meta.h;
  float il, it;
  scenePropInk(gx, gy, sx, sy, flip, &il, &it);
  const float ix0 = il * VIEW, iy0 = it * VIEW;                 // the ink box before turning
  const float rad = deg * 0.017453292f, ca = cosf(rad), sa = sinf(rad);
  // THE PIVOT IS THE ANCHOR — the bottom of the canvas, where the prop's feet are — not the
  // canvas centre. placer.html:414 translates to `(it.x, it.y + pad*kh)` before rotating, and
  // says why in the line above it: "ROTATE ABOUT THE ANCHOR, not the bounding box: the anchor is
  // the object's feet, and turning about the middle would lift a floor prop off the floor."
  // Since gy is the canvas TOP, the anchor is gy + origH*sy. Turning about the middle instead put
  // Jon's 15-degree sconce about 6px out, and would throw a 45-degree prop 18px.
  const float cx = gx * VIEW, cy = (gy + oh * sy) * VIEW;
  // Only visit what the turned INK can cover: its four corners, forwards through the rotation.
  float ex = 0, ey = 0;
  for (int c = 0; c < 4; c++) {
    const float ux = (c & 1 ? ix0 + iw : ix0) - cx, uy = (c & 2 ? iy0 + ih : iy0) - cy;
    const float rx = ux * ca - uy * sa, ry = ux * sa + uy * ca;
    if (fabsf(rx) > ex) ex = fabsf(rx);
    if (fabsf(ry) > ey) ey = fabsf(ry);
  }
  const int x0 = (int)(cx - ex) - 1, x1 = (int)(cx + ex) + 1;
  const int y0 = (int)(cy - ey) - 1, y1 = (int)(cy + ey) + 1;
  for (int dy = y0; dy <= y1; dy++) {
    for (int dx = x0; dx <= x1; dx++) {
      const float rx = dx + 0.5f - cx, ry = dy + 0.5f - cy;
      const float ux = rx * ca + ry * sa + cx, uy = -rx * sa + ry * ca + cy;   // back upright
      // floorf, not a cast: C truncates toward zero, so a back-rotated point just LEFT of the ink
      // gives su = 0 instead of -1 and is not rejected — repeating source column 0 as a one-pixel
      // fringe, or column w-1 when flipped, which is the sprite's opposite edge.
      int su = (int)floorf((ux - ix0) * g_meta.w / iw), sv = (int)floorf((uy - iy0) * g_meta.h / ih);
      if (flip) su = g_meta.w - 1 - su;
      if (su < 0 || su >= g_meta.w || sv < 0 || sv >= g_meta.h) continue;
      if (!g_rotCov[sv * ROT_MAX + su]) continue;
      if (petStenHides(dx, dy)) continue;      // a prop on a layer behind the pet stops at him
      scene.drawPixel(dx, dy, g_rotPix[sv * ROT_MAX + su]);
    }
  }
}

static void drawItemAtXY(const char *name, float gx, float gy, float sx, float sy, bool flip) {
  // The last line of defence. Everything below turns these floats into array indices, and a
  // non-finite one is an out-of-bounds write rather than a misplaced sprite — which is how a
  // rolling-object NaN became a panic. Cheap, and it protects every caller, not just the sim.
  if (!isfinite(gx) || !isfinite(gy) || !isfinite(sx) || !isfinite(sy)) return;
  if (!spriteLoad(name)) return;
  int dw = (int)(g_meta.w * sx * VIEW + 0.5f), dh = (int)(g_meta.h * sy * VIEW + 0.5f);
  if (dw <= 0 || dh <= 0) return;
  int left = (int)(gx * VIEW + 0.5f) - dw / 2, top = (int)(gy * VIEW + 0.5f);
  uint32_t xs = ((uint32_t)g_meta.w << 16) / dw, ysp = ((uint32_t)g_meta.h << 16) / dh;
  for (int dy = 0; dy < dh; dy++) {
    int sy = (int)((uint32_t)dy * ysp >> 16); if (sy >= g_meta.h) sy = g_meta.h - 1;
    uint16_t line[128]; uint8_t cov[128]; memset(cov, 0, sizeof(cov));
    uint32_t p = g_rowOff[sy]; uint8_t segs = g_spr[p++]; int x = 0;
    for (int i = 0; i < segs; i++) {
      x += g_spr[p++]; uint8_t l = g_spr[p++];
      for (int k = 0; k < l && x + k < 128; k++)
          line[x + k] = (uint16_t)(g_spr[p + k * 2] | (g_spr[p + k * 2 + 1] << 8)), cov[x + k] = 1;
      p += l * 2; x += l;
    }
    for (int dx = 0; dx < dw; dx++) {
      int si = (int)((uint32_t)(flip ? dw - 1 - dx : dx) * xs >> 16);
      if (!cov[si]) continue;
      if (petStenHides(left + dx, top + dy)) continue;  // behind-the-pet layer: he wins this pixel
      scene.drawPixel(left + dx, top + dy, line[si]);   // drawPixel swaps for us
    }
  }
}

// the old shape, for everything that scales a sprite evenly
static void drawItemAt(const char *name, float gx, float gy, float s, bool flip) {
  drawItemAtXY(name, gx, gy, s, s, flip);
}

// Kit-Cat clock: sprite body, tail swinging as a pendulum, and hands from the real time.
// THE TAIL AND THE HANDS ARE DRAWN LINES, NOT PART OF THE SPRITE.
// Which is why they vanished the moment a child's scene supplied the clock: the room only draws
// its own fixture `if (!sceneHas("items/catclock"))`, and everything below used to live inside
// that. The scene's clock got the picture and nothing else — no swinging tail, no time.
// Jon: "the passive cat tail is missing". Split out so BOTH paths get a working clock.
// gx is the sprite's centre, gy its canvas top, s its scale — the same anchor drawItemAt takes.
static void catClockFace(float gx, float gy, float s, bool tailBehind) {
  if (!spriteLoad("items/catclock")) return;
  const float k = s * VIEW;
  const int w = (int)(g_meta.origW * k + 0.5f), h = (int)(g_meta.origH * k + 0.5f);
  const int left = (int)(gx * VIEW + 0.5f) - w / 2, top = (int)(gy * VIEW + 0.5f);

  if (tailBehind) {
    const float swing = sinf(millis() / 700.0f) * 0.42f;   // lazy ~24-degree pendulum
    const float px = left + w * 0.5f, py = top + h * 0.93f;
    const float tx = px + sinf(swing) * 22 * k, ty = py + cosf(swing) * 22 * k;
    // The tail was hard-coded to ink, so against a darkened wall the cat appeared to lose it.
    // Take the colour from the cat's own body instead, sampled just above the tail root.
    uint16_t tail = C_INK;
    for (int probe = 0; probe < 6; probe++)
      if (spritePixel(g_meta.w / 2, g_meta.h - 3 - probe * 2, &tail)) break;
    scene.drawLine((int)px, (int)py, (int)tx, (int)ty, tail);
    scene.drawLine((int)px + 1, (int)py, (int)tx + 1, (int)ty, tail);
    return;                              // the caller paints the body next, over the root
  }

  const int cx = left + (int)(DIAL_X * k), cy = top + (int)(DIAL_Y * k);
  const int r = max(3, (int)(DIAL_R * k));
  const int mins = clockNowMin() % 60, hrs = clockNowMin() / 60;
  const float ma = mins * 6.0f * DEG_TO_RAD;
  const float ha = ((hrs % 12) + mins / 60.0f) * 30.0f * DEG_TO_RAD;
  scene.drawLine(cx, cy, cx + (int)(sinf(ha) * r * 0.55f), cy - (int)(cosf(ha) * r * 0.55f), C_INK);
  scene.drawLine(cx, cy, cx + (int)(sinf(ma) * r * 0.85f), cy - (int)(cosf(ma) * r * 0.85f), C_INK);
}

static void drawCatClock() {
  const ItemSpot &sp = (S.phase == PH_BABY) ? CATCLOCK_BABY
                     : (S.phase == PH_TEEN) ? CATCLOCK_TEEN : CATCLOCK_ADULT;
  catClockFace(sp.x, sp.y, sp.s, true);            // tail first, so the body covers its root
  drawItemAt("items/catclock", sp.x, sp.y, sp.s, false);
  catClockFace(sp.x, sp.y, sp.s, false);           // then the hands, over the dial
}

// The soft-edged light table. Drawing the beam as sparse scanlines looked like scratches;
// this precomputes a per-pixel intensity map ONCE (the sconce never moves) so the render
// loop only does a cheap blend. 240x180 bytes = 43KB, which we can afford, and it preserves
// the smooth falloff that was tuned in the browser instead of approximating it.
// Stored at HALF resolution: 120x90 = 10.8KB instead of 43KB. The full-size map was failing
// to allocate once the scene sprite (86KB) and the MP3 decoder had taken the heap, so
// g_light came back NULL and the glow was silently skipped. Light is smooth, so halving the
// resolution costs nothing visually.
static const int LIGHT_W = SCENE_W / 2, LIGHT_H = SCENE_H / 2;
static uint8_t *g_light = nullptr;
static int g_lightPhase = -1;
static int g_lightScalePct = -1;
static uint16_t g_lightGen = 0xffff;   // which scene generation this map was built for
static bool g_lightFailed = false;     // the alloc failed once; do not retry it every frame

static float smoothstep(float a, float b, float x) {
  if (x <= a) return 0; if (x >= b) return 1;
  float t = (x - a) / (b - a);
  return t * t * (3 - 2 * t);
}
// One fixture's throw, in the HTML's 320x240 room space. A direct port of the builder's
// geomFor() (placer.html:821), whose comments are the record of what each term is for:
// the bulb sits 27px ABOVE a wall-mounted fixture's anchor; a sconce throws INTO the room, so
// the direction comes from which half of the room it is on and NOT from the art's flip; ROT
// swings the whole axis about the bulb; and the pool is where that axis MEETS THE FLOOR, lying
// flat on it, rather than the end of a fixed-length beam.
struct LampGeom { float sx, sy, px, py, rx, ry, half, ux, uy, len, power; bool hitsFloor; };
static bool lampGeomFor(const SceneProp *p, float lsc, LampGeom *g, const SceneLamp *cfg) {
  // pakFind, NOT spriteLoad. buildLightMap() is called from inside the render path, and
  // spriteLoad() overwrites the ONE shared sprite buffer — g_spr, g_meta and g_sprName — which
  // every draw in that frame is reading. Loading a sconce here to measure it stopped the cat
  // clock and both sconces drawing at all, and it was only obvious once the sconces were taken
  // out of the scene and the clock came back. pakFind only reads the index: it is pure, it costs
  // nothing, and every field wanted here is in the entry already.
  if (!p) return false;
  const PakEntry *e = pakFind(p->name);
  if (!e) return false;

  // The builder anchors a fixture by its FEET; the scene carries the CANVAS TOP. Recover the
  // anchor the way the trim says: the bottom of the ink is where the builder's y was.
  const float ax = p->x;
  // THE ANCHOR THE BUILDER USES, when the scene carries it. scene_push sends the fixture's own
  // `it.y` — canvasBottom - pad*k — because pad is authored in ASSETS and is not recoverable
  // from the pak's trim. Falling back to the ink bottom reproduces the old behaviour for a
  // scene.json written before this field existed, and that fallback is what Jon was seeing:
  // "the light off of the sconces is shifted too low relative to where they are placed", and
  // "the light should be coming all the way up to the lights there is a gap". The sconce's pad
  // is 9, its ink fills its canvas, so the bulb hung 9px below where the builder puts it.
  const float ay = (cfg && cfg->anchorY != INT16_MIN)
                     ? (float)cfg->anchorY
                     : p->y + ((float)e->offY + e->h) * p->sy;

  // PER-LAMP CONTROLS, as the builder offers them. `sc = lightScale * lampSize` there, and
  // bulbHeightOf() honours an explicit bulbY before it judges by where the fixture sits — which
  // is the whole answer to "i have a standing lamp and it is just projecting from the base".
  if (cfg && cfg->size != 100) lsc *= cfg->size / 100.0f;
  const bool standing = (ay >= 120.0f);        // bulbHeightOf: up on the wall, or on the floor
  // -3, NOT -27, AND THE REASON IS THIS FUNCTION'S OWN HISTORY.
  // -27 was reverse-engineered from the shipped LAMP_ADULT {297,29} against a hard-coded source
  // at 294,56: 56 - 29 = 27. But that 29 is a CANVAS TOP, because drawItemAt() takes the top.
  // `ay` is no longer a canvas top — carrying the builder's anchor moved it to
  // canvasBottom - pad, which for the sconce is top + 35. The constant was re-pointed at a
  // different origin and never re-derived, so the bulb ended up 27 below the anchor instead of
  // 27 below the top: 18px BELOW the bottom of the sprite's own canvas, hanging in mid-air.
  // That is the residual Jon kept seeing after the anchor fix — "the light glow looks better but
  // it is still off from the sconces". Measured against his reference frame: the builder's bulb
  // sits at room y 64 for both fixtures, against anchors of 61 and 59. Hence -3.
  // Verified on 6D1C without a rebuild, by pushing "by":-3 through the per-lamp override: the
  // left cone's outer edge at room rows 112/120/128 moved 56/64/72 -> 83/91, against a predicted
  // 80/88/96. Within the half-res light map's own ~1.3px quantisation.
  // THE BUILDER CARRIES THE SAME -27 (bulbHeightOf) and must be changed with this, or the record
  // and the player disagree again the moment anyone re-checks.
  float bulbUp = standing ? max(6.0f, (float)e->origH * p->sy - 6.0f) : -3.0f;
  if (cfg && cfg->bulbY != INT16_MIN) bulbUp = (float)cfg->bulbY;
  const float mir = (ax > 160.0f) ? 1.0f : -1.0f;
  const float sx = ax - (standing ? 0.0f : 3.0f * mir);
  const float sy = ay - bulbUp;
  const float floorY = FLOOR_Y + 10.0f;
  const float drop = max(1.0f, floorY - sy), ref = 210.0f - 56.0f;
  const float t = drop / ref;
  const float lean = standing ? 0.25f : 1.0f;
  const float vx0 = -58.0f * t * mir * lean, vy0 = drop;
  const float a = p->rot * 0.017453292f, ca = cosf(a), sa = sinf(a);
  const float vx = vx0 * ca - vy0 * sa, vy = vx0 * sa + vy0 * ca;
  const float L0 = max(1.0f, sqrtf(vx * vx + vy * vy));
  const float ux = vx / L0, uy = vy / L0;
  float px, py, len;
  bool hits = false;
  const float reach = (uy > 0.001f) ? (floorY - sy) / uy : 1e9f;
  if (uy > 0.15f && reach <= 340.0f) { len = reach; hits = true; px = sx + ux * len; py = floorY; }
  else { len = max(40.0f, drop); px = sx + ux * len; py = sy + uy * len; }
  g->sx = sx * VIEW; g->sy = sy * VIEW; g->px = px * VIEW; g->py = py * VIEW;
  g->rx = 96.0f * max(0.45f, t) * lsc * VIEW;
  g->ry = 22.0f * max(0.5f, t) * lsc * VIEW;
  g->half = 12.0f * lsc * VIEW;
  g->ux = ux; g->uy = uy;
  g->len = max(1.0f, len * VIEW);        // the beam's own axis, which is what intensity uses
  g->power = cfg ? (cfg->power / 100.0f) : 1.0f;   // brightness only; spread is `size`, above
  g->hitsFloor = hits;
  return true;
}

// Paint one fixture's cone and pool into the map, keeping the brightest contribution per pixel —
// the builder composites its lamps the same way, so two lamps overlapping do not double up into
// a hot spot.
// One fixture's contribution at one point, as litRoom() computes it.
static float lampIntensityAt(const LampGeom *g, float x, float y) {
  float inten = 0;
  // MEASURED ALONG THE BEAM, NOT DOWN THE SCREEN. The builder's comment says exactly why:
  // "so a turned lamp still works". u runs 0 at the bulb to 1 at the landing, v is the
  // distance across the axis. The old firmware walked down the screen instead, which is only
  // right for a beam that falls straight down — and Jon's right sconce carries 15 degrees.
  const float ex = x - g->sx, ey = y - g->sy;
  const float u = (ex * g->ux + ey * g->uy) / g->len;
  const float v = fabsf(-ex * g->uy + ey * g->ux);
  if (u >= 0.0f && u <= 1.0f) {
    const float hw = g->half + (g->rx - g->half) * u;
    const float dd = v / max(1.0f, hw);
    if (dd < 1.0f) {
      const float along = u < 0.88f ? 1.0f : 1.0f - 0.45f * ((u - 0.88f) / 0.12f);
      inten = along * (1.0f - smoothstep(0.72f, 1.0f, dd)) * 0.62f;
      // the beam through the air is faint; only the POOL it lands in reaches full strength
    }
  }
  // The pool lies FLAT on the floor — axis-aligned where the beam lands, never tilted with the
  // beam, because the floor is horizontal whatever the lamp does. A beam angled up or sideways
  // never reaches it, so it washes what it crosses and lands nothing.
  if (g->hitsFloor) {
    const float ddx = (x - g->px) / g->rx, ddy = (y - g->py) / g->ry;
    const float rr = sqrtf(ddx * ddx + ddy * ddy);
    if (rr < 1.0f) {
      const float pp = 1.0f - smoothstep(0.82f, 1.0f, rr);
      if (pp > inten) inten = pp;
    }
  }
  return inten;
}

// Every lamp, ADDED — "their light ADDS, clamped at white" — not maxed. Two sconces throwing
// across the same patch of floor make it brighter than either alone, which is the difference
// between a room with two lamps in it and a room with one lamp drawn twice.
static void lightPaint(const LampGeom *lamps, int n) {
  for (int ly = 0; ly < LIGHT_H; ly++) {
    for (int lx = 0; lx < LIGHT_W; lx++) {
      // SCREEN SPACE, AND DELIBERATELY SO — do not "fix" this to room coordinates.
      // It looks wrong because lampIntensityAt()'s constants are room numbers (floorY 210,
      // rx 96, a bulb at the scene's own y), but lampGeomFor() has ALREADY converted every one
      // of them: it stores sx/sy/px/py/rx/ry/half/len each multiplied by VIEW. LampGeom is a
      // screen-space record. The band loop then reads this map as `lm[x >> 1]` on row
      // `ry2 >> 1` with x and ry2 both screen pixels, so map cell (lx,ly) displays at screen
      // (2lx,2ly) and evaluating at (2lx,2ly) is exactly right.
      // Dividing by VIEW here was tried on 0.1.284 and is WRONG. It made the RIGHT sconce
      // disappear outright: that fixture's beam runs screen x 214 -> 148, and rescaling dragged
      // its cone off the right wall into the middle of the room. Measured on 6D1C — the right
      // wall went flat at 85..98 with peak 171, against the left wall's clear 238 cone.
      // The genuine "light is too low" bug was the lamp ANCHOR (see lampGeomFor), not this.
      const float x = lx * 2, y = ly * 2;
      float acc = 0;
      for (int i = 0; i < n; i++) acc += lampIntensityAt(&lamps[i], x, y) * lamps[i].power;
      // THE BUILDER'S HEADROOM, NOT 1.0. litRoom() clamps at `Math.min(1.6, acc)` — lamps ADD,
      // and two of them overlapping are allowed to reach half again as bright as one. Capping at
      // 1.0 here threw that away and made the middle of the room, where both sconces meet, no
      // brighter than either cone alone. Stored over 1.6 so the byte still spans the full range.
      if (acc > 1.6f) acc = 1.6f;
      g_light[ly * LIGHT_W + lx] = (uint8_t)(acc * (255.0f / 1.6f));
    }
  }
}

static void buildLightMap() {
  if (g_lightFailed) return;             // asked once, could not have it; do not ask every frame
  // PSRAM, like every other buffer on this path (g_spr, g_petSten, g_rotPix, g_sceneBase). This
  // was the last 10.8KB internal-RAM resident on a board with ~30KB free, and internal RAM is the
  // thing that has bitten this firmware hardest.
  if (!g_light) g_light = (uint8_t *)heap_caps_malloc(LIGHT_W * LIGHT_H, MALLOC_CAP_SPIRAM);
  if (!g_light) g_light = (uint8_t *)malloc(LIGHT_W * LIGHT_H);      // no PSRAM: try anywhere
  if (!g_light) {
    Serial.println("light map alloc failed - no glow this run");
    // Latch it. The old code returned here WITHOUT stamping the cache keys, so the caller's
    // guard stayed true and this ran again — malloc plus a blocking Serial print — every frame.
    g_lightFailed = true;
    return;
  }
  memset(g_light, 0, LIGHT_W * LIGHT_H);
  // LIGHT SIZE, from the scene (the builder's lightScale). Geometry, not a clock value, so it is
  // safe to carry: it scales the cone's spread and the pool it lands in, exactly as the builder's
  // geomFor() does with sc = lightScale * lampSize. 100 = the shipped throw.
  const SceneEnv *env = sceneEnv();
  const float lsc = env ? env->lightS / 100.0f : 1.0f;

  // EVERY LAMP THE SCENE PLACED, not one compiled-in cone (Jon: "only one light is on in the
  // scene"). His farmhouse hangs two sconces; the old map was hard-wired to a source at 294,56
  // and a pool at x236, which happens to sit near where the RIGHT one is, so the left one has
  // been decorative since the day scenes could carry lamps at all.
  // STATIC, not on the stack. This is 24 x ~48 bytes = over a kilobyte, and buildLightMap() runs
  // inside the render path on a task whose headroom is measured in single-digit KB. 0.1.257 put
  // it on the stack and the unit came back with reset_reason 4 (PANIC) and an RTC breadcrumb of
  // 24 — a value nothing in the firmware ever writes, i.e. the marker itself had been scribbled
  // on. That is what a stack overflow looks like from the outside.
  // Four is also plenty: a room with more than four lamps in it is not a room, it is a runway.
  static LampGeom lamps[4];
  int lit = 0;
  const int n = sceneActive() ? sceneLampCount() : 0;
  for (int i = 0; i < n && lit < (int)(sizeof(lamps) / sizeof(lamps[0])); i++)
    if (lampGeomFor(sceneLamp(i), lsc, &lamps[lit], sceneLampCfg(i))) lit++;
  if (!lit && !g_scRoom[0]) {
    // No scene at all: the room the firmware ships, cone and all. A CUSTOM room with no
    // lamps gets NO cone - the builder is the lighting record, and a glow from a sconce
    // that is not there was the device inventing light ("the light from the phantom
    // sconce at work is still there").
    LampGeom *g = &lamps[0];
    g->sx = 294.0f * VIEW; g->sy = 56.0f * VIEW;
    g->px = 236.0f * VIEW; g->py = (FLOOR_Y + 10) * VIEW;
    g->rx = 96.0f * VIEW * lsc; g->ry = 22.0f * VIEW * lsc; g->half = 12.0f * VIEW * lsc;
    const float dx = g->px - g->sx, dy = g->py - g->sy;
    g->len = max(1.0f, sqrtf(dx * dx + dy * dy));
    g->ux = dx / g->len; g->uy = dy / g->len;
    g->power = 1.0f;
    g->hitsFloor = true;
    lit = 1;
  }
  if (lit) lightPaint(lamps, lit);       // 0 lamps = an honest, even room
  g_lightPhase = S.phase;
  g_lightScalePct = env ? env->lightS : 100;
  g_lightGen = g_scGen;
  Serial.printf("light map built - %d lamp(s), scene gen %u\n", lit, (unsigned)g_scGen);
}
static void drawLampFixture() {
  const ItemSpot &sp = (S.phase == PH_BABY) ? LAMP_BABY
                     : (S.phase == PH_TEEN) ? LAMP_TEEN : LAMP_ADULT;
  drawItemAt("items/sconce", sp.x, sp.y, sp.s, sp.flip);
}
// Teen only, and only if the art made it into the pack — the radio is the thing he walks to
// before dancing, so drawing it in the baby or adult room would be a prop with no purpose.
static void drawRadio() {
  if (S.phase != PH_TEEN || !pakFind("items/radio")) return;
  drawItemAt("items/radio", RADIO_TEEN.x, RADIO_TEEN.y, RADIO_TEEN.s, RADIO_TEEN.flip);
}

// ---------------- dance mode: disco ball and lights ----------------
// Drawn in CODE, not as sprite art. The ball has to turn and the lights have to flash on beats
// whose tempo is only known at runtime — art could only ever be a fixed-rate loop, which is the
// exact opposite of what is wanted here. It also costs no flash and no pak rebuild.
//
// The button only exists while something is audible, and audible is measured from PCM actually
// reaching I2S (audioLive()), not from g_musicOn or airStatus(). Those record what the firmware
// intended; this records what the speaker is doing. Offering a DANCE button over silence because
// a flag was stale is precisely the class of bug that has cost this project the most time.

static float g_discoSpin  = 0.0f;    // facet rotation, radians

static const int   DISCO_R      = 13;     // ball radius, screen px
static const int   DISCO_HANG   = 44;     // centre y once fully lowered
static const float DISCO_DROP_S = 0.9f;   // seconds to lower or raise

// Centred at the top. Clear of the wifi icon and battery on the right, and clear of the clock
// digits, which sit above the cat clock at x=62-88 depending on phase.
// 24 tall, not 17 (menu redesign P3): the chip row is now a real row — PAUSE, DANCE and the
// gear all stand 24px on the same y4 baseline, so three chips read as one strip of controls
// rather than three unrelated marks. Same X, same width, so nothing below it moved.
static const int DANCE_BW = 78, DANCE_BH = 24;
static const int DANCE_BX = (UI_W - DANCE_BW) / 2, DANCE_BY = 4;

// alive() gates it: an egg has nothing to dance with, and the disco fixtures are skipped in
// drawScene for anything that is not a living pet anyway — so without this the button offered a
// party that could not visibly happen.
//
// W-024 (Jon's design): the button is an INVITATION, not furniture. It shows
// for a few seconds when a song starts or changes, then rolls away; a room
// with music playing shouldn't wear a button the whole time. Double-tap
// remains the standing way in, and the button stays while dance is ON so
// there is always a visible way out.
static uint32_t g_danceBtnUntil = 0;   // window opened by song start/change
static const uint32_t DANCE_BTN_LINGER_MS = 5000;
static void danceBtnInvite() { g_danceBtnUntil = millis() + DANCE_BTN_LINGER_MS; }
// The window stands on its own once opened — audioLive() is AMPLITUDE-based
// ("is anything audible"), so requiring it per-frame made the button strobe
// with every beat of a quiet passage (Jon, field, the night W-024 shipped
// to the bench). The invitation only opens on real audio events anyway.
static inline bool danceBtnVisible() {
  return AUDIO_ENABLED && alive() &&
         (g_danceMode || millis() < g_danceBtnUntil);
}
static inline bool discoVisible()    { return g_discoDrop > 0.002f; }

static void discoTick(float dt) {
  // An egg cannot dance. Cleared rather than merely ignored, so the ball retracts if the pet is
  // reset to an egg while the party is running.
  if (!alive() && g_danceMode) g_danceMode = false;

  // Hold the CPU at full speed while dancing. The scene runs at 16fps instead of 10, every
  // light spot is a per-pixel read-blend-write, and on the AirPlay path g_musicOn is FALSE — so
  // the standby-exit rule would happily leave this at 160MHz, which is where the audio path
  // starts underrunning. Saves the previous value rather than assuming 160: a normal boot runs
  // at 240 and clamping it down on exit would be a regression dressed up as a restore.
  static bool lastMode = false;
  static uint32_t cpuBefore = 0;
  if (g_danceMode != lastMode) {
    lastMode = g_danceMode;
    if (g_danceMode) {
      cpuBefore = getCpuFrequencyMhz();
      if (cpuBefore < 240) setCpuFrequencyMhz(240);
    } else if (cpuBefore && cpuBefore != 240) {
      setCpuFrequencyMhz(cpuBefore);
    }
  }

  float target = g_danceMode ? 1.0f : 0.0f;
  float step   = dt / DISCO_DROP_S;
  if (g_discoDrop < target)      g_discoDrop = min(target, g_discoDrop + step);
  else if (g_discoDrop > target) g_discoDrop = max(target, g_discoDrop - step);

  // CONSTANT rotation, by request and by observation: beat-modulated spin made the ball's
  // speed lurch with every pulse, which read as stutter rather than reaction. The beat now
  // lives only in the brightness flicker; the motion is steady.
  g_discoSpin += dt * 2.0f;
  if (g_discoSpin > 6.2831853f) g_discoSpin -= 6.2831853f;
}

// Additive blend into an RGB565 pixel. The light spots have to LIGHT the room rather than paint
// over it — a flat filled circle reads as a sticker on the wall, not as a beam.
static inline uint16_t addLight(uint16_t base, uint8_t r, uint8_t g, uint8_t b, int amt) {
  int br = ((base >> 11) & 0x1F) << 3, bg = ((base >> 5) & 0x3F) << 2, bb = (base & 0x1F) << 3;
  br += (r * amt) >> 8; bg += (g * amt) >> 8; bb += (b * amt) >> 8;
  if (br > 255) br = 255;
  if (bg > 255) bg = 255;
  if (bb > 255) bb = 255;
  return RGB565(br, bg, bb);
}

// Direct access to the scene sprite's framebuffer. readPixel()/drawPixel() are per-call bounds
// checks and byte swaps against a sprite that lives in PSRAM — fine for a handful of pixels,
// ruinous for effects touching tens of thousands per frame while the audio pipeline saturates
// the same bus. Dance mode's lights are ~24,000 read-modify-writes a frame; through the API,
// under AirPlay 2 streaming, that measured out as a slideshow — it IS the "stalls in dance
// mode" report. Row access walks memory sequentially, so the cache fetches each 64-byte line
// once instead of twice per pixel.
//
// The sprite stores 16bpp pixels BYTE-SWAPPED (so pushSprite can stream them out untouched), so
// everything read from or written to the raw buffer must swap. Skipping the swap does not
// crash — it just quietly mangles red and blue, which this project has already done once today.
static inline uint16_t bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t *sceneRow(int y) { return (uint16_t *)scene.getPointer() + y * SCENE_W; }

// SCREEN CAPTURE (Jon 8/14: "I want to see it on the device to confirm locations of
// objects"). The scene sprite is the room and ALL its chrome — clock, pause pad, gear,
// dance chip, the pet himself — so dumping it answers every "where does that sit" question
// without a camera. Returns a 24-bit BMP in PSRAM; the caller frees it. Bottom-up rows and
// BGR byte order are the format's, not ours. The sprite stores byte-swapped 565 (see
// setSwapBytes at boot), so each pixel is unswapped before it is unpacked.
extern "C" uint8_t *bunbun_scene_bmp(size_t *out_len) {
  const int W = SCENE_W, H = SCENE_H;
  const int rowBytes = (W * 3 + 3) & ~3;             // BMP rows pad to 4 bytes
  const size_t len = 54 + (size_t)rowBytes * H;
  uint8_t *b = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!b) return nullptr;
  memset(b, 0, 54);
  b[0] = 'B'; b[1] = 'M';
  b[2] = (uint8_t)len; b[3] = (uint8_t)(len >> 8);
  b[4] = (uint8_t)(len >> 16); b[5] = (uint8_t)(len >> 24);
  b[10] = 54;                                        // pixel data offset
  b[14] = 40;                                        // DIB header size
  b[18] = (uint8_t)W; b[19] = (uint8_t)(W >> 8);
  b[22] = (uint8_t)H; b[23] = (uint8_t)(H >> 8);
  b[26] = 1;                                         // planes
  b[28] = 24;                                        // bits per pixel
  for (int y = 0; y < H; y++) {
    uint16_t *src = sceneRow(y);
    uint8_t *dst = b + 54 + (size_t)(H - 1 - y) * rowBytes;   // BMP is bottom-up
    for (int x = 0; x < W; x++) {
      uint16_t v = bswap16(src[x]);
      *dst++ = (uint8_t)((v & 0x1F) << 3);           // B
      *dst++ = (uint8_t)(((v >> 5) & 0x3F) << 2);    // G
      *dst++ = (uint8_t)(((v >> 11) & 0x1F) << 3);   // R
    }
  }
  *out_len = len;
  return b;
}

// Six saturated colours, cycled per spot. Kept away from the room's own bone/orange palette so
// they read as coloured light rather than as part of the furniture.
static const uint8_t DISCO_COL[6][3] = {
  {255,  60,  90},   // pink
  { 60, 160, 255},   // blue
  {120, 255, 120},   // green
  {255, 200,  60},   // amber
  {200,  90, 255},   // violet
  { 60, 255, 230},   // cyan
};
static const int DISCO_SPOTS = 12;

static void drawDiscoLights() {
  if (!discoVisible()) return;
  float pulse = beatPulse();
  // A floor under the pulse so the spots never vanish entirely between beats — a room that goes
  // completely dark between hits reads as a glitch rather than as lighting. The ceiling is well
  // past what 8-bit channels can hold on purpose: clipping the centre of each beam to white is
  // what makes it look like a LIGHT rather than a coloured circle, and leaves the colour showing
  // in the falloff where the eye actually reads it.
  int base = (int)(70 + 330 * pulse * g_discoDrop);

  for (int i = 0; i < DISCO_SPOTS; i++) {
    // Alternate spots flare on alternate beats. Every spot pulsing identically looks like the
    // whole screen flashing; staggering them is what makes it read as a mirror ball throwing
    // separate beams around the room.
    int amt = (((g_beatCount + i) & 1) == 0) ? base : (base * 2) / 3;
    if (amt <= 2) continue;

    float a  = g_discoSpin * 0.55f + i * (6.2831853f / DISCO_SPOTS);
    int   cx = UI_W / 2 + (int)(sinf(a) * 112.0f);
    int   cy = 26 + (int)(cosf(a * 0.8f + i * 1.7f) * 30.0f) + (i % 4) * 38;
    int   rad = 18 + (i % 3) * 7;

    const uint8_t *c = DISCO_COL[i % 6];
    int r2 = rad * rad;
    for (int dy = -rad; dy <= rad; dy++) {
      int y = cy + dy;
      if (y < 0 || y >= SCENE_H) continue;
      int span = (int)sqrtf((float)(r2 - dy * dy));
      uint16_t *row = sceneRow(y);
      for (int dx = -span; dx <= span; dx++) {
        int x = cx + dx;
        if (x < 0 || x >= UI_W) continue;
        // Fall off toward the edge so each spot has a soft edge instead of a hard disc.
        int d2 = dx * dx + dy * dy;
        // Falloff on the RADIUS, not on d2. Squared distance collapses to near zero over most
        // of the disc and left only a small hot core with a wide dead margin, which is what
        // made the whole effect read as faint.
        int d  = (int)sqrtf((float)d2);
        int fall = amt - (amt * d) / (rad + 1);
        if (fall <= 1) continue;
        row[x] = bswap16(addLight(bswap16(row[x]), c[0], c[1], c[2], fall));
      }
    }
  }
}

static void drawDiscoBall() {
  if (!discoVisible()) return;
  int cx = UI_W / 2;
  // Rides down from above the ceiling, so it enters the frame rather than appearing in it.
  int cy = (int)(-DISCO_R - 4 + (DISCO_HANG + DISCO_R + 4) * g_discoDrop);

  if (cy - DISCO_R > 0) scene.drawFastVLine(cx, 0, cy - DISCO_R, C_INK);

  float pulse = beatPulse();
  int r2 = DISCO_R * DISCO_R;
  for (int dy = -DISCO_R; dy <= DISCO_R; dy++) {
    int y = cy + dy;
    if (y < 0 || y >= SCENE_H) continue;
    int span = (int)sqrtf((float)(r2 - dy * dy));
    for (int dx = -span; dx <= span; dx++) {
      int x = cx + dx;
      if (x < 0 || x >= UI_W) continue;
      // Facet grid. Longitude via asin so the cells COMPRESS toward the edge the way they do on
      // a real sphere; a linear mapping makes it look like a flat printed circle.
      int   row = (dy + DISCO_R) / 4;
      float lon = asinf((float)dx / (float)(span > 0 ? span : 1)) + g_discoSpin;
      int   col = (int)floorf(lon * 2.6f);
      bool  lit = ((row + col) & 1) != 0;

      // Shade by height so the ball reads as round, then let the beat brighten the whole thing.
      int base = lit ? 210 : 130;
      base -= (dy * 3);
      base += (int)(45 * pulse);
      if (base < 40)  base = 40;
      if (base > 255) base = 255;
      // Tint the lit facets with the current beat colour so the ball itself joins in.
      const uint8_t *c = DISCO_COL[(g_beatCount + (col & 3)) % 6];
      int rr = lit ? (base * 3 + c[0]) >> 2 : base;
      int gg = lit ? (base * 3 + c[1]) >> 2 : base;
      int bb = lit ? (base * 3 + c[2]) >> 2 : base;
      sceneRow(y)[x] = bswap16(RGB565(rr, gg, bb));
    }
  }
  scene.drawCircle(cx, cy, DISCO_R, C_INK);       // keep the thick-outline house style
}

// The toggle. Only drawn while something is audible or it is already on — the second half of
// that matters, because otherwise turning the music off would strand dance mode with no way to
// switch it back off.
static void drawDanceButton() {
  if (!danceBtnVisible()) return;
  uint16_t fill = g_danceMode ? C_ORANGE : C_PAPER;
  uint16_t ink  = g_danceMode ? C_PAPER  : C_INK;
  scene.fillRoundRect(DANCE_BX, DANCE_BY, DANCE_BW, DANCE_BH, 4, fill);
  scene.drawRoundRect(DANCE_BX, DANCE_BY, DANCE_BW, DANCE_BH, 4, C_INK);
  scene.setTextColor(ink);
  scene.setTextFont(1);
  scene.setTextSize(1);
  // Centred now that the chip is 24 tall (P3): "DANCE ON" font1 = 48px inside 78, "DANCE"
  // = 30px. Centring is what keeps both states on the same optical line.
  scene.setTextDatum(MC_DATUM);
  scene.drawString(g_danceMode ? "DANCE ON" : "DANCE", DANCE_BX + DANCE_BW / 2,
                   DANCE_BY + DANCE_BH / 2, 1);
  scene.setTextDatum(TL_DATUM);
}

// ---- the gear (menu redesign P2) ----
// One small chip, always in the same corner, and EVERYTHING grown-up lives
// behind it: wifi, clock, bedtime, haptics, updates, version, start over.
// The SND panel's centred SETUP door and the RESET pin both retired into it.
// Drawn into the scene sprite like the dance chip, so it dims with the room
// and vanishes under the paused banner (paused locks the scene anyway — the
// pin row is still the only live surface while paused, unchanged in P2).
// Overlap audit: DANCE right edge 159 vs gear left 166 = 7px; gear right 192
// vs the wifi glyph at 196 = 4px. No emoji font exists, so the gear is a
// W-061-style vector: hub, rim, six teeth. Calm on purpose — no badge, no
// pulse; it is a door, not an invitation (charter R5).
static const int GEAR_BX = 166, GEAR_BY = 4, GEAR_BW = 26, GEAR_BH = 24;
// NO chip box (Jon 8/14: "the pause and settings icons at the top stand out
// like a sore thumb"). A filled paper rectangle with a hard border is UI
// chrome pasted onto a hand-drawn room; the mark alone, in soft ink, is a
// pencil note on the wall. The touch rect is unchanged — only the paint is
// quieter, so nothing about reachability moves.
static void drawGearChip() {
  // A soft grey pad, not a paper card (Jon 8/14: "put a light grey box around it"). C_BONE_LO
  // is one step off the room's own ground, so it reads as a place to press without the hard
  // paper-and-ink contrast that made the old chip shout.
  scene.fillRoundRect(GEAR_BX, GEAR_BY, GEAR_BW, GEAR_BH, 5, C_BONE_LO);
  int cx = GEAR_BX + GEAR_BW / 2, cy = GEAR_BY + GEAR_BH / 2;
  for (int i = 0; i < 6; i++) {
    float a = i * (PI / 3.0f) + 0.52f;   // teeth off-axis so none hides in the rim
    scene.drawLine(cx + (int)(cosf(a) * 4), cy + (int)(sinf(a) * 4),
                   cx + (int)(cosf(a) * 8), cy + (int)(sinf(a) * 8), C_INK_SOFT);
  }
  scene.drawCircle(cx, cy, 5, C_INK_SOFT);
  scene.drawCircle(cx, cy, 2, C_INK_SOFT);
}

// ---- the PAUSE chip (menu redesign P3, RED FLAG R2) ----
// PAUSE used to be the first of three 76x14 pins along the bottom edge, and "the pin row is
// the one surface that stays live while paused" WAS the resumability guarantee. The pin row
// is gone, so that guarantee moves here, in this same commit: the chip's rect is now the
// pause whitelist (see the touch dispatch). Miss that and the device cannot be resumed.
//
// Drawn into the scene sprite like DANCE and the gear, but LAST — after drawPausedBanner()'s
// dim pass — so the one control that can un-pause the device never sits at half brightness.
// Everything else on the chip row dims with the room, exactly as before.
// Overlap audit: PAUSE 6..55 vs DANCE left 81 = 25px; the chip row's bottom edge is y28,
// which is the number the lifted pet's ear clearance is measured against.
// ORIGINAL PLACEMENT, RESTORED (Jon 8/14: "the time is now missing or behind the pause
// button. lets go back to the original placement"). Putting the pad on the clock could never
// have worked in this render order: the digits go into the sprite early, with the room, and
// every chip is composed LAST so it sits on top of the scene — so the pad painted straight
// over the time. The chip is back in its own corner, keeping only the part that was actually
// asked for: the soft grey pad instead of the loud paper card.
// Trimmed to the time it wraps (Jon 8/14: "trim the box around pause"). "12:45" at font1 is
// 30px and the digits are 8 tall, so 38x16 leaves 4px of padding all round — a pad that fits
// its contents instead of a chip with air in it. The touch zone keeps its ±4/±3 slop, so the
// target a finger has to find is unchanged even though the paint shrank.
static const int PAUSE_BW = 38, PAUSE_BH = 16;
// Centred on the clock's column (Jon 8/14: "move the pause button over just centered on the
// time"), staying on the chip row where it covers nothing. The cat clock's shelf moves with
// the phase, so the x is computed; one function owns it so the drawn pad and the pause
// whitelist can never disagree about where the resume control is (R2).
// Where drawClock() puts its digits — shared so the chip can tell whether the time will
// land inside it (see drawPauseChip) without either copy drifting.
static int clockDigitsY() {
  const ItemSpot &sp = (S.phase == PH_BABY) ? CATCLOCK_BABY
                     : (S.phase == PH_TEEN) ? CATCLOCK_TEEN : CATCLOCK_ADULT;
  int y = (int)(sp.y * VIEW + 0.5f) - 10;
  return y < 1 ? 1 : y;
}
static int pauseChipX() {
  const ItemSpot &sp = (S.phase == PH_BABY) ? CATCLOCK_BABY
                     : (S.phase == PH_TEEN) ? CATCLOCK_TEEN : CATCLOCK_ADULT;
  int x = (int)(sp.x * VIEW + 0.5f) - PAUSE_BW / 2;
  if (x < 2) x = 2;
  if (x > SCENE_W - PAUSE_BW - 2) x = SCENE_W - PAUSE_BW - 2;
  return x;
}
// PINNED TO THE CHIP ROW (Jon 8/14: "for teen we need to make sure the pause is at the top
// aligned with the other top items"). Tracking the digits put it wherever the cat clock's
// shelf happened to hang — fine on the adult, but the teen's clock sits low enough that the
// pad fell out of the row entirely. It shares y4 with DANCE and the gear now, so the three
// read as one line; only its X still follows the clock's column. The cat clocks were each
// nudged down far enough that no phase's digits reach back up into this row.
static int pauseChipY() { return 4; }
static void drawPauseChip() {
  const int PAUSE_BX = pauseChipX();
  const int PAUSE_BY = pauseChipY();
  // PLAY while paused, PAUSE while running — the same two words the pin wore, and the same
  // two words the paused banner tells you to press ("press PLAY to carry on").
  // RESTING: the word alone in soft ink — a pencil note, not a button (Jon
  // 8/14, "sore thumb"). PAUSED: the full orange chip comes back, loud and
  // undimmed, because it is the only way out and the banner points at it.
  scene.setTextFont(1);
  scene.setTextSize(1);
  scene.setTextDatum(MC_DATUM);
  if (g_paused) {
    scene.fillRoundRect(PAUSE_BX, PAUSE_BY, PAUSE_BW, PAUSE_BH, 4, C_ORANGE);
    scene.drawRoundRect(PAUSE_BX, PAUSE_BY, PAUSE_BW, PAUSE_BH, 4, C_INK);
    scene.setTextColor(C_PAPER);
  } else {
    // the same soft grey pad the gear wears — a place to press, not a shout (Jon 8/14).
    // A one-pixel edge in the next tone up: without it the pad vanishes on the paler walls,
    // which is the other half of why the baby room looked like it had no button at all.
    scene.fillRoundRect(PAUSE_BX, PAUSE_BY, PAUSE_BW, PAUSE_BH, 4, C_BONE_LO);
    scene.drawRoundRect(PAUSE_BX, PAUSE_BY, PAUSE_BW, PAUSE_BH, 4, C_BONE_EDGE);
    scene.setTextColor(C_INK);
  }
  // The word is back: the pad sits above the digits now, so there is nothing to collide with.
  // "PAUSE" font1 = 30px inside 38; "PLAY" = 24px — centred, neither reaches the border.
  scene.drawString(g_paused ? "PLAY" : "PAUSE", PAUSE_BX + PAUSE_BW / 2,
                   PAUSE_BY + PAUSE_BH / 2, 1);
  scene.setTextDatum(TL_DATUM);
}
// The chip's touch zone, with the same slop the dance chip and gear get. Shared by the
// pause whitelist and the chip's own handler so the two can never disagree about where
// the one un-pausing control lives.
static inline bool pauseChipHit(int x, int y) {
  int bx = pauseChipX(), by = pauseChipY();
  return x >= bx - 4 && x < bx + PAUSE_BW + 4 &&
         y >= by - 4 && y < by + PAUSE_BH + 4;
}

// ---------------- ambient events ----------------
// The HTML's weighted scheduler: one beat every ~45s. Weights are time-of-day aware, since
// with a single static weight the two night-only beats sat at ~4% around the clock and you'd
// wait many minutes to see one. flop/groom/gaze are weight 0 there too â€” they borrowed poses
// that read as something else, and a wrong signal is worse than no event.
enum { FX_MOTE, FX_FLY, FX_STAR };
struct Fx { uint8_t kind; float life, dur, x, y, vx, vy, ph; };
static Fx g_parts[24];
static int g_partN = 0;
static uint32_t g_nextEvt = 0;

static void fxAdd(uint8_t k, float dur, float x, float y, float vx, float vy) {
  if (g_partN >= 24) return;
  Fx &f = g_parts[g_partN++];
  f.kind = k; f.life = 0; f.dur = dur; f.x = x; f.y = y; f.vx = vx; f.vy = vy;
  f.ph = (esp_random() % 628) / 100.0f;
}
static bool startMotes() {
  // dust in the lamp beam, seeded along the cone rather than in a box above the floor
  if (lampLevel() < 0.05f) return false;
  for (int i = 0; i < 14; i++) {
    float t = 0.15f + (esp_random() % 78) / 100.0f;
    float cx = 222 * VIEW + (236 - 222) * VIEW * t;
    float half = (6 + t * (96 - 6)) * VIEW;
    fxAdd(FX_MOTE, 7 + (esp_random() % 500) / 100.0f,
          cx + ((int)(esp_random() % 200) - 100) / 100.0f * half,
          (54 + (FLOOR_Y + 10 - 54) * t) * VIEW,
          ((int)(esp_random() % 200) - 100) / 100.0f, -1.0f - (esp_random() % 100) / 100.0f);
  }
  return true;
}
static bool startFirefly() {
  if (nightAmount() < 0.5f) return false;
  for (int i = 0; i < 3; i++)
    fxAdd(FX_FLY, 12 + (esp_random() % 500) / 100.0f,
          (90 + esp_random() % 140) * VIEW, (FLOOR_Y - 70 + esp_random() % 50) * VIEW,
          0, 0);
  return true;
}
static bool startStar() {
  if (nightAmount() < 0.6f || !g_haveSky) return false;
  fxAdd(FX_STAR, 1.5f, g_skyX0 + (esp_random() % max(1, (g_skyX1 - g_skyX0) / 2)),
        g_skyY0 + 3 + esp_random() % 8, 46 * VIEW, 16 * VIEW);
  return true;
}

// ---- rain ----
// A shower once an hour, as specified: it starts on the hour and runs 40-65s. Drawn only
// over the sky, so it reads as weather outside the window rather than indoors.
static uint32_t g_rainUntil = 0;
static uint32_t g_rainStart = 0;
static uint32_t g_nextRainAt = 0;
static int g_lastRainHour = -1;
static struct { float x, y, sp; } g_drops[18];
static bool g_dropsInit = false;
static bool isRaining() { return millis() < g_rainUntil; }
// 0..1 with ~3s ramps at both ends, so the overcast gloom rolls in with the shower and lifts
// as it passes instead of snapping on one frame. Everything visual about rain (room dimming,
// grey sky, grey clouds) reads this rather than the raw boolean.
static float rainAmount() {
  if (!isRaining()) return 0.0f;
  uint32_t now = millis();
  float in  = (now - g_rainStart) / 3000.0f;
  float out = (g_rainUntil - now) / 3000.0f;
  float a = min(in, out);
  return constrain(a, 0.0f, 1.0f);
}

// ---- weather afterglow (W-003: variety, not frequency) ----
// The council's scope, verbatim: keep shower cadence, add a post-shower puddle
// that dries, an occasional daytime rainbow, and a tap-the-window ripple
// during rain. No thunder in v1 (Piper: "ABSOLUTELY NOT after dark").
// Everything here is sky-gated pixel blending in the same per-scanline pass
// the drops/stars/clouds already use — row-interval math, no allocations, and
// nothing runs at all outside its little time window.
static uint32_t g_puddleUntil = 0;
static uint32_t g_rainbowStart = 0, g_rainbowUntil = 0;
static struct { int x, y; uint32_t start; } g_ripples[2];
static uint32_t g_rippleSaidAt = 0; // one ticker line per minute, not per tap
// (comment corrected per council 8/7 Item 3: the code always did one per
// minute; the old "per shower burst" wording was the paper being wrong.
// Known quirk, in the minutes so it never gets "discovered": the same rate
// limit means no ripple line in the first 60s after boot.)

#define PUDDLE_DRY_MS 150000  // ~2.5 min: long enough to notice, gone by the next shower
#define RIPPLE_MS 900

static void weatherShowerEnded(void) {
  g_puddleUntil = millis() + PUDDLE_DRY_MS;
  // A rainbow is an event, not a fixture: daytime only, under half the time,
  // and it fades on its own. nightAmount() gates the trigger; the draw also
  // scales by daylight so a shower ending AT dusk can't paint colours on a
  // dark sky.
  if (nightAmount() < 0.35f && (esp_random() % 100) < 40) {
    g_rainbowStart = millis();
    g_rainbowUntil = g_rainbowStart + 28000 + esp_random() % 14000;
    say("a rainbow!");
  }
}

static bool weatherRippleStart(int x, int y) {
  if (!isRaining() || !g_haveSky) return false;
  if (x < g_skyX0 - 3 || x > g_skyX1 + 3 || y < g_skyY0 - 3 || y > g_skyY1 + 3)
    return false;
  // Two slots so quick little fingers get two rings; a third tap recycles the
  // older one.
  int slot = (g_ripples[0].start <= g_ripples[1].start) ? 0 : 1;
  g_ripples[slot].x = constrain(x, g_skyX0, g_skyX1);
  g_ripples[slot].y = constrain(y, g_skyY0, g_skyY1);
  g_ripples[slot].start = millis();
  if (millis() - g_rippleSaidAt > 60000) {
    g_rippleSaidAt = millis();
    say("ripples race across the glass");
  }
  return true;
}

// 0..1 envelope for the rainbow: 4s in, 6s out, flat in between.
static float rainbowAmount(void) {
  uint32_t now = millis();
  if (!g_rainbowUntil || now >= g_rainbowUntil) return 0.0f;
  float in  = (now - g_rainbowStart) / 4000.0f;
  float out = (g_rainbowUntil - now) / 6000.0f;
  float a = min(in, out);
  return constrain(a, 0.0f, 1.0f);
}

// HOW WET THIS ROOM IS - not whether it is raining. The scene sets a CHARACTER; the device keeps
// rolling its own dice on its own timer, because a bunbun that rained forever because someone
// saved a scene with RAIN on would be a broken bunbun. 1.0 = the shipped cadence.
static float rainGapK() {
  const SceneEnv *e = sceneEnv();
  if (!e || e->rainW < 0) return 1.0f;
  if (e->rainW == 0) return 0.0f;         // a room that never rains
  if (e->rainW == 2) return 0.35f;        // showers about three times as often
  if (e->rainW == 3) return 0.15f;        // very wet
  return 1.0f;
}

static void updateRain(float dt) {
  int h = clockNowMin() / 60;
  // The browser seeds lastRainHour to -1, so the first tick after load always rains. That is
  // invisible in a tab you never reload, but this reboots on every flash and every wake — so
  // it rained at every startup. Seed from the current hour instead and let the NEXT change
  // decide. It is also a roll rather than a certainty now: an hourly shower, guaranteed, was
  // more weather than an ambient room wants.
  // Showers roll through on their own timer rather than on the hour. The HTML fired once an
  // hour, which meant you could watch for a long time and never see weather.
  (void)h;
  // The scene's wetness only ever stretches or shrinks the GAPS between showers. The shower
  // itself, and when it starts, stay the device's own roll. gk == 1.0f leaves both sums bit for
  // bit what they were, so a device with no scene keeps exactly the shipped cadence.
  float gk = rainGapK();
  if (!g_nextRainAt)
    g_nextRainAt = millis() + (uint32_t)((60000 + esp_random() % 240000) * gk);
  // Shower-end edge: the afterglow (puddle, maybe a rainbow — W-003) begins
  // the moment the rain stops, not on a timer of its own.
  // It stays ABOVE the "never rains" return below on purpose: a dry scene arriving mid-shower
  // must still get its puddle and its rainbow, not have them swallowed.
  static bool s_wasRaining = false;
  if (s_wasRaining && !isRaining()) weatherShowerEnded();
  s_wasRaining = isRaining();
  if (gk <= 0.0f) {                       // "never rains" - but let a shower in flight finish
    if (!isRaining()) { g_rainAudio = false; return; }
  }
  if (!isRaining() && millis() >= g_nextRainAt) {
    g_rainStart = millis();
    g_rainUntil = millis() + 45000 + esp_random() % 40000;   // 45-85s of rain
    g_nextRainAt = g_rainUntil + (uint32_t)((420000 + esp_random() % 360000) * gk);  // then 7-13 min
    g_rainbowUntil = 0;            // fresh overcast owns the sky; no leftover bow
    say("rain patters on the window");
  }
  // the mixer fades the hiss in and out on this flag; silent in the arcade
  // (Jon 8/13: "I was playing and I heard the rain")
  g_rainAudio = isRaining() && !gamesLive();
  if (!isRaining() || !g_haveSky) return;
  if (!g_dropsInit) {
    for (int i = 0; i < 18; i++) {
      g_drops[i].x = g_skyX0 + esp_random() % max(1, g_skyX1 - g_skyX0);
      g_drops[i].y = g_skyY0 + esp_random() % max(1, g_skyY1 - g_skyY0);
      g_drops[i].sp = 26 + esp_random() % 18;
    }
    g_dropsInit = true;
  }
  for (int i = 0; i < 18; i++) {
    g_drops[i].y += g_drops[i].sp * dt;
    g_drops[i].x += g_drops[i].sp * 0.22f * dt;      // slight slant
    if (g_drops[i].y > g_skyY1 || g_drops[i].x > g_skyX1) {
      g_drops[i].x = g_skyX0 + esp_random() % max(1, g_skyX1 - g_skyX0);
      g_drops[i].y = g_skyY0;
    }
  }
}
// ENVIRONMENT-SPEC layer 2: the sun follows the day, the moon follows the night. The
// position is a pure function of the wall clock across the sky shape - rise on its left
// at 6:00, arc, set on its right at 20:00, exactly the edges daylight() uses; the moon
// mirrors across 20:00-6:00. Drawn from the pak sprite's own RLE rows, gated by the same
// sky rules as clouds, UNDER stars and weather.
static const PakEntry *g_celEntry[2] = {nullptr, nullptr};
static uint32_t g_celGen = 0;
static bool celestialPos(uint8_t track, const PakEntry *e, int *tx, int *ty) {
  if (!g_haveSky || !e) return false;
  int m = clockNowMin();
  float frac;
  if (track == 1) {
    if (m < 360 || m > 1200) return false;
    frac = (m - 360) / 840.0f;
  } else {
    if (m >= 1200) frac = (m - 1200) / 600.0f;
    else if (m < 360) frac = (m + 240) / 600.0f;
    else return false;
  }
  int spanX = g_skyX1 - g_skyX0 - (int)e->w; if (spanX < 0) spanX = 0;
  int spanY = g_skyY1 - g_skyY0 - (int)e->h; if (spanY < 0) spanY = 0;
  *tx = g_skyX0 + (int)(frac * spanX);
  *ty = g_skyY1 - (int)e->h - (int)(sinf(frac * (float)M_PI) * spanY);
  return true;
}
static void drawCelestial(const uint8_t *rowIdx, uint16_t *d, int ay) {
  if (!g_scCelN) return;
  if (g_celGen != g_scGen) {              // resolve pak entries once per scene
    for (int i = 0; i < 2; i++) g_celEntry[i] = nullptr;
    for (int i = 0; i < g_scCelN; i++) g_celEntry[i] = pakFind(g_scCel[i].s);
    g_celGen = g_scGen;
  }
  for (int c = 0; c < g_scCelN; c++) {
    const PakEntry *e = g_celEntry[c];
    int tx, ty;
    if (!e || e->fmt != 0 || !celestialPos(g_scCel[c].t, e, &tx, &ty)) continue;
    int r = ay - ty;
    if (r < 0 || r >= (int)e->h) continue;
    // walk the RLE rows up to r (rows are variable length; h is small, this is cheap)
    uint32_t off = e->offset;
    uint8_t segs; uint8_t hdr[2];
    for (int row = 0; row < r; row++) {
      pakRead(off, &segs, 1); off += 1;
      for (int sgi = 0; sgi < segs; sgi++) {
        pakRead(off, hdr, 2);
        off += 2 + (uint32_t)hdr[1] * 2;
      }
    }
    pakRead(off, &segs, 1); off += 1;
    int x = tx;
    static uint16_t px[240];
    for (int sgi = 0; sgi < segs; sgi++) {
      pakRead(off, hdr, 2); off += 2;
      x += hdr[0];
      int len = hdr[1];
      if (len > 240) len = 240;
      pakRead(off, px, (size_t)len * 2); off += (uint32_t)hdr[1] * 2;
      for (int k = 0; k < len; k++, x++) {
        if (x < 0 || x >= UI_W) continue;
        if (g_skyAuthored) {
          if (x < g_skyX0 || x > g_skyX1) continue;
          if (!skyPolyHit(x, ay)) continue;
          if (g_skyBoxMasked && !g_isSky[rowIdx[x]]) continue;
        } else if (!g_isSky[rowIdx[x]]) continue;
        d[x] = px[k];
      }
    }
  }
}

static void drawEnvStars(uint16_t *d, int ay) {
  if (!g_envStarN) return;
  float night = 1.0f - daylight();
  if (night < 0.25f) return;
  uint8_t tbase = (uint8_t)(millis() >> 5);
  for (int i = 0; i < g_envStarN; i++) {
    int dy = ay - (int)g_envStars[i].y;
    if (dy < -1 || dy > 1) continue;
    bool big = g_envStars[i].big;
    if (dy != 0 && !big) continue;
    uint8_t t = (uint8_t)(tbase + g_envStars[i].ph);
    int tri = t < 128 ? t : 255 - t;
    float k = night * (0.35f + 0.65f * tri / 127.0f);
    int x = g_envStars[i].x;
    int a0 = (dy == 0 && big) ? -1 : 0, a1 = (dy == 0 && big) ? 1 : 0;
    for (int a = a0; a <= a1; a++) {
      int xx = x + a;
      if (xx < 0 || xx >= UI_W) continue;
      float ka = (a == 0 && dy == 0) ? k : k * 0.45f;
      uint16_t v = d[xx];
      int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
      r += (int)((28 - r) * ka); g += (int)((60 - g) * ka); b += (int)((30 - b) * ka);
      d[xx] = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }
}

static void drawRain(const uint8_t *rowIdx, uint16_t *d, int ay) {
  if (!isRaining() || !g_haveSky) return;
  if (ay < g_skyY0 || ay > g_skyY1) return;
  for (int i = 0; i < 18; i++) {
    if ((int)g_drops[i].y != ay) continue;
    int x = (int)g_drops[i].x;
    if (x < 0 || x >= UI_W) continue;
    if (g_skyAuthored) {
      if (x < g_skyX0 || x > g_skyX1) continue;
      if (!skyPolyHit(x, ay)) continue;
      if (g_skyBoxMasked && !g_isSky[rowIdx[x]]) continue;      // frames occlude
    } else if (!g_isSky[rowIdx[x]]) continue;
    uint16_t v = d[x];
    int r = ((v >> 11) & 0x1F), g = ((v >> 5) & 0x3F), b = (v & 0x1F);
    r += (24 - r) / 2; g += (48 - g) / 2; b += (31 - b) / 2;
    d[x] = (uint16_t)((r << 11) | (g << 5) | b);
  }
}

static void drawWeatherAfterglow(const uint8_t *rowIdx, uint16_t *d, int ay) {
  if (!g_haveSky || ay < g_skyY0 || ay > g_skyY1) return;
  uint32_t now = millis();

  // Ripples: expanding rings on the glass while it rains. Ring test is done
  // in squared distances — no sqrt in the pixel loop.
  if (isRaining()) {
    for (int ri = 0; ri < 2; ri++) {
      if (!g_ripples[ri].start) continue;
      uint32_t age = now - g_ripples[ri].start;
      if (age >= RIPPLE_MS) { g_ripples[ri].start = 0; continue; }
      float t = age / (float)RIPPLE_MS;
      float rad = 4.0f + t * 22.0f;
      int dy = ay - g_ripples[ri].y;
      int inner = (int)((rad - 1.3f) * (rad - 1.3f));
      int outer = (int)((rad + 1.3f) * (rad + 1.3f));
      if (dy * dy > outer) continue;
      int k = (int)((1.0f - t) * 150);          // ring fades as it grows
      int x0 = max(g_skyX0, g_ripples[ri].x - (int)rad - 2);
      int x1 = min(g_skyX1, g_ripples[ri].x + (int)rad + 2);
      for (int x = x0; x <= x1; x++) {
        int dx = x - g_ripples[ri].x;
        int dd = dx * dx + dy * dy;
        if (dd < inner || dd > outer) continue;
        if (!g_isSky[rowIdx[x]]) continue;
        uint16_t v = d[x];
        int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        r += ((24 - r) * k) >> 8; g += ((48 - g) * k) >> 8; b += ((31 - b) * k) >> 8;
        d[x] = (uint16_t)((r << 11) | (g << 5) | b);
      }
    }
  }

  // Rainbow: three concentric arcs over the pane, centred just below the
  // sill so only the top of the bow shows. Clouds draw after this and drift
  // across it, which reads as depth for free.
  float rba = rainbowAmount() * (1.0f - nightAmount());
  if (rba > 0.02f && !isRaining()) {
    int bw = g_skyX1 - g_skyX0 + 1;
    int cx = (g_skyX0 + g_skyX1) / 2, cy = g_skyY1 + 6;
    int R = (int)(bw * 0.46f);
    int dy = ay - cy; // negative above the centre; dy² is what matters
    // band radii: [R-1..R+1] red, [R-3..R-1] green, [R-5..R-3] violet
    int r2hi = (R + 1) * (R + 1), r2lo = (R - 5) * (R - 5);
    if (dy * dy <= r2hi) {
      int k = (int)(rba * 80); // deliberately faint — weather, not wallpaper
      static const int bandR[3] = {30, 14, 14};
      static const int bandG[3] = {28, 52, 24};
      static const int bandB[3] = {6, 10, 28};
      int x0 = max(g_skyX0, cx - R - 1), x1 = min(g_skyX1, cx + R + 1);
      for (int x = x0; x <= x1; x++) {
        int dx = x - cx;
        int dd = dx * dx + dy * dy;
        if (dd > r2hi || dd < r2lo) continue;
        int band;
        if (dd >= (R - 1) * (R - 1)) band = 0;
        else if (dd >= (R - 3) * (R - 3)) band = 1;
        else band = 2;
        if (!g_isSky[rowIdx[x]]) continue;
        uint16_t v = d[x];
        int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        r += ((bandR[band] - r) * k) >> 8;
        g += ((bandG[band] - g) * k) >> 8;
        b += ((bandB[band] - b) * k) >> 8;
        d[x] = (uint16_t)((r << 11) | (g << 5) | b);
      }
    }
  }

  // Puddle: a wet sheen pooled on the outside sill (the bottom two sky rows),
  // shrinking from the edges as it dries. Subtle shimmer so it reads as water
  // rather than paint.
  if (g_puddleUntil && now < g_puddleUntil && ay >= g_skyY1 - 1) {
    float wet = (g_puddleUntil - now) / (float)PUDDLE_DRY_MS;
    int bw = g_skyX1 - g_skyX0 + 1;
    int half = (int)(bw * 0.5f * wet);
    if (half > 1) {
      int cx = (g_skyX0 + g_skyX1) / 2;
      int x0 = max(g_skyX0, cx - half), x1 = min(g_skyX1, cx + half);
      int base = (int)(70 * wet);
      for (int x = x0; x <= x1; x++) {
        if (!g_isSky[rowIdx[x]]) continue;
        int k = base + (int)(20.0f * sinf(now / 350.0f + x * 0.6f));
        if (k <= 0) continue;
        uint16_t v = d[x];
        int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        r += ((20 - r) * k) >> 8; g += ((44 - g) * k) >> 8; b += ((31 - b) * k) >> 8;
        d[x] = (uint16_t)((r << 11) | (g << 5) | b);
      }
    }
  } else if (g_puddleUntil && now >= g_puddleUntil) {
    g_puddleUntil = 0; // dried
  }
}

// ---- the visitor ----
// The two-beat design: something lands on the sill, THEN bunbun notices and goes to watch.
// A bird by day, a firefly after dark.
static float g_birdT = 0;
static float g_birdNotice = 0;
static const int WINDOW_WATCH_X = 145, WINDOW_WATCH_Y = FLOOR_Y - 4;

// ---- THE CAT WHO VISITS (Jon 8/14) ----
// A guest, not a prop: she wanders in from the right, waits to be noticed, gets a fuss from
// bunbun, then takes herself to the armchair, sleeps a good while, wakes, has a long stretch,
// and lets herself out. Built on the bird's proven chained-reaction shape (arrive -> he
// notices -> he crosses the room -> the beat -> it ends) because that is the one interaction
// in this house that already reads as unforced. This also answers a wish on the shelf —
// 5DC0's "friends who come and visit... ambient creatures that walk through the screen".
//
// Phases: 0 none | 1 walking in | 2 waiting to be noticed | 3 being petted |
//         4 crossing to the chair | 5 asleep | 6 stretching | 7 leaving
static uint8_t g_catPhase = 0;
static float g_catT = 0, g_catX = 0, g_catY = 0;
static float g_catTx = 0;                  // where she is heading, in room x
static bool g_catPetted = false;
static uint32_t g_catWakeAt = 0;
static bool g_catFaceE = false;            // she is facing east, so her west-facing art mirrors
static bool g_catFromLeft = false;         // which door she came in by, so she leaves the same side
static float g_catHold = 0;                // how long she is staring at nothing
static int  g_catKnock = -1;               // the loose thing she is on her way to push off
static bool g_catChecked = false;          // her spot has been looked at for this visit
// HER SPEEDS, from the builder — CAT_POTTER 30, CAT_TROT 40, CAT_OFF 58. A 1.9x spread, and the
// point of it is that having somewhere to be is visibly FASTER than pottering. The device had it
// inverted: purposeful movement at 22, aimless at 30, and the exit — the builder's most decisive
// move — at half speed.
#define CAT_POTTER 30.0f
#define CAT_TROT   40.0f
#define CAT_OFF    58.0f

// SHE LIVES IN A LANE, like he does. Her y was the constant FLOOR_Y+4 = 204 on every walking
// frame, while Jon's floor polygon runs 206..238 — so she walked ABOVE the drawn floor, through
// all three keep-outs, and the depth sort against the pet could never fire: dy = 204 - (209..237)
// is always negative, so `g_catInFront` was false for the life of the device and the whole
// stencil branch was dead. The builder seeds `c.y = c.home = R(214,226)` and picks a fresh lane
// for every roam goal.
static float g_catHomeY = FLOOR_Y + 4.0f;
static void catEaseLane(float dt) {
  g_catY += (g_catHomeY - g_catY) * fminf(1.0f, dt * 2.4f);
}

static int  g_catToy = -1;                 // the ball she is playing with
static int  g_catPlayDir = 0;              // which way she is hitting it; 0 = not decided yet
static int  g_catBats = 0;                 // how many times she has hit it this visit
static bool g_catInFront = false;          // depth sort against the pet; see the note at drawCat


// ---- HER CLIPS, WHICH ARE THE BUILDER'S CLIPS ----
// Jon: "the cat is not the same as the builder, only use those animations and sprites".
// These are exactly what CAT_ART (placer.html:618) resolves to, packed out of the builder's own
// PIX pool, frame counts and fps included. Two rules come with them, both stated in the builder
// and both confirmed by rendering all 72 frames and looking at them:
//
//   1. EVERY CLIP FACES WEST. Walking east is the same art mirrored — which is also how one
//      swat clip gives a west and an east swipe. Hence g_blitFlip.
//   2. FRAME 0 IS THE SHARED TRAY IMAGE. framesOf() prepends it, so `sit` reads as a transition
//      — stand, then fold down, then hold — rather than snapping into the pose. Not a quirk to
//      fix: it is what makes them look right.
//
// HER SIZE IS A CONSTANT, not something derived per frame. The old code normalised the scale by
// the TRIMMED height of whatever still was loaded, which holds one image at a fixed size — fine
// for two stills, fatal for a clip, because the trim changes every frame (39 to 55 rows across
// these) and she would have pulsed. The builder never did that: it draws the whole 64x64 canvas
// at the asset's own `k` and lets the height fall out of the art. This is that same k, from
// ASSETS['cat-*']. VIEW converts room units to the screen, where spriteBlitDirect works.
static const float CAT_K = 0.6226415f;
// AND HER ANCHOR IS HER FEET, which is the other half of that asset record and was missing.
// The builder draws every object as `drawImage(img, x-dw/2, round(it.y + pad*kh) - dh, dw, dh)`:
// the canvas BOTTOM lands at it.y + pad*k, so the anchor sits pad*k above the bottom of the
// canvas — an object's anchor is its FEET, as its own comment says. ASSETS reports every cat
// clip as w=64 h=64 k=0.6226415 pad=6, one record for all nine, and that k is this CAT_K.
// The device instead put the canvas CENTRE at (g_catX, g_catY), which is 26 canvas px — 16 room
// px — below where the builder puts it. Measured on 6D1C: with the sim reporting cat_y 220 (the
// floor line, FLOOR_Y+4) her paws rendered at room y 232, sixteen pixels out into the room,
// while bunbun reporting bun_y 220 rendered his feet at exactly 220.
// That one number is both of Jon's complaints at once. She stood too near the camera — "the cat
// is near the front of the room" — AND the depth sort below, which is feet-vs-feet in the
// builder, was comparing her mid-body against his feet, so he won it while she was visibly
// closer: "bunbun shows up in front". The sim was never wrong; it has always set g_catY to a
// surface (FLOOR_Y+4, a perch's y, a rung's y). Only the draw disagreed with it.
static const float CAT_PAD = 6.0f;

struct CatClip { const char *folder; uint8_t frames; float fps; bool hold; };
static const CatClip CAT_CLIP[] = {
  {"cat-walk",    9, 10.0f, false},   // 0 walking
  {"cat-sit",     9,  6.0f, true },   // 1 waiting to be noticed: stand, then sit, then stay sat
  {"cat-petted",  7,  5.0f, false},   // 2 being made a fuss of
  {"cat-sleep",   4,  2.0f, false},   // 3 asleep
  {"cat-stretch", 9,  7.0f, true },   // 4 waking
  {"cat-look",    7,  6.0f, true },   // 5 having a look round
  {"cat-swat",    9,  8.0f, true },   // 6 the paw going in
  {"cat-jump",    9,  9.0f, true },   // 7 the hop up, and the drop down
  // THE MISSING IN-BETWEEN. cat-lie was packed and never played: the device had 8 of the
  // builder's 9 clips. It is the best-animated of the set — the silhouette genuinely folds to
  // the floor over frames 4-6 — and its last three frames are pixel-identical to the first
  // three of cat-sleep, so it was authored to hand straight into the sleep loop. Without it she
  // snapped from standing to curled in one frame, and back again on waking.
  {"cat-lie",     9,  7.0f, true },   // 8 settling down, and (reversed) getting up
};
enum { CC_WALK = 0, CC_SIT, CC_PETTED, CC_SLEEP, CC_STRETCH, CC_LOOK, CC_SWAT, CC_JUMP, CC_LIE };
// ---- A RESTART REMEMBERS WHAT WAS GOING ON ----
// Jon: "on restart the cat just teleports in from the left" ... "all actions should be
// procedural ... except a restart remembers what was going on and comes back to where it was
// before". Everything she does is decided in the room from seeded urges, and that is the point;
// a REBOOT is the one thing that is not part of the story and must not be allowed to narrate.
// Without this the cat state is simply gone at boot, g_nextCatAt is re-armed 2-6s later and she
// starts a brand new visit at the door — so a unit that was flashed while she slept on the shelf
// came back with her walking in again, which reads as a teleport and throws away a whole visit.
//
// RTC slow memory, not NVS. It survives a software reset and an OTA reboot — which is every
// restart that happens while she is mid-visit — costs no flash wear, and is deliberately LOST on
// a true power cut, where starting the morning fresh is the honest answer. Same mechanism as the
// panic breadcrumb above; note this is the SoC's own RTC domain, not the removed RTC chip.
#define CATKEEP_MAGIC 0xCA71FE11u
RTC_NOINIT_ATTR struct {
  uint32_t magic;
  int16_t  phase, x, y, homeY, spotX, spotY, roamGoal, tx;
  int16_t  rx[4], ry[4];         // CAT_MAX_STEPS, which is defined below this point
  float    urge[3], rungT;
  uint32_t sleepLeftMs;          // how much of her nap was still owed
  uint8_t  faceE, step, routeN, clip, fromLeft, petted, checked;
  int8_t   toy, playDir, knock;
} g_catKeep;
static bool g_catRestored = false;

static uint8_t g_catClip = CC_WALK;
static float   g_catClipT = 0;
// A clip change restarts the clip: a transition that began halfway through is not a transition.
static void catSetClip(uint8_t c) {
  if (g_catClip == c) return;
  g_catClip = c; g_catClipT = 0;
}
static uint32_t g_nextCatAt = 0;
static inline bool catHere() { return g_catPhase != 0; }
// Jon: "if we have a bathroom / kitchen / work scene the cat should never show up."
static void catDismissIfAway() {
  if (g_scCurRole == SCENE_ROLE_MAIN) return;
  if (g_catPhase) { g_catPhase = 0; g_catPetted = false; }
  g_birdPhase = 0; g_birdReacted = false; g_birdLeaveAt = 0;   // "nor the bird"
}
// Mid-visit and still working on the room: walking in, waiting, being fussed over, crossing to
// her spot, or swiping. Phase 5 (asleep) onward she has settled and he may tidy around her.
static bool catStillBusy() { return g_catPhase != 0 && g_catPhase < 5; }
// The armchair, per room — measured off the same art the furniture spots use. She sleeps ON
// it, so this is the seat, not the floor in front of it.
// Half the sleeping cat's drawn height, in ROOM units. The builder marks a sleeping spot by the
// cat's FEET, the way it anchors everything; spriteBlitDirect anchors by the CENTRE. The device
// also sizes her against the PET rather than against the builder's k, so this cannot be worked
// out on the PC — it has to be asked here, at the size she will actually be drawn.
// catSleepHalfH() lived here: the canvas-centre-to-feet drop, used to shift a child's sleep
// mark. The draw anchors on her feet now, so there is nothing left to convert. See catPickSpot.

// Where this visit's nap happens. Chosen ONCE, in startCat() — this is read every frame while she
// walks, so rolling the dice in here would have her twitching between the shelf and the chair
// instead of walking to either.
static float g_catSpotX = 0, g_catSpotY = 0;
static void catChairSpotDefault(float *x, float *y) {
  if (S.phase == PH_TEEN)      { *x = 232; *y = FLOOR_Y - 2; }   // the beanbag
  else if (S.phase == PH_BABY) { *x = 250; *y = FLOOR_Y - 6; }   // the nursery armchair
  else                         { *x = 246; *y = FLOOR_Y - 6; }   // the farmhouse chair
}
static void catChairSpot(float *x, float *y) { *x = g_catSpotX; *y = g_catSpotY; }

// A scene's own marks win over all three constants above. Those were measured off the
// COMPILED-IN room, where the farmhouse chair stands at x=246 — so the moment a child moves the
// chair, the constant points at furniture that is no longer there, and she sleeps on bare boards
// beside it. Which is exactly what 6D1C showed with Jon's chair at x=207.
static void catPickSpot() {
  if (sceneCatSpot(&g_catSpotX, &g_catSpotY)) {
    // The mark is her feet, and she is NOW DRAWN FROM HER FEET, so it is used as it stands.
    // This line used to read `g_catSpotY -= catSleepHalfH()`, converting the child's mark into
    // the canvas-centre position the old draw wanted. Once the draw was corrected to the
    // builder's anchor (see CAT_PAD) that subtraction became a 16px lift, and Jon saw it
    // immediately: "the cat is now above the table". The arithmetic is exact — his floor mark
    // at y=225 was reporting cat_y 208, and 225 - 16.2 = 208.8.
    // Worth noting WHY it hid for so long: two compensating errors. The draw was 16px low and
    // the mark was moved 16px high, so a marked spot landed right while an UNMARKED one — a
    // perch from SEAT_SRC, which never went through here — was 16px wrong. Fixing one of a
    // compensating pair always looks like a regression.
    return;
  }
  catChairSpotDefault(&g_catSpotX, &g_catSpotY);
}
// Where bunbun stands to reach her — beside her, never on top of her.
static void catPetSpot(int *x, int *y) {
  *x = (int)g_catX - 26;
  *y = (int)g_catY + 4;
  if (*x < 20) *x = 20;
  clearOfBlocks(x, y);
}

// She only calls when the room is calm and someone is home to appreciate it: awake, lights
// on, not mid-errand, not mid-party, not away, and never while he is poorly.
// Straight off the builder's onHerSpot():
//   (c.st==='sleep'||'settle'||'wake'||'getdown') && c.perchClimb && q._home
//   && Math.abs(c.x - q._home.x) < 50     // over the same spot on the floor plan
//   && c.y <= q._home.y + 14              // and level with it or anywhere above it
// Note what it is NOT: it does not wait for her to leave the room. She can be asleep on the
// chair while he stands the jar back up on the table — it is only HER surface he keeps off.
static bool catOnItsSpot(int i) {
  if (!catHere()) return false;
  // OFF THE FURNITURE FIRST. Jon, watching it happen: "bunbun just put the jar on when the cat
  // was there ... he needs to wait until the cat is off the table or shelf."
  // The builder's rule is per-object — she may nap on the chair while he tidies the table — but
  // she is only ever UP on something because she climbed a surface, and putting a jar down
  // beside a sleeping cat is the thing that looked wrong. So while she is off the ground at all,
  // nothing gets tidied. Back on the floor or out of the room, and he gets on with it.
  if (g_catY < FLOOR_Y - 6.0f) return true;
  if (g_catPhase < 4) return false;                  // still walking in, waiting, being fussed
  const SceneProp *home = &g_scProp[g_scLoose[i].prop];
  if (fabsf(g_catX - home->x) >= 50.0f) return false;
  return g_catY <= home->y + 14.0f;                  // smaller y is higher up the room
}

// ================= GETTING UP THERE, AND BACK DOWN =================
// placer.html stagesTo(): the ladder is built FROM THE TARGET DOWNWARDS, "so it is the same route
// wherever she is standing when she works it out". At each rung it wants the highest surface that
// is comfortably within one hop below; failing that, the highest thing under it at all. Four
// rungs maximum, and nothing to step on means honestly unreachable.
//   MAX_HOP 46 — "a cat's hop at this scale - the table is 39px above the floor"
//   GROUND  216 — where a walker's feet are on the floor
#define CAT_MAX_HOP 46.0f
#define CAT_GROUND  216.0f
#define CAT_MAX_STEPS 4
static CatSpot g_catRoute[CAT_MAX_STEPS];   // lowest rung first; the last one is the destination
static int     g_catRouteN = 0;
static int     g_catStep = 0;
// SHE STOPS ON THE WAY. Jon: "the cat needs to knock over the jar, jump to the table, wait 2
// seconds and then jump to the shelf, and then getting down get up, wait then jump to table for
// a second and then the ground."
// This is a deliberate ADDITION, not a port: the builder tweens straight from one rung to the
// next (settle sets c.t=0.9, getdown c.t=0.8) with no dwell in between, so she flowed up two
// storeys in one continuous motion and the table read as a waypoint rather than as somewhere
// she actually stood. A beat on each rung is what makes a climb look considered — she lands,
// takes stock, then commits to the next one. Longer going up than coming down, because going up
// is the decision and coming down is just gravity.
#define CAT_RUNG_UP_S   2.0f
#define CAT_RUNG_DOWN_S 1.0f
static float   g_catRungT = 0;              // >0 = she is stood on a rung, gathering herself

static int catStagesTo(float tx, float ty) {
  g_catRouteN = 0;
  CatSpot steps[CAT_MAX_STEPS];
  int n = 0;
  steps[n++] = (CatSpot){(int16_t)tx, (int16_t)ty};
  for (int guard = 0; guard < CAT_MAX_STEPS - 1; guard++) {
    const CatSpot *cur = &steps[0];
    if (CAT_GROUND - cur->y <= CAT_MAX_HOP) break;      // she can make this one from the floor
    const CatSpot *best = nullptr, *reach = nullptr;
    for (int i = 0; i < g_scPerchN; i++) {
      const CatSpot *q = &g_scPerch[i];
      if (!(q->y > cur->y + 4 && fabsf((float)(q->x - cur->x)) < 56.0f)) continue;
      if (q->y - cur->y <= CAT_MAX_HOP) { if (!reach || q->y > reach->y) reach = q; }
      if (!best || q->y < best->y) best = q;
    }
    if (reach) best = reach;
    if (!best) break;                                    // nothing to step on
    for (int k = n; k > 0; k--) steps[k] = steps[k - 1];  // unshift
    steps[0] = *best;
    n++;
  }
  for (int i = 0; i < n; i++) g_catRoute[i] = steps[i];
  g_catRouteN = n;
  return n;
}

// ================= SHE ARRIVES WITHOUT A PLAN =================
// Jon: "all of this is supposed to be procedural and not mechanical or pre determined movement".
// The builder says the same thing in its own words, and says why it had to change:
//
//   "This block used to choose her sleeping spot at the door - so the visit was decided before
//    she was through it, and every visit came out the same shape however much I varied the
//    middle. She now arrives carrying nothing but URGES and picks what to do from them, in the
//    room, as she goes. The urges are seeded, so the same seed still replays the same visit."
//
// Three appetites, each seeded in its own range and each GROWING at its own rate while it goes
// unanswered. Whenever she is free she takes whichever is strongest AND possible; when none of
// them is pressing any more she lets herself out. Two visits in the same room are different
// visits because the dice differ, not because anything is scripted.
static float g_urge[3], g_urgeRate[3];      // 0 play, 1 rest, 2 look
enum { U_PLAY = 0, U_REST, U_LOOK };
static float g_roamGoal = -1.0f;

static float rndf(float a, float b) { return a + (b - a) * ((esp_random() % 10000) / 10000.0f); }

static void catSeedUrges() {
  g_urge[U_PLAY] = rndf(0.10f, 0.75f);  g_urgeRate[U_PLAY] = rndf(0.010f, 0.030f);
  g_urge[U_REST] = rndf(0.05f, 0.70f);  g_urgeRate[U_REST] = rndf(0.014f, 0.034f);
  g_urge[U_LOOK] = rndf(0.35f, 0.95f);  g_urgeRate[U_LOOK] = rndf(0.012f, 0.032f);
}
static void catGrowUrges(float dt) {
  for (int i = 0; i < 3; i++) {
    g_urge[i] += g_urgeRate[i] * dt;
    if (g_urge[i] > 1.6f) g_urge[i] = 1.6f;
  }
}

// The strongest thing she can actually have, or 7 (out) when nothing is pressing any more.

// SHE IS BACK ON THE FLOOR AND HAS NOT NECESSARILY FINISHED.
// The device had no path from sleep back to a decision: stretch went to descend went to leave,
// unconditionally. So rest — the only urge nothing ever subtracted from, and the fastest-growing
// of the three — won by construction, every visit ended in a nap, and the builder's real ending
// ("she has had enough, and lets herself out", want.v < 0.55) was unreachable. Between a third
// and two thirds of every visit was bit-identical to the last one.
// The builder's wake is: `c.urge.rest = 0; c.urge.look += R(0.15,0.4)` and back to deciding.
static int catDecide();
static void catWokeOnFloor() {
  g_catY = FLOOR_Y + 4;
  g_catRouteN = 0; g_catStep = 0; g_catChecked = false; g_catRungT = 0;
  g_urge[U_REST] = 0;
  g_urge[U_LOOK] += rndf(0.15f, 0.4f);
  if (g_urge[U_LOOK] > 1.6f) g_urge[U_LOOK] = 1.6f;
  g_catPhase = catDecide();
  g_catT = 0;
  g_catClipT = 0;
}
static int catDecide() {
  const int toy = looseToy();
  const bool canRest = (g_scCatN > 0 || g_scPerchN > 0);
  float v[3] = { toy >= 0 ? g_urge[U_PLAY] : -1.0f,
                 canRest  ? g_urge[U_REST] : -1.0f,
                 g_urge[U_LOOK] };
  int best = 0;
  for (int i = 1; i < 3; i++) if (v[i] > v[best]) best = i;
  if (v[best] < 0.55f) return 7;                 // "she has had enough, and lets herself out"

  if (best == U_PLAY) { g_catToy = toy; g_catPlayDir = 0; return 10; }
  if (best == U_REST) { catPickSpot(); g_catRouteN = 0; g_catStep = 0; g_catChecked = false;
                        g_catRungT = 0;
                        return 4; }

  // LOOK: somewhere in the room, chosen now, and gone to for its own sake
  Bounds b = bounds();
  g_roamGoal = (float)(b.x0 + 10 + (int)(esp_random() % (uint32_t)max(1, (b.x1 - b.x0) - 20)));
  { int top, bot;                            // and a lane to walk it in, off the drawn floor
    if (sceneFloorLane((int)g_roamGoal, &top, &bot) && bot - top > 6)
      g_catHomeY = rndf((float)(top + 3), (float)(bot - 1));
  }
  return 13;
}

static bool startCat() {
  // NO DAYLIGHT GATE. There used to be a `nightAmount() > 0.55f` clause here, which works out as
  // a hard window of 06:54 to 18:39 — outside it she could not be called at all, not by her own
  // timer and not by /api/debug/cat. Jon, looking at the shelf in the evening: "i have yet to see
  // the cat show up on the device".
  // The builder is the golden record and it gates her by nothing but the room being available;
  // `!S.lights` already IS the pet's sleep state, so a dark room is covered and the clock clause
  // was a second, blunter gate sitting on top of a working one.
  if (catHere() || !alive() || !S.lights || bunAway() || g_action || g_workStage ||
      discoDown() || S.sick)
    return false;
  if (g_scCurRole != SCENE_ROLE_MAIN) return 0;   // she visits the main room only
  g_catPhase = 1; g_catT = 0; g_catPetted = false;
  // EITHER DOOR, 50/50 — placer.html: `const fromLeft = rnd()<0.5; c.x = fromLeft ? -16 : 336`.
  // The device always came in from the right and always stopped at 214, so with her visiting
  // every 70-130s she was dragging bunbun to the same spot over and over: he goes to her at
  // catX-26, which is why he looked like he was living on the right-hand side of the room.
  g_catFromLeft = (esp_random() & 1) != 0;
  g_catX = g_catFromLeft ? -10.0f : 330.0f;
  g_catHomeY = rndf(214.0f, 226.0f);        // her lane for this visit, per the builder
  g_catY = g_catHomeY;
  g_catTx = g_catFromLeft ? rndf(90.0f, 130.0f) : rndf(196.0f, 236.0f);   // she stops a little inside, whichever door
  // NOTHING IS DECIDED AT THE DOOR. She used to pick her nap spot here; that is precisely the
  // thing the builder took out, because it made every visit the same shape. She arrives with
  // appetites and works it out in the room.
  catSeedUrges();
  g_catClip = CC_WALK; g_catClipT = 0; g_catFaceE = false;
  g_catKnock = -1; g_catChecked = false;
  g_catToy = -1; g_catPlayDir = 0; g_catBats = 0; g_roamGoal = -1.0f;
  g_catRouteN = 0; g_catStep = 0; g_catRungT = 0;
  return true;
}

// Written every frame she is in the room: cheap (a struct store to RTC RAM, no flash) and it
// means whenever the reset happens the last frame is already saved. Cleared when she leaves, so
// a reboot during the gap between visits does not resurrect her.
static void catKeepSave() {
  if (!catHere()) { g_catKeep.magic = 0; return; }
  g_catKeep.magic   = CATKEEP_MAGIC;
  g_catKeep.phase   = (int16_t)g_catPhase;
  g_catKeep.x       = (int16_t)lroundf(g_catX);
  g_catKeep.y       = (int16_t)lroundf(g_catY);
  g_catKeep.homeY   = (int16_t)lroundf(g_catHomeY);
  g_catKeep.spotX   = (int16_t)lroundf(g_catSpotX);
  g_catKeep.spotY   = (int16_t)lroundf(g_catSpotY);
  g_catKeep.roamGoal= (int16_t)lroundf(g_roamGoal);
  g_catKeep.tx      = (int16_t)lroundf(g_catTx);
  for (int i = 0; i < CAT_MAX_STEPS; i++) {
    g_catKeep.rx[i] = (i < g_catRouteN) ? g_catRoute[i].x : 0;
    g_catKeep.ry[i] = (i < g_catRouteN) ? g_catRoute[i].y : 0;
  }
  for (int i = 0; i < 3; i++) g_catKeep.urge[i] = g_urge[i];
  g_catKeep.rungT   = g_catRungT;
  const uint32_t now = millis();
  g_catKeep.sleepLeftMs = (g_catWakeAt > now) ? (g_catWakeAt - now) : 0;
  g_catKeep.faceE   = g_catFaceE ? 1 : 0;
  g_catKeep.step    = (uint8_t)g_catStep;
  g_catKeep.routeN  = (uint8_t)g_catRouteN;
  g_catKeep.clip    = g_catClip;
  g_catKeep.fromLeft= g_catFromLeft ? 1 : 0;
  g_catKeep.petted  = g_catPetted ? 1 : 0;
  g_catKeep.checked = g_catChecked ? 1 : 0;
  g_catKeep.toy     = (int8_t)g_catToy;
  g_catKeep.playDir = (int8_t)g_catPlayDir;
  g_catKeep.knock   = (int8_t)g_catKnock;
}

// Called once, from the loop's cat scheduler, so it runs AFTER the scene has been read — her
// route and her spot refer to perches the scene supplies, and restoring before that would put
// her on furniture the firmware does not know about yet.
static void catKeepRestore() {
  if (g_catKeep.magic != CATKEEP_MAGIC) return;
  if (g_catKeep.phase <= 0) { g_catKeep.magic = 0; return; }
  if (!alive() || !S.lights || bunAway()) return;      // the room is not hers to come back to
  g_catPhase  = g_catKeep.phase;
  g_catX      = (float)g_catKeep.x;
  g_catY      = (float)g_catKeep.y;
  g_catHomeY  = (float)g_catKeep.homeY;
  g_catSpotX  = (float)g_catKeep.spotX;
  g_catSpotY  = (float)g_catKeep.spotY;
  g_roamGoal  = (float)g_catKeep.roamGoal;
  g_catTx     = (float)g_catKeep.tx;
  g_catRouteN = g_catKeep.routeN; if (g_catRouteN > CAT_MAX_STEPS) g_catRouteN = CAT_MAX_STEPS;
  g_catStep   = g_catKeep.step;   if (g_catStep > g_catRouteN) g_catStep = g_catRouteN;
  for (int i = 0; i < g_catRouteN; i++) {
    g_catRoute[i].x = g_catKeep.rx[i]; g_catRoute[i].y = g_catKeep.ry[i];
  }
  for (int i = 0; i < 3; i++) g_urge[i] = g_catKeep.urge[i];
  g_catRungT  = g_catKeep.rungT;
  g_catWakeAt = millis() + g_catKeep.sleepLeftMs;      // she owes the same nap, not a fresh one
  g_catFaceE  = g_catKeep.faceE != 0;
  g_catClip   = g_catKeep.clip; g_catClipT = 0;
  g_catFromLeft = g_catKeep.fromLeft != 0;
  g_catPetted = g_catKeep.petted != 0;
  g_catChecked= g_catKeep.checked != 0;
  g_catToy    = g_catKeep.toy;
  g_catPlayDir= g_catKeep.playDir;
  g_catKnock  = g_catKeep.knock;
  g_catT      = 0;
  Serial.printf("cat resumed: phase %d at %.0f,%.0f\n", g_catPhase, g_catX, g_catY);
}

static void updateCat(float dt) {
  if (!g_catPhase) return;
  g_catT += dt;
  g_catClipT += dt;
  catGrowUrges(dt);      // the appetites build the whole time she is in the room
  switch (g_catPhase) {
    case 1: {                                 // walking in
      g_catX += (g_catFromLeft ? CAT_TROT : -CAT_TROT) * dt; catEaseLane(dt);
      // She faces the way she is WALKING. The arrival test was written for the right door
      // only (x <= target) - a left-door cat passed it on her first frame and TELEPORTED to
      // her stop point ("the cat keeps teleporting in instead of walking in").
      catSetClip(CC_WALK); g_catFaceE = g_catFromLeft;
      bool inNow = g_catFromLeft ? (g_catX >= g_catTx) : (g_catX <= g_catTx);
      if (inNow) { g_catX = g_catTx; g_catPhase = 2; g_catT = 0; }
      break;
    }
    case 2: {                                 // waiting to be noticed
      // She sits down and has a look round while she waits — `sit` first, because it is the
      // clip that transitions out of standing, then `look` once she is settled.
      // She used to stand back up 1.6s after sitting down, because cat-look's frame 0 is the
      // shared standing tray image — a one-frame snap from seated to standing, on every
      // arrival. Hold the sit; it is a `hold` clip and ends settled.
      catSetClip(CC_SIT);
      // He goes to her the moment he is free. If he never does (busy, asleep, mid-game),
      // she waits a while and then puts herself on the chair anyway — a cat is not offended.
      // ...but not while the bird already has his attention. See the note at the bird's own
      // claim: two beats writing his destination is what sent him to a third place.
      // ...and not while he is ON AN ERRAND (Jon: "the cat interrupted his go to work
      // action"): a door trip or a placed visit is a commitment; greeting her used to
      // overwrite his target mid-walk and the work trip simply vanished. She waits -
      // the fallback below already puts her on the chair if he never comes.
      if (!g_catPetted && !g_action && !g_workStage && g_visit < 0 && !g_doorTrip &&
          g_tx < 0 &&   // only from a standstill - never steal a walk already underway
          !g_sleepPending &&   // bedtime outranks the cat ("he pet a cat and then didnt go to sleep")
          alive() && S.lights && !S.sick &&
          g_catT > 1.4f && !(g_birdReacted && g_birdPhase == 2)) {
        g_catPetted = true;
        catPetSpot(&g_tx, &g_ty);
        clampErrandToFloor(&g_tx, &g_ty);
        g_visit = -1;
        g_tripLen = sqrtf((g_tx - g_fx) * (g_tx - g_fx) + (g_ty - g_fy) * (g_ty - g_fy));
        g_crawling = !babyCanStand(); g_crawlFrac = babyCanStand() ? 2 : 0;
      }
      if (g_catPetted && g_visit < 0 && !g_doorTrip) {
        // the greet owns this walk ONLY when no errand does - with g_catPetted armed,
        // this used to read ANY reached target as "he reached her", so arriving at a
        // room's middle mid-errand turned into petting a cat two rooms away
        float dx = g_tx - g_fx, dy = g_ty - g_fy;
        if (dx * dx + dy * dy < 64.0f) {      // he reached her
          g_catPhase = 3; g_catT = 0;
          setAnim("love");                    // the fuss
          sfxPurr();
          g_love = min(100.0f, g_love + 6.0f);
          loveSave();
          say("a cat came to visit!");
        }
      }
      if (g_catT > 22.0f) { g_catPhase = catDecide(); g_catT = 0; }   // unnoticed: on she goes
      break;
    }
    case 3: {                                 // being petted
      catSetClip(CC_PETTED);
      g_catFaceE = (g_fx > g_catX);           // she turns to whoever is making the fuss
      if (g_catT > 3.5f) {
        g_catPhase = catDecide(); g_catT = 0;
        if (g_visit < 0 && !g_doorTrip) {     // release him - unless an errand owns him now
          g_tx = g_ty = -1;                   // ("the cat shouldnt ever cause him to be stuck")
          g_wanderT = 5.0f;
        }
      }
      break;
    }
    case 13: {         // pottering somewhere she fancied the look of
      const float dx = g_roamGoal - g_catX;
      catSetClip(CC_WALK); g_catFaceE = (dx > 0);
      if (fabsf(dx) > 3.0f) { g_catX += (dx > 0 ? CAT_POTTER : -CAT_POTTER) * dt; catEaseLane(dt); }
      else {
        g_urge[U_LOOK] -= rndf(0.35f, 0.6f);
        if (g_urge[U_LOOK] < 0) g_urge[U_LOOK] = 0;
        // SHE DOES NOT STOP AND STARE ANY MORE. Jon: "can we remove the cat stopping and looking
        // at nothing". There was a 45% chance here of a 4-8 second hold in cat-look on arrival,
        // meant to read as a cat pausing to take the room in. On the panel it read as her
        // freezing: cat-look holds on its last frame, so most of those seconds were a still
        // image, and landing it on nearly every other potter meant the roam kept stalling.
        // She now decides her next move the moment she arrives, which keeps her moving.
        g_catPhase = catDecide();
        g_catT = 0;
      }
      break;
    }
    // case 14 was the stare, and is gone with its entry above. Left named here so the phase
    // numbers reported by /api/system/info still line up with the trace notes in the review pack.
    case 10: {         // off to have a go at the yarn
      // WHERE SHE STANDS DECIDES WHICH WAY IT GOES. placer.html toplay:
      //   playDir = (ball.x<90) ? 1 : (ball.x>230) ? -1 : coin      -- away from a wall
      //   playSide = -playDir                                       -- so she is on the far side
      // and then she stands 13px to that side of it.
      if (g_catPlayDir == 0) {
        const float bx = g_lrt[g_catToy].x;
        g_catPlayDir = (bx < 90.0f) ? 1 : (bx > 230.0f) ? -1 : ((esp_random() & 1) ? 1 : -1);
      }
      float standAt = g_lrt[g_catToy].x - g_catPlayDir * 13.0f;
      if (standAt < 13.0f) standAt = 13.0f;
      if (standAt > 307.0f) standAt = 307.0f;
      const float dx = standAt - g_catX;
      catSetClip(CC_WALK); g_catFaceE = (dx > 0);
      if (fabsf(dx) > 3.0f) { g_catX += (dx > 0 ? CAT_TROT : -CAT_TROT) * dt; catEaseLane(dt); }
      else {
        g_catPhase = 11; g_catT = 0;
        g_catFaceE = (g_catPlayDir > 0);       // she faces the way she is hitting it
        catSetClip(CC_SWAT);
        say("she bats the yarn");
      }
      break;
    }
    case 11: {         // the bat itself
      catSetClip(CC_SWAT);
      // 0.5s, not 0.9s: at 8fps the contact frame is f3-f4. The object used to leave three
      // frames after she hit it, with her paw already back under her.
      if (g_catT > 0.5f) {
        looseKnock(g_catToy, g_catX);
        sfxTick();
        g_catBats++;
        g_catPlayDir = 0;
        g_urge[U_PLAY] -= rndf(0.28f, 0.5f);       // that scratched the itch
        if (g_urge[U_PLAY] < 0) g_urge[U_PLAY] = 0;
        // STILL IN THE MOOD? The builder re-enters toplay while the play urge holds up —
        // "she chases the yarn" — which is what reads as batting it back and forth across the
        // room rather than one poke and done. It is the URGE that decides, not a counter.
        if (g_urge[U_PLAY] > 0.5f && looseToy() >= 0) {
          g_catToy = looseToy(); g_catPlayDir = 0;
          g_catPhase = 10; g_catT = 0;
          say("she chases the yarn");
        } else {
          g_catPhase = catDecide(); g_catT = 0;
        }
      }
      break;
    }
    case 8: {          // something is standing on her spot: go and push it off first
      const float tx = g_lrt[g_catKnock].x + (g_catX < g_lrt[g_catKnock].x ? -14.0f : 14.0f);
      const float dx = tx - g_catX;
      catSetClip(CC_WALK); g_catFaceE = (dx > 0);
      if (fabsf(dx) > 3.0f) { g_catX += (dx > 0 ? CAT_TROT : -CAT_TROT) * dt; catEaseLane(dt); }
      else {
        g_catPhase = 9; g_catT = 0;
        g_catFaceE = (g_lrt[g_catKnock].x > g_catX);   // she faces the thing she is hitting
        catSetClip(CC_SWAT);
        say(g_scLoose[g_catKnock].roller ? "she bats the yarn" : "she pushes it off her spot");
      }
      break;
    }
    case 9: {          // the swipe itself — 0.9s, as the builder holds it
      catSetClip(CC_SWAT);
      if (g_catT > 0.5f) {                     // see the note on the yarn swipe: contact is f3-f4
        looseKnock(g_catKnock, g_catX);
        sfxTick();
        const bool wasToy = g_scLoose[g_catKnock].roller;
        g_catKnock = -1;
        // LOOK AGAIN: the next step up may be blocked too. The builder does exactly this —
        // `c.knockStep = null; c.checked = false;` — so a route with two occupied rungs gets
        // cleared one rung at a time rather than only ever its first.
        g_catChecked = false;
        g_catPhase = 4; g_catT = 0;
        if (!wasToy) say("she clears the surface, and takes it");
      }
      break;
    }
    case 4: {                                 // walking to the chair, then the JUMP up onto it
      // Stood on a rung, taking stock. Only ever reached with g_catStep >= 1 — she has landed on
      // something — so the floor-crossing branch below cannot be swallowed by it.
      if (g_catRungT > 0) {
        g_catRungT -= dt;
        catSetClip(CC_LOOK);
        if (g_catRungT <= 0) { g_catRungT = 0; g_catT = 0; g_catClipT = 0; sfxTick(); }
        break;
      }
      float cx, cy;
      catChairSpot(&cx, &cy);
      // EVERY RUNG, LOWEST FIRST — not just the spot she is aiming for. Jon: "it is floor knock
      // jar off then table then shelf". The jar stands on the TABLE, and the table is the step
      // she needs to reach the shelf, so a check that only looked at the destination found
      // nothing in the way and sent her straight up through it. The builder clears the route:
      //   for (const step of route) { const in_the_way = occupantOf(step,S); ... }
      // Checked once per approach rather than every frame, or she would re-target the instant
      // the thing started to fall; re-armed after each swipe so the next rung is looked at too.
      if (!g_catRouteN) { catStagesTo(cx, cy); g_catStep = 0; }
      if (!g_catChecked) {
        g_catChecked = true;
        for (int r = g_catStep; r < g_catRouteN; r++) {
          const int occ = looseOccupant((float)g_catRoute[r].x, (float)g_catRoute[r].y);
          if (occ >= 0) { g_catKnock = occ; g_catPhase = 8; g_catT = 0; break; }
        }
        if (g_catPhase == 8) break;
      }
      // THE LADDER. Worked out once per approach. A spot within one hop of the floor is a
      // single jump as before; a shelf is not, so she goes up by way of whatever IS — which for
      // Jon's room means the right table. Same route on the way down, in reverse.
      if (!g_catRouteN) { catStagesTo(cx, cy); g_catStep = 0; }
      const CatSpot *rung = &g_catRoute[g_catStep < g_catRouteN ? g_catStep : g_catRouteN - 1];
      const float fromY = (g_catStep == 0) ? (FLOOR_Y + 4.0f) : (float)g_catRoute[g_catStep - 1].y;
      float dx = (float)rung->x - g_catX;
      if (g_catStep == 0 && fabsf(dx) > 3.0f) {   // still crossing the floor, at floor height
        g_catX += (dx > 0 ? CAT_TROT : -CAT_TROT) * dt;
        catEaseLane(dt);
        catSetClip(CC_WALK); g_catFaceE = (dx > 0);
        // THE HOP'S CLOCK STARTS WHEN THE WALK ENDS, and it did not before. g_catT is the PHASE
        // timer: it is zeroed when phase 4 begins and then runs through the whole floor crossing,
        // which is seconds. Nothing restarted it here, so the moment she arrived the hop's own
        // `k = g_catT/HOP_S` was already >= 1 — lift = 1 — and she reached the rung ON THE FIRST
        // FRAME. Every ground-to-rung hop was a teleport, and the landing sound below, guarded on
        // `g_catT < 0.05f`, could never fire. The tell was that a shelf's SECOND hop arced
        // properly while its first did not: the rung dwell happens to reset g_catT, and the walk
        // did not. It is also most of why the chair read better than the table — the chair's
        // getdown is an ease, so it never showed the missing arc.
        if (fabsf((float)rung->x - g_catX) <= 3.0f) { g_catT = 0; g_catClipT = 0; sfxTick(); }
      } else {
        // The hop. Timed off the builder rather than the old flat 0.45s: settle uses
        // c.t = perchClimb ? 1.2 : 0.6 for the first one and 0.9 for each rung above it. At
        // ~10fps a 0.45s arc is four frames of a nine-frame clip, so cat-jump never played out
        // and she arrived frozen at its apex.
        const float HOP_S = (g_catStep == 0) ? 1.2f : 0.9f;
        const float k = g_catT > HOP_S ? 1.0f : g_catT / HOP_S;
        // ...and she FOLDS on the landing instead of arriving mid-leap. Straight off the builder:
        // `c.a = c.t > 0.5 ? CAT_ART.jump : CAT_ART.lie`. cat-lie's last three frames are
        // pixel-identical to cat-sleep's first three, so on the final rung this flows into
        // phase 15 and then into sleep with no cut at all.
        catSetClip(k < 0.62f ? CC_JUMP : CC_LIE);
        const float lift = sinf(k * 1.5708f);     // ease-out: fast off, soft landing
        g_catX += ((float)rung->x - g_catX) * fminf(1.0f, dt * 6.0f);
        g_catY = fromY + ((float)rung->y - fromY) * lift;
        if (k >= 1.0f) {
          g_catX = rung->x; g_catY = rung->y;
          g_catStep++;
          if (g_catStep < g_catRouteN) {
            // ...and she stands there a moment before going up again. The clip restart that
            // used to happen here now happens when the dwell expires, or she would spend the
            // whole pause frozen on the hop's last frame.
            g_catRungT = CAT_RUNG_UP_S;
            g_catClipT = 0;
            sfxTick();
          } else {
            g_catPhase = 15;      // fold down first — cat-lie, then sleep
            g_catT = 0; g_catClipT = 0;
          }
        }
      }
      if (g_catPhase == 4 && g_catStep == 0 && fabsf(dx) <= 3.0f && g_catT < 0.05f) sfxTick();
      break;
    }
    case 15:           // folding down onto the spot — the in-between that was missing
      catSetClip(CC_LIE);
      if (g_catT > 1.29f) {                   // 9 frames at 7fps; its last three frames ARE
        g_catPhase = 5; g_catT = 0;           // cat-sleep's first three, so the join is seamless
        g_catClipT = 0;
        g_catWakeAt = millis() + 25000 + (esp_random() % 30000);
      }
      break;
    case 5:                                   // asleep on the chair
      catSetClip(CC_SLEEP);
      if (millis() >= g_catWakeAt) {
        g_catPhase = 6; g_catT = 0;
        sfxYawn();                            // the yawn is audible, like bunbun's own
        say("the cat woke up");
      }
      break;
    case 6:                                   // the yawn and stretch
      catSetClip(CC_STRETCH);                 // a real stretch clip now, not the standing frame
                                              // scaled wide, which is what this used to be
      // stretch, then a look round — the builder's wake is `if (c.t<1.4) c.a = CAT_ART.look`,
      // which fills the 1.1s that used to sit frozen on the last stretch frame
      if (g_catT > 1.3f) catSetClip(CC_LOOK);
      if (g_catT > 2.4f) {
        // SHE COMES DOWN THE WAY SHE WENT UP. placer.html startDescent(): the steps under her,
        // reversed — highest first — and then the ground. Jon: "the cat also gets down from the
        // shelf using the table". She used to be teleported to floor height in one line.
        if (g_catRouteN > 1 && g_catStep > 1) { g_catPhase = 12; g_catT = 0; g_catStep--;
                                               g_catClipT = 0; }
        else { catWokeOnFloor(); }
      }
      break;
    case 12: {                                // climbing back down, a rung at a time
      if (g_catRungT > 0) {                   // the same beat on the way down, but shorter
        g_catRungT -= dt;
        catSetClip(CC_LOOK);
        if (g_catRungT <= 0) { g_catRungT = 0; g_catT = 0; g_catClipT = 0; }
        break;
      }
      catSetClip(CC_JUMP);
      const float fromY = (float)g_catRoute[g_catStep].y;
      const float toY   = (g_catStep > 0) ? (float)g_catRoute[g_catStep - 1].y
                                          : (FLOOR_Y + 4.0f);
      const float toX   = (g_catStep > 0) ? (float)g_catRoute[g_catStep - 1].x : g_catX;
      const float k = g_catT > 0.45f ? 1.0f : g_catT / 0.45f;
      g_catX += (toX - g_catX) * fminf(1.0f, dt * 6.0f);
      // k*k, not k. Linear is constant velocity — the ghost-drift the RISE was fixed to avoid,
      // still sitting in the fall right under a comment saying "dropping is a fall".
      g_catY = fromY + (toY - fromY) * (k * k);
      if (k >= 1.0f) {
        g_catY = toY; g_catX = toX;
        sfxTick();
        if (g_catStep > 0) { g_catStep--; g_catRungT = CAT_RUNG_DOWN_S; g_catClipT = 0; }
        else { catWokeOnFloor(); }
      }
      break;
    }
    default: {                                // letting herself out
      // The nearer door, as the builder does it: `const edge = c.x<160 ? -24 : 344`.
      const bool west = (g_catX < 160.0f);
      g_catX += (west ? -CAT_OFF : CAT_OFF) * dt;
      g_catY += 8.0f * dt;
      catSetClip(CC_WALK); g_catFaceE = !west;     // her art faces west; east mirrors it
      if (west && g_catX < -24.0f) {
        g_catPhase = 0;
        g_nextCatAt = millis() + 70000 + (esp_random() % 60000);
        break;
      }
      if (g_catX > 336) {
        g_catPhase = 0;
        // R(70,130) — placer.html: `c.t=(c.urge&&c.urge.rest<0.2)?R(70,130):R(45,100)`, with
        // "Jon: she needs to show up more" written next to it and "a nap keeps her away longer".
        // This visit always ends in a nap, so it is the longer branch. Was 15-40 min here, which
        // is roughly twenty times the builder's cadence and is why she was never seen.
        g_nextCatAt = millis() + 70000 + (esp_random() % 60000);
      }
      break;
    }
  }
}

// ---- HER CLIPS, WHICH ARE THE BUILDER'S CLIPS ----
// Jon: "the cat is not the same as the builder, only use those animations and sprites".
// These nine are exactly what CAT_ART (placer.html:618) resolves to, packed from the builder's
// own PIX pool by tools/pullcat.mjs, frame counts and fps included. Two rules come with them,
// both stated in the builder and both confirmed by looking at all 72 frames:
//
//   1. EVERY CLIP FACES WEST. Walking east is the same art mirrored — which is also how one
//      swat clip gives a west and an east swipe. Hence g_blitFlip.
//   2. FRAME 0 IS THE SHARED TRAY IMAGE. framesOf() prepends it, so `sit` and `lie` read as
//      transitions — stand, then fold down, then hold — rather than snapping into the pose.
//      That is not a quirk to fix; it is what makes them look right.
// She sits in the room's own coordinate space, drawn with the furniture so she is occluded
// and lit exactly like it.
static void drawCat() {
  if (!g_catPhase) return;
  const CatClip *cl = &CAT_CLIP[g_catClip];
  int f = (int)(g_catClipT * cl->fps);
  if (cl->hold) { if (f >= cl->frames) f = cl->frames - 1; }   // play once, then stay there
  else f %= cl->frames;
  char fn[40];
  snprintf(fn, sizeof(fn), "%s/%d", cl->folder, f);
  // The two stills stay as the fallback, so a device carrying an older pak still shows a cat
  // rather than nothing at all.
  if (!spriteLoad(fn) &&
      !spriteLoad(g_catPhase == 5 ? "items/cat-sleep" : "items/cat") &&
      !spriteLoad("items/cat")) return;
  // ANCHOR HER BY THE CANVAS, not by the ink — the same rule the scene props needed.
  // spriteBlitDirect centres the TRIMMED box, and these frames trim to anywhere from 39 to 55
  // rows tall (cat-lie and cat-sleep fold her down), so centring the ink would have lifted her
  // off the floor as she settled and dropped her again as she got up. The builder draws the
  // whole 64x64 canvas, so the canvas centre is what belongs at (g_catX, g_catY).
  const float s = CAT_K * VIEW;
  const int ow = g_meta.origW ? g_meta.origW : g_meta.w;
  const int oh = g_meta.origH ? g_meta.origH : g_meta.h;
  // mirroring is about the WHOLE canvas, so an off-centre trim moves to the far side with it
  const int ox = g_catFaceE ? (ow - g_meta.offX - g_meta.w) : g_meta.offX;
  const float adjX = ((float)ox + g_meta.w * 0.5f - ow * 0.5f) * s;
  const float adjY = ((float)g_meta.offY + g_meta.h * 0.5f - oh * 0.5f) * s;
  g_blitFlip = g_catFaceE;                     // the art faces west; east is it mirrored
  // ...and then lift her from canvas-centre onto her feet, so g_catY means for her what it
  // means for every other actor and what it means in the builder. See CAT_PAD.
  // The old `- 2` screen-pixel fudge went with this: it was trimming a symptom of the wrong
  // anchor, and the builder has no equivalent. If she ever reads a couple of pixels high, that
  // is where it came from.
  const float anchorUp = (oh * 0.5f - CAT_PAD) * s;
  spriteBlitDirect(gx2s((int)g_catX) + (int)lroundf(adjX),
                   gy2s((int)g_catY) + (int)lroundf(adjY - anchorUp), s);
  g_blitFlip = false;
}

// The perf numbers the 2-second diagnostic prints to serial, over HTTP as well — because a unit
// on a shelf has no serial attached, and "is this running slower than normal" should be answered
// with a measurement rather than an opinion. Held from the last completed window.
static volatile float g_fpsLast = 0;
static volatile uint32_t g_drawMaxLast = 0, g_iterMaxLast = 0;
extern "C" void bunbun_perf_stages(int *pak_us, int *pix_us, int *push_us) {
  *pak_us = (int)g_tPakLast; *pix_us = (int)g_tPixLast; *push_us = (int)g_tPushLast;
}
extern "C" void bunbun_perf_snapshot(float *fps, int *draw_max_ms, int *iter_max_ms) {
  *fps = g_fpsLast;
  *draw_max_ms = (int)(g_drawMaxLast / 1000);
  *iter_max_ms = (int)(g_iterMaxLast / 1000);
}

extern "C" void bunbun_actor_snapshot(int *bx, int *by, int *cx, int *cy, int *cphase) {
  *bx = (int)lroundf(g_fx);  *by = (int)lroundf(g_fy);
  *cx = (int)lroundf(g_catX); *cy = (int)lroundf(g_catY);
  *cphase = g_catPhase;
}

// Call the cat on demand, for watching the visit without waiting out her own schedule.
extern "C" int bunbun_call_cat(void) {
  g_nextCatAt = 0;
  return startCat() ? 1 : 0;
}

static bool startBird() {
  if (!g_haveSky || g_birdPhase) return false;
  if (g_scCurRole != SCENE_ROLE_MAIN) return false;   // visitors keep to the main room
  g_birdPhase = 1; g_birdT = 0; g_birdReacted = false;
  g_birdNotice = 1.2f + (esp_random() % 260) / 100.0f;
  return true;
}
static void updateBird(float dt) {
  if (!g_birdPhase) return;
  g_birdT += dt;
  if (g_birdPhase == 1) {
    if (g_birdT > 1.3f) { g_birdPhase = 2; g_birdT = 0; }
  } else if (g_birdPhase == 2) {
    // he notices a moment later, and only once he's free — that delay is what makes it
    // feel unforced rather than him snapping to attention
    // ONE BEAT AT A TIME. If the cat already has him — he is on his way over to say hello —
    // the bird does not get to overwrite his destination mid-walk. Both of these wrote g_tx/g_ty
    // with no idea the other existed, so a visit and a visitor at the same moment sent him off
    // to a third place and neither arrival test ever fired. Jon: "if pet and watch the
    // bird/firefly happen at the same time ... walking into the back wall and in random places".
    // RETIRED (Jon 8/18: "let's get rid of the turn and look at the bird or firefly").
    // The visitor still lands in the glass as ambience; the pet no longer breaks off
    // whatever he is doing to walk over and stare. In authored scenes the sim owns his
    // time, and outdoor rooms have no window for the watch-spot heuristics to find —
    // the walk-to-the-window beat only made sense in the original indoor rooms.
    (void)g_birdNotice; (void)g_birdReacted; (void)windowWatchSpot;
    if ((g_birdLeaveAt && millis() >= g_birdLeaveAt) || g_birdT > 20) {
      g_birdPhase = 3; g_birdT = 0;
    }
  } else {
    if (g_birdT > 0.9f) {
      g_birdPhase = 0; g_birdLeaveAt = 0;
      if (g_watching) { g_watching = false; g_settleUntil = 0; g_wanderT = 0.9f; }
    }
  }
}
static void drawBird() {
  if (!g_birdPhase || !g_haveSky) return;
  // The perch is the measured centre of the bottom-right PANE. It used to be 3/4 of the way
  // across the sky bounding box, and since that box over-reached the window the visitor
  // landed inside the room, to the right of the frame.
  int perchX = g_perchX, perchY = g_perchY;
  float k = (g_birdPhase == 1) ? min(1.0f, g_birdT / 1.3f) : 1.0f;
  int x = (g_birdPhase == 1) ? (int)((g_skyX0 - 14) + (perchX - (g_skyX0 - 14)) * k)
        : (g_birdPhase == 3) ? perchX + (int)(g_birdT * 60) : perchX;
  int y = (g_birdPhase == 3) ? perchY - (int)(g_birdT * 34) : perchY;
  if (nightAmount() > 0.5f) {                      // a firefly after dark
    // A soft pulsing glow with a slow drift, not three static pixels — at this scale a
    // 3px dot on a dark pane is effectively invisible, which is why it read as absent.
    float t = g_birdT;
    float cxf = x + sinf(t * 1.6f) * 3.0f, cyf = y - 6 + cosf(t * 1.9f) * 2.0f;
    float pulse = 0.5f + 0.5f * fabsf(sinf(t * 2.4f));
    for (int dy = -3; dy <= 3; dy++)
      for (int dx = -3; dx <= 3; dx++) {
        int px = (int)cxf + dx, py = (int)cyf + dy;
        if (px < 0 || px >= UI_W || py < 0 || py >= SCENE_H) continue;
        if (!isGlass(px, py)) continue;          // it is OUTSIDE — never draw it in the room
        float dist = sqrtf((float)(dx * dx + dy * dy));
        if (dist > 3.2f) continue;
        float a = (1.0f - dist / 3.2f) * pulse;    // radial falloff, same as the HTML gradient
        a *= a;
        uint16_t v = scene.readPixel(px, py);
        int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        int ka = (int)(a * 255);
        r += ((28 - r) * ka) >> 8; g += ((63 - g) * ka) >> 8; b += ((17 - b) * ka) >> 8;
        scene.drawPixel(px, py, (uint16_t)((r << 11) | (g << 5) | b));
      }
    return;
  }
  if (spriteLoad("items/bird")) {
    g_clipGlass = true;
    spriteBlitDirect(x, y - 4, 0.4f);
    g_clipGlass = false;
  }
}

struct EvtDef { const char *id; uint8_t wDay, wNight; bool (*run)(); };
static const EvtDef EVENTS[] = {
    {"bird",    36, 28, startBird},      // gaze's weight folded in: window-sitting always
    {"motes",   18, 10, startMotes},     // means there is something out there to see
    {"firefly",  0, 26, startFirefly},
    {"star",     0, 18, startStar},
};
static const int N_EVENTS = 4;

static void updateEvents(float dt) {
  for (int i = 0; i < g_partN; i++) g_parts[i].life += dt;
  int w = 0;
  for (int i = 0; i < g_partN; i++) if (g_parts[i].life < g_parts[i].dur) g_parts[w++] = g_parts[i];
  g_partN = w;

  uint32_t now = millis();
  if (now < g_nextEvt) return;
  if (!alive() || g_paused) { g_nextEvt = now + 6000; return; }
  bool night = nightAmount() > 0.5f;
  int total = 0;
  for (int i = 0; i < N_EVENTS; i++) total += night ? EVENTS[i].wNight : EVENTS[i].wDay;
  if (!total) { g_nextEvt = now + 6000; return; }
  int r = esp_random() % total;
  const EvtDef *pick = &EVENTS[0];
  for (int i = 0; i < N_EVENTS; i++) {
    int wt = night ? EVENTS[i].wNight : EVENTS[i].wDay;
    r -= wt;
    if (r < 0) { pick = &EVENTS[i]; break; }
  }
  bool ok = pick->run();
  Serial.printf("event: %s %s (night=%d)\n", pick->id, ok ? "started" : "declined", (int)night);
  // ~45s between beats was far too eager — the bird in particular felt constant. 2.5-5 min
  // now, so a visitor at the window is something you notice rather than expect.
  g_nextEvt = now + (ok ? 150000 + esp_random() % 150000 : 8000);
}

static void drawFx() {
  for (int i = 0; i < g_partN; i++) {
    Fx &f = g_parts[i];
    float k = f.life / f.dur;
    float fade = min(1.0f, min(k * 6, (1 - k) * 6));
    if (fade <= 0) continue;
    if (f.kind == FX_MOTE) {
      int x = (int)(f.x + f.vx * f.life + sinf(f.ph + f.life * 1.2f) * 3 * VIEW);
      int y = (int)(f.y + f.vy * f.life);
      if (x >= 0 && x < SCENE_W && y >= 0 && y < SCENE_H)
        scene.drawPixel(x, y, RGB565(255, 190, 104));
    } else if (f.kind == FX_FLY) {
      // Two pixels was invisible on a 240px panel. A small pulsing glow reads as a firefly.
      int x = (int)(f.x + sinf(f.ph + f.life * 1.6f) * 10 * VIEW);
      int y = (int)(f.y + cosf(f.ph + f.life * 1.9f) * 6 * VIEW);
      float pulse = fabsf(sinf(f.life * 2.4f));
      if (pulse > 0.25f && x >= 1 && x < SCENE_W - 1 && y >= 1 && y < SCENE_H - 1) {
        scene.fillCircle(x, y, pulse > 0.7f ? 2 : 1, RGB565(236, 255, 176));
        scene.drawPixel(x - 2, y, RGB565(150, 200, 90));
        scene.drawPixel(x + 2, y, RGB565(150, 200, 90));
        scene.drawPixel(x, y - 2, RGB565(150, 200, 90));
        scene.drawPixel(x, y + 2, RGB565(150, 200, 90));
      }
    } else {
      // A shooting star is OUTSIDE. drawLine cannot be masked, so the streak is walked pixel
      // by pixel against the glass — otherwise it sails straight through the bedroom, which
      // is exactly what the firefly used to do before it got the same treatment.
      int x = (int)(f.x + f.vx * f.life * 30), y = (int)(f.y + f.vy * f.life * 30);
      for (int s = 0; s <= 6; s++) {
        int px = x - s, py = y - (s + 1) / 3;
        if (!isGlass(px, py)) continue;
        scene.drawPixel(px, py, RGB565(255, 252, 230));
      }
    }
  }
}

// ---------------- render ----------------
static uint16_t g_band[UI_W * BAND_H];
static uint8_t  g_rows[UI_W * BAND_H];

// The character-frame loader, split out of drawScene because the dance path needs to call it
// TWICE: composing the base layer blits poop, and spriteLoad("items/poop") clobbers the single
// loaded-sprite slot — the first cached-dance build drew a dancing poop where bunbun should be.
// bunbun's current sprite height in source pixels, remembered across the slot being reused,
// so props can be sized RELATIVE TO HIM (the poop is a quarter of his height) instead of by
// magic constants that silently drift whenever the art is re-exported.
static int g_charH = 0;
// The cat's draw sizes herself against him, and she is declared above this point.
int catCharH() { return g_charH; }
static void loadCharSprite() {
  if (S.stage == STAGE_EGG) { g_anim = &EGG_ANIM; g_animT = 0; spriteLoad(EGG_F[0]); }
  else if (S.stage == STAGE_HATCHING) { g_anim = &EGG_ANIM; spriteLoad(EGG_F[currentFrame()]); }
  // Character-pack insertion point 1 of 2 (the other is petFrameKey, which the games use).
  // Both snprintf()s are covered here, so the "%s/0" fallback resolves against the species too.
  else { char fn[48], sk[64];
         g_canimAnchor = (strncmp(g_anim->folder, "canim/", 6) == 0);
         snprintf(fn, sizeof(fn), "%s/%d", g_anim->folder, currentFrame());
         if (!spriteLoad(charSpriteKey(fn, sk, sizeof(sk)))) {
           snprintf(fn, sizeof(fn), "%s/0", g_anim->folder);
           spriteLoad(charSpriteKey(fn, sk, sizeof(sk)));
         } }
  if (g_sprOK) g_charH = g_meta.h;
}

// ---- layer-cached rendering, dance mode only ----
// Under streaming, rebuilding the whole room from the pak costs 30-60ms a frame in PSRAM-bus
// contention even though during dance almost nothing in the room changes — only the character,
// the ball and the lights move. So while the ball is down, the composited room (bands, sky,
// fixtures, poop — everything EXCEPT those three) is cached in a base buffer and each frame
// becomes: copy base, blit character, stamp disco, push. ~15-25ms under load, which holds the
// 60ms dance gate that raw composition could not. The base refreshes every 250ms (clouds and
// stars amble at 4fps during a party; nobody is watching them) and whenever the dim palette
// changes. The NON-dance path is untouched: it composes exactly as it always has.
static bool g_composeNoChar  = false;   // base pass: character drawn separately per frame
static bool g_composeNoDisco = false;   // base pass: disco stamped separately per frame
static uint16_t *g_sceneBase = nullptr; // PSRAM snapshot of the composed room
static uint32_t g_baseMs  = 0;
static int      g_baseDim = -12345;     // g_dimApplied when the base was composed

// Foot-anchored character blit straight into the scene sprite's buffer — the cached path's
// replacement for the band-loop blit. Same anchor math as spriteBlit (left = footX - dw/2,
// top = footY - dh) so the pet lands on exactly the pixel it would have; direct row access
// with the byte swap the raw buffer requires, same as the disco effects.
static void spriteBlitScene(int footX, int footY, float scale) {
  if (!g_sprOK || !g_meta.w) return;
  int dw = (int)(g_meta.w * scale + 0.5f), dh = (int)(g_meta.h * scale + 0.5f);
  if (dw <= 0 || dh <= 0) return;
  int left = g_canimAnchor
           ? footX - (int)floorf((g_meta.origW * 0.5f - g_meta.offX) * scale + 0.5f)
           : footX - dw / 2;
  int top  = g_canimAnchor
           ? footY - (int)floorf(((g_meta.origH - 6) - g_meta.offY) * scale + 0.5f)
           : footY - dh;
  uint32_t xs = ((uint32_t)g_meta.w << 16) / dw, ysp = ((uint32_t)g_meta.h << 16) / dh;
  for (int dy = 0; dy < dh; dy++) {
    int py = top + dy;
    if (py < 0 || py >= SCENE_H) continue;
    int sy = (int)((uint32_t)dy * ysp >> 16); if (sy >= g_meta.h) sy = g_meta.h - 1;
    uint16_t line[128]; uint8_t cov[128]; memset(cov, 0, sizeof(cov));
    uint32_t pr = g_rowOff[sy]; uint8_t segs = g_spr[pr++]; int x = 0;
    for (int sg = 0; sg < segs; sg++) {
      x += g_spr[pr++]; uint8_t l = g_spr[pr++];
      for (int k = 0; k < l && x + k < 128; k++) {
        line[x + k] = (uint16_t)(g_spr[pr + k * 2] | (g_spr[pr + k * 2 + 1] << 8));
        cov[x + k] = 1;
      }
      pr += l * 2; x += l;
    }
    uint16_t *row = sceneRow(py);
    for (int dx = 0; dx < dw; dx++) {
      int px = left + dx;
      if (px < 0 || px >= UI_W) continue;
      int si = (int)((uint32_t)dx * xs >> 16);
      if (cov[si]) row[px] = bswap16(line[si]);
    }
  }
}

// Disco, applied INSIDE the band pipeline while the strip is still in internal RAM.
//
// The overnight layer cache missed the point, and the measurement caught it: under streaming,
// dance ran 8.5-10.5fps against 17.5 for the plain room, because the cache ADDED a full-frame
// PSRAM copy and left the lights doing ~100KB of scattered read-modify-write in contended
// PSRAM. The expensive resource was never the compose — mmap made that cheap — it is PSRAM
// round-trips. In here, the read-modify-write hits g_band in internal SRAM, which costs almost
// nothing, and the scene sprite keeps its single sequential write per band. Dance now prices
// the same as the normal room plus a little arithmetic.
//
// Band colors are LOGICAL RGB565 (the byte swap happens in pushImage), so no bswap here.
// The beat pulse SNAPSHOTTED once per frame. discoBandStage used to call beatPulse() per band,
// and a frame's bands are composed across ~20ms — so when the flash edge landed mid-frame, the
// top of the screen flashed and the bottom did not: horizontal seams of clipped light, seen by
// the user as "portions of the frame not drawn". Every band now shares one moment in time.
static float g_discoPulseFrame = 0.0f;

static void discoBandStage(uint16_t *band, int bandY, int rows) {
  if (!discoVisible()) return;
  uint32_t t0 = micros();
  float pulse = g_discoPulseFrame;    // one moment in time for ALL bands of this frame
  int base = (int)(70 + 330 * pulse * g_discoDrop);

  for (int i = 0; i < DISCO_SPOTS; i++) {
    int amt = (((g_beatCount + i) & 1) == 0) ? base : (base * 2) / 3;
    if (amt <= 2) continue;
    float a  = g_discoSpin * 0.55f + i * (6.2831853f / DISCO_SPOTS);
    int   cx = UI_W / 2 + (int)(sinf(a) * 112.0f);
    int   cy = 26 + (int)(cosf(a * 0.8f + i * 1.7f) * 30.0f) + (i % 4) * 38;
    int   rad = 18 + (i % 3) * 7;
    const uint8_t *c = DISCO_COL[i % 6];
    int r2 = rad * rad;
    int y0 = cy - rad, y1 = cy + rad;
    if (y1 < bandY || y0 >= bandY + rows) continue;
    // Falloff via lookup table and octagonal distance, replacing a sqrtf AND an integer divide
    // PER PIXEL — measured at ~20ms a frame across 25k pixels, which was the whole remaining
    // gap to the frame budget once the core split removed the preemption noise. The octagonal
    // approximation (max + min/2, scaled) is within ~6% of true distance, invisible on a soft
    // glow; the table turns the falloff into one index per pixel. Both computed per spot, once.
    uint8_t fall_lut[48];
    for (int dd = 0; dd <= rad && dd < 48; dd++) {
      int f = amt - (amt * dd) / (rad + 1);
      fall_lut[dd] = (uint8_t)(f < 0 ? 0 : (f > 255 ? 255 : f));
    }
    for (int y = max(y0, bandY); y <= min(y1, bandY + rows - 1); y++) {
      int dy = y - cy;
      int span = (int)sqrtf((float)(r2 - dy * dy));       // once per ROW, not per pixel
      int ady = dy < 0 ? -dy : dy;
      uint16_t *d = band + (y - bandY) * UI_W;
      int x0 = max(cx - span, 0), x1 = min(cx + span, UI_W - 1);
      for (int x = x0; x <= x1; x++) {
        int adx = x - cx; if (adx < 0) adx = -adx;
        int hi = adx > ady ? adx : ady, lo = adx > ady ? ady : adx;
        int dd = hi + (lo >> 1) - (lo >> 3);              // octagonal ~|d|, no sqrt
        if (dd > rad) continue;
        int fall = fall_lut[dd];
        if (fall <= 1) continue;
        d[x] = addLight(d[x], c[0], c[1], c[2], fall);
      }
    }
  }

  // The ball and its string, banded the same way.
  {
    int cx = UI_W / 2;
    int cy = (int)(-DISCO_R - 4 + (DISCO_HANG + DISCO_R + 4) * g_discoDrop);
    // string: from the ceiling down to the ball, only the part crossing this band
    int sy1 = min(cy - DISCO_R, bandY + rows) - 1;
    for (int y = bandY; y <= sy1; y++)
      if (y >= 0) band[(y - bandY) * UI_W + cx] = C_INK;
    int r2 = DISCO_R * DISCO_R;
    int y0 = max(cy - DISCO_R, bandY), y1 = min(cy + DISCO_R, bandY + rows - 1);
    for (int y = y0; y <= y1; y++) {
      int dy = y - cy;
      int span = (int)sqrtf((float)(r2 - dy * dy));
      uint16_t *d = band + (y - bandY) * UI_W;
      for (int dx = -span; dx <= span; dx++) {
        int x = cx + dx;
        if (x < 0 || x >= UI_W) continue;
        int dist2 = dx * dx + dy * dy;
        // outline ring in place of the old drawCircle
        if (dist2 >= (DISCO_R - 1) * (DISCO_R - 1)) { d[x] = C_INK; continue; }
        int   row = (dy + DISCO_R) / 4;
        float lon = asinf((float)dx / (float)(span > 0 ? span : 1)) + g_discoSpin;
        int   col = (int)floorf(lon * 2.6f);
        bool  lit = ((row + col) & 1) != 0;
        int b2 = lit ? 210 : 130;
        b2 -= (dy * 3);
        b2 += (int)(45 * pulse);
        if (b2 < 40)  b2 = 40;
        if (b2 > 255) b2 = 255;
        const uint8_t *c = DISCO_COL[(g_beatCount + (col & 3)) % 6];
        int rr = lit ? (b2 * 3 + c[0]) >> 2 : b2;
        int gg = lit ? (b2 * 3 + c[1]) >> 2 : b2;
        int bb = lit ? (b2 * 3 + c[2]) >> 2 : b2;
        d[x] = RGB565(rr, gg, bb);
      }
    }
  }
  g_discoUsFrame += micros() - t0;
}

static void composeRoom(int fx, int fy, float sc, int lampOn, bool cloudLit, float nt) {
  // Overcast (by request): while it rains by day the pane greys over and the clouds turn
  // storm-grey. All flat colours are computed ONCE per pass — the per-pixel sky branch just
  // assigns them, so this costs the hot loop nothing. rn folds daylight in via (1 - nt):
  // after dark the night colours own the pane and rain changes nothing.
  float rn = rainAmount() * (1.0f - nt);
  int skR = g_skyR + (int)((15 - g_skyR) * rn);   // day blue -> pale overcast grey...
  int skG = g_skyG + (int)((33 - g_skyG) * rn);
  int skB = g_skyB + (int)((17 - g_skyB) * rn);
  skR += (int)((2 - skR) * nt);                   // ...then -> deep night, exactly as before
  skG += (int)((6 - skG) * nt);
  skB += (int)((8 - skB) * nt);
  bool paintSky = (nt > 0.01f || rn > 0.01f);
  // A fresh silhouette every frame, before the pet is blitted into it band by band. If he is away
  // (or the base pass is skipping him) nothing is marked, the mask reads empty, and the scene's
  // furniture draws whole — which is right, because there is nobody standing in front of it.
  petStenBegin();
  // daytime cloud target: white normally, a grey visibly darker than the overcast pane in
  // rain, so the clouds read as weather rather than vanishing into the sky behind them
  int ctR = 31 - (int)((31 - 12) * rn), ctG = 63 - (int)((63 - 25) * rn),
      ctB = 31 - (int)((31 - 14) * rn);
  for (int by = 0; by < SCENE_H; by += BAND_H) {
    int rows = min(BAND_H, SCENE_H - by);
    { uint32_t t0 = micros();
      pakRead(g_roomPix + (uint32_t)by * g_roomW, g_rows, (size_t)rows * g_roomW);
      g_tPakAcc += micros() - t0; }
    uint32_t tPix0 = micros();
    for (int y = 0; y < rows; y++) {
      uint16_t *d = g_band + y * UI_W; const uint8_t *s = g_rows + y * g_roomW;
      int ry2 = by + y;
      const uint8_t *lm = (g_light && lampOn > 0 && (ry2 >> 1) < LIGHT_H)
                          ? g_light + (ry2 >> 1) * LIGHT_W : nullptr;
      // SCENE_W is 240, so the ground plane is exactly 30 bytes a row and the row base hoists
      // out of the inner loop — the per-pixel cost is a shift and a mask, not a divide.
      const uint8_t *gm = (g_gndMask && ry2 < SCENE_H) ? g_gndMask + ry2 * (SCENE_W / 8) : nullptr;
      for (int x = 0; x < UI_W; x++) {
        uint8_t idx = s[x];
        // Beyond the glass the landscape goes to night WITH the sky, never daylit under it.
        const bool gnd = gm && (gm[x >> 3] & (1 << (x & 7)));
        uint16_t c = gnd ? g_palGnd[idx] : g_palDim[idx];
        // Never light the sky. The sconce is indoors; the window looks OUTSIDE. The beam
        // clips the right of the frame, and lighting those pixels restored them to full
        // daytime blue — which is why the sky refused to look like night.
        // ...and the sconce does not light it either. The builder gates its lamp add on `!out`,
        // which covers the field as well as the sky; without the `!gnd` the beam clipping the
        // window would restore the hills toward their daylight value, the same way it used to
        // restore the sky before g_isSky was added to this test.
        if (lm && !g_isSky[idx] && !gnd) {
          int a = (lm[x >> 1] * lampOn) >> 8;     // half-res map, sampled per pixel
          if (a) {
            // WARM, AND ADDED — which is what the builder does and what this used to miss.
            // litRoom() grades the room down and THEN adds `R+92 G+72 B+34`, an amber. Blending
            // toward the undimmed palette instead reads back only the first half of that, and
            // it made lamplight COOLER in hue than the ambient shadow: a lit wall measured
            // (192,192,152) — R exactly equal to G, neutral sage — against a warm (104,100,72)
            // dark. Backwards, and the reason the night read as merely dark rather than warm.
            // Capped below the surface's own daylight value so a wall sconce cannot out-shine
            // noon, which it was doing at 86% of the midday wall.
            // STRENGTH FROM THE REVEAL, WARMTH FROM THE ADD.
            // Two failed attempts are worth recording. Blending fully toward the undimmed
            // palette (the original) had the right strength but came out COOLER in hue than the
            // ambient shadow — a lit wall at R exactly equal to G against a warm dark, which is
            // backwards. Replacing it with the builder's pure amber add (+92/+72/+34, i.e.
            // 12/18/4 in 5/6/5) was faithful but far weaker than a full reveal, so the whole
            // room went dim and the LEFT sconce's cone disappeared entirely — its wall is a
            // darker shade in the room art, so a small fixed add simply did not register.
            // Both together: reveal most of the way back to daylight, weighted warm so red
            // recovers fully and blue lags, then a little amber on top.
            // THE BUILDER'S ADD, EXACTLY. litRoom():
            //     const w = Math.min(1.6, acc) * lamp;
            //     R = min(255, R + 92*w);  G = min(255, G + 72*w);  B = min(255, B + 34*w)
            // In 5/6/5 that is +11.6 / +18.1 / +4.2 per unit of w. The device previously
            // REVEALED toward the undimmed palette instead, weighted warm — which had the right
            // strength but the wrong behaviour: revealing is bounded by the surface's own
            // daylight value, so a dark wall could barely brighten at all and the lit and unlit
            // wall ended up nearly the same tone. Jon's reference frame shows the opposite —
            // deep warm brown wall, two distinct bright pools on it.
            // An earlier attempt at this exact add was reverted on 0.1.265 because "the left
            // cone vanished". That test was confounded: the lamp ANCHOR was 9px low at the time
            // (see lampGeomFor), so the left cone was being cast onto a different, darker part
            // of the wall than the builder casts it. With the anchor carried from the scene the
            // add is being judged on its own terms for the first time.
            // `a` is 0..255 over the 1.6 headroom, so w = a/255 * 1.6.
            int r = (c >> 11) & 0x1F,  g = (c >> 5) & 0x3F,  b = c & 0x1F;
            r += (int)((11.6f * 1.6f / 255.0f) * a);
            g += (int)((18.1f * 1.6f / 255.0f) * a);
            b += (int)(( 4.2f * 1.6f / 255.0f) * a);
            if (r > 31) r = 31;
            if (g > 63) g = 63;
            if (b > 31) b = 31;
            c = (uint16_t)((r << 11) | (g << 5) | b);
          }
        }
        // The glass takes its OWN colour rather than being darkened along with the room:
        // daylight blue easing to a deep indigo. Dimming the painted landscape is what made
        // night look like "a dark day" — the HTML repaints the whole pane flat and lets
        // stars and clouds carry the sky instead.
        if (g_isSky[idx] && paintSky) {
          c = (uint16_t)((skR << 11) | (skG << 5) | skB);
        }
        d[x] = c;
      }
      int ay = by + y;
      // stars fade in as it gets dark. Fixed pseudo-random positions so they hold still
      // instead of crawling, with a gentle twinkle on top.
      if (g_haveSky && nt > 0.25f && ay >= g_skyY0 && ay <= g_skyY1) {
        int bw = g_skyX1 - g_skyX0 + 1, bh = g_skyY1 - g_skyY0 + 1;
        float amt = (nt - 0.25f) / 0.75f;
        for (int i = 0; i < 26; i++) {
          if (g_skyY0 + ((i * 131) % bh) != ay) continue;
          int sx = g_skyX0 + ((i * 73) % bw);
          if (sx < 0 || sx >= UI_W || !g_isSky[s[sx]]) continue;
          float tw = 0.6f + 0.4f * sinf(millis() / 900.0f + i);
          int k = (int)(amt * tw * 255);
          uint16_t v = d[sx];
          int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
          r += ((31 - r) * k) >> 8; g += ((61 - g) * k) >> 8; b += ((27 - b) * k) >> 8;
          d[sx] = (uint16_t)((r << 11) | (g << 5) | b);
        }
      }
      // rain on the glass. updateRain() was running and even announcing itself on the ticker,
      // but drawRain() was never called from anywhere — so no shower was ever visible.
      drawCelestial(s, d, ay);   // deepest: the sun and moon ride the clock
      drawEnvStars(d, ay);   // stars first: rain and clouds drift OVER them
      drawRain(s, d, ay);
      // W-003 afterglow: ripples during rain, then puddle + occasional
      // rainbow after. Before the clouds so they drift across the bow.
      drawWeatherAfterglow(s, d, ay);
      // clouds: only over pixels that are actually sky, so the frame and curtains occlude
      // them for free without needing a separate mask
      if (g_haveSky && g_cloudN > 0 && ay >= g_skyY0 && ay <= g_skyY1) {
        // Opacity is read once per band, not per pixel. Divided by 255 rather than shifted by 8
        // on purpose: 153/255 is EXACTLY 3/5 and 102/255 is EXACTLY 2/5, so a device with no
        // scene reproduces the shipped blend bit for bit. (>>8 would be 153/256, which drifts by
        // one level at every multiple of 5.)
        const int ka = g_cloudAlphaK, kb = (g_cloudAlphaK * 2) / 3;   // blue lags, as it always has
        for (int c = 0; c < g_cloudN; c++) {
          const Cloud &cl = g_clouds[c];
          if (ay < cl.y - cl.h || ay > cl.y + cl.h) continue;
          float dy2 = (ay - cl.y) / cl.h;
          int half = (int)(cl.w * sqrtf(max(0.0f, 1.0f - dy2 * dy2)));
          for (int x = (int)cl.x - half; x <= (int)cl.x + half; x++) {
            if (x < 0 || x >= UI_W) continue;
            if (g_skyAuthored) {
              if (x < g_skyX0 || x > g_skyX1) continue;
              if (!skyPolyHit(x, ay)) continue;
              if (g_skyBoxMasked && !g_isSky[s[x]]) continue;   // frames occlude
            } else if (!g_isSky[s[x]]) continue;
            uint16_t v = d[x];
            int r = ((v >> 11) & 0x1F), g = ((v >> 5) & 0x3F), b = (v & 0x1F);
            // white by day (storm-grey in rain), dark grey after dark — brightening toward
            // white at night was undoing the sky's night colour and looking like daytime
            if (cloudLit) { r += (ctR - r) * ka / 255;
                            g += (ctG - g) * ka / 255;
                            b += (ctB - b) * kb / 255; }
            else          { r += (7 - r) / 2;      g += (13 - g) / 2;     b += (18 - b) / 2; }
            d[x] = (uint16_t)((r << 11) | (g << 5) | b);
          }
        }
      }
    }
    g_tPixAcc += micros() - tPix0;
    if (!g_composeNoChar && !bunAway()) spriteBlit(g_band, by, fx, fy, sc);
    discoBandStage(g_band, by, rows);
    { uint32_t t0 = micros();
      scene.pushImage(0, by, UI_W, rows, g_band);
      g_tPushAcc += micros() - t0; }
  }
  // drawBird() was missing entirely — the event fired and the state machine ran, but nothing
  // ever rendered it, which is why no bird or firefly was ever seen.
  // Indoor fittings and the window's ambient beats are all suppressed out at the farm, as the
  // HTML does with its `!workSeq` guards — a cat clock hanging in a field would be absurd.
  // The night dimming still applies, so working after dark still looks like after dark.
  if (alive() && !g_workStage) {
    // An uploaded scene's furniture, under everything below — and now under the PET as well.
    // The first pass is the room's floor furniture (z <= 0, which is every prop a child places
    // without touching the layer control); the stencil holds it out of his silhouette so he walks
    // in front of the tables and chairs instead of behind them. The second pass is the front
    // layer (z > 0), which paints over him on purpose. Both passes still go down before the
    // fittings, the cat and the fx, exactly as one call used to.
    BC(BC_SCENEPROPS);
    g_stenApply = true;
    sceneDrawProps(false);
    g_stenApply = false;
    sceneDrawProps(true);
    // ...and the room does not draw its own copy of anything the scene already placed
    // THE BUILDER IS THE LIGHTING RECORD (and the furnishing record): the compiled-in
    // clock, sconce and radio belong to the SHIPPED farmhouse only. A custom room shows
    // exactly what the child placed - a sconce materialising in the work room is the
    // device inventing scenery ("there is a sconce in work that shouldnt be there").
    const bool shippedRoom = !g_scRoom[0];
    // the CAT CLOCK is the home fixture and the house's real time - it stays in the
    // MAIN room even when the room art is the child's ("the cat clock disappeared in
    // the main room"). Side rooms show only what the child placed; the sconce and
    // radio stay scene-only in custom rooms either way.
    // BISECT STEP 1 (8/19): the clock returns alone - if the streaming panic
    // (bc 14) comes back now, the clock draw in a custom room is the killer
    const bool clockRoom = shippedRoom || g_scCurRole == SCENE_ROLE_MAIN;
    if (!sceneHas("items/catclock")) { if (clockRoom) drawCatClock(); }
    else {
      // The scene drew the clock's PICTURE in the props pass. Its tail and its hands are lines
      // the firmware paints, so they have to be added here or a scene-supplied clock is a dead
      // ornament — no swing, no time.
      const SceneProp *cc = sceneFindProp("items/catclock");
      if (cc) { catClockFace(cc->x, cc->y, cc->sx, true); catClockFace(cc->x, cc->y, cc->sx, false); }
    }
    if (shippedRoom && !sceneHas("items/sconce") && !sceneHas("items/lamp")) drawLampFixture();
    if (shippedRoom && !sceneHas("items/radio")) drawRadio();
    drawBird();
    // WHO IS IN FRONT, bunbun or the cat. Straight off the builder (placer.html drawSim):
    //   const dy = S.cat.y - S.bun.y;
    //   if (Math.abs(dy) > 3) S.catInFront = (dy > 0);      // hysteresis: no flicker on a
    //   for (const w of (S.catInFront ? [S.bun,S.cat] : [S.cat,S.bun])) ...   shared lane
    // Larger y is nearer the viewer, so whoever is further down the screen paints last. It is a
    // depth sort, not a fixed order — which is the whole point: she is IN FRONT when she pads
    // across the floor between him and the camera, and BEHIND when she is asleep up on a shelf.
    // The device drew her last unconditionally, so she covered him even from the top shelf
    // (Jon: "bunbun should be in front of the cat").
    // He is already painted by now, inside the band pipeline, so "draw him last" is expressed by
    // arming his stencil and letting his silhouette hold her out — the same trick the furniture
    // uses. The 3px band keeps it from flickering when they are level.
    // THIS TEST WAS RIGHT AND STILL LOST, for a while, because the two y's were not the same
    // kind of thing: g_fy is bunbun's feet, but the cat was DRAWN from her canvas centre, 16 room
    // px above hers. She had to get 19px in front of him before this could see it. The fix was at
    // the draw (see CAT_PAD), not here — once both anchors are feet, this is the builder's line
    // unchanged. Worth remembering the shape of it: a correct comparison between two values that
    // are measured from different places is still a wrong answer, and it reads as a bug in the
    // comparison.
    {
      const float dy = g_catY - g_fy;
      if (fabsf(dy) > 3.0f) g_catInFront = (dy > 0);
      g_stenApply = !g_catInFront;
      BC(BC_DRAWCAT); drawCat();
      g_stenApply = false;
    }
    drawFx();
    // After the character has been blitted, so the beams fall ACROSS bunbun too rather
    // than being hidden behind it. Lights first, ball on top of them.
    // disco now happens inside the band pipeline (discoBandStage) — see the note there
  }
  // messes on the floor, drawn over the room. A quarter of bunbun's own on-screen height
  // (by request — the old fixed 0.35 drew a mess nearly half his size). Computed from both
  // sprites' real heights so it tracks his phase scale and survives art re-exports.
  // Age-appropriate messes (the kids' revision): babies poop, teens drop
  // clothes, adults stack dirty dishes. Same counter, same SWEEP, three
  // skins — and until the clothes/dishes art lands in the pak, the sprite
  // lookup falls back to the poop so nothing draws blank.
  for (int i = 0; i < S.poopN; i++) {
    const char *ms = (S.phase == PH_TEEN)  ? "items/clothes"
                   : (S.phase == PH_ADULT) ? "items/dishes"
                                           : "items/poop";
    // Animated messes (kids: "so they have some movement"): when the pak
    // carries items/<name>-0..4, cycle them (~3.5 fps, offset per mess so
    // two piles don't wiggle in lockstep). Single-frame art and the poop
    // fallback keep working unchanged.
    char mfn[40];
    snprintf(mfn, sizeof(mfn), "%s-%d",
             ms, (int)((millis() / 280 + i * 2) % 5));
    if (spriteLoad(mfn) || spriteLoad(ms) || spriteLoad("items/poop")) {
      float ps = (g_charH && g_meta.h)
                     ? (g_charH * spriteScale()) / (4.0f * g_meta.h)
                     : 0.18f;
      spriteBlitDirect(gx2s(S.poopX[i]), gy2s(S.poopY[i]) - 4, ps);
    }
  }
  // W-036 treats: the basket waits on the floor in the MIDDLE of the room (Jon 8/14: "the
  // treat needs to be centered in the room and bigger"). It used to sit at the far left
  // edge, small — which read as scenery rather than as the one hopeful thing in an empty
  // room, and gave him a two-pixel walk to reach it. Centred and at roughly two-thirds his
  // own height, it is unmistakably the subject, and the walk across to it is a journey.
  // Drawn during the homecoming too, right up until he reaches it and it goes.
  if ((bunAway() || g_homeStage == 1) && g_treatsOutMs && spriteLoad("items/treats")) {
    float ps = (g_charH && g_meta.h)
                   ? (g_charH * spriteScale()) / (1.5f * g_meta.h)
                   : 0.48f;
    spriteBlitDirect(gx2s(TREAT_X), gy2s(TREAT_Y) - 4, ps);
  }
}

// The lift's easing. Target is "feet just above the sheet's top edge", with one hard cap:
// the ears must never reach the chip row. The adult sprite is the tall case and the one the
// spec flagged as the tightest fit in the whole redesign (R6) — so rather than trust a
// measured constant, the clamp is computed from the sprite that is ACTUALLY loaded, every
// frame. A future art re-export cannot silently push his ears into the PAUSE chip.
static const int SHEET_FEET_Y = 104;   // 8px of slack under his feet before the y112 border
static const int SHEET_EAR_Y  = 30;    // 2px clear of the chip row's bottom edge at y28
static void sheetLiftTick() {
  // THE EASE RIDES THE OPEN/CLOSE EDGE, NOT THE TARGET (review 8/14, BUG-1). The first
  // version re-based its clock every time `want` changed — and `want` is derived from S.y,
  // which is rewritten every frame while he WALKS or dances. So the elapsed time was reset
  // to ~0 on those frames and the ease never advanced: open a sheet while he was crossing
  // the room and he simply never rose, cut off at the knees by the very clip the lift
  // exists to clear. Now a 0..1 progress eases off the sheetOpen() transition alone and the
  // lift is recomputed from the live target each frame, so a moving pet tracks smoothly.
  bool open = sheetOpen();
  static bool wasOpen = false;
  if (open != wasOpen) { wasOpen = open; g_sheetLiftT0 = millis(); }
  int want = 0;
  if (open) {
    int feet = gy2s(S.y);
    int dh   = (int)(g_meta.h * spriteScale() + 0.5f);   // his on-screen height, this phase
    int top  = SHEET_FEET_Y;
    if (dh > 0 && top - dh < SHEET_EAR_Y) top = SHEET_EAR_Y + dh;   // ears win over feet
    want = feet - top;
    if (want < 0) want = 0;             // he is already above the sheet; don't push him down
  }
  uint32_t el = millis() - g_sheetLiftT0;
  float p = (el >= 250) ? 1.0f : el / 250.0f;
  p = p * p * (3.0f - 2.0f * p);        // smoothstep: he arrives and settles, not a linear jerk
  // Closing eases the SAME progress back down, so the descent is symmetric with the rise.
  g_sheetLift = (int)(want * p + 0.5f);
  if (!open) g_sheetLift = (int)(g_sheetLiftTo * (1.0f - p) + 0.5f);
  else       g_sheetLiftTo = g_sheetLift;   // remembered so the close has something to fall from
}

static void drawScene() {
  g_discoPulseFrame = beatPulse();    // snapshot: every band of this frame sees the same beat
  loadCharSprite();
  sheetLiftTick();                    // AFTER loadCharSprite: the clamp needs g_meta.h
  int fx = gx2s(S.x) + g_danceSway, fy = gy2s(S.y) - g_danceHop - g_sheetLift;
  float sc = spriteScale() * customBreathe();   // the editor's breathing, feet-pinned
  if (!g_canimAnchor) sc *= travelFactor();     // the master dial sizes the traveller
  // THE EAR CLAMP GUARDS THE RENDERED POSITION (review 8/14, BUG-2). It used to clamp the
  // lift's TARGET, so the 7px dance hop — subtracted here, after the clamp — carried his
  // ears to y23, inside the y4..28 chip row, where PAUSE/DANCE/gear paint straight over
  // them. DANCE stays live above an open sheet on purpose, so "open CARE, tap DANCE" was a
  // two-tap route into the breach. Clamping the final foot line closes hop, transit and any
  // future third contributor in one place. R6 was the redesign's tightest fit; this is where
  // it has to hold.
  if (sheetOpen()) {
    int dh = (int)(g_meta.h * sc + 0.5f);
    if (dh > 0 && fy - dh < SHEET_EAR_Y) fy = SHEET_EAR_Y + dh;
  }
  updateDimPalette();
  g_sfxNight = nightAmount();     // the sound follows the room into the dark; see sfx.h
  // buildLightMap() is also called once at boot, potentially before SPIFFS is up and so before
  // any scene exists - so the scale has to be re-checked here or a scene's light throw would
  // never land. It only malloc()s on its first call, so a rebuild costs one 120x90 pass and no
  // heap, which matters given the g_spr internal-RAM history.
  {
    const SceneEnv *e = sceneEnv();
    int wantLS = e ? e->lightS : 100;
    // g_scGen is the one that matters: the map is first built at boot, BEFORE SPIFFS is mounted,
    // so it is always built without a scene and has to be rebuilt when one turns up.
    // BRACES. Without them the `if` bound to the breadcrumb alone and buildLightMap() ran
    // UNCONDITIONALLY, every frame: memset of the 10,800-entry map, then lampIntensityAt() at
    // every one of those points for every lamp — two divides, a fabsf, a smoothstep and a sqrtf
    // each — and the whole result thrown away because nothing had changed. Two sconces made it
    // roughly 9ms of a 62ms compose, and it printed a "light map built" line every frame on top.
    // Mine, from adding the g_lightGen term to this condition earlier tonight; the cache key was
    // the thing being fixed and the guard it was attached to had never actually guarded anything.
    if (g_lightPhase != S.phase || g_lightScalePct != wantLS || g_lightGen != g_scGen) {
      BC(BC_LIGHTMAP);
      buildLightMap();
    }
  }
  // Out at the farm there is no lamp. Hiding only the FIXTURE left its glow still painted
  // across the field after dark, because the light map is applied down in the band loop.
  // Lamp off during dance mode — you do not run a disco with the big light on. lampLevel() is
  // purely time-of-day, so this suppresses the BEAM without touching S.lights, which is
  // bunbun's sleep state and would have put it to bed instead.
  // NOBODY SLEEPS WITH THE LAMPS ON (Jon: "if he is sleeping in a room all lights
  // should turn off in that room"): once he is actually settled asleep - not still
  // walking home in the dark - every beam in the room goes out with him.
  // NOBODY SLEEPS WITH THE LAMPS ON - acquitted by the stabilization crash (the
  // image panicked with this off) and restored
  const bool fastAsleep = !S.lights && g_tx < 0 && g_visit < 0 && !g_doorTrip && !g_action;
  int lampOn = (g_workStage || g_danceMode || fastAsleep) ? 0 : (int)(lampLevel() * 255);
  bool cloudLit = nightAmount() < 0.5f;
  const float nt = nightAmount();

  // The overnight cached-dance branch lived here and was REMOVED the next morning: measured
  // under real streaming it ran 8.5-10.5fps against 17.5 for the plain compose, because its
  // full-frame PSRAM copy and scene-level light painting cost more than the compose it saved.
  // Dance now takes the same path as everything else, with the disco applied in-band.
  composeRoom(fx, fy, sc, lampOn, cloudLit, nt);
  g_sprName[0] = 0;
}


// Only repaint what changed. Clearing the whole strip and redrawing it every frame showed the
// blank fill for an instant each time, which is the flicker. The caches live out here so that
// anything which clears the screen can invalidate them — a full-screen wipe erased the labels
// and outlines, and the "already drawn" state then stopped them ever coming back.
static bool g_statsFirst = true;
static int  g_lastFill[STAT_COLS] = {-1, -1, -1, -1, -1, -1, -1};
static bool g_lastLow[STAT_COLS]  = {false, false, false, false, false, false, false};
static void statsInvalidate() {
  g_statsFirst = true;
  for (int i = 0; i < STAT_COLS; i++) { g_lastFill[i] = -1; g_lastLow[i] = false; }
}

// y is a parameter now: the bars live at STATS_Y under the room, and at SHEET_STATS_Y when
// the CARE sheet lifts them to its top (Jon 8/14). Same bars, same incremental redraw —
// statsInvalidate() is what makes them repaint their labels and frames at a new address.
static void drawStats(int sy = STATS_Y) {
  const char *lbl[STAT_COLS] = {"FOOD","FUN","CLN","ZZZ","CUDL","LOVE","HP"};
  if (S.phase == PH_TEEN)       lbl[4] = "SCHL";
  else if (S.phase == PH_ADULT) lbl[4] = "WORK";
  float val[STAT_COLS] = {S.food, S.fun, S.clean, S.energy, S.disc, g_love, S.health};
  // Straight from the stylesheet: .bar i is --orange, #b_disc i is #8a6ee6, #b_hp i is
  // #27ae60, and .bar.low i turns #c0392b. LOVE is pink, obviously.
  uint16_t col[STAT_COLS] = {C_ORANGE, C_ORANGE, C_ORANGE, C_ORANGE, C_DISC, 0xFB56, C_HP};
  int pad = 3, gap = 2;
  int w = (UI_W - pad * 2 - gap * (STAT_COLS - 1)) / STAT_COLS;
  if (g_statsFirst) {
    tft.fillRect(0, sy, UI_W, STATS_H, C_PAPER);
    for (int i = 0; i < STAT_COLS; i++) {
      int x = pad + i * (w + gap);
      tft.setTextColor(C_INK_SOFT, C_PAPER);
      tft.drawString(lbl[i], x + 1, sy + 1, 1);
      tft.drawRect(x, sy + 12, w, BAR_H + 2, C_INK);
    }
    g_statsFirst = false;
  }
  for (int i = 0; i < STAT_COLS; i++) {
    int x = pad + i * (w + gap), by = sy + 12;
    int f = (int)((w - 2) * val[i] / 100.0f);
    bool low = val[i] < 20;
    if (f == g_lastFill[i] && low == g_lastLow[i]) continue;      // nothing to redraw
    g_lastFill[i] = f; g_lastLow[i] = low;
    tft.fillRect(x + 1, by + 1, f, BAR_H, low ? C_LOW : col[i]);
    if (f < w - 2) tft.fillRect(x + 1 + f, by + 1, w - 2 - f, BAR_H, C_PAPER);
  }
}

// ---- THE TAB BAR (menu redesign P3) ----
// The icon row, the UP/OK/DOWN keys and the PAUSE/MUSIC/NAP pins were three strips of
// chrome doing one job badly. This is the whole of what replaced them: four doors, each
// 55x88, each a target a mitten can hit without aiming.
//
// No emoji glyphs exist in any TFT_eSPI font, so the icons are pak art where the pak has it
// and W-061-style VECTORS where it does not — the same zero-new-art build the game seat
// proved. Every icon has a fallback, so a pak that has never heard of icons/feed still puts
// a readable door on the screen.
//
// Charter R5 is the reason there is no selected state: tabs are DOORS, not modes. No
// persistent highlight, no badge, no pulse. The single exception is the MUSIC tab's small
// hollow "off" dot, which is the lowercase-`snd` cue the pin row used to carry (R4a) — it
// says a real thing about the device, so it earns its pixels.
static const char *TAB_LBL[N_TABS] = {"CARE", "PLAY", "MUSIC", "SLEEP"};
static inline int tabX(int i) { return TAB_X0 + i * (TAB_W + TAB_GAP); }

// The four glyphs, drawn into a box of `d` px centred on (cx, cy). Ink colour is passed so
// the same code serves both bar heights without a second geometry to keep in step.
static void drawTabIcon(int i, int cx, int cy, int d, uint16_t ink) {
  int r = d / 2;
  if (i == 0) {
    // CARE — a heart. Two lobes and a point, in the house's crayon weight.
    const char *fn = "icons/feed";
    if (spriteLoad(fn)) { spriteBlitPanel(cx, cy, (float)d / 20.0f); return; }
    for (int k = 0; k < r; k++) {                       // the two lobes
      int w = k;
      tft.drawFastHLine(cx - r + 1, cy - r + 2 + k, w + 1, ink);
      tft.drawFastHLine(cx + r - 1 - w, cy - r + 2 + k, w + 1, ink);
    }
    for (int k = 0; k < r; k++)                          // the point
      tft.drawFastHLine(cx - r + 1 + k, cy + 2 + k, 2 * (r - k) - 1, ink);
  } else if (i == 1) {
    // PLAY — the mini board the game seat drew for itself: ink #, carrot x, heart-red o.
    int gx = cx - r, gy = cy - r, s = d;
    tft.drawFastVLine(gx + s / 3,     gy, s, ink);
    tft.drawFastVLine(gx + 2 * s / 3, gy, s, ink);
    tft.drawFastHLine(gx, gy + s / 3,     s, ink);
    tft.drawFastHLine(gx, gy + 2 * s / 3, s, ink);
    tft.drawLine(gx + 1, gy + 1, gx + s / 3 - 2, gy + s / 3 - 2, C_ORANGE);
    tft.drawLine(gx + s / 3 - 2, gy + 1, gx + 1, gy + s / 3 - 2, C_ORANGE);
    tft.drawCircle(gx + 5 * s / 6, gy + 5 * s / 6, s / 8 + 1, C_LOW);
  } else if (i == 2) {
    // MUSIC — a drawn quaver: a filled head with a stem and a flag.
    tft.fillCircle(cx - r / 2 + 1, cy + r / 2, r / 3 + 1, ink);
    tft.drawFastVLine(cx - r / 2 + r / 3 + 1, cy - r + 2, r + r / 2 - 1, ink);
    tft.drawFastVLine(cx - r / 2 + r / 3 + 2, cy - r + 2, r + r / 2 - 1, ink);
    tft.drawLine(cx - r / 2 + r / 3 + 2, cy - r + 2, cx + r - 1, cy - r / 2 + 1, ink);
  } else {
    // SLEEP — the same crescent as always, now YELLOW (Jon 8/14: "I just needed the same
    // shape as previously but yellow"). One disc with a second punched out of it, and the
    // punch stays the ground colour it always was. The outlines I briefly added here read
    // as two overlapping rings rather than a moon — the shape was never the problem.
    tft.fillCircle(cx, cy, r, C_MOON);
    tft.fillCircle(cx + r / 2, cy - r / 3, r - 1, C_PAPER);
  }
}

// One bar, two heights. The room shows the tall form; an open sheet shows the short one so
// the sheet gets the height back. Both put the LABEL on the same baseline, so opening a
// sheet never makes the four words jump — only the icons move up.
static void drawTabBar(int y, int h) {
  tft.fillRect(0, y, UI_W, UI_H - y, C_BONE);
  bool tall = (h >= 70);
  int iconD  = tall ? 28 : 16;
  int iconCy = y + (tall ? 34 : 14);
  int lblY   = y + h - 22;
  for (int i = 0; i < N_TABS; i++) {
    int x = tabX(i), cx = x + TAB_W / 2;
    tft.fillRoundRect(x, y, TAB_W, h, 7, C_PAPER);
    tft.drawRoundRect(x, y, TAB_W, h, 7, C_INK);
    drawTabIcon(i, cx, iconCy, iconD, C_INK);
    tft.setTextColor(C_INK, C_PAPER);
    // font1, <=5 ch: "MUSIC" is 30px inside a 55px tab, so no label can reach a border.
    tft.drawCentreString(TAB_LBL[i], cx, lblY, 1);
    // The music-off cue (R4a): a small hollow dot on the MUSIC tab whenever the level is 0.
    // Hollow, not filled — this is a state, not an alarm.
    if (i == 2 && g_musicLevel == 0)
      tft.drawCircle(x + TAB_W - 9, y + 9, 3, C_INK_SOFT);
  }
}

// 0..N_TABS-1, or -1. The 4px gutters count toward the tab on their left — mitten physics,
// the same first-match rule the sheet rows and the old care row used.
static int tabHit(int x, int y, int barY) {
  if (y < barY) return -1;
  for (int i = N_TABS - 1; i >= 0; i--)
    if (x >= tabX(i)) return i;
  return -1;
}

// The HTML's .ticker SCROLLS its text (a span translated across an overflow:hidden bar).
// Mine was static, which is why it read as a plain status line rather than the ticker.
// Drawn through a sprite so the scroll doesn't flicker.
static TFT_eSprite tickSpr = TFT_eSprite(&tft);
static void drawTicker() {
  char buf[80];
  if (millis() < g_tickUntil) snprintf(buf, sizeof(buf), "%s", g_ticker);
  else if (bunAway()) {
    // AWAY IS A STATE, NOT AN ANNOUNCEMENT (Jon 8/14: "it needs to keep the
    // scrolling text of bunbun hopped away, it just says jon baby 1h55m all
    // good"). The leaving line was a 4-second say() and then the idle line
    // came back reading "all good" — about an empty room. While he is gone the
    // ticker says so, and says the way to fix it, for as long as it is true.
    if (g_treatsOutMs)
      fmtPet(buf, sizeof(buf), "%s will be back soon...");
    else
      fmtPet(buf, sizeof(buf), "%s hopped away - leave a treat to call him home");
  } else {
    long m = ageMin();
    const char *st = S.stage == STAGE_EGG ? "EGG"
                   : (S.phase == PH_BABY ? (m >= TODDLER_AT ? "TODDLER" : "BABY")
                                         : S.phase == PH_TEEN ? "TEEN" : "ADULT");
    // Say something USEFUL when idle rather than just the age: what he wants, and which
    // button fixes it. The age alone told you nothing you could act on.
    const char *need = nullptr;
    if (!alive())            need = nullptr;
    else if (S.sick)         need = "feels poorly - MEDS";
    else if (S.food   < 35)  need = "hungry - FEED";
    else if (S.energy < 35)  need = "sleepy - ZZZ";
    else if (S.clean  < 35)  need = !S.poopN ? "grubby - BATH"
                                  : (S.phase == PH_TEEN)  ? "clothes everywhere - SWEEP"
                                  : (S.phase == PH_ADULT) ? "dishes piling up - SWEEP"
                                                          : "mess to clear - SWEEP";
    // The door has been called PLAY since the 0.1.227 menu redesign; this string
    // kept sending kids to a "GAME" button that no longer exists anywhere on the
    // device. Flagged as owed drift by the 8/12 registry sweep (§8) and carried
    // unfixed since. Every other line here names a real button — this one now does
    // too. The whole point of the hint is that it is actionable.
    else if (S.fun    < 35)  need = "bored - PLAY";
    else if (S.disc   < 25)  need = (S.phase == PH_BABY) ? "wants you - CUDL"
                                  : (S.phase == PH_TEEN) ? "behind - SCHL" : "behind - WORK";
    // Now playing leads, with any outstanding need APPENDED rather than replacing it. The
    // ticker scrolls, so there is room for both — and hiding "hungry - FEED" behind a song
    // title would make the one actionable line on the screen disappear for as long as music
    // happens to be on.
    if (g_nowPlaying[0] && audioLive()) {
      if (need) snprintf(buf, sizeof(buf), "%c %s  -  %s", 0x0E, g_nowPlaying, need);
      else      snprintf(buf, sizeof(buf), "%c %s", 0x0E, g_nowPlaying);
    }
    // The idle line LEADS WITH THE NAME the kid chose (their ask, 8/9:
    // "so kids can identify it is their own"). It is the line the ticker
    // shows most of the time, so this is where a name earns its keep —
    // "CLOVER  BABY  22m  all good". Needs still win: an unnamed-pet or
    // an urgent need keeps the shorter, more actionable form.
    else if (need && g_petName[0])
                             snprintf(buf, sizeof(buf), "%s  %s", g_petName, need);
    else if (need)           snprintf(buf, sizeof(buf), "%s", need);
    else if (m < 60)         snprintf(buf, sizeof(buf), "%s%s%s  %ldm  all good",
                                      g_petName[0] ? g_petName : "", g_petName[0] ? "  " : "",
                                      st, m);
    else                     snprintf(buf, sizeof(buf), "%s%s%s  %ldh%ldm  all good",
                                      g_petName[0] ? g_petName : "", g_petName[0] ? "  " : "",
                                      st, m / 60, m % 60);
  }
  tickSpr.fillSprite(C_INK);
  tickSpr.setTextColor(C_PAPER, C_INK);
  int w = tickSpr.textWidth(buf, 1);
  // Only scroll when it genuinely does not fit. Scrolling text that already fits just delays
  // reading it, which is most of why the ticker felt behind the action.
  if (w <= UI_W - 8) {
    tickSpr.drawString(buf, 4, 3, 1);
    g_tickX = 4;
  } else {
    tickSpr.drawString(buf, g_tickX, 3, 1);
    g_tickX -= 2;
    if (g_tickX < -w) g_tickX = UI_W;
  }
  // THE TICKER SURVIVES AS THE SHEET'S MESSAGE STRIP (P3). Its push position is the only
  // thing that changes: with the CARE sheet open it lands at y252, between the card grid and
  // the short tab bar, so "bunbun is full" and "nothing to sweep" arrive exactly where the
  // kid is already looking. Zero new copy, zero lost voice — every refusal, every verdict,
  // every care line still has somewhere to be said.
  tickSpr.pushSprite(0, g_careSheet ? SHEET_STRIP_Y : TICKER_Y);
}

// The one way back to the room. This exact quad was pasted at every panel exit — the wipe
// erases whatever owned the glass, statsInvalidate un-caches the bars the wipe took with it,
// then the chrome repaints direct to the panel. Extracted in P1 for exactly this moment:
// swapping the retired shell + icon row for the tab bar was a TWO LINE change here, and
// nothing else in the tree could resurrect the key row as a ghost over the new chrome
// (R3 — `drawShell` is now zero call sites, and zero definitions).
//
// It also closes any open sheet. "The room owns the glass again" is precisely what a sheet
// must not survive, and putting that here covers every one of the 22 sites at once —
// including the OTA night-restore, exitScreenOff and exitStandby (P3 must-not-regress:
// "sheet flags cleared by enterStandby/exitScreenOff/night-restore").
static void redrawRoomChrome() {
  g_careSheet = g_sleepSheet = false;
  // Land him on the floor, not mid-hop: a full-screen panel doesn't tick the lift, so
  // returning from a game drew one frame of a still-raised pet before he sank (NIT-12).
  g_sheetLift = g_sheetLiftTo = 0;
  tft.fillScreen(C_BONE);
  statsInvalidate();
  drawTabBar(TAB_Y, TAB_H);
}

// Closing a sheet is NOT a redrawRoomChrome(): the scene region was never touched, and a
// fillScreen would flash the whole room white for up to a frame just to put it back. Only
// the glass BELOW the scene is repainted — stats, ticker strip and the tall tab bar. The
// scene rows the sheet covered (112..179) come back on the very next push, which the caller
// makes immediate by resetting the frame schedule.
static void redrawRoomBelowScene() {
  g_careSheet = g_sleepSheet = false;
  tft.fillRect(0, SCENE_H, UI_W, UI_H - SCENE_H, C_BONE);
  statsInvalidate();
  drawTabBar(TAB_Y, TAB_H);
}

// ---------------- battery ----------------
// Found by probing every free ADC pin rather than trusting a pinout: GPIO9 sat at 2097mV
// while the rest floated near zero, i.e. a LiPo behind a 1:2 divider (2.097 x 2 = 4.19V).
static const int PIN_VBAT = 9;
static const float VBAT_DIV = 2.0f;
static float g_vbat = 0;

// Resting discharge curve for a single LiPo cell. Voltage is a poor fuel gauge under load,
// so this is smoothed hard below and only ever used to pick one of five bars.
static const struct { float v; int pct; } LIPO[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.90f, 65}, {3.80f, 50},
    {3.70f, 35},  {3.60f, 20}, {3.50f, 10}, {3.40f, 5},  {3.30f, 0},
};

static float batteryVolts() {
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) acc += analogReadMilliVolts(PIN_VBAT);
  return (acc / 8) * VBAT_DIV / 1000.0f;
}

// GPIO6 looked like a charger status line — low with a pullup while every other free pin read
// high — but it reads the same unplugged, so it is not one. Freenove document no such pin
// either. It is still sampled into the log so a correlation can be spotted if one exists, but
// nothing depends on it.
//
// Charging is therefore taken from the voltage itself. On a 400mAh cell running ~80mA the
// pack sags fast and unmistakably, so a fast average pulling ahead of a slow one means
// something is feeding it; and above 4.15V nothing but a charger can hold it there.
static const int PIN_CHRG = 6;      // logged only, not trusted
static bool g_charging = false;
static float g_vbatSlow = 0;

// ---- W-032 charge-sense experiment: rail stiffness via the motor ----
// The question: can bunbun FEEL whether he's plugged in, with no divider,
// no resistor, no new wire? Physics: a motor pulse is a ~100mA load step;
// on battery alone the rail sags through the cell's internal resistance,
// on a charger the node is held stiff. On a 2000mAh cell the difference
// is only ~5-15mV, so this measures with 32-sample bursts and reports —
// LOG-ONLY, nothing acts on it. Serial 'p' fires 10 probe pulses 1.5s
// apart; run it plugged, then unplugged, and compare the dip columns.
// If the clusters separate, W-032 ships as pure firmware; if they smear,
// the divider goes in as designed and this experiment retires honest.
static int g_probeLeft = 0;
static int g_probePhase = 0;
static uint32_t g_probeAt = 0;
static float g_probePre = 0;

static float probeBurstMv() {           // ~2ms of tight ADC reads
  uint32_t acc = 0;
  for (int i = 0; i < 32; i++) acc += analogReadMilliVolts(PIN_VBAT);
  return (acc / 32.0f) * VBAT_DIV;
}

static void chargeProbeTick() {
  if (!g_probeLeft) return;
  uint32_t now = millis();
  if (g_probePhase == 0 && now >= g_probeAt) {
    g_probePre = probeBurstMv();        // baseline, motor still
    hapticPulse();                      // 200ms window at pulse duty
    g_probePhase = 1;
    g_probeAt = now + 110;              // sample mid-pulse, past spool-up
  } else if (g_probePhase == 1 && now >= g_probeAt) {
    float mid = probeBurstMv();
    Serial.printf("W-032 probe %d: pre=%.1fmV mid=%.1fmV dip=%.1fmV chrg=%d bl=%u\n",
                  g_probeLeft, g_probePre, mid, g_probePre - mid,
                  (int)g_charging, g_blNow);
    g_probePhase = 0;
    g_probeLeft--;
    g_probeAt = now + 1500;
    if (!g_probeLeft) Serial.println("W-032 probe: run complete");
  }
}
// Reference point for the direction-of-travel test in batteryUpdate(). Deliberately NOT a rate:
// see the comment there for why this is the only capacity-agnostic signal available.
static float g_vbatRef = 0;
static void powerLogReset();      // defined with the log, below
static void traceSave();          // defined with the trace, below
static void powerLogTick();
static void batteryBegin() {
  pinMode(PIN_CHRG, INPUT_PULLUP);
  g_vbat = g_vbatSlow = g_vbatRef = batteryVolts();
  // Seed the state from the level alone. With both averages equal there is no trend to read
  // yet, and the falling test would otherwise report "not charging" for the first few seconds
  // on a board that is plainly plugged in. Take the USB host as proof when it is there, since
  // otherwise every reboot starts out claiming "not charging" and has to climb the whole
  // voltage band before it can correct itself.
  g_charging = (g_vbat >= 4.10f) || (bool)Serial;
}
// Discharge meter. Rather than estimating current from a datasheet, watch what the pack
// actually does: note the voltage and time when the charger comes off, and from the drop so
// far project how long the whole usable range (4.20V -> 3.40V) will take. It needs a few
// minutes of real running before it means anything, but then it is measured, not guessed.
static float    g_unplugV = 0;
static uint32_t g_unplugMs = 0;
static bool     g_wasCharging = true;
static float projectedHours() {
  if (!g_unplugMs || g_charging) return 0;
  float dropped = g_unplugV - g_vbat;
  uint32_t elapsed = millis() - g_unplugMs;
  if (dropped < 0.015f || elapsed < 120000UL) return 0;      // too early to say
  float hours = elapsed / 3600000.0f;
  return (0.80f / dropped) * hours;                          // 4.20 -> 3.40 is the usable span
}

static void batteryUpdate() {
  // Heavy smoothing: the backlight and the amplifier both pull the rail around, and an
  // indicator that twitches between bars is worse than no indicator at all.
  float raw = batteryVolts();
  float jump = raw - g_vbat;                          // measured against the smoothed value
  g_vbat += jump * 0.05f;
  g_vbatSlow += (g_vbat - g_vbatSlow) * 0.0015f;      // roughly a minute behind
  float d = g_vbat - g_vbatSlow;

  // A STEP is the real signature. Plugging in lifts the rail at once — hundreds of mV if the
  // pack was low — and unplugging drops it just as sharply. Load changes cannot fake this:
  // switching the whole backlight off shifts maybe 10mV across this cell's internal
  // resistance, well under the threshold. The slower tests below only catch what the step
  // missed, such as connecting while already near full.
  // USB HOST ENUMERATION — the one instant, capacity-proof answer available on this board.
  // ARDUINO_USB_MODE=1 with CDC on boot means the USB-C socket is wired to the S3's own USB
  // rather than through a serial bridge, so if a host has enumerated us there is certainly
  // VBUS on that socket. Checked FIRST because it is a fact rather than an inference, and it
  // costs none of the minutes the voltage band needs.
  //
  // It only sees hosts, not dumb chargers: a wall wart supplies VBUS without ever enumerating,
  // so that case still falls through to the voltage tests below. That is the whole reason the
  // band survives alongside this.
  //
  // FALLING BEATS EVERYTHING, including the USB test, and that order matters. A stale CDC
  // "connected" flag after the cable is pulled would otherwise pin this to "charging" forever
  // and the discharge log would never start. Physically it is sound: the terminal voltage
  // cannot fall while the pack is net gaining charge.
  // A STEP SNAPS THE SMOOTHED VALUE WITH IT (field, 8/8: "flickered with a
  // charge icon for a second but never settled"). The old re-base set the
  // reference to the raw post-step voltage while g_vbat was still smoothing
  // its way up to it — so the sustained-fall test below saw g_vbat sitting
  // >15mV under the new reference and cancelled the plug-in on the next
  // pass, and with the slow-rise rules gone there was no way back. A step
  // is the one unambiguous event here; there is nothing gradual about it,
  // so the smoothed value has no business lagging through it.
  if (jump < -0.050f)       { g_charging = false; g_vbat = raw; g_vbatSlow = raw; g_vbatRef = raw; }
  // A SUSTAINED FALL OUTRANKS THE USB FLAG. (bool)Serial means "a host has the CDC port open",
  // and it does not always clear when the cable goes — stale-true on battery used to pin this
  // to "charging" until a >50mV drop happened along. A pack that is genuinely falling is not
  // being charged, whatever any flag claims.
  else if (g_vbat < g_vbatRef - 0.015f) { g_charging = false; g_vbatRef = g_vbat; }
  else if ((bool)Serial)    { g_charging = true;  g_vbatRef = g_vbat; }
  // A step is unambiguous, so it also re-bases the reference below — otherwise the 300mV a
  // plug-in adds would sit in the band for ages afterwards, still being re-counted.
  else if (jump > 0.050f)   { g_charging = true;  g_vbat = raw; g_vbatSlow = raw; g_vbatRef = raw; }
  // LOAD MUST BE STEADY before any trend is believed. Changing the backlight moves the terminal
  // voltage instantly across the cell's internal resistance — 61mV on the tired 400mAh pack,
  // but only a few mV on a healthy one — so no fixed correction works across batteries. Rather
  // than model the step, ignore the trend while it is happening and re-base the reference once
  // things settle, so the step is absorbed instead of being counted as charging.
  //
  // This replaces a measured 0.33mV-per-backlight-unit compensation, which was derived from
  // averages contaminated by the discharge trend, over-corrected, and simply moved the flapping
  // to the other direction. Waiting needs no constant and works on any cell.
  // THIRTY seconds, not four. Taking load off a cell does not settle instantly: the terminal
  // voltage jumps immediately across the internal resistance and then keeps drifting up for
  // tens of seconds as the chemistry relaxes. At a 4s gate the trend test resumed while that
  // drift was still going, read it as a charger, and — because the backlight follows the
  // charging flag — brightened the screen, reloaded the cell, and started the whole cycle over.
  // The reference is re-based throughout, so the entire recovery is absorbed rather than judged.
  else if (millis() - g_blChangedMs < 60000) { g_vbatRef = g_vbat; }
  // Falling needs 15mV, not 8. At 100mA into a 2000mAh pack the charge ramp is only ~0.8mV/min,
  // so the margin over a transient dip is thin — and the amplifier and SD card both twitch the
  // rail by several mV with no change in charge state. A dropped 1-minute-EMA test used to sit
  // here too and was the twitchiest of the lot: it flipped the icon off mid-charge. Asymmetry
  // is deliberate; a false "not charging" is the visible, annoying direction.
  // NO absolute-voltage rule here any more. There used to be `g_vbat >= 4.10f -> charging`, on
  // the reasoning that only a charger holds a pack that high. That was true of the tired 400mAh
  // cell, which sagged under 4.10V the moment it came off the charger — but a healthy 2000mAh
  // pack RESTS at 4.15-4.20V for a long time after unplugging, so the rule fired constantly on
  // battery and is the false "charging" that kept showing up. It was previously masked by a
  // falling-EMA test sitting in front of it, which was removed when the hysteresis band went in.
  //
  // Nothing is lost: USB enumeration catches the host case instantly, the +/-50mV step catches
  // plug and unplug, and the band below catches a wall charger. A full pack merely being held at
  // its resting voltage is not evidence of anything, which is exactly why it misled.
  // DIRECTION OF TRAVEL, not rate — this is the part that does not care how big the pack is.
  //
  // Every rate-based test above is implicitly tuned to a capacity: mV-per-minute scales with
  // the cell, so thresholds picked on a 400mAh pack read a 2000mAh one as permanently flat and
  // the icon sticks on whatever it was seeded with. What does NOT depend on capacity is the
  // sign: charging always means the voltage ends up higher, discharging always means lower.
  // Capacity only changes how long you wait to see it.
  //
  // So instead of asking "how fast is it moving", hold a reference and wait for the voltage to
  // travel a fixed distance from it in either direction. Whenever it does, that direction is
  // the answer and the reference moves up to meet it. A bigger pack simply takes longer to
  // cross the band; it never gets the wrong answer, and there is no capacity constant anywhere.
  //
  // ASYMMETRIC on purpose. Falling only needs to clear the +/-2.5mV measurement noise, but
  // rising has to clear something much larger: taking load OFF the cell lifts the terminal
  // voltage across its internal resistance, and dimming this backlight measurably moves it
  // 10-20mV with no charger involved at all. At an 8mV rising band every dim step read as
  // "charging", which then made the plug/unplug edge fire spuriously. 25mV sits above the
  // load-step artefact while still being far below a real charge ramp.
  // A rise must be BOTH large enough and SUSTAINED. 10mV alone kept tripping on battery: the
  // load-steady gate only knows about the backlight, so the amplifier, SD reads and CPU
  // frequency changes all move the rail without it noticing — and each time load comes off, the
  // cell keeps recovering for tens of seconds afterwards. Those are transients; they plateau.
  // A charger does not — it climbs and keeps climbing.
  //
  // So hold the rise for 90 seconds before believing it. A load-step recovery has long since
  // levelled off by then, while a charger is still going. This is the "slow it down" knob: it
  // costs a delay before the bolt appears and buys a symbol that stays put.
  // NO slow rising rule. There used to be "rose 20mV and held 90s -> charging", meant to catch a
  // dumb wall charger that never enumerates. On battery it fired by itself: taking load off the
  // cell lifts the terminal voltage and the chemistry keeps recovering for minutes afterwards,
  // which clears 20mV and holds it. That was the remaining source of false charge readings.
  //
  // Nothing much is lost. This board has a real power path, so plugging in removes the load from
  // the cell ENTIRELY and the terminal voltage jumps by the full IR drop at once — a big, sharp
  // step that the >50mV test above already catches. The slow band was only covering cases the
  // step missed, and it cost more than it bought.
  if (g_wasCharging && !g_charging) {
    g_unplugV = g_vbat; g_unplugMs = millis();
    // NO powerLogReset() here. Wiping the log on this edge made the log only as trustworthy as
    // the charge detector: one spurious "charging" blip (see the asymmetric band above) and the
    // whole run vanished, which is exactly what emptied two 10-minute tests. The log now runs
    // continuously for the whole power-on session and records the charging flag per sample, so
    // the dump can pick out the discharge stretch afterwards and a misread costs one sample
    // rather than everything.
  }
  if (g_charging) { g_unplugMs = 0; }
  // Persist the trace on EITHER edge. The plug-in edge is the one that matters — it fires
  // seconds before the host can connect and reboot us — but saving on unplug too means a run
  // that ends some other way is still recoverable.
  if (g_wasCharging != g_charging) traceSave();
  g_wasCharging = g_charging;
  powerLogTick();
}
static int batteryPercent() {
  if (g_vbat >= LIPO[0].v) return 100;
  for (unsigned i = 1; i < sizeof(LIPO) / sizeof(LIPO[0]); i++) {
    if (g_vbat >= LIPO[i].v) {
      float t = (g_vbat - LIPO[i].v) / (LIPO[i - 1].v - LIPO[i].v);
      return (int)(LIPO[i].pct + (LIPO[i - 1].pct - LIPO[i].pct) * t);
    }
  }
  return 0;
}

// ---- power log ----
// Survives losing USB, which is the whole point: unplug, use it on battery for as long as you
// like, plug back in, and the run is dumped to serial as CSV on boot. Serial cannot observe a
// battery test, because attaching the cable IS the thing that ends it.
// `bl` is the raw backlight value, not a bright/dim bit. A boolean here reported "0% bright"
// for a run the screen was visibly lit through, because the threshold sat at BL_MID=70 and the
// panel is perfectly readable at 70/255 — so the flag and the eye disagreed with no way to
// tell which was right. The number settles it, and it is what scales the draw estimate anyway.
struct PowerSample { uint16_t mv; uint8_t pct; uint8_t flags; uint8_t bl; };
static const int PLOG_MAX = 288;
// Starts FINE and coarsens as it fills, rather than sampling every 5 minutes from the outset.
// At 5 minutes a short test recorded nothing at all: powerLogReset() restarts the interval at
// the moment the charger drops, so the first sample only landed 5 minutes in and a 5-minute
// run came back empty. One a minute means even a brief unplug produces a usable slope.
// FIVE SECONDS to start. Serial dies the instant USB comes out — the cable is both the power
// and the data link — so the log is the only witness to a discharge, and its interval sets the
// shortest test that can tell you anything. At 60s a 20-second unplug fell entirely between
// two samples and recorded nothing. Decimation (see powerLogAdd) coarsens this as the buffer
// fills, so starting fine costs nothing on a long run: 288 samples covers 24 min at 5s, then
// 48 min at 10s, and so on indefinitely.
static const uint32_t PLOG_INTERVAL0 = 5000;
static uint32_t g_plogInterval = PLOG_INTERVAL0;
static PowerSample g_plog[PLOG_MAX];
static uint16_t g_plogN = 0;
static uint32_t g_plogLast = 0;

static void powerLogSave() {
  prefs.begin("bunbun", false);
  prefs.putUShort("plogN", g_plogN);
  prefs.putULong("plogIv", g_plogInterval);
  prefs.putBytes("plog2", g_plog, g_plogN * sizeof(PowerSample));
  prefs.end();
}
static void powerLogAdd();          // defined with the tick, below
static void powerLogReset() {
  g_plogN = 0; g_plogInterval = PLOG_INTERVAL0; g_plogLast = millis();
  // Record the starting point IMMEDIATELY rather than waiting out the first interval. Without
  // this the log has no t=0 reference, and a run shorter than one interval produces nothing at
  // all — which is exactly how two 10-minute tests came back empty.
  powerLogAdd();
  powerLogSave();
}

// Prints whatever is in RAM. Kept separate from the NVS load so the log can be read WITHOUT a
// reboot: samples only reach NVS every 12th one, so a short test lives entirely in RAM and a
// reset-to-dump would show a stale copy — or, as happened, just the single boot sample.
static void powerLogPrint() {
  if (!g_plogN) { Serial.println("plog: empty"); return; }
  const float stepMin = g_plogInterval / 60000.0f;
  Serial.printf("plog: %u samples, %.1f min apart\nmin,volts,pct,charging,gpio6,backlight\n",
                g_plogN, stepMin);
  for (int i = 0; i < g_plogN; i++)
    Serial.printf("%.1f,%.3f,%u,%u,%u,%u\n", i * stepMin, g_plog[i].mv / 1000.0f, g_plog[i].pct,
                  (unsigned)(g_plog[i].flags & 1), (unsigned)((g_plog[i].flags >> 1) & 1),
                  (unsigned)g_plog[i].bl);
  // The number actually wanted: how fast the pack fell, and what that implies for a full run.
  // The log spans the whole session, charging and not, so find the LONGEST run of consecutive
  // discharging samples and measure that. Doing it here rather than by erasing at the plug/
  // unplug edge means a misread charging flag costs one sample instead of the entire run.
  int bs = 0, bn = 0;
  for (int i = 0; i < g_plogN;) {
    if (g_plog[i].flags & 1) { i++; continue; }
    int j = i;
    while (j < g_plogN && !(g_plog[j].flags & 1)) j++;
    if (j - i > bn) { bn = j - i; bs = i; }
    i = j;
  }
  if (bn < 3) { Serial.println("plog: no discharge run of 3+ samples yet"); return; }
  float drop = (g_plog[bs].mv - g_plog[bs + bn - 1].mv) / 1000.0f;
  float hours = (bn - 1) * stepMin / 60.0f;
  int blSum = 0, blMin = 255, blMax = 0;
  for (int i = bs; i < bs + bn; i++) {
    blSum += g_plog[i].bl;
    if (g_plog[i].bl < blMin) blMin = g_plog[i].bl;
    if (g_plog[i].bl > blMax) blMax = g_plog[i].bl;
  }
  Serial.printf("plog: discharge run = samples %d..%d (%.1f min, backlight avg %d, %d..%d)\n",
                bs, bs + bn - 1, (bn - 1) * stepMin, blSum / bn, blMin, blMax);
  if (drop > 0.01f)
    Serial.printf("plog: fell %.3fV in %.2fh -> %.1fh for the full 4.20-3.40V range\n",
                  drop, hours, (0.80f / drop) * hours);
  else
    Serial.printf("plog: only fell %.3fV in %.2fh - too little to project, run it longer\n",
                  drop, hours);
}

// Boot-time variant: pull the previous session out of NVS first, then print it.
static void powerLogDump() {
  prefs.begin("bunbun", true);
  g_plogN = prefs.getUShort("plogN", 0);
  g_plogInterval = prefs.getULong("plogIv", PLOG_INTERVAL0);
  if (g_plogN > PLOG_MAX) g_plogN = 0;
  if (!g_plogInterval) g_plogInterval = PLOG_INTERVAL0;
  if (g_plogN) prefs.getBytes("plog2", g_plog, g_plogN * sizeof(PowerSample));
  prefs.end();
  esp_reset_reason_t rr = esp_reset_reason();
  const char *rrs = rr == ESP_RST_POWERON  ? "POWERON (lost all power - no battery?)"
                  : rr == ESP_RST_SW       ? "SW (serial open / reflash)"
                  : rr == ESP_RST_DEEPSLEEP? "DEEPSLEEP"
                  : rr == ESP_RST_BROWNOUT ? "BROWNOUT" : "other";
  Serial.printf("boot: reset=%d %s\n", (int)rr, rrs);
  powerLogPrint();
}

static void powerLogAdd() {
  // DECIMATE rather than stop. Full used to mean "stop recording", which capped a run at the
  // buffer length; instead throw away every other sample and double the interval, so the log
  // covers an unbounded run at whatever resolution still fits. Fine detail early, coarse later
  // — which is the right way round, since the interesting part is the first hour.
  if (g_plogN >= PLOG_MAX) {
    for (int i = 0; i < PLOG_MAX / 2; i++) g_plog[i] = g_plog[i * 2];
    g_plogN = PLOG_MAX / 2;
    g_plogInterval *= 2;
  }
  g_plog[g_plogN].mv = (uint16_t)(g_vbat * 1000.0f);
  g_plog[g_plogN].pct = (uint8_t)batteryPercent();
  g_plog[g_plogN].flags = (g_charging ? 1 : 0) | (digitalRead(PIN_CHRG) == LOW ? 2 : 0);
  g_plog[g_plogN].bl = g_blNow;
  g_plogN++;
  // NO periodic persist. This save ran every 60 seconds (12 samples x 5s), and every NVS
  // commit is a FLASH WRITE — which freezes the caches on BOTH cores for tens of milliseconds,
  // hundreds when a sector erase rotates in. That was the "hitch 3-4 times per 4-minute song"
  // the user could see from the couch: one frozen frame per minute, like clockwork. The log
  // was battery bring-up instrumentation on the standalone board; in this build it lives in
  // RAM (the `p` command still prints it) and persists only at standby/reset, where a stall
  // costs nothing anyone can see.
}

static void powerLogTick() {
  if (millis() - g_plogLast < g_plogInterval) return;
  g_plogLast = millis();
  powerLogAdd();
}

// Small cell in the scene's top-right corner, drawn in the room's own ink so it sits in the
// picture rather than on top of it.
// The digits sit just ABOVE the Kit-Cat clock, centred on it — so the wall clock and the time
// read as one object rather than two competing status marks. Follows the same per-phase spot as
// drawCatClock(), so it moves with the cat when a room changes rather than needing its own
// coordinates kept in sync.
//
// 24-hour: the room's lighting already tells you morning from evening, so an am/pm suffix would
// be two more characters carrying no information.
static void drawClock() {
  if (!g_clockSet) return;              // nothing honest to show yet
  const ItemSpot &sp = (S.phase == PH_BABY) ? CATCLOCK_BABY
                     : (S.phase == PH_TEEN) ? CATCLOCK_TEEN : CATCLOCK_ADULT;
  int cm = clockNowMin();
  // t[8], was t[6]: exactly HH:MM fits in 6, but the compiler's range
  // analysis (rightly) refuses to assume cm stays under 100 hours, and the
  // W-020 build promoted that doubt to an error. Two spare bytes buy peace.
  // 12-hour with am/pm (Jon 8/14) — a wall clock in a child's room does not
  // speak 24-hour. No leading zero on the hour, so "5:04pm" stays inside the
  // pad's width at font 1 (7 chars = 42px in the 38px pad's 44px slop... the
  // suffix is drawn a size down to keep the whole stamp under 38).
  char t[12];
  int h12 = (cm / 60) % 12; if (h12 == 0) h12 = 12;
  snprintf(t, sizeof(t), "%d:%02d%s", h12, cm % 60, (cm / 60) >= 12 ? "pm" : "am");
  // Back where it always was, under the cat clock (Jon 8/14).
  int cx = (int)(sp.x * VIEW + 0.5f);
  int y  = (int)(sp.y * VIEW + 0.5f) - 10;   // font 1 is 8px tall, so this clears the ears
  if (y < 1) y = 1;                          // never let the adult's high shelf push it off
  // Flip to light ink once the room goes dark. The digits are drawn AFTER the night dimming,
  // so they keep their full-strength colour while the wall behind them sinks — dark text on a
  // dark wall, which is why it disappeared in the evening. Switching the ink is enough; the
  // clock is a glance, not something to read across the room.
  scene.setTextColor(nightAmount() > 0.45f ? C_BONE : C_INK);
  scene.setTextDatum(TC_DATUM);
  scene.drawString(t, cx, y, 1);

}

// A black banner with white letters, across the middle of the room where bunbun is. Everything
// stops when paused — the animation clock, movement, ambient events — so without this the only
// cue was the pin button reading PLAY instead of PAUSE, which is far too easy to miss. It goes
// over the character deliberately: covering the thing that has stopped moving is what makes the
// stillness read as intentional rather than broken.
// FEATURE (wish) button on the pause screen: centred below the PAUSED banner.
static const int WISH_BW = 128, WISH_BH = 26;
static const int WISH_BX = (SCENE_W - WISH_BW) / 2;
static const int WISH_BY = (SCENE_H - 48) / 2 + 48 + 10;

// Floating Z's while bunbun sleeps (Jon, launch night) — three glyphs
// rising from above his head on staggered loops, drawn as text so they
// cost no art and ride every age's sprite. Into the scene sprite, so they
// dim with the room and vanish under the paused banner like everything
// else. The snore haptic (touch-gated, haptics.h) breathes on the same
// idea: the Z's are what sleep looks like, the snore is what it feels like.
static void drawSleepZs() {
  if (S.lights || !alive() || bunAway()) return;
  // The child's sleep animation BAKES its own Z's into the frames - drawing the firmware's
  // on top gave him two sets. One sleeper, one snore.
  if (sceneActAnim("sleep")) return;
  // The Z's ride the sheet lift too, or a sleeping bunny hoisted above his SLEEP sheet would
  // leave his snores behind on the floor, where the clipped push discards them.
  int fx = gx2s(S.x), fy = gy2s(S.y) - g_sheetLift;
  scene.setTextDatum(MC_DATUM);
  uint32_t t = millis();
  for (int i = 0; i < 3; i++) {
    float ph = ((t / 20 + i * 60) % 180) / 180.0f;   // 3.6s rise, staggered
    if (ph > 0.85f) continue;                        // fade by absence at the top
    int zx = fx + 18 + i * 9 + (int)(4.0f * sinf(ph * 6.28f + i));
    int zy = fy - 24 - (int)(ph * 30.0f);
    if (zy < 8) continue;
    scene.setTextColor(C_INK);
    scene.drawString(i == 1 ? "Z" : "z", zx, zy, i == 1 ? 2 : 1);
  }
}

static void drawPausedBanner() {
  if (!g_paused) return;
  // Dim the whole room first, so the banner sits on something visibly switched off rather than
  // floating over a normal-looking scene. Halving each RGB565 channel is the standard trick:
  // >>1 shifts every channel down one bit and the mask clears the bits that bled across
  // boundaries. Read and write both go through the sprite API so its byte-swapping cancels out.
  //
  // Cost is fine BECAUSE it is paused: nothing is animating, so this is the only work the frame
  // is doing.
  // Direct buffer access — this is 43,200 read-modify-writes, and through the sprite API on a
  // PSRAM sprite it was a full-second stall when entered during streaming. The swap has to
  // bracket the arithmetic: shifting the SWAPPED representation bleeds a bit across the byte
  // boundary into the wrong channel.
  for (int y = 0; y < SCENE_H; y++) {
    uint16_t *row = sceneRow(y);
    for (int x = 0; x < SCENE_W; x++)
      row[x] = bswap16((uint16_t)((bswap16(row[x]) >> 1) & 0x7BEF));
  }

  const int h = 48, y = (SCENE_H - h) / 2;
  scene.fillRect(0, y, SCENE_W, h, C_INK);
  scene.drawFastHLine(0, y, SCENE_W, TFT_WHITE);              // crisp edges against the dim room
  scene.drawFastHLine(0, y + h - 1, SCENE_W, TFT_WHITE);
  scene.setTextColor(TFT_WHITE);
  scene.setTextDatum(MC_DATUM);
  scene.drawString("PAUSED", SCENE_W / 2, y + 17, 4);
  scene.drawString("press PLAY to carry on", SCENE_W / 2, y + 36, 1);

  // The WISH button is GONE from this banner (Jon 8/14: "we can also drop
  // wish from the pause screen"). It lived here because pause was the only
  // place quiet enough to record — P3 gave wishing its own door on the CARE
  // sheet, which quiets the room by itself, so the overlay went back to
  // being one thing: a stopped room and the way to start it again.
  bool rec = wish_recorder_active();

  // Unsent-wish count below the button (by request, with the spec given
  // verbatim: "5 unsent wishes below the wish button and if there is
  // nothing, it stays blank"). SPIFFS directory listing is not a per-frame
  // operation — the count is cached and refreshed every few seconds, which
  // is faster than any wish can appear or leave anyway.
  static int pendCache = 0;
  static uint32_t pendAt = 0;
  if (pendAt == 0 || millis() - pendAt > 3000) {
    pendAt = millis();
    pendCache = wish_uploader_pending();
  }
  if (!rec && pendCache > 0) {
    // If the uploader is holding these back because a phone is streaming (or
    // paused-but-connected, which still trickles samples), say what to DO
    // about it (Jon 8/11: "it needs to say stop streaming music") rather than
    // just counting them. Same gate the uploader uses: real stream < 45s ago.
    uint32_t ls = audio_output_last_stream_ms();
    bool heldForMusic = ls && (millis() - ls < 45000);
    char w[36];
    if (heldForMusic) {
      snprintf(w, sizeof(w), "stop music to send %d", pendCache);
    } else {
      snprintf(w, sizeof(w), "%d unsent wish%s", pendCache,
               pendCache == 1 ? "" : "es");
    }
    scene.setTextColor(TFT_WHITE);
    scene.setTextDatum(MC_DATUM);
    scene.drawString(w, SCENE_W / 2, y + h - 12, 1);   // in the banner's own footer now
    scene.setTextDatum(TL_DATUM);
  }
}

// WiFi state, tucked just left of the battery so the two read as one status cluster rather than
// marks scattered round the frame. Nothing is drawn at all when the radio is off, which is the
// normal state — an icon that is always present stops carrying information.
//
// Connected: the usual three arcs. Setup mode: the letters "AP", because a signal glyph would
// imply a working connection when what it actually means is "come and configure me".
// Takes the target by reference so the same code serves the game screen and the nap screen —
// TFT_eSprite derives from TFT_eSPI, so `scene` and `tft` are both valid here.
static void drawWifiOn(TFT_eSPI &g, int x, int y, uint16_t ink, uint16_t soft) {
  if (g_netState == NET_OFF) return;
  if (g_netState == NET_AP) {
    g.setTextColor(ink);
    g.setTextDatum(TL_DATUM);
    g.drawString("AP", x + 2, y + 1, 1);
    return;
  }
  // Three arcs over a dot. Short horizontal runs rather than real arcs — at 13px wide that
  // reads more cleanly than anything curved, and costs three drawFastHLine calls.
  const int cx = x + 7, by = y + 9;
  const bool live = (g_netState == NET_ONLINE);
  uint16_t c = live ? ink : soft;           // still joining: drawn faint
  g.fillRect(cx - 1, by - 1, 2, 2, c);                     // the dot
  g.drawFastHLine(cx - 2, by - 3, 5, c);                   // small arc
  if (live) {
    g.drawFastHLine(cx - 4, by - 5, 9, c);                 // middle arc
    g.drawFastHLine(cx - 6, by - 7, 13, c);                // outer arc
  }
}
// Game screen: just left of the battery, so the two read as one status cluster rather than
// marks scattered round the frame.
static void drawWifi() { drawWifiOn(scene, UI_W - 44, 5, C_INK, C_INK_SOFT); }

static void drawBattery() {
  const int w = 20, h = 10, x = UI_W - w - 6, y = 5;
  int pct = batteryPercent();
  scene.drawRoundRect(x, y, w, h, 2, C_INK);
  scene.fillRect(x + w, y + 3, 2, h - 6, C_INK);          // the nub
  // Green normally, red when genuinely low. It was drawn in ink, which read as "a dark blob"
  // rather than a charge level and gave no at-a-glance sense of state.
  int fill = ((w - 4) * pct) / 100;
  uint16_t c = (pct <= 15) ? C_LOW : RGB565(60, 190, 90);
  if (fill > 0) scene.fillRect(x + 2, y + 2, fill, h - 4, c);
  // Charging: a solid black lightning bolt over the green fill. Drawn as two stacked wedges so
  // it reads as a bolt at 10px rather than a smudge, and in ink so it shows against the green
  // whatever the charge level is.
  if (g_charging) {
    const int bx = x + w / 2, by = y + 1;
    for (int i = 0; i < 4; i++) scene.drawFastHLine(bx - i / 2, by + i, 3 - i / 2, C_INK);
    scene.drawFastHLine(bx - 3, by + 4, 6, C_INK);
    for (int i = 0; i < 4; i++) scene.drawFastHLine(bx - 2 + i / 2, by + 5 + i, 3 - i / 2, C_INK);
  }
}

// ---------------- travel standby ----------------
// A backpack mode. NOT esp_deep_sleep: that cuts the panel entirely, and a wake button you
// can see has to stay lit. This instead strips out everything expensive while leaving one
// dim, static screen up — the backlight is far and away the biggest draw, so taking it to
// ~2% is most of the saving on its own, with the CPU dropped to 80MHz and audio silenced.
// The clock keeps running because millis() keeps running, so nothing needs carrying over.
static bool g_standby = false;

// ---- high-resolution trace ----
// The 5-second power log answers "how fast does the pack fall". This answers a different
// question: "did the board do the right things at the moment the cable moved". It samples
// every second into a RING, so the last ~7 minutes are always present and a transition is
// captured no matter when it happens — there is nothing to arm or start.
//
// It records the inputs to the charge decision alongside its outputs, so the logic can be
// checked rather than trusted: the reference voltage and the distance from it are what the
// 8mV/25mV bands act on, and `usb` is the CDC enumeration flag that overrides them.
struct Trace {
  uint32_t ms;
  uint16_t mv, refmv;
  uint16_t idles;              // seconds since the screen was last touched
  uint8_t  pct, bl, cpu, flags;
};
static const int TRACE_MAX = 400;
static Trace g_trace[TRACE_MAX];
static uint16_t g_traceHead = 0, g_traceCount = 0;
static uint32_t g_traceLast = 0;
static uint32_t g_traceInterval = 1000;   // coarsens as the ring fills; see traceTick()
static uint32_t g_traceCsvLast;   // defined with traceFlushSD, below
static void traceFlushSD();

static void traceTick() {
  if (millis() - g_traceLast < g_traceInterval) return;
  g_traceLast = millis();
  // DECIMATE when full instead of dropping the oldest. A plain ring keeps only the last
  // TRACE_MAX seconds, so an hour-long run would come back holding the final six minutes —
  // useless for a discharge slope. Halving the resolution and doubling the interval keeps the
  // WHOLE run at whatever detail still fits: 1s for the first ~7 min, then 2s, 4s, 8s and so on.
  // Fine detail early is the right way round, since transitions happen at the start.
  if (g_traceCount >= TRACE_MAX) {
    int start = (g_traceHead + TRACE_MAX - g_traceCount) % TRACE_MAX;
    for (int i = 0; i < TRACE_MAX / 2; i++)
      g_trace[i] = g_trace[(start + i * 2) % TRACE_MAX];
    g_traceCount = TRACE_MAX / 2;
    g_traceHead  = TRACE_MAX / 2;
    g_traceInterval *= 2;
  }
  Trace &t = g_trace[g_traceHead];
  t.ms    = millis();
  t.mv    = (uint16_t)(g_vbat * 1000.0f);
  t.refmv = (uint16_t)(g_vbatRef * 1000.0f);
  t.idles = (uint16_t)min<uint32_t>((millis() - g_lastTouchMs) / 1000, 65535);
  t.pct   = (uint8_t)batteryPercent();
  t.bl    = g_blNow;
  t.cpu   = (uint8_t)(getCpuFrequencyMhz() / 10);
  t.flags = (g_charging ? 1 : 0) | ((bool)Serial ? 2 : 0) | (g_standby ? 4 : 0)
          | (g_paused ? 8 : 0) | (g_musicOn ? 16 : 0) | (g_ampOn ? 32 : 0);
  g_traceHead = (g_traceHead + 1) % TRACE_MAX;
  if (g_traceCount < TRACE_MAX) g_traceCount++;
  // Flush to the card periodically. 30s, NOT 10: this writes ~22KB to the same card the audio
  // task is streaming MP3s from, and at 10s intervals the contention was audible as periodic
  // distortion under the music. A diagnostic must not degrade the thing it is observing. The
  // charge transitions still flush immediately (see batteryUpdate), which is what actually
  // matters for catching an unplug.
  if (millis() - g_traceCsvLast >= 30000) traceFlushSD();
}

static void tracePrintBuf(const Trace *buf, uint16_t count, uint16_t head, const char *what);

// Write the last TRACE_CSV_N samples to /trace.csv, oldest first, replacing the file.
//
// This exists because nothing on the HOST side can read the RAM ring reliably: the first
// connection after the cable is physically re-plugged re-enumerates the USB device and reboots
// the chip, wiping the ring before it can be dumped. The card does not care — it keeps the
// last few minutes on disk at all times, so the evidence is already written before any
// connection is attempted, and it survives resets, brownouts and flat batteries alike.
//
// Rewritten whole each time rather than appended in a circle, so the file is always in
// chronological order and readable straight off the card with no wrap point to reason about.
// 180 lines is about 12KB, which at this cadence is a trivial load on the card.
// 320s of CSV, so a 5-minute run fits WHOLE rather than only its tail. The first minutes after
// the charger drops are surface-charge relaxation and have to be discarded, so the file has to
// span enough to contain both the part being thrown away and a usable stretch after it.
// Write the WHOLE ring, not a window into it. Decimation already caps this at TRACE_MAX rows,
// and every row carries its own ms timestamp, so a long run stays readable at coarser spacing
// rather than being truncated to its tail.
static const int TRACE_CSV_N = TRACE_MAX;


static void traceFlushSD() {
  if (!g_sdOk || !g_traceCount) return;
  File f = SD_MMC.open("/trace.csv", FILE_WRITE);      // FILE_WRITE truncates
  if (!f) {
    // NEVER remount from here. This runs on the UI task, and calling SD_MMC.end()/begin() while
    // the audio task is mid-read tears the mount out from under it — a LoadProhibited panic.
    // Just flag the card as down; the audio task owns recovery and will pick it up.
    g_sdOk = false;
    return;
  }
  f.print("ms,volts,ref,d_mv,pct,bl,cpu,chrg,usb,standby,paused,music,amp,idle_s\n");
  int n = min<int>(g_traceCount, TRACE_CSV_N);
  int start = (g_traceHead + TRACE_MAX - n) % TRACE_MAX;
  char line[128];
  for (int i = 0; i < n; i++) {
    const Trace &t = g_trace[(start + i) % TRACE_MAX];
    snprintf(line, sizeof(line), "%lu,%.3f,%.3f,%+d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
             (unsigned long)t.ms, t.mv / 1000.0f, t.refmv / 1000.0f,
             (int)t.mv - (int)t.refmv, t.pct, t.bl, (unsigned)t.cpu * 10,
             (unsigned)(t.flags & 1), (unsigned)((t.flags >> 1) & 1),
             (unsigned)((t.flags >> 2) & 1), (unsigned)((t.flags >> 3) & 1),
             (unsigned)((t.flags >> 4) & 1), (unsigned)((t.flags >> 5) & 1), t.idles);
    f.print(line);
  }
  f.close();
  g_traceCsvLast = millis();
}
static void traceSave() { traceFlushSD(); }      // charge-transition hook

// Echo a card copy over serial, so a run can be read without pulling the card out.
// path is /trace.csv (this session) or /trace_prev.csv (the run before the last reboot).
static void traceDumpFile(const char *path) {
  if (!g_sdOk) { Serial.println("trace: no SD card"); return; }
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) { Serial.printf("trace: no %s yet\n", path); return; }
  Serial.printf("trace: %s, %u bytes\n", path, (unsigned)f.size());
  while (f.available()) Serial.write(f.read());
  f.close();
}
static void traceDumpSaved() { traceDumpFile("/trace.csv"); }
static void traceDumpPrev()  { traceDumpFile("/trace_prev.csv"); }

static void traceDump() {
  // Uptime and reset cause FIRST. A trace that is shorter than the test means the board
  // restarted and wiped the ring, and without these two numbers that is indistinguishable from
  // the logger not working — which cost several rounds of testing to notice.
  esp_reset_reason_t rr = esp_reset_reason();
  const char *rrs = rr == ESP_RST_POWERON  ? "POWERON (lost all power)"
                  : rr == ESP_RST_SW       ? "SW (reflash/serial)"
                  : rr == ESP_RST_BROWNOUT ? "BROWNOUT (supply dipped)"
                  : rr == ESP_RST_PANIC    ? "PANIC (crash)"
                  : rr == ESP_RST_DEEPSLEEP? "DEEPSLEEP"
                  : rr == ESP_RST_UNKNOWN  ? "UNKNOWN" : "other";
  Serial.printf("trace: uptime %.1fs, last reset = %d %s\n", millis() / 1000.0f, (int)rr, rrs);
  tracePrintBuf(g_trace, g_traceCount, g_traceHead, "live");
}

static void tracePrintBuf(const Trace *buf, uint16_t count, uint16_t head, const char *what) {
  if (!count) { Serial.printf("trace: %s empty\n", what); return; }
  Serial.printf("trace: %u samples (%s), 1s apart, newest last\n"
                "s,volts,ref,d_mv,pct,bl,cpu,chrg,usb,standby,paused,music,amp,idle_s\n",
                count, what);
  int start = (head + TRACE_MAX - count) % TRACE_MAX;
  uint32_t t0 = buf[start].ms;
  for (int i = 0; i < count; i++) {
    const Trace &t = buf[(start + i) % TRACE_MAX];
    Serial.printf("%.0f,%.3f,%.3f,%+d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                  (t.ms - t0) / 1000.0f, t.mv / 1000.0f, t.refmv / 1000.0f,
                  (int)t.mv - (int)t.refmv, t.pct, t.bl, (unsigned)t.cpu * 10,
                  (unsigned)(t.flags & 1), (unsigned)((t.flags >> 1) & 1),
                  (unsigned)((t.flags >> 2) & 1), (unsigned)((t.flags >> 3) & 1),
                  (unsigned)((t.flags >> 4) & 1), (unsigned)((t.flags >> 5) & 1), t.idles);
  }
}
static int  g_preStandbyMusic = 3;
static uint32_t g_standbySince = 0;
static const int WAKE_X = 60, WAKE_Y = 150, WAKE_W = 120, WAKE_H = 46;
// Same width and styling as WAKE, sat directly under it — the two are a pair, and the outlined
// version read as disabled rather than as the quieter option.
static const int OFF_X = WAKE_X, OFF_Y = 204, OFF_W = WAKE_W, OFF_H = 30;

// After this long dozing, drop all the way to deep sleep. On a 400mAh cell the lit panel is
// worth about five hours; deep sleep is ~50uA, so the pack lasts weeks and the only cost is
// that waking needs the physical BOOT button instead of the on-screen one.
static const uint32_t DEEP_SLEEP_AFTER_MS = 60000;
static const int DEEP_SLEEP_BELOW_PCT = 40;
// Past this, the clock is re-asked on waking rather than trusted. Over a short nap millis()
// (or the RTC across deep sleep) is fine; over a long one the drift is worth a correction.
static const uint32_t RETIME_AFTER_MIN = 30;

// W-043: auto screen sleep — the screen falls asleep with bunbun. Owner's
// spec (Jon, 2026-08-09): past 22:00, ten untouched minutes slip the unit
// into the same nap face the NAP button gives; at 06:00 it returns to where
// it was, by itself — the household gets ONE night, not two. Audio holds it
// awake (a nap mid-song reads as a crash), and an unset clock disables the
// whole feature: a wrong clock napping at noon is worse than no nap at all.
// g_autoNap survives the low-battery drop to screen-off so the 06:00 wake
// works from there too; any MANUAL exit clears it — a hand on the device
// outranks the schedule.
static bool g_autoNap = false;
static uint32_t g_lastAudioLiveMs = 0;
static const uint32_t AUTONAP_IDLE_MS = 10UL * 60000UL;
// audioLive() is amplitude-based and flaps on quiet passages (the W-024
// lesson) — only a full silent minute counts as "the music is over".
static const uint32_t AUTONAP_QUIET_MS = 60000UL;
// W-059: the window is the family's BEDTIME setting now (SETUP row) —
// g_bedStartMin/g_bedEndMin, defaults 22:00-06:00 per the owner's spec.
static bool autoNapWindowNow() {
  int cm = clockNowMin();
  return cm >= g_bedStartMin || cm < g_bedEndMin;
}
// The pause problem (owner's spec, option b): standby pauses the game, and
// simulate() is what refills energy overnight — so an auto-napped night
// would hand back a bunny with exactly his 10pm energy. The nap IS his
// night's sleep: credit a full night at the 06:00 wake. Needs are left
// alone — awake nights barely drain them (DECAY_NIGHT) and W-036 set the
// precedent that absent time never punishes. If he was night-asleep going
// in, simulate()'s own morning branch wakes him with the usual line.
static void autoNapWakeCredit() {
  S.energy = 100.0f;
  g_autoNap = false;
  saveSleepState(0);
}
// W-059: the scheduled morning wake (no hand present) starts the quiet
// morning; the manual tap-tap wakes are already a greeting and answer with
// the purr directly at their call sites.
static void quietMorningBegin() { g_quietGreet = true; }
static void greetAnswer() {
  if (!g_quietGreet) return;
  g_quietGreet = false;
  hapticPurrStart(1500);               // the first touch answers with the purr
}
static void drawSetTime();          // defined with the input handling, below
extern int g_setH, g_setM;          // the digits that prompt starts from

// millis() does not survive deep sleep and the whole clock is built on it, so the wall time
// and the pet's age ride across in RTC memory — that domain stays powered when RAM does not.
RTC_DATA_ATTR static uint32_t g_rtcMagic = 0;
RTC_DATA_ATTR static int32_t  g_rtcClockMin = 0;
RTC_DATA_ATTR static int64_t  g_rtcSleepAtUs = 0;
static const uint32_t RTC_MAGIC = 0xB0FFB0FF;
static const gpio_num_t PIN_WAKE = GPIO_NUM_0;      // BOOT button, active low

static void drawStandbyScreen() {
  tft.fillScreen(C_INK);
  tft.setTextColor(C_BONE_LO, C_INK);
  // "<name> is napping", the name rule's two-line lockup: a 12-char name in
  // font 4 blows past 240px on one line with the verb, so the name gets a
  // line of its own (never squeezed, never cut) and the verb sits under it.
  // The clock and battery lines shuffle down a few px to keep every line in
  // its own band — nothing may cross anything.
  {
    char nm[16];
    snprintf(nm, sizeof(nm), "%s", petName());
    tft.drawCentreString(nm, UI_W / 2, 58, fitFont(nm, UI_W - 8));
  }
  tft.drawCentreString("is napping", UI_W / 2, 88, 2);
  int cm = clockNowMin();
  char t[8]; snprintf(t, sizeof(t), "%02d:%02d", cm / 60, cm % 60);
  tft.drawCentreString(t, UI_W / 2, 108, 4);
  char b[40];
  float ph = projectedHours();
  if (g_charging)   snprintf(b, sizeof(b), "battery %d%% - charging", batteryPercent());
  else if (ph > 0)  snprintf(b, sizeof(b), "battery %d%% - approx %.0fh left",
                             batteryPercent(), ph * batteryPercent() / 100.0f);
  else              snprintf(b, sizeof(b), "battery %d%%", batteryPercent());
  tft.setTextColor(C_INK_SOFT, C_INK);
  tft.drawCentreString(b, UI_W / 2, 134, 2);
  // Same status cluster as the game screen, top-right, in the light inks this dark screen needs.
  drawWifiOn(tft, UI_W - 30, 6, C_BONE_LO, C_INK_SOFT);
  tft.fillRoundRect(WAKE_X, WAKE_Y, WAKE_W, WAKE_H, 8, C_BONE_LO);
  tft.drawRoundRect(WAKE_X, WAKE_Y, WAKE_W, WAKE_H, 8, C_BONE);
  tft.setTextColor(C_INK, C_BONE_LO);
  tft.drawCentreString("WAKE", UI_W / 2, WAKE_Y + 14, 4);
  // POWER OFF, on the nap screen because that is already the "putting it away" screen. It is
  // deep sleep rather than a true cut — the board has no power switch — but at ~50uA it will
  // sit for months, and the physical BOOT button brings it back. Outlined rather than filled so
  // it reads as the secondary of the two.
  tft.fillRoundRect(OFF_X, OFF_Y, OFF_W, OFF_H, 8, C_BONE_LO);
  tft.drawRoundRect(OFF_X, OFF_Y, OFF_W, OFF_H, 8, C_BONE);
  tft.setTextColor(C_INK, C_BONE_LO);
  tft.drawCentreString("SCREEN OFF", UI_W / 2, OFF_Y + 9, 2);
}

static void enterStandby() {
  saveState();
  // A nap wipes the glass, so no sheet can survive it (P3 must-not-regress: sheet flags are
  // cleared by enterStandby/exitScreenOff/night-restore). The other two go through
  // redrawRoomChrome(), which clears them for every one of its 22 call sites at once; this
  // is the one path that paints its own screen instead.
  g_careSheet = g_sleepSheet = false;
  g_standby = true;
  g_paused = true;
  g_standbySince = millis();
  g_preStandbyMusic = g_musicLevel;
  g_musicOn = false;                       // the audio task stops the song on this
  delay(60);
  ampEnable(false);                   // amp enable is active LOW — this mutes it
  drawStandbyScreen();
  backlightSet(6);                         // ~2%, still readable in a dim room
  setCpuFrequencyMhz(80);
}

// Full deep sleep, entered from standby. The panel goes dark — that is unavoidable, the
// display cannot be lit without powering it — so waking is the physical BOOT button.
// SCREEN OFF — the closest thing to "off" that can still be woken by TOUCH.
//
// Deep sleep would be lower (~50uA vs ~30mA) but it can only be woken by the BOOT button, and
// in a case there is no button to press: choosing it would strand the device until the pack ran
// flat. Panel asleep, backlight off, amp muted, CPU at 80MHz, and the loop still turning so the
// touch controller is still polled. On a 2000mAh pack that is a couple of days rather than
// months, which is the right trade for a thing you can always get back.
static bool g_screenOff = false;
static void enterScreenOff() {
  saveState();
  saveSleepState(g_autoNap ? 2 : 1);   // an auto-nap that fell to screen-off keeps its nap identity
  g_screenOff = true;
  g_paused = true;
  g_musicOn = false;                      // the audio task stops the song on this
  delay(60);
  ampEnable(false);                  // amp enable is active LOW - mute
  ledcWrite(PIN_BL, 0);
  hapticOffNow();                    // no tick runs in here — a mid-pulse duty would freeze ON
  tft.writecommand(0x28);                 // display off
  tft.writecommand(0x10);                 // ILI9341 sleep in
  setCpuFrequencyMhz(80);
}
static void exitScreenOff() {
  setCpuFrequencyMhz(240);
  // A bare sleep-out (0x11) trusts the panel to restore its own register file, and after a
  // long night in sleep-in these clone ILI9341s occasionally wake with the scan start a few
  // pixels off — every repaint lands correctly drawn but shifted, and stays shifted until the
  // panel is re-initialized. So don't trust it: re-run the full boot init (its list ends with
  // sleep-out + display-on and rebuilds every register on the way). ~150ms, invisible inside
  // a wake that already repaints the whole screen.
  tft.init(); tft.setRotation(0); tft.setSwapBytes(false);
  // tft.init() drives BL (45) as plain GPIO via TFT_BL — that detaches LEDC. Re-attach, same
  // order as boot.
  backlightBegin();
  hapticReattach();                  // same casualty class as the backlight
  backlightSet(255); g_blNow = g_blTarget = 255; g_lastTouchMs = millis();
  ampEnable(true);                   // amp back on
  g_screenOff = false;
  if (!g_autoNap) saveSleepState(0); // a woken screen-off is awake (nap keeps its own flag)
  g_paused = false;
  g_musicOn = (g_musicLevel > 0);
  redrawRoomChrome();
}

static void enterDeepSleep() {
  saveState();
  powerLogSave();
  g_rtcClockMin = clockNowMin();
  g_rtcSleepAtUs = esp_timer_get_time();
  g_rtcMagic = RTC_MAGIC;

  tft.fillScreen(C_INK);
  tft.setTextColor(C_BONE_LO, C_INK);
  tft.drawCentreString("press BOOT to wake", UI_W / 2, 110, 2);
  delay(1200);

  // No g_audio.stopSong() — the MP3 decoder is gone and the host owns the audio pipeline.
  ampEnable(false);                  // amp enable is active LOW; mute before the rails go
  hapticOffNow();
  ledcWrite(PIN_BL, 0);
  ledcDetach(PIN_BL);
  pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, LOW);
  tft.writecommand(0x28);                 // display off
  tft.writecommand(0x10);                 // ILI9341 sleep in
  delay(120);
  SD_MMC.end();

  esp_sleep_enable_ext0_wakeup(PIN_WAKE, 0);
  esp_deep_sleep_start();
}

// Returns true if the clock was carried across a deep sleep, so the set-time prompt can be
// skipped — being asked the time every time you open your bag would defeat the point.
static bool restoreAfterDeepSleep(int *clockMinOut, int *sleptMinOut) {
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) return false;
  if (g_rtcMagic != RTC_MAGIC) return false;
  int64_t sleptUs = esp_timer_get_time() - g_rtcSleepAtUs;
  if (sleptUs < 0) sleptUs = 0;
  int mins = (int)(sleptUs / 60000000LL);
  *clockMinOut = (int)((g_rtcClockMin + mins) % 1440);
  *sleptMinOut = mins;
  S.ageMs += (int64_t)mins * 60000LL;     // he keeps growing in the bag
  Serial.printf("woke: slept %.1f min, clock %02d:%02d -> %02d:%02d\n", sleptUs / 60000000.0,
                g_rtcClockMin / 60, g_rtcClockMin % 60, *clockMinOut / 60, *clockMinOut % 60);
  return true;
}

static void exitStandby() {
  // 240 while music is on, 160 otherwise. The render loop never needs 240 — it is 10fps at its
  // busiest — but the AUDIO path does: MP3 decode plus the per-sample mixer in
  // audio_process_i2s is the heaviest work on the board, and rain adds filtered-noise
  // generation for every sample on top. At 160MHz that combination underruns the I2S buffer and
  // is audible as distortion. Costs ~25mA, which is real but worth less than working audio.
  setCpuFrequencyMhz((g_musicOn || g_danceMode) ? 240 : 160);
  backlightSet(255);
  g_blNow = 255; g_lastTouchMs = millis();
  ampEnable(true);                    // amp back on
  uint32_t napMin = (millis() - g_standbySince) / 60000;
  g_standby = false;
  saveSleepState(0);                  // woken from standby/nap = awake
  g_paused = false;
  g_musicLevel = g_preStandbyMusic;
  g_musicOn = (g_musicLevel > 0);
  // A long nap NEVER asks for the time anymore (Jon, launch eve): during a
  // nap the chip stays powered and millis() never stopped — the clock is at
  // worst seconds adrift, and with W-037 any WiFi corrects it within five
  // minutes. A chip, if fitted, is still re-read for free. The prompt is
  // reserved for exactly one situation: a COLD BOOT that gets no internet.
  if (napMin >= RETIME_AFTER_MIN) {
    clockSyncFromChip();          // best-effort; carried clock is fine without it
    g_clockProvisional = true;    // let the 5-minute sync take it from here
  }
  redrawRoomChrome();
}

// ---- sound panel ----
// Tapping MUS opens this rather than blind-toggling: off plus four levels, each auditioned
// as you pick it. Deliberately quiet even at 4 — this is meant to sit in a room, not fill it.
static bool g_soundPanel = false;
// Rows tightened (66/136/196 -> 58/116/158) to make room for the UPDATE bar
// at the bottom — by request: "the update button on the bottom of the snd
// screen", with feedback nobody can miss.
// Rows tightened again (W-055) to seat the HAPTICS strength row between
// EFFECTS and the buttons: 52/104/156, SONGS at 208, the AirPlay footer
// takes the strip below (the SETUP door's old seat — the shelf answers to
// the gear on the room screen now, menu redesign P2).
static const int SP_MUS_Y = 52, SP_FX_Y = 104, SP_HAP_Y = 156, SP_DONE_Y = 208;
static const int SP_CELL_W = 40, SP_CELL_H = 34, SP_LEFT = (UI_W - 5 * SP_CELL_W) / 2;
static const char *SP_NUM[5] = {"off", "1", "2", "3", "4"};

static void drawLevelRow(const char *title, int y, int level) {
  tft.setTextColor(C_INK, C_BONE);
  // Label at y-16, not y-18 (menu redesign P1): the title bar's crayon rule
  // ends at y36 worst-wobble, and the MUSIC label's cell must start below
  // it — 2px bought here keeps the two apart at every jitter.
  tft.drawString(title, SP_LEFT, y - 16, 2);
  for (int i = 0; i < 5; i++) {
    int x = SP_LEFT + i * SP_CELL_W;
    bool on = (level == i);
    tft.fillRoundRect(x + 2, y, SP_CELL_W - 4, SP_CELL_H, 5, on ? C_ORANGE : C_PAPER);
    tft.drawRoundRect(x + 2, y, SP_CELL_W - 4, SP_CELL_H, 5, C_INK);
    tft.setTextColor(on ? C_PAPER : C_INK, on ? C_ORANGE : C_PAPER);
    tft.drawCentreString(SP_NUM[i], x + SP_CELL_W / 2, y + 9, 2);
  }
}

// IVY-4: the sound panel was two screens wearing one name — music levels
// (what a kid touches) sharing a room with WIFI, updates, and the clock
// (the grown-up's shelf). Split before the BT row moves in; menu redesign
// P2 finished the divorce: the shelf's door is the GEAR on the room screen
// now (its old SETUP button on this panel is gone), and this panel is
// plain MUSIC. The BT row lands here with W-021 — under the music level,
// where "where sound comes from" controls stack. The flag KEEPS ITS NAME
// on purpose: the fw-update loop hooks key on g_setupPanel and the update
// path is frozen under the panic investigation (P2 guardrail 2).
static bool g_setupPanel = false;
// Set by the shelf's CLOCK row so the set-time screen can hand BACK to the
// shelf it came from (§2's nav stack: settings ← set-time). The cold-boot
// prompt never sets it, so that path still lands in the room as always.
static bool g_setTimeFromSetup = false;

// The standard title bar (menu redesign P1). Defined down with the games'
// shared chrome — it draws its rule with tttStroke — and declared here
// because every panel above that point in the file wears it.
static void drawBackChip(int x, int y, int w, int h, const char *label);
// The crayon (defined with the games' shared chrome). Every frame and rule on every new
// surface goes through it — never drawRect — so the whole device stays hand-drawn.
static void tttStroke(int x0, int y0, int x1, int y1, uint16_t col, int w, uint32_t id);
// `ink` is the grown-up variant (menu redesign P2): the bar fills solid ink
// for SETTINGS and the start-over confirm, which says "this shelf is not a
// toy" before a single word is read. Kid surfaces keep the bone bar.
static void drawTitleBar(const char *title, const char *back, const char *right, uint32_t jid,
                         bool ink = false);
static bool titleBarBackHit(int x, int y);

static void drawSoundPanel() {
  tft.fillScreen(C_BONE);
  drawLevelRow("MUSIC", SP_MUS_Y, g_musicLevel);
  drawLevelRow("EFFECTS & RAIN", SP_FX_Y, g_fxLevel);
  // W-055: strength row, honest-button gated like everything motor - it
  // only exists on units with a declared motor.
  if (hapticAvailable()) drawLevelRow("HAPTICS", SP_HAP_Y, g_haptLevel);
  // SONGS takes the whole row — the way out is BACK in the bar, and DONE
  // survives only as an edit-confirm word (menu redesign, rule 2).
  tft.fillRoundRect(SP_LEFT, SP_DONE_Y, 5 * SP_CELL_W, 30, 5, C_PAPER);
  tft.drawRoundRect(SP_LEFT, SP_DONE_Y, 5 * SP_CELL_W, 30, 5, C_INK);
  tft.setTextColor(C_INK, C_PAPER);
  tft.drawCentreString("SONGS", SP_LEFT + 5 * SP_CELL_W / 2, SP_DONE_Y + 9, 1);
  // The SETUP door is gone (menu redesign P2): the grown-up shelf answers to
  // the gear on the room screen now, so this panel is finally ALL music —
  // and its footer says the one thing about music this screen never said:
  // that the whole device is a speaker. Display only — and it prints the name
  // the RECEIVER actually advertises (settings_get_device_name), not the
  // pet-derived g_airName: review 8/14 caught those two disagreeing, so this
  // screen was telling a kid to look for a speaker that isn't in the list.
  // Overlap audit: "airplay: " (9) + a 17-ch device name + " - no wifi" (10)
  // = 36 ch = 216px, inside the 232px budget at font 1; longer names fitFont.
  tft.setTextColor(C_INK_SOFT, C_BONE);
  {
    char dev[40] = "";
    if (settings_get_device_name(dev, sizeof(dev)) != ESP_OK || !dev[0])
      strlcpy(dev, "bunbun", sizeof(dev));
    dev[19] = 0;            // 9 + 19 + 10 = 38 ch = 228px, inside the budget
    char ap[64];
    snprintf(ap, sizeof(ap), "airplay: %s - %s", dev,
             g_netState == NET_ONLINE ? "ready" : "no wifi");
    tft.drawCentreString(ap, UI_W / 2, 246, 1);
    tft.drawCentreString("pick it on your phone's speaker menu", UI_W / 2, 257, 1);
  }
  // Bar LAST on this panel: the MUSIC row's label sits right under the rule,
  // and the crayon's worst wobble may share a row with the label cell's top
  // padding — ink over padding is invisible, but a label drawn AFTER the
  // rule would erase a notch out of it with its background fill.
  drawTitleBar("MUSIC", "\x1B ROOM", nullptr, 511);
}

// SETTINGS geometry (menu redesign P2): one 216px column of 38px rows under
// the ink bar, instead of the old scatter of differently-sized buttons that
// twice ended up drawn on top of each other (the W-055 and 8/10 regressions
// both started as "borrowed constant lands on someone else's row" — a single
// column with one row height retires that whole failure class). Overlap
// audit: WIFI 40..78, status lines 82/93, CLOCK 106..144, BEDTIME 150..188,
// HAPTICS 194..232, UPDATE 238..276, bottom row 288..318 — nothing crosses
// anything, and the UPDATE row keeps 12px clear of the bottom row.
static const int SU_X = 12, SU_W = 216, SU_ROW_H = 38;
static const int SU_WIFI_Y = 40, SU_CLK_Y = 106, SU_BED_Y = 150, SU_HAPT_Y = 194;
static const int SU_UPD_Y = 238, SU_BOT_Y = 288;
static const int SU_SO_X = 12, SU_SO_W = 130, SU_RS_X = 152, SU_RS_W = 76;

// The UPDATE row alone — extracted so the live download repaint (2.5x/s for
// the length of a download) touches 216x38px instead of clearing the whole
// screen and rebuilding six rows behind it (review 8/14: the "verbatim move"
// guardrail was honoured to the letter, but the download screen is exactly
// where a parent stares, and it was the one place still flashing).
// Every state keeps its exact look so success and failure stay unmistakable:
// idle paper, working orange with a live percent, up-to-date ink, failure
// red with the reason spelled out.
static void drawUpdateRow() {
  fw_update_state_t st = fw_update_state();
  uint16_t bg = C_PAPER, fg = C_INK;
  char lbl[48];
  switch (st) {
    case FW_CHECKING:    bg = C_ORANGE; fg = C_PAPER; strcpy(lbl, "checking for updates..."); break;
    case FW_DOWNLOADING: bg = C_ORANGE; fg = C_PAPER;
                         snprintf(lbl, sizeof(lbl), "downloading %d%%", fw_update_pct()); break;
    case FW_UP_TO_DATE:  bg = C_INK;    fg = C_PAPER; strcpy(lbl, "up to date!"); break;
    case FW_FAILED:      bg = TFT_RED;  fg = C_PAPER;
                         snprintf(lbl, sizeof(lbl), "failed: %s", fw_update_reason()); break;
    case FW_SUCCESS:     bg = C_ORANGE; fg = C_PAPER; strcpy(lbl, "updated! restarting..."); break;
    default:             strcpy(lbl, "CHECK FOR UPDATES"); break;
  }
  tft.fillRoundRect(SU_X, SU_UPD_Y, SU_W, SU_ROW_H, 5, bg);
  tft.drawRoundRect(SU_X, SU_UPD_Y, SU_W, SU_ROW_H, 5, C_INK);
  // the percent doubles as a progress fill so it reads from across the room
  if (st == FW_DOWNLOADING) {
    int fill = (SU_W - 4) * fw_update_pct() / 100;
    if (fill > 0) tft.fillRoundRect(SU_X + 2, SU_UPD_Y + 2, fill, SU_ROW_H - 4, 4, C_INK);
  }
  tft.setTextColor(fg, bg);
  tft.drawCentreString(lbl, UI_W / 2, SU_UPD_Y + 15, 1);
}

static void drawSetupPanel() {
  tft.fillScreen(C_BONE);
  // The grown-up shelf, rehoused behind the gear (menu redesign P2). Ink bar
  // = grown-up territory: it says "not a toy" before a single word is read.
  //
  // The kids' ask (2026-08-09): "so kids can identify it is their own" —
  // the shelf still leads with the name THEY chose, drawn into the bar
  // (fitFont steps a long name down rather than ever cutting it; the 132px
  // budget keeps a 12-ch name 6px clear of the version slot at x192, the
  // overlap audit's tightest bar fit).
  //
  // The running firmware version rides in the bar's right slot. Added after
  // a full day of field debugging over chat in which "which build is this
  // unit actually running" was the most important unanswerable question
  // (2026-08-07 — five bins deep, the answer turned out to be "an old
  // one"). Never remove this.
  // The right slot used to repeat the version that the sub-line below already spells out —
  // the same string twice in one 34px bar (Jon 8/14: "there is a version duplicate, what do
  // they both represent?" — nothing different; they were the same number). The never-remove
  // rule needs it ONCE, and the sub-line is the readable one.
  drawTitleBar("", "\x1B ROOM", nullptr, 513, true);
  {
    const char *nm = g_petName[0] ? g_petName : "SETTINGS";
    tft.setTextColor(C_PAPER, C_INK);
    // 120, not 132 (review 8/14): centred text spans 120±W/2, and the BACK
    // chip owns x4..56 — clearing it needs W<=128, so the old budget let a
    // long name nick the chip's edge with its ink background fill.
    tft.drawCentreString(nm, UI_W / 2, g_petName[0] ? 4 : 9, fitFont(nm, 120, 2));
    if (g_petName[0]) {
      char sub[48];
      snprintf(sub, sizeof(sub), "settings - %s", esp_app_get_description()->version);
      tft.setTextColor(C_BONE_LO, C_INK);
      tft.drawCentreString(sub, UI_W / 2, 22, 1);
    }
  }
  // WIFI: lit while the radio is up, with the status lines directly beneath
  // — the "which network is this unit on" question lives and dies here.
  {
    uint16_t bg = (g_netState != NET_OFF) ? C_ORANGE : C_PAPER;
    tft.fillRoundRect(SU_X, SU_WIFI_Y, SU_W, SU_ROW_H, 5, bg);
    tft.drawRoundRect(SU_X, SU_WIFI_Y, SU_W, SU_ROW_H, 5, C_INK);
    tft.setTextColor(bg == C_ORANGE ? C_PAPER : C_INK, bg);
    tft.drawCentreString("WIFI", UI_W / 2, SU_WIFI_Y + 11, 2);
  }
  // Two status lines (the audit's split — the old combined line ran 46 ch
  // against a 12-ch name). Line 1 is the radio: state, then network and
  // address once it has them; in AP mode the name shown is the AP's own,
  // since that is what you go looking for on your phone. Line 2 is the
  // DEVICE name (Jon, launch eve): four bunbuns in one room and the AirPlay
  // picker full of them — the unit should say which one it is. Same name
  // mDNS advertises. "unit: " + a 17-ch name = 23 ch = 138px, safe.
  tft.setTextColor(C_INK_SOFT, C_BONE);
  {
    // The SSID is user data and can be 32 chars; at font 1 (6px) this line
    // ran off BOTH edges from an 11-char network onward and ate the IP —
    // the one fact the row exists to show (review 8/14). Budget it: 232px
    // = 38 ch, so the network name gets whatever is left after the state
    // and the IP, and loses its tail rather than the line losing its ends.
    String l1 = String("wifi: ") + netStateName();
    String ip = ((g_netState == NET_ONLINE || g_netState == NET_AP) && netIp().length())
                    ? ("  " + netIp()) : String("");
    int room = 38 - (int)l1.length() - (int)ip.length() - 2;
    if (netName().length() && room > 3) {
      String nm = netName();
      if ((int)nm.length() > room) nm = nm.substring(0, room);
      l1 += "  " + nm;
    }
    l1 += ip;
    tft.drawCentreString(l1, UI_W / 2, SU_WIFI_Y + SU_ROW_H + 4, 1);
    char dev[40] = "";
    if (settings_get_device_name(dev, sizeof(dev)) == ESP_OK && dev[0])
      tft.drawCentreString(String("unit: ") + dev, UI_W / 2,
                           SU_WIFI_Y + SU_ROW_H + 15, 1);
  }
  // CLOCK — the manual door to set-time, at any time. The cold-boot prompt
  // itself is untouched (W-015: internet first, ask only as the fallback).
  tft.fillRoundRect(SU_X, SU_CLK_Y, SU_W, SU_ROW_H, 5, C_PAPER);
  tft.drawRoundRect(SU_X, SU_CLK_Y, SU_W, SU_ROW_H, 5, C_INK);
  tft.setTextColor(C_INK, C_PAPER);
  tft.drawCentreString("CLOCK", UI_W / 2, SU_CLK_Y + 11, 2);
  // W-059: the BEDTIME row — two buttons, tap to cycle. Left is when the
  // quiet hours begin, right is when morning comes. The morning number moves
  // the screen wake AND the pet wake together (Grim's clause); the 30-min
  // grid and range clamps are unchanged from W-059.
  {
    int w = (SU_W - 8) / 2;
    // am/pm on both, so "BED 5:00" can never be read as five in the morning
    // (Jon 8/14). 12-hour with the suffix is how a family says a bedtime.
    char bl[20], wl[20];
    int bh = g_bedStartMin / 60, wh = g_bedEndMin / 60;
    snprintf(bl, sizeof(bl), "BED %d:%02d%s", bh % 12 == 0 ? 12 : bh % 12,
             g_bedStartMin % 60, bh >= 12 ? "pm" : "am");
    snprintf(wl, sizeof(wl), "WAKE %d:%02d%s", wh % 12 == 0 ? 12 : wh % 12,
             g_bedEndMin % 60, wh >= 12 ? "pm" : "am");
    for (int i = 0; i < 2; i++) {
      int x = SU_X + i * (w + 8);
      tft.fillRoundRect(x, SU_BED_Y, w, SU_ROW_H, 5, C_PAPER);
      tft.drawRoundRect(x, SU_BED_Y, w, SU_ROW_H, 5, C_INK);
      tft.setTextColor(C_INK, C_PAPER);
      tft.drawCentreString(i == 0 ? bl : wl, x + w / 2, SU_BED_Y + 11, 2);
    }
  }
  // W-022: the HAPTICS switch — drawn ONLY on units whose motor is
  // declared (honest-button rule: no control for a body part that isn't
  // there). Some homes want a stiller bunny; the switch is theirs.
  if (hapticAvailable()) {
    uint16_t bg = g_haptOn ? C_ORANGE : C_PAPER;
    tft.fillRoundRect(SU_X, SU_HAPT_Y, SU_W, SU_ROW_H, 5, bg);
    tft.drawRoundRect(SU_X, SU_HAPT_Y, SU_W, SU_ROW_H, 5, C_INK);
    tft.setTextColor(bg == C_ORANGE ? C_PAPER : C_INK, bg);
    tft.drawCentreString(g_haptOn ? "HAPTICS ON" : "HAPTICS OFF",
                         UI_W / 2, SU_HAPT_Y + 11, 2);
  }

  // UPDATE row — MOVED VERBATIM (P2 guardrail: the update path is under the
  // panic investigation's freeze, so only the rectangle changed). Every
  // state keeps its exact look so success and failure stay unmistakable:
  // idle paper, working orange with a live percent, up-to-date ink, failure
  // red with the reason spelled out.
  drawUpdateRow();
  // The very bottom, smallest things on the shelf, as far from a kid's
  // thumb as the panel allows: "start over?" opens the KEEP-first confirm
  // (which now also demands a 2s hold), and RESTART is the button on the
  // back, unreachable once the unit is in a case. The RESET pin's old seat
  // by NAP is gone — this is the one home either of them has.
  tft.fillRoundRect(SU_SO_X, SU_BOT_Y, SU_SO_W, 30, 5, C_PAPER);
  tft.drawRoundRect(SU_SO_X, SU_BOT_Y, SU_SO_W, 30, 5, C_INK);
  tft.setTextColor(C_LOW, C_PAPER);
  tft.drawCentreString("start over?", SU_SO_X + SU_SO_W / 2, SU_BOT_Y + 8, 2);
  tft.fillRoundRect(SU_RS_X, SU_BOT_Y, SU_RS_W, 30, 5, C_PAPER);
  tft.drawRoundRect(SU_RS_X, SU_BOT_Y, SU_RS_W, 30, 5, C_INK);
  tft.setTextColor(C_INK, C_PAPER);
  tft.drawCentreString("restart", SU_RS_X + SU_RS_W / 2, SU_BOT_Y + 8, 2);
}

// ---- track picker ----
// Reached from SND. Row 0 is "rotate all", then one row per file on the card. Also carries the
// WIFI button, since adding music and choosing music are the same errand.
static bool g_trackPanel = false;
static const int TP_Y = 56, TP_ROW = 24, TP_X = 16, TP_W = UI_W - 32;
static const int TP_ROWS_SHOWN = 6;

static char g_panelMsg[48] = "";

static int g_trackScroll = 0;   // first visible row (Jon: a real scroll bar)

static void drawTrackPanel() {
  tft.fillScreen(C_BONE);
  // SONGS, under the standard bar (menu redesign P1, the F2 fix): BACK goes
  // one level up to MUSIC (the panel wearing SOUND's old name), never
  // straight to the room.
  drawTitleBar("SONGS", "\x1B MUSIC", nullptr, 512);

  // LOOP ALL moved OUT of the bar to the bottom of the panel (Jon 8/14) —
  // it is a setting you reach for once, not a title, and the bar reads
  // cleaner with the title alone. Drawn below, with the arrows.
  int maxScroll = max(0, g_trackN - TP_ROWS_SHOWN);
  if (g_trackScroll > maxScroll) g_trackScroll = maxScroll;
  if (g_trackScroll < 0) g_trackScroll = 0;
  int shown = min(g_trackN - g_trackScroll, TP_ROWS_SHOWN);
  for (int i = 0; i < shown; i++) {
    int t = g_trackScroll + i;
    int y = TP_Y + i * TP_ROW;
    bool on = (!g_rotate && g_trackSel == t);
    tft.fillRoundRect(TP_X, y, TP_W, TP_ROW - 3, 4, on ? C_ORANGE : C_PAPER);
    tft.drawRoundRect(TP_X, y, TP_W, TP_ROW - 3, 4, C_INK);
    tft.setTextColor(on ? C_PAPER : C_INK, on ? C_ORANGE : C_PAPER);
    char buf[26];
    strncpy(buf, g_tracks[t], sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    tft.drawString(buf, TP_X + 6, y + 5, 2);
  }
  if (!g_trackN) {
    tft.setTextColor(C_INK_SOFT, C_BONE);
    tft.drawCentreString("no mp3 on the card", UI_W / 2, TP_Y + TP_ROW + 8, 2);
  } else if (g_trackN > TP_ROWS_SHOWN) {
    // Position caption between the list and the buttons.
    tft.setTextColor(C_INK_SOFT, C_BONE);
    char pos[32];
    snprintf(pos, sizeof(pos), "songs %d-%d of %d", g_trackScroll + 1,
             g_trackScroll + shown, g_trackN);
    tft.drawCentreString(pos, UI_W / 2, TP_Y + TP_ROWS_SHOWN * TP_ROW - 1, 1);
  }

  // Bottom row: big PREV / NEXT (Jon: the skinny side rail was too fiddly
  // for fingers — scrolling gets the fattest targets on the panel). DONE's
  // old centre seat is empty now; BACK in the bar is the way out.
  int wy = TP_Y + TP_ROWS_SHOWN * TP_ROW + 6;
  {
    // The arrows are PREV / NEXT song (Jon reached for dead scroll arrows
    // three times — an arrow that exists should always do something):
    // they step the selection, play it, and the view follows. Grown to
    // 56x44 (menu redesign P2, §1.6) — the fattest targets on the panel,
    // since scrolling is what mittens do most here.
    bool live = g_trackN > 1;
    uint16_t afg = live ? C_INK : C_BONE_LO;
    tft.fillRoundRect(TP_X, wy, 56, 44, 5, C_PAPER);
    tft.drawRoundRect(TP_X, wy, 56, 44, 5, afg);
    tft.setTextColor(afg, C_PAPER);
    tft.drawCentreString("<", TP_X + 28, wy + 15, 2);
    tft.fillRoundRect(TP_X + TP_W - 56, wy, 56, 44, 5, C_PAPER);
    tft.drawRoundRect(TP_X + TP_W - 56, wy, 56, 44, 5, afg);
    tft.setTextColor(afg, C_PAPER);
    tft.drawCentreString(">", TP_X + TP_W - 28, wy + 15, 2);
  }
  // LOOP ALL, in the gap the arrows leave between them (Jon 8/14). 96x36
  // centred: 4px clear of each 56px arrow, and a real target rather than the
  // 26px chip it wore in the bar.
  {
    bool on = g_rotate;
    int lx = (UI_W - 96) / 2;
    tft.fillRoundRect(lx, wy + 4, 96, 36, 5, on ? C_ORANGE : C_PAPER);
    tft.drawRoundRect(lx, wy + 4, 96, 36, 5, C_INK);
    tft.setTextColor(on ? C_PAPER : C_INK, on ? C_ORANGE : C_PAPER);
    tft.drawCentreString("LOOP ALL", UI_W / 2, wy + 15, 1);
  }
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawCentreString("add or remove tracks on the SD card", UI_W / 2, wy + 52, 1);
  if (g_panelMsg[0]) {
    tft.setTextColor(C_LOW, C_BONE);
    tft.drawCentreString(g_panelMsg, UI_W / 2, wy + 66, 1);
  }
}

// -1 nothing, 0 loop-all (the chip), 1..n a track (ABSOLUTE index + 1),
// 101 back (one level, to SOUND), 102 prev song, 103 next song
static int trackPanelHit(int x, int y) {
  if (titleBarBackHit(x, y)) return 101;                      // BACK
  int wy = TP_Y + TP_ROWS_SHOWN * TP_ROW + 6;
  if (y >= wy - 3 && y < wy + 47) {                           // bottom row (44-tall arrows)
    // LOOP ALL sits between the arrows now (Jon 8/14), so it is tested first —
    // its 96px band is inside the same row the arrows own.
    if (x >= (UI_W - 96) / 2 && x < (UI_W + 96) / 2) return 0;
    if (x < TP_X + 60) return g_trackN > 1 ? 102 : -1;        // PREV song
    if (x >= TP_X + TP_W - 60) return g_trackN > 1 ? 103 : -1;// NEXT song
    return -1;                                                // the empty middle
  }
  if (x < TP_X || x > TP_X + TP_W) return -1;
  int shown = min(g_trackN - g_trackScroll, TP_ROWS_SHOWN);
  for (int i = 0; i < shown; i++) {
    int ry = TP_Y + i * TP_ROW;
    if (y >= ry && y < ry + TP_ROW - 3) return 1 + g_trackScroll + i;
  }
  return -1;
}

// ---- start-over confirmation (M9) ----
// "start over?" throws the pet away and hatches a new egg. Reached from the
// bottom of the SETTINGS shelf now (menu redesign P2 — the RESET pin is
// gone), and DOUBLY guarded: KEEP is the huge obvious choice, and the wipe
// only fires after a 2-SECOND PRESS-AND-HOLD with a thinning spoke ring
// counting it down — release early and nothing happens. A slip can't end a
// friend. RESTART stayed one level up on the shelf; it never belonged next
// to goodbye.
static bool g_resetPanel = false;
static const int RP_X = 12, RP_W = 216;
static const int RP_KEEP_Y = 186, RP_KEEP_H = 48;
static const int RP_WIPE_X = 40, RP_WIPE_Y = 252, RP_WIPE_W = 130, RP_WIPE_H = 32;
// The hold's countdown dial, beside the wipe button. Overlap audit: ring
// span 182..214 x, 252..284 y — clear of KEEP (bottom 234), clear of the
// hint line at y300, 12px right of the wipe button's edge at 170.
static const int RP_RING_X = 198, RP_RING_Y = 268, RP_RING_R = 13;

// Spokes, not a smooth arc — hand-stepped like everything else here. Ink
// spokes are the seconds left; drawing the spent ones back in C_BONE thins
// the ring without repainting anything else (no-flash law: the screen
// painted once, on open, and a cancel erases only the dial).
static void rpDrawRing(int spokesLeft) {
  for (int i = 0; i < 24; i++) {
    float a = -PI / 2 + i * (2.0f * PI / 24.0f);
    uint16_t c = (i < spokesLeft) ? C_INK : C_BONE;
    int x0 = RP_RING_X + (int)(cosf(a) * 8), y0 = RP_RING_Y + (int)(sinf(a) * 8);
    int x1 = RP_RING_X + (int)(cosf(a) * RP_RING_R), y1 = RP_RING_Y + (int)(sinf(a) * RP_RING_R);
    tft.drawLine(x0, y0, x1, y1, c);
    tft.drawLine(x0 + 1, y0, x1 + 1, y1, c);
  }
}
static void rpEraseRing() {
  tft.fillRect(RP_RING_X - RP_RING_R - 3, RP_RING_Y - RP_RING_R - 3,
               2 * RP_RING_R + 7, 2 * RP_RING_R + 7, C_BONE);
}

static void drawResetPanel() {
  tft.fillScreen(C_BONE);
  // Ink bar, like the shelf it opens from — grown-up territory. BACK goes
  // one level up to SETTINGS, the same safe exit KEEP is.
  drawTitleBar("START OVER?", "\x1B SETTINGS", nullptr, 514, true);
  // Goodbyes are said to SOMEONE (the name rule): the kid's own name, huge,
  // between the plain words — nobody wipes "CLOVER" thinking it was a
  // generic bunbun. fitFont steps the name line down before the panel would
  // ever cut it.
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawCentreString("say goodbye to", UI_W / 2, 52, 2);
  {
    char nm[16];
    snprintf(nm, sizeof(nm), "%s", petName());
    tft.setTextColor(C_INK, C_BONE);
    tft.drawCentreString(nm, UI_W / 2, 76, fitFont(nm, UI_W - 8));
  }
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawCentreString("and hatch a new egg?", UI_W / 2, 112, 2);

  // KEEP, 216x48 — the biggest button anywhere on the device, on purpose.
  tft.fillRoundRect(RP_X, RP_KEEP_Y, RP_W, RP_KEEP_H, 6, C_ORANGE);
  tft.drawRoundRect(RP_X, RP_KEEP_Y, RP_W, RP_KEEP_H, 6, C_INK);
  tft.setTextColor(C_PAPER, C_ORANGE);
  {
    // "KEEP <NAME>", button caps. Long names drop the font, never a letter.
    char keep[24] = "KEEP ";
    petNameUpper(keep + 5, sizeof(keep) - 5);
    int kf = fitFont(keep, RP_W - 8);
    tft.drawCentreString(keep, UI_W / 2, RP_KEEP_Y + (kf == 4 ? 14 : 16), kf);
  }

  // The wipe, small and in the danger ink. A tap does NOTHING — the hint
  // line under it says why, and the dispatch owns the 2s hold.
  tft.fillRoundRect(RP_WIPE_X, RP_WIPE_Y, RP_WIPE_W, RP_WIPE_H, 6, C_PAPER);
  tft.drawRoundRect(RP_WIPE_X, RP_WIPE_Y, RP_WIPE_W, RP_WIPE_H, 6, C_INK);
  tft.setTextColor(C_LOW, C_PAPER);
  tft.drawCentreString("start over", RP_WIPE_X + RP_WIPE_W / 2, RP_WIPE_Y + 9, 2);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawCentreString("hold 2s - a slip can't end a friend", UI_W / 2, 300, 1);
}

// 0 = keep (KEEP or BACK), 1 = the wipe button, -1 = nothing
static int resetPanelHit(int x, int y) {
  if (titleBarBackHit(x, y)) return 0;   // BACK is the same safe exit KEEP is
  if (y >= RP_KEEP_Y && y < RP_KEEP_Y + RP_KEEP_H && x >= RP_X && x < RP_X + RP_W) return 0;
  if (y >= RP_WIPE_Y - 3 && y < RP_WIPE_Y + RP_WIPE_H + 3 &&
      x >= RP_WIPE_X - 4 && x < RP_WIPE_X + RP_WIPE_W + 4) return 1;
  return -1;
}

// ---- the SLEEP surface (menu redesign P2 as a screen, P3 as a sheet) ----
// Three rest verbs, three labeled buttons, ONE door (the SLEEP tab now). They
// used to be a single tap, a pin, and a hidden evening double-tap — the
// last of which nobody ever found without being told. The verbs' code
// paths are untouched: row 1 is the old single-tap toggle, row 2 is the
// NAP pin's enterStandby() (the pin is gone; this row is its only home now),
// and row 3 is the night double-tap's exact logic with its refusal and its
// "see you at" promise drawn as the row's OWN sub-line, game-status style
// — this surface deliberately carries no ticker strip. All AUTOMATIC sleep
// (self-bedtime, auto-nap, the 06:00 wake, the quiet-greet) is untouched;
// it only names what already happens.
//
// P3 lifts it off the glass and onto the live room: the rows keep their exact
// P2 geometry (SL_* live in ui.h now, beside the sheet's own constants), and
// the only thing that changed is what surrounds them — a crayon rule and a
// living room above instead of a title bar and a wiped screen.
// Overlap audit: rule 112..118, rows 120..163 / 168..211 / 216..259, tab bar
// top 268 = 9px clear; the promise sub-line is ≤31 ch right-aligned at inset
// 8 inside the 208px usable width — this surface's tightest text fit.
static char g_sleepSub[40] = "";   // a row's own line: refusal or promise
// Which row the line belongs to (0-based). It is almost always row 3's promise, but a
// refusal must appear under the row that refused — a line about the egg sitting beneath
// TUCK IN FOR THE NIGHT would be answering a question nobody asked.
static int  g_sleepSubRow = 2;

static void sleepPromiseLine(char *d, size_t n) {
  // W-059: the morning hour is a setting, so the promise names it. The
  // ticker's old line carried the name too; the row is only 216px, so the
  // sub-line keeps the promise and the room keeps the pet.
  // "night night" not "in for the night" (review 8/14): at a :30 wake hour
  // the longer copy was 36 ch = 216px right-aligned at inset 8, which paints
  // 8px of paper OUTSIDE the row's left edge, past the rounded corner. The
  // usable width at inset 8 is 208px, not 216 — 31 ch clears it.
  int wh = g_bedEndMin / 60, w12 = wh % 12 ? wh % 12 : 12;
  const char *ap = wh >= 12 ? "pm" : "am";
  if (g_bedEndMin % 60)
    snprintf(d, n, "night night - see you at %d:%02d%s", w12, g_bedEndMin % 60, ap);
  else
    snprintf(d, n, "night night - see you at %d%s", w12, ap);
}

// Rows repaint whole (fill-then-ink, self-healing) so a sub-line change or
// the LIGHTS OUT / WAKE UP morph never needs the full screen again.
static void drawSleepRows() {
  const int ys[3] = {SL_Y1, SL_Y2, SL_Y3};
  for (int i = 0; i < 3; i++) {
    const char *lbl = i == 0 ? (S.lights ? "LIGHTS OUT" : "WAKE UP")
                    : i == 1 ? "NAP THE SCREEN"
                             : "TUCK IN FOR THE NIGHT";
    bool sub = g_sleepSub[0] && i == g_sleepSubRow;
    tft.fillRoundRect(SL_X, ys[i], SL_W, SL_H, 6, C_PAPER);
    tft.drawRoundRect(SL_X, ys[i], SL_W, SL_H, 6, C_INK);
    tft.setTextColor(C_INK, C_PAPER);
    tft.drawCentreString(lbl, UI_W / 2, ys[i] + (sub ? 6 : 14), 2);
    if (sub) {
      tft.setTextColor(C_INK_SOFT, C_PAPER);
      tft.drawRightString(g_sleepSub, SL_X + SL_W - 8, ys[i] + 29, 1);
    }
  }
}

// ================= THE SHEET (menu redesign P3) =================
// A sheet is a card the room is holding up, not a screen the room went away for. Three
// things make that true, and all three are load-bearing:
//
//  1. The room above SHEET_TOP keeps composing and pushing EVERY FRAME. The single
//     scene.pushSprite() becomes a viewport-clipped push of rows 0..111 — three adjacent
//     statements with no branch between them, so the viewport law holds on every path
//     including early returns. Pushing 112 rows of 180 is CHEAPER than today, so the frame
//     budget improves while a sheet is open rather than degrading.
//  2. The BODY is painted exactly ONCE, direct to tft, on open and on state change. It is
//     static content; per-frame writes into the PSRAM sprite are the expensive thing this
//     whole design exists to avoid, and a repainted body is also what a flash IS.
//  3. THE PET LIFTS instead of the sheet sliding (see g_sheetLift). He hops up to sit above
//     his own card, which reads as the motion the mockup wanted and costs nothing.
//
// No room dim: the >>1 halving is the PAUSED colour law and must keep meaning only that.
// The sheet's crayon border is what carries the layering. Adapt the element, never the law.
static void drawSheetChrome() {
  // The body, once. From SHEET_TOP to the bottom edge — the tab bar paints its own ground.
  tft.fillRect(0, SHEET_TOP, UI_W, TAB_SHEET_Y - SHEET_TOP, C_BONE);
  // The top edge, in crayon. Jitter ids are fixed per surface so a repaint never makes the
  // ink squirm: 501 = CARE, 502 = SLEEP.
  tttStroke(0, SHEET_RULE_Y, UI_W - 1, SHEET_RULE_Y, C_INK, 1, g_careSheet ? 501 : 502);
  // (the grab handle retired 8/14 — the CARE sheet's stat band owns that row now, and on
  //  the SLEEP sheet the handle was painted underneath row 1 and never seen)
}

// ---- the CARE sheet (M2) ----
// The eight care verbs the icon row used to hide in 23px squares, as 53x52 cards a mitten
// can hit. Every card dispatches into the EXACT runMenu() case its old seat did, so every
// refusal line, every dim rule, every animation and every hidden delight — petting's purr,
// the poop sweep, the away-mode treats — is the same code it always was.
//
// Card order is the row's order, minus the two verbs that became tabs (PLAY, ZZZ), plus the
// two the sheet adds: WISH (promoted out of the pause overlay, spec 1.8) and CLOSE.
// -1 in the runMenu column = the card is not a care verb.
static const int8_t CARE_MENU[8] = {0, 2, 3, 4, 5, 7, -1, -1};
static const int CARE_WISH = 6, CARE_CLOSE = 7;

// The sheet says the longer word. The icon row had 23px and said "CUDL"; a 53px card has
// room to say "CUDDLE", and R4b noted the row's spelled-out label was the only place seat
// names were readable at all — the cards carry that now. menuLabel() still owns the state
// logic (TREAT while away, SCHOOL for a teen, EMPTY for a baby's chore seat).
static const char *careCardLabel(int c) {
  if (c == CARE_WISH)  return "WISH";
  if (c == CARE_CLOSE) return "CLOSE";
  int m = CARE_MENU[c];
  const char *l = menuLabel(m);
  if (!l[0]) return "";                       // the baby's empty chair
  if (m == 5) return "CUDDLE";
  if (m == 7) return teenSchool() ? "SCHOOL" : "WORK";
  return l;                                   // FEED / BATH / SWEEP / MEDS / TREAT
}

// Same zero-new-art rule the tab bar follows: pak icon where one exists, vector where none
// does. WISH gets a star (the label is what names it — R5: a star alone would read as a
// badge), CLOSE gets a down-chevron, which is the gesture the sheet would have if it slid.
static void drawCareCardIcon(int c, int cx, int cy, uint16_t ink) {
  if (c == CARE_WISH) {
    // A twinkle, not a pentagram (Jon 8/14: "the wish icon is screwed up").
    // Five jittered strokes crossing inside an 11px radius came out as a
    // scribble — crayon wobble needs room, and there isn't any at this size.
    // This is the arcade's own celebration mark (gameSparkles) instead: a
    // big four-point star with a small companion, drawn with straight
    // primitives so it stays crisp on a 53px card.
    tft.drawFastHLine(cx - 10, cy, 21, ink);
    tft.drawFastVLine(cx, cy - 10, 21, ink);
    tft.drawPixel(cx - 4, cy - 4, ink); tft.drawPixel(cx + 4, cy - 4, ink);
    tft.drawPixel(cx - 4, cy + 4, ink); tft.drawPixel(cx + 4, cy + 4, ink);
    tft.drawPixel(cx - 3, cy - 3, ink); tft.drawPixel(cx + 3, cy - 3, ink);
    tft.drawPixel(cx - 3, cy + 3, ink); tft.drawPixel(cx + 3, cy + 3, ink);
    int sx = cx + 9, sy = cy - 9;             // the little one, up and to the right
    tft.drawFastHLine(sx - 3, sy, 7, ink);
    tft.drawFastVLine(sx, sy - 3, 7, ink);
    return;
  }
  if (c == CARE_CLOSE) {                      // a down-chevron: "put this away"
    for (int k = 0; k < 3; k++) {
      tft.drawLine(cx - 9, cy - 5 + k, cx, cy + 4 + k, ink);
      tft.drawLine(cx, cy + 4 + k, cx + 9, cy - 5 + k, ink);
    }
    return;
  }
  int m = CARE_MENU[c];
  // A DIM CARD NEVER WEARS ITS COLOUR ICON (Jon 8/14: "if he feels fine we should grey out
  // meds like sweep"). The pak blit returned before any dim treatment, so a card whose art
  // exists looked lit while one without art greyed out properly — MEDS had a pill icon,
  // SWEEP fell back to a letter, and the two states disagreed on screen while agreeing in
  // code. Greyed cards all take the crayon initial now, so "not needed" looks the same
  // everywhere regardless of which icons the pak happens to carry.
  bool dim = (ink == C_BONE_LO);
  char fn[24];
  if (m == 7) snprintf(fn, sizeof(fn), "icons/%s", teenSchool() ? "school" : "work");
  else        snprintf(fn, sizeof(fn), "icons/%s", MID[m]);
  if (!dim && spriteLoad(fn)) { spriteBlitPanel(cx, cy, 1.2f); return; }
  // Fallback: the verb's initial in the crayon's own hand. Never a blank card.
  tft.setTextColor(ink, C_PAPER);
  char in[2] = {careCardLabel(c)[0], 0};
  tft.drawCentreString(in, cx, cy - 8, 4);
}

// One card, repaintable on its own (border self-heal: fill-then-ink, so an erase can never
// leave a card frameless). Dim cards keep itemDim()'s exact rules — the seat that used to
// grey out greys out here, and the button behind it still says the same no.
static void drawCareCard(int c) {
  int x = CARE_COL_X[c % 4], y = CARE_ROW_Y[c / 4];
  const char *lbl = careCardLabel(c);
  if (!lbl[0]) {
    // THE EMPTY CHAIR (L3904's rule, kept): a baby has no chore seat at all. An outlined
    // empty card reads as "not yet"; a greyed icon read as something withheld.
    tft.fillRoundRect(x, y, CARE_CARD_W, CARE_CARD_H, 7, C_BONE);
    tft.drawRoundRect(x, y, CARE_CARD_W, CARE_CARD_H, 7, C_BONE_LO);
    return;
  }
  int m = CARE_MENU[c];
  bool dim = (m >= 0) && itemDim(m);          // verbatim: the sheet asks the same question
  uint16_t ink = dim ? C_BONE_LO : C_INK;
  tft.fillRoundRect(x, y, CARE_CARD_W, CARE_CARD_H, 7, C_PAPER);
  tft.drawRoundRect(x, y, CARE_CARD_W, CARE_CARD_H, 7, ink);
  drawCareCardIcon(c, x + CARE_CARD_W / 2, y + 18, ink);
  tft.setTextColor(ink, C_PAPER);
  // font1, <=6 ch: "CUDDLE"/"SCHOOL" are 36px inside 53. Nothing can reach a card border.
  tft.drawCentreString(lbl, x + CARE_CARD_W / 2, y + CARE_CARD_H - 14, 1);
}

static void drawCareSheet() {
  drawSheetChrome();
  // AWAY: one door, not eight (Jon 8/14: "the only button at the bottom needs to be just
  // leave a treat"). Nothing else on this sheet can be done to a bunny who isn't here, and
  // a grid of six greyed cards around one live one buries the only thing that helps. The
  // basket is the icon, because the basket is the thing you are putting out.
  if (bunAway()) {
    statsInvalidate();
    drawStats(SHEET_STATS_Y);
    int w = 200, x = (UI_W - w) / 2, y = 160, h = 76;
    bool out = g_treatsOutMs != 0;
    tft.fillRoundRect(x, y, w, h, 9, out ? C_BONE : C_PAPER);
    tft.drawRoundRect(x, y, w, h, 9, out ? C_BONE_LO : C_INK);
    uint16_t ink = out ? C_BONE_LO : C_INK;
    // The basket sits CENTRED, with the words under it (Jon 8/14) — the treat is
    // the subject of this screen, so it gets the middle rather than a corner.
    int mid = x + w / 2;
    if (!out && spriteLoad("items/treats")) {
      spriteBlitPanel(mid, y + 26, 1.6f);
    } else {                                   // the woven basket, drawn
      int bx = mid - 18, by = y + 18;
      tft.fillRoundRect(bx, by, 36, 20, 5, out ? C_BONE_LO : C_BONE_EDGE);
      tft.drawRoundRect(bx, by, 36, 20, 5, ink);
      tft.drawFastVLine(bx + 12, by + 3, 14, ink);
      tft.drawFastVLine(bx + 24, by + 3, 14, ink);
      for (int c = 0; c < 3; c++) {            // carrot tops peeking over the rim
        int cx = bx + 8 + c * 10;
        tft.fillTriangle(cx - 3, by, cx + 3, by, cx, by - 9, out ? C_BONE_LO : C_ORANGE);
        tft.drawFastVLine(cx, by - 12, 4, out ? C_BONE_LO : C_HP);
      }
    }
    tft.setTextColor(ink, out ? C_BONE : C_PAPER);
    tft.drawCentreString(out ? "TREAT IS OUT" : "LEAVE A TREAT", mid, y + 44, 2);
    tft.setTextColor(C_INK_SOFT, out ? C_BONE : C_PAPER);
    tft.drawCentreString(out ? "he is on his way" : "to call him home", mid, y + 62, 1);
    drawTabBar(TAB_SHEET_Y, TAB_SHEET_H);
    return;
  }
  // The title line gave its row to the stat bars (Jon 8/14): the sheet is opened FROM a
  // highlighted CARE tab that is still visible below it, so the words were the redundant
  // half of that row and the needs are the useful half. statsInvalidate() forces the
  // labels and frames to repaint at the sheet's address rather than the room's.
  statsInvalidate();
  drawStats(SHEET_STATS_Y);
  for (int c = 0; c < 8; c++) drawCareCard(c);
  drawTabBar(TAB_SHEET_Y, TAB_SHEET_H);
}

// 0..7 = a card, -1 = nothing. Gutters count toward the card on their left; the strip and
// the tab bar are tested by the caller, not here.
static int careSheetHit(int x, int y) {
  // While away the sheet is one big button (see drawCareSheet): anywhere on it is TREAT,
  // which is card index 3 — the seat menuLabel() already relabels while he is gone.
  if (bunAway()) {
    if (y >= 157 && y < 240 && x >= 20 && x < 220) return 3;
    return -1;
  }
  for (int r = 0; r < 2; r++) {
    if (y < CARE_ROW_Y[r] - 3 || y >= CARE_ROW_Y[r] + CARE_CARD_H + 3) continue;
    for (int c = 3; c >= 0; c--)
      if (x >= CARE_COL_X[c] - 4) return r * 4 + c;
  }
  return -1;
}

// ---- the SLEEP sheet (M3) ----
// Same three rows the P2 screen drew, on the sheet instead of a wiped panel. No ticker
// strip here on purpose (the rows carry their own sub-line), so the tab bar is what sits
// under row 3 and the room is what sits above the rule.
static void drawSleepSheet() {
  drawSheetChrome();
  // Walking in on an armed night hold shows the promise straight away — the sheet reports
  // the state it finds, it doesn't pretend a fresh start.
  g_sleepSub[0] = 0;
  g_sleepSubRow = 2;
  if (g_nightSleep && !S.lights) sleepPromiseLine(g_sleepSub, sizeof(g_sleepSub));
  drawSleepRows();
  drawTabBar(TAB_SHEET_Y, TAB_SHEET_H);
}

// 1 lights toggle · 2 nap the screen · 3 tuck in · -1 nothing. The 4px gaps between rows
// split their slop first-match-up, the same mitten physics the cards use.
static int sleepSheetHit(int x, int y) {
  if (x < SL_X - 4 || x >= SL_X + SL_W + 4) return -1;
  if (y >= SL_Y1 - 3 && y < SL_Y1 + SL_H + 3) return 1;
  if (y >= SL_Y2 - 3 && y < SL_Y2 + SL_H + 3) return 2;
  if (y >= SL_Y3 - 3 && y < SL_Y3 + SL_H + 3) return 3;
  return -1;
}

// ================= THE WISH SCREEN (menu redesign P3, M10, spec 1.8) =================
// A wish used to be reachable only by pausing — a grown-up's route to a child's feature.
// It moves to a labeled star on the CARE sheet, and lands here: a full ink screen that IS
// the input lock the recording always demanded, instead of an invisible lock over a room
// that looked live. The pause overlay KEEPS its WISH button (coverage table), and BOTH
// entries now run the same two functions below, so the two can never drift apart.
//
// The pipeline underneath is untouched, to the letter: the music refusal, the 5-wish shelf
// cap, the 800ms tap debounce, the 2-second minimum with its coaching line, the 15s cap,
// the saving state, and every W-008/W-011 failure verdict. Not one recorder or uploader
// call changed. What changed is where the words land: on the screen the kid tapped, rather
// than on a ticker the recording had hidden.
//
// Colour law (L4518): TFT_RED means "the mic is live RIGHT NOW" and nothing else. Saving is
// ink. This is the one screen on the device allowed to use red at all.
static const int WS_CX = 120, WS_CY = 148, WS_R = 62;
static const int WS_STATUS_Y = 228, WS_COUNT_Y = 262;

// Two lines, broken at a space, centred. The wish verdicts are the longest strings the
// device says — "stop the music first - <12-char name> can't hear over it" is 53 ch = 318px
// at font1 — and the name rule forbids truncating, so the line wraps instead.
static void drawWrapped2(const char *s, int cx, int y, int maxW, int lineH, uint16_t fg,
                         uint16_t bg) {
  tft.setTextColor(fg, bg);
  tft.fillRect(0, y - 1, UI_W, lineH * 2 + 2, bg);
  if (tft.textWidth(s, 1) <= maxW) { tft.drawCentreString(s, cx, y + lineH / 2, 1); return; }
  // Longest prefix that fits, broken at the last space inside it.
  char a[72], b[72];
  int cut = 0, n = (int)strlen(s);
  for (int i = 0; i < n && i < 70; i++) {
    a[i] = s[i]; a[i + 1] = 0;
    if (tft.textWidth(a, 1) > maxW) break;
    if (s[i] == ' ') cut = i;
  }
  if (!cut) cut = n < 70 ? n : 70;
  memcpy(a, s, cut); a[cut] = 0;
  snprintf(b, sizeof(b), "%s", s + cut + (s[cut] == ' ' ? 1 : 0));
  tft.drawCentreString(a, cx, y, 1);
  tft.drawCentreString(b, cx, y + lineH, 1);
}

// The button alone — repainted on every state change, never the whole screen (no-flash).
static void drawWishButton() {
  bool rec = wish_recorder_active();
  bool saving = rec && wish_recorder_saving();
  uint16_t fill = (rec && !saving) ? TFT_RED : C_INK;
  tft.fillCircle(WS_CX, WS_CY, WS_R, fill);
  tft.drawCircle(WS_CX, WS_CY, WS_R, TFT_WHITE);
  tft.drawCircle(WS_CX, WS_CY, WS_R - 1, TFT_WHITE);
  // TRANSPARENT text, single-argument setTextColor: an opaque background fill paints a
  // RECTANGLE, and a rectangle of TFT_RED around a sub-line 120px wide sticks out past a
  // circle that is only ~118px across at that height — red leaking onto the ink ground,
  // and red is the one colour on this device that must mean exactly one thing. The
  // fillCircle above already cleared the whole face, so nothing needs erasing behind it.
  // Every string below is also short enough to sit inside the circle's own width at its
  // own row: the widest is "SAVING" at 84px against 121px of circle.
  tft.setTextColor(TFT_WHITE);
  if (saving) {
    tft.drawCentreString("SAVING", WS_CX, WS_CY - 13, 4);
    tft.drawCentreString("your wish...", WS_CX, WS_CY + 14, 1);
  } else if (rec) {
    char t[16];
    snprintf(t, sizeof(t), "%ds", 15 - wish_recorder_seconds());
    tft.drawCentreString(t, WS_CX, WS_CY - 16, 4);
    tft.drawCentreString("tap when done", WS_CX, WS_CY + 16, 1);
  } else {
    tft.drawCentreString("WISH", WS_CX, WS_CY - 13, 4);
    tft.drawCentreString("say it out loud", WS_CX, WS_CY + 14, 1);
  }
  tft.setTextColor(C_INK, C_BONE);   // leave the shared text state where everything else expects it
}

// The verdict band: whatever bunbun is saying right now, on the screen instead of the
// hidden ticker. Same strings, same say() — zero new copy.
static void drawWishStatus() {
  const char *msg = (millis() < g_tickUntil && g_ticker[0]) ? g_ticker : "";
  drawWrapped2(msg, UI_W / 2, WS_STATUS_Y, UI_W - 16, 12, C_BONE_LO, C_INK);
  // The unsent shelf, exactly as the pause overlay counts it — including the "stop music"
  // form, because the cure for a full shelf is a quiet radio, not more shelf. Cached on the
  // same few-second cadence the banner uses: a SPIFFS directory listing is not something to
  // do on a repaint, and no wish can appear or leave faster than this anyway.
  static int pendCache = 0;
  static uint32_t pendAt = 0;
  if (pendAt == 0 || millis() - pendAt > 3000) { pendAt = millis(); pendCache = wish_uploader_pending(); }
  int pend = pendCache;
  char w[40] = "";
  if (pend > 0) {
    uint32_t ls = audio_output_last_stream_ms();
    if (ls && (millis() - ls < 45000)) snprintf(w, sizeof(w), "stop music to send %d", pend);
    else snprintf(w, sizeof(w), "%d unsent wish%s", pend, pend == 1 ? "" : "es");
  }
  tft.fillRect(0, WS_COUNT_Y - 1, UI_W, 10, C_INK);
  if (w[0]) {
    tft.setTextColor(C_BONE_LO, C_INK);
    tft.drawCentreString(w, UI_W / 2, WS_COUNT_Y, 1);
  }
}

static void drawWishScreen() {
  tft.fillScreen(C_INK);
  // Ink bar, ink screen: this is one surface, and the crayon rule reads as the bar's own
  // wobbly bottom edge. BACK goes exactly one level, to the CARE sheet it was opened from.
  drawTitleBar("MAKE A WISH", "\x1B CARE", nullptr, 516, true);
  {
    // New copy, written with fmtPet from day one. 12-ch worst case: "tell MAXIMILIANUS what
    // you wish for" = 38 ch = 228px, 6px inside the 234px budget — this screen's tightest fit.
    char h[56];
    fmtPet(h, sizeof(h), "tell %s what you wish for");
    tft.setTextColor(C_BONE_LO, C_INK);
    tft.drawCentreString(h, UI_W / 2, 46, 1);
  }
  drawWishButton();
  drawWishStatus();
}

// ---- the wish pipeline, shared by the pause overlay and the wish screen ----
// Extracted rather than copied: two entries into one recorder is exactly how a "verbatim"
// second door drifts from the first. These ARE the old bodies, moved without edit.
static void wishTapStart() {
  // Tap debounce: mashing must not start/stop/start a hardware cycle per bounce (each cycle
  // churns the audio clocks - field-caught).
  static uint32_t lastWishTap = 0;
  if (millis() - lastWishTap < 800) return;
  lastWishTap = millis();
  if (audioLive()) {
    sfxNo(); say("stop the music first - bunbun can't hear over it");
  } else if (wish_uploader_pending() >= 5) {
    // Shelf cap (by request): 5 unsent wishes is the limit. The cure is a network, not more
    // shelf — delivered wishes free their slots the moment WiFi appears.
    sfxNo(); say("wish shelf is full! find wifi so they can fly");
  } else {
    esp_err_t we = wish_recorder_start(wishDoneCb);
    if (we == ESP_OK) { sfxOK(); say("bunbun is listening... make a wish!"); }
    else if (we == ESP_ERR_NO_MEM) {
      sfxNo(); say("wish shelf is full - they fly when it's quiet");
    }
  }
}
// Kid-proofing (field logs: 1-second clips every 6 seconds from button mashing, W-008):
// stops inside the first 2s are coached instead of honored.
static void wishTapStop() {
  if (wish_recorder_saving()) {
    // The stop already landed; the flash burst is finishing. Say so instead of silence —
    // this exact gap taught kids to double-tap (W-008).
    say("got it! wrapping up your wish...");
  } else if (wish_recorder_seconds() < 2) {
    say("say your wish! tap again when you're done");
  } else {
    // Acknowledge THIS press, this frame: chirp + ticker, and the button flips to SAVING on
    // the next draw. The press must never feel ignored.
    wish_recorder_stop();
    sfxOK();
    say("got it! saving your wish...");
  }
}

// ================= the tab bar and the sheets, in action =================
// Each of these returns 1 when a sheet went away and left nothing in its place, meaning the
// caller owes the room its chrome back. That repaint is deliberately NOT redrawRoomChrome():
// a fillScreen would wipe rows 0..111 that the sheet never touched, and putting them back on
// the next push is a white flash across the room on a path that never had one.

// A tab is a DOOR, not a mode (charter R5). Tapping the tab whose sheet is already open
// closes it — same door, same tap, nothing new to learn.
static int tabOpen(int t) {
  uiTick();
  bool wasSheet = sheetOpen();
  if (t == 0) {                                   // CARE
    if (!alive()) { sfxNo(); say("tap the egg to warm it"); return wasSheet ? 1 : 0; }
    if (g_careSheet) { g_careSheet = false; return 1; }
    g_sleepSheet = false; g_careSheet = true;
    drawCareSheet();
    return 0;
  }
  if (t == 3) {                                   // SLEEP
    if (g_sleepSheet) { g_sleepSheet = false; return 1; }
    g_careSheet = false;
    // THE DOOR ALWAYS OPENS; THE ROWS DO THE REFUSING (review 8/14, BUG-3). Routing a living
    // pet through runMenu case 6 put the ZZZ seat's four gates — paused, busy, away, dance —
    // between the tab and the sheet, and NAP THE SCREEN lives nowhere else now that the pin
    // row is gone. Away lasts 24h, so a kid who hadn't found the TREAT card could be locked
    // out of napping the screen for a DAY; the old NAP pin was unconditional. The sheet is a
    // menu of three verbs with their own rules (row 1 already refuses politely for an egg),
    // so opening it is always safe and each row still answers for itself.
    g_sleepSheet = true;
    drawSleepSheet();
    return 0;
  }
  // PLAY and MUSIC are full screens, so any open sheet is going away either way.
  g_careSheet = g_sleepSheet = false;
  if (t == 1) {                                   // PLAY
    if (!alive()) { sfxNo(); say("tap the egg to warm it"); return wasSheet ? 1 : 0; }
    // The care-first refusal stays AT THE DOOR (spec 1.4): runMenu case 1 is the same code
    // the PLAY seat ran, with the same words — "take care of bunbun first, then play".
    bool before = g_gameRoster;
    runMenu(1);
    if (g_gameRoster && !before) return 0;        // the shelf owns the glass now
    return wasSheet ? 1 : 0;                      // refused: put the room back
  }
  // MUSIC — deliberately NOT gated on alive(), exactly as the pin never was: a speaker is
  // still a speaker while the egg is warming.
  if (!AUDIO_ENABLED) { say("audio off in this build"); return wasSheet ? 1 : 0; }
  g_soundPanel = true;
  drawSoundPanel();
  return 0;
}

// A CARE card. Every one of them dispatches into the exact runMenu() case its old icon-row
// seat did — same refusals, same animations, same stat changes, same hidden delights.
static int careSheetTap(int x, int y) {
  int c = careSheetHit(x, y);
  if (c < 0) return 0;                            // gutters and the strip swallow the tap
  if (c == CARE_CLOSE) { uiTick(); g_careSheet = false; return 1; }
  if (c == CARE_WISH) {
    // THE STAR. It quiets the room the way pausing always did for this feature: a wish used
    // to be reachable ONLY from a paused room, so that quiet is part of the pipeline, not an
    // accident of where the button happened to live. The screen restores the pause state it
    // found on the way back out.
    uiTick();
    g_wishPrevPaused = g_paused;
    g_paused = true;
    g_careSheet = false;
    g_wishScreen = true;
    drawWishScreen();
    return 0;
  }
  if (!careCardLabel(c)[0]) return 0;             // the baby's empty chair answers nothing
  runMenu(CARE_MENU[c]);
  if (g_menuActed) { g_careSheet = false; return 1; }
  return 0;                                       // refused: the line is in the strip already
}

// The three rest verbs, each running its EXACT old code path (menu redesign P2, unmoved).
static int sleepSheetTap(int x, int y) {
  int hit = sleepSheetHit(x, y);
  if (hit == 1) {
    // LIGHTS OUT / WAKE UP — the ZZZ seat's old single tap, verbatim, then the sheet steps
    // aside: the room showing him settle (or stretch) IS the answer.
    if (!alive()) {
      sfxNo();
      snprintf(g_sleepSub, sizeof(g_sleepSub), "the egg is still warming");
      g_sleepSubRow = 0;
      drawSleepRows();
      return 0;
    }
    // GOING TO SLEEP walks to the child's sleep spot first ("when i hit sleep he walks
    // over to the sleep location and does the sleep animation") - the lights go out on
    // arrival, applied by the mark path. Waking, and a room with no sleep mark, keep the
    // old immediate behaviour word for word.
    if (S.lights && sceneErrandTo("sleep")) {
      g_sleepPending = 1;
      say("bunbun is off to bed");
      sfxOK();
      saveState();
      g_sleepSheet = false;
      return 1;
    }
    S.lights = !S.lights;
    // a manual wake must also CLEAR the persisted night byte (review 8/14): tuck in, change
    // your mind, then take an OTA — W-072's restore would put him straight back to sleep.
    if (S.lights) { g_nightSleep = false; saveSleepState(0); g_sleepPending = 0; }
    // Stamp bedtime HERE, not only in simulate()'s prevLights tracker: that tracker runs
    // AFTER the energy-full wake check in the same pass, so a rested bunbun put to bed was
    // judged against the PREVIOUS sleep's stamp and popped awake on the first tick.
    if (!S.lights) g_sleepAtMs = millis();
    say(S.lights ? "bunbun woke up" : "bunbun is asleep");
    sfxOK();
    saveState();
    g_sleepSheet = false;
    return 1;
  }
  if (hit == 2) {                                 // NAP THE SCREEN — the NAP pin's exact door
    uiTick();
    g_sleepSheet = false;
    enterStandby();                               // same nap screen, same two-tap confirms
    return 0;
  }
  if (hit == 3) {
    // An EGG cannot be tucked in (review 8/14, RISK-6): row 1 refuses politely for an egg
    // and row 3 did not check at all, so a tap here wrote lights-off + the persisted night
    // byte for a pet that does not exist yet — and W-072 would faithfully restore that
    // across an OTA. Same guard, same words.
    if (!alive()) {
      sfxNo();
      snprintf(g_sleepSub, sizeof(g_sleepSub), "the egg is still warming");
      g_sleepSubRow = 2;
      drawSleepRows();
      return 0;
    }
    // TUCK IN FOR THE NIGHT — the old evening double-tap, now a button anyone can find: no
    // energy-full wake, lights stay off until the W-059 morning hour. Evening spans 6pm-6am
    // so it still works past midnight; any manual wake cancels the hold, exactly as before.
    // The refusal and the promise render as the row's OWN sub-line — this surface carries no
    // ticker strip to whisper into.
    int cm = clockNowMin();
    bool evening = (cm >= 18 * 60) || (cm < g_bedEndMin);
    g_sleepSubRow = 2;
    if (!evening) {
      sfxNo();
      snprintf(g_sleepSub, sizeof(g_sleepSub), "too early for bedtime - after 6pm");
      drawSleepRows();
    } else {
      if (S.lights && sceneErrandTo("sleep")) {
        g_sleepPending = 2;              // the night hold engages when he reaches the bed
      } else {
        if (S.lights) { S.lights = 0; g_sleepAtMs = millis(); }
        g_nightSleep = true;
        saveSleepState(3);
      }
      sleepPromiseLine(g_sleepSub, sizeof(g_sleepSub));
      sfxOK();
      saveState();
      drawSleepRows();                            // row 1 morphs to WAKE UP, row 3 promises
    }
  }
  return 0;
}

// ---- W-061 game #1: tic-tac-toe ----
// The council's build-ready sketch (2026-08-10 item G, 13-0): statics only, no
// allocation, no new tasks, renders in the UI loop — risk to the music path:
// none. Kid is X (carrot orange), bunbun is O (heart red), both drawn wobbly
// because "it looks like somebody's crayon, which is the point" (Luna).
// g_gamePanel and the forward declarations live up beside the menu.
static int8_t   g_tttBoard[9];       // 0 empty, 1 kid, 2 bunbun
static uint8_t  g_tttState;          // 0 kid's turn, 1 bunbun thinking, 2 over
static int8_t   g_tttWinner;         // valid at state 2: 0 draw, 1 kid, 2 bunbun
static int8_t   g_tttWinLine = -1;   // index into TTT_LINES for the strike
static uint32_t g_tttMoveAt = 0;     // when bunbun places his mark
static uint32_t g_tttTouchMs = 0;    // last life on the board — feeds the fold-up
static uint32_t g_tttSeed = 1;       // per-game wobble seed — stable per redraw
static uint16_t g_tttKidWins = 0;    // lifetime, NVS "tttwins"; 3 wakes clever bunny
static bool g_tttWinsDirty = false;  // a win happened; flush to NVS on game-exit, not mid-play
static void tttFlushWins() {
  if (!g_tttWinsDirty) return;
  g_tttWinsDirty = false;
  prefs.begin("bunbun", false);
  prefs.putUShort("tttwins", g_tttKidWins);
  prefs.end();
}
static bool     g_tttJustWoke = false;   // the unlock happened THIS win

static const int8_t TTT_LINES[8][3] = {{0,1,2},{3,4,5},{6,7,8},
                                       {0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
static const int TTT_BX = 24, TTT_BY = 56, TTT_CELL = 64;   // board 192px square

static bool tttClever() { return g_tttKidWins >= 3; }

// Deterministic per-cell wobble so a redraw doesn't make the crayon squirm.
static uint32_t tttJit(uint32_t a, uint32_t b) {
  uint32_t h = g_tttSeed ^ (a * 2654435761u) ^ (b * 40503u);
  h ^= h << 13; h ^= h >> 17; h ^= h << 5;
  return h;
}

// A hand-drawn stroke: 8 short segments, each joint nudged off the true line,
// thickened by parallel passes. w=1 is the grid's pen, w=2 the marks' crayon.
static void tttStroke(int x0, int y0, int x1, int y1, uint16_t col, int w,
                      uint32_t id) {
  int px = x0, py = y0;
  for (int s = 1; s <= 8; s++) {
    int nx = x0 + (x1 - x0) * s / 8, ny = y0 + (y1 - y0) * s / 8;
    if (s < 8) {                       // endpoints stay honest, the middle wobbles
      nx += (int)(tttJit(id, s) % 5) - 2;
      ny += (int)(tttJit(id, s + 100) % 5) - 2;
    }
    for (int o = -w; o <= w; o++) {
      tft.drawLine(px + o, py, nx + o, ny, col);
      tft.drawLine(px, py + o, nx, ny + o, col);
    }
    px = nx; py = ny;
  }
}

// A wobbly ring for bunbun's O — 16 spokes of jittered radius, crayon weight.
static void tttRing(int cx, int cy, int r, uint16_t col, uint32_t id) {
  int px = 0, py = 0;
  for (int s = 0; s <= 16; s++) {
    float a = s * (2.0f * PI / 16);
    int wr = r + (int)(tttJit(id, s & 15) % 5) - 2;
    int nx = cx + (int)(cosf(a) * wr), ny = cy + (int)(sinf(a) * wr);
    if (s) for (int o = -2; o <= 2; o++) {
      tft.drawLine(px + o, py, nx + o, ny, col);
      tft.drawLine(px, py + o, nx, ny + o, col);
    }
    px = nx; py = ny;
  }
}

static void tttDrawMark(int cell) {
  int cx = TTT_BX + (cell % 3) * TTT_CELL + TTT_CELL / 2;
  int cy = TTT_BY + (cell / 3) * TTT_CELL + TTT_CELL / 2;
  if (g_tttBoard[cell] == 1) {          // kid: carrot-orange X
    tttStroke(cx - 18, cy - 18, cx + 18, cy + 18, C_ORANGE, 2, cell * 2 + 1);
    tttStroke(cx + 18, cy - 18, cx - 18, cy + 18, C_ORANGE, 2, cell * 2 + 2);
  } else if (g_tttBoard[cell] == 2) {   // bunbun: heart-red O
    tttRing(cx, cy, 20, C_LOW, cell * 2 + 1);
  }
}

// Little four-point stars over the paper — the joy sparkle fires on ANY kid
// win, because bunbun celebrates the kid (Piper's rule, double-weighted).
static void tttSparkles() {
  for (int i = 0; i < 12; i++) {
    int sx = 14 + (int)(tttJit(300 + i, 1) % 212);
    int sy = (i & 1) ? 8 + (int)(tttJit(300 + i, 2) % 38)
                     : 250 + (int)(tttJit(300 + i, 3) % 24);
    uint16_t c = (i % 3 == 0) ? C_DISC : C_ORANGE;
    tft.drawFastHLine(sx - 4, sy, 9, c);
    tft.drawFastVLine(sx, sy - 4, 9, c);
    tft.drawPixel(sx - 2, sy - 2, c); tft.drawPixel(sx + 2, sy - 2, c);
    tft.drawPixel(sx - 2, sy + 2, c); tft.drawPixel(sx + 2, sy + 2, c);
  }
}

// The games roster: one full-width row per game behind the GAME door.
// Playable games are lit; the ones still coming show greyed with "soon" so
// the shelf reads as a growing collection, not an empty promise.
// FREE PLAY (the quick toy) leads; then the games. "soon" ones are greyed.
// P3 (spec 1.4): rows grow 30 -> 36 and gain the best-score column the scores were already
// in RAM for. Fixed columns, the games' own header discipline: label font2 left at x20,
// score font1 right at x220. Overlap audit, the tightest row on the shelf — "BASKET BOUNCE"
// font2 = 143px ending x163, "best 9999" font1 = 54px starting x166: 3px, and neither
// column can move because both are anchored, not centred.
// Rows 44 / 84 / 124 / 164 / 204 / 244, last one bottoming at 279; the hint line sits at
// 296 and ends at 304, 16px clear of the panel's bottom edge.
static const int GR_X = 16, GR_W = 208, GR_H = 36, GR_TOP = 44, GR_GAP = 4;
struct GameEntry { const char *label; bool ready; };
static const GameEntry GAME_ROSTER[] = {
    {"TIC-TAC-TOE", true},
    {"CARROT CHASE", true},
    {"BASKET BOUNCE", true},
    {"GARDEN GUARD", true},
    {"BURROW BLOCKS", true},
    {"BUNNY HOP", true},
};
static const int GAME_ROSTER_N = 6;

// Blit the CURRENTLY LOADED pak sprite straight to the panel, scaled, clipped
// to a rect — the games' bridge to the real bunbun art (Jon 8/12: "use our
// sprites"). Same sampling as spriteBlitDirect, but tft instead of scene
// (drawScene is blocked while a game owns the screen, so scene never flushes)
// and with the manual byte swap scene.drawPixel used to do internally.
// bgc >= 0: FLICKER-FREE path (Jon 8/13, on the road: "sprites all
// flickering, basket bounce slows down") — composite sprite over that solid
// background colour in a PSRAM buffer and push the rect in ONE SPI burst.
// The old per-pixel path spent hundreds of address-window transactions per
// sprite per tick; that was both the shimmer and the slowdown. bgc < 0 keeps
// the transparent per-pixel path for textured grounds (bunny riding a log).
static uint16_t *g_gsbBuf = nullptr;             // 128x40 px, PSRAM, lazy
static const int GSB_BW = 128, GSB_BH = 40;
static void gameSpriteBlit(int cx, int cy, float scale,
                           int clx0, int cly0, int clx1, int cly1,
                           bool flipX = false, int bgc = -1) {
  // flipX mirrors the sprite (8/13: Bunny Hop traffic runs both directions
  // and the PixelLab farm sprites are strict side profiles — a tractor
  // driving backwards reads as a glitch to a five-year-old)
  if (!g_sprOK || !g_meta.w) return;
  int dw = (int)(g_meta.w * scale + 0.5f), dh = (int)(g_meta.h * scale + 0.5f);
  if (dw <= 0 || dh <= 0) return;
  // OUTLINE-PRESERVING downsample (Jon 8/12: "the sprites keep losing their
  // outline"). Nearest-neighbour skipped the art's thin black outline at
  // small scales. Instead, each output pixel examines its WHOLE source block
  // and keeps the DARKEST opaque pixel — outlines survive any shrink, and
  // interiors stay their own colour because blocks inside a flat fill are
  // uniform anyway.
  // Row-cached sampling (review 8/13): the naive version re-walked the RLE
  // per SAMPLE — source-area walks per blit, 12-30ms for a 15px player, most
  // of a 33ms game tick. Now each source row is decoded ONCE into a line
  // cache and the block-min scan reads arrays: ~1-2ms per blit.
  int bx = (g_meta.w + dw - 1) / dw, by = (g_meta.h + dh - 1) / dh;   // block size

  // clipped destination rect (shared by both paths)
  int rx0 = cx - dw / 2, ry0 = cy - dh / 2;
  int rcx0 = rx0 < clx0 ? clx0 : rx0, rcy0 = ry0 < cly0 ? cly0 : ry0;
  int rcx1 = rx0 + dw - 1 > clx1 ? clx1 : rx0 + dw - 1;
  int rcy1 = ry0 + dh - 1 > cly1 ? cly1 : ry0 + dh - 1;
  if (rcx1 < rcx0 || rcy1 < rcy0) return;
  int bw = rcx1 - rcx0 + 1, bh = rcy1 - rcy0 + 1;
  bool buffered = bgc >= 0 && bw <= GSB_BW && bh <= GSB_BH;
  if (buffered && !g_gsbBuf) {
    g_gsbBuf = (uint16_t *)heap_caps_malloc(GSB_BW * GSB_BH * 2,
                                            MALLOC_CAP_SPIRAM);
    if (!g_gsbBuf) buffered = false;
  }
  if (buffered) {
    for (int i = 0; i < bw * bh; i++) g_gsbBuf[i] = (uint16_t)bgc;
  }

  for (int dy = 0; dy < dh; dy++) {
    int py = cy - dh / 2 + dy;
    if (py < cly0 || py > cly1) continue;
    int sy0 = (int)((uint32_t)dy * g_meta.h / dh);
    uint16_t best[128]; uint8_t have[128];
    memset(have, 0, dw > 128 ? 128 : dw);
    uint32_t bestLum[128];
    for (int oy = 0; oy < by; oy++) {
      int sy = sy0 + oy;
      if (sy >= g_meta.h) break;
      // decode this source row once
      uint16_t line[128]; uint8_t cov[128]; memset(cov, 0, sizeof(cov));
      uint32_t p = g_rowOff[sy]; uint8_t segs = g_spr[p++]; int x = 0;
      for (int s = 0; s < segs; s++) {
        x += g_spr[p++]; uint8_t l = g_spr[p++];
        for (int k = 0; k < l && x + k < 128; k++) {
          line[x + k] = (uint16_t)(g_spr[p + k * 2] | (g_spr[p + k * 2 + 1] << 8));
          cov[x + k] = 1;
        }
        p += l * 2; x += l;
      }
      // merge the darkest opaque pixel of each dest column's block
      for (int dx = 0; dx < dw && dx < 128; dx++) {
        int sx0 = (int)((uint32_t)dx * g_meta.w / dw);
        for (int ox = 0; ox < bx; ox++) {
          int sx = sx0 + ox;
          if (sx >= 128 || sx >= g_meta.w || !cov[sx]) continue;
          uint16_t c = line[sx];   // pak stores panel-order 565: NO byte swap
          uint32_t lum = ((c >> 11) & 0x1F) * 2 + ((c >> 5) & 0x3F) + (c & 0x1F) * 2;
          if (!have[dx] || lum < bestLum[dx]) { best[dx] = c; bestLum[dx] = lum; have[dx] = 1; }
        }
      }
    }
    for (int dx = 0; dx < dw && dx < 128; dx++) {
      int px = rx0 + (flipX ? dw - 1 - dx : dx);
      if (px < rcx0 || px > rcx1) continue;
      if (!have[dx]) continue;
      if (buffered) g_gsbBuf[(py - rcy0) * bw + (px - rcx0)] = best[dx];
      else tft.drawPixel(px, py, best[dx]);
    }
  }
  if (buffered) {
    // pak colours are logical 565 like scene's packed art: swap on the wire
    // for the burst, then restore the panel's global no-swap state
    tft.setSwapBytes(true);
    tft.pushImage(rcx0, rcy0, bw, bh, g_gsbBuf);
    tft.setSwapBytes(false);
  }
}

// The arcade's shared celebration: the tic-tac-toe joy stars, scattered over
// any rect (UI-dev P1 — "the arcade should cheer in the same handwriting").
// Callers follow with a full field/panel redraw, which is what wipes them.
static void gameSparkles(int x, int y, int w, int h, int seed) {
  for (int i = 0; i < 10; i++) {
    int sx = x + 4 + (int)(tttJit(seed + i, 1) % (w - 8));
    int sy = y + 4 + (int)(tttJit(seed + i, 2) % (h - 8));
    uint16_t c = (i % 3 == 0) ? C_DISC : C_ORANGE;
    tft.drawFastHLine(sx - 4, sy, 9, c);
    tft.drawFastVLine(sx, sy - 4, 9, c);
    tft.drawPixel(sx - 2, sy - 2, c); tft.drawPixel(sx + 2, sy - 2, c);
    tft.drawPixel(sx - 2, sy + 2, c); tft.drawPixel(sx + 2, sy + 2, c);
  }
}

// ---- the standard title bar (menu redesign P1) ----
// One component for every full-screen panel, so the way out is always the
// same corner: BACK top-left, title centred on the SCREEN (every bar's title
// lines up with every other's), an optional right slot, and a crayon rule
// underneath. BACK goes exactly one level up — the nav model is a stack
// (game -> shelf -> room, songs -> sound -> room, setup -> sound -> room) and
// DONE survives only as an edit-confirm word. The rule's jitter id is fixed
// PER SCREEN (510+) so a repaint never makes the ink squirm.
static void drawBackChip(int x, int y, int w, int h, const char *label) {
  tft.fillRoundRect(x, y, w, h, 5, C_BONE_LO);
  tft.drawRoundRect(x, y, w, h, 5, C_INK);
  tft.setTextColor(C_INK, C_BONE_LO);
  tft.drawCentreString(label, x + w / 2, y + (h - 8) / 2, 1);
}
static int g_titleBackW = 52;   // last-drawn chip width; the hit test tracks it
static void drawTitleBar(const char *title, const char *back, const char *right, uint32_t jid,
                         bool ink) {
  // The ink variant paints the whole band first; the crayon rule then reads
  // as the wobbly bottom edge of the ink block, which is exactly the look —
  // one component, two moods, no second geometry to keep in step.
  uint16_t bg = ink ? C_INK : C_BONE;
  if (ink) tft.fillRect(0, 0, UI_W, 34, C_INK);
  int backW = tft.textWidth(back, 1) + 12;
  if (backW < 52) backW = 52;
  g_titleBackW = backW;
  drawBackChip(4, 3, backW, 28, back);
  int rightEdge = UI_W - 4;
  if (right && right[0]) {
    tft.setTextColor(ink ? C_BONE_LO : C_INK_SOFT, bg);
    tft.drawRightString(right, UI_W - 6, 12, 1);
    rightEdge = UI_W - 6 - tft.textWidth(right, 1) - 4;
  }
  // fitFont steps the title down before it can touch either chip — the gap
  // is measured, not assumed, so a long pet name in the SETUP bar simply
  // arrives smaller instead of under the BACK chip.
  // Centre in the space that is actually FREE, not symmetrically about the
  // screen (review 8/14): with a wide BACK chip and an empty right slot,
  // symmetry threw away ~114px and squeezed "START OVER?" — the most serious
  // screen on the device — down to the 6px font.
  int left = 4 + backW + 4;
  int avail = rightEdge - left;
  if (avail < 60) avail = 60;
  int f = fitFont(title, avail);
  tft.setTextColor(ink ? C_PAPER : C_INK, bg);
  tft.drawCentreString(title, left + avail / 2, f == 4 ? 4 : f == 2 ? 9 : 13, f);
  tttStroke(0, 33, UI_W - 1, 33, C_INK, 1, jid);
}
// The chip's touch zone, with the slop every small target here gets — width
// tracks the drawn chip so a wide "← SETTINGS" chip stays fully tappable.
static bool titleBarBackHit(int x, int y) {
  return y < 36 && x < 4 + g_titleBackW + 12;
}

#include "snake.h"     // Carrot Chase — uses tft/palette/sfx/prefs, all in scope by here
#include "breakout.h"  // Basket Bounce
#include "garden.h"    // Garden Guard
#include "burrow.h"    // Burrow Blocks
#include "crossing.h"  // Carrot Crossing

static void drawGameRoster() {
  tft.fillScreen(C_BONE);
  drawTitleBar("GAMES", "\x1B ROOM", nullptr, 510);
  for (int i = 0; i < GAME_ROSTER_N; i++) {
    int y = GR_TOP + i * (GR_H + GR_GAP);
    uint16_t bg = GAME_ROSTER[i].ready ? C_ORANGE : C_PAPER;
    tft.fillRoundRect(GR_X, y, GR_W, GR_H, 7, bg);
    tft.drawRoundRect(GR_X, y, GR_W, GR_H, 7, C_INK);
    tft.setTextColor(GAME_ROSTER[i].ready ? C_PAPER : C_INK_SOFT, bg);
    tft.drawString(GAME_ROSTER[i].label, GR_X + 4, y + 10, 2);
    // The right column. Every one of these numbers was already sitting in RAM at boot; the
    // shelf just never said them, so a kid's best run lived only inside the game that owned
    // it. Tic-tac-toe counts WINS rather than points, and says so — a "best" on a game with
    // no score would be a lie in a column the whole point of which is honesty.
    tft.setTextColor(GAME_ROSTER[i].ready ? C_PAPER : C_INK_SOFT, bg);
    char r[16] = "";
    if (!GAME_ROSTER[i].ready)  strcpy(r, "soon");
    else if (i == 0) { if (g_tttKidWins) snprintf(r, sizeof(r), "wins %u", g_tttKidWins); }
    else {
      uint16_t hi = i == 1 ? g_snkHigh : i == 2 ? g_bbHigh
                  : i == 3 ? g_ggHigh  : i == 4 ? g_bkHigh : g_ccHigh;
      if (hi) snprintf(r, sizeof(r), "best %u", hi);
    }
    if (r[0]) tft.drawRightString(r, GR_X + GR_W - 4, y + 14, 1);
  }
  // What the shelf is FOR, in bunbun's own voice. 12-ch worst case: "games fill
  // MAXIMILIANUS's fun bar" = 32 ch = 192px, well inside the 232px budget.
  {
    char h[48];
    fmtPet(h, sizeof(h), "games fill %s's fun bar");
    tft.setTextColor(C_INK_SOFT, C_BONE);
    tft.drawCentreString(h, UI_W / 2, 296, 1);
  }
  // (No bottom DONE anymore — BACK in the bar is the shelf's one way out.)
}

// Returns the game index tapped (0..N-1), 100 for BACK, or -1.
static int gameRosterHit(int x, int y) {
  if (titleBarBackHit(x, y)) return 100;
  if (x < GR_X || x >= GR_X + GR_W) return -1;
  for (int i = 0; i < GAME_ROSTER_N; i++) {
    int ry = GR_TOP + i * (GR_H + GR_GAP);
    if (y >= ry && y < ry + GR_H) return i;
  }
  return -1;
}

static void drawGamePanel() {
  tft.fillScreen(C_BONE);
  // The arcade header (menu redesign P1): BACK top-left like every other
  // game, title font2 at x64 to match — the font4 headline was the one
  // header in the arcade that didn't follow its own law.
  drawBackChip(4, 4, 52, 26, "\x1B SHELF");
  tft.setTextColor(C_INK, C_BONE);
  tft.drawString("TIC-TAC-TOE", 64, 8, 2);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawCentreString(tttClever() ? "clever bunny is playing"
                                   : "sleepy bunny is playing", UI_W / 2, 40, 1);

  // the board: a paper card with a hand-drawn # on it
  tft.fillRoundRect(TTT_BX - 8, TTT_BY - 8, TTT_CELL * 3 + 16, TTT_CELL * 3 + 16,
                    8, C_PAPER);
  tft.drawRoundRect(TTT_BX - 8, TTT_BY - 8, TTT_CELL * 3 + 16, TTT_CELL * 3 + 16,
                    8, C_BONE_EDGE);
  for (int k = 1; k <= 2; k++) {
    tttStroke(TTT_BX + k * TTT_CELL, TTT_BY + 2,
              TTT_BX + k * TTT_CELL, TTT_BY + TTT_CELL * 3 - 2, C_INK, 1, 20 + k);
    tttStroke(TTT_BX + 2, TTT_BY + k * TTT_CELL,
              TTT_BX + TTT_CELL * 3 - 2, TTT_BY + k * TTT_CELL, C_INK, 1, 30 + k);
  }
  for (int c = 0; c < 9; c++) tttDrawMark(c);

  // the strike through a finished line, in the winner's own colour
  if (g_tttState == 2 && g_tttWinLine >= 0) {
    int a = TTT_LINES[g_tttWinLine][0], b = TTT_LINES[g_tttWinLine][2];
    tttStroke(TTT_BX + (a % 3) * TTT_CELL + TTT_CELL / 2,
              TTT_BY + (a / 3) * TTT_CELL + TTT_CELL / 2,
              TTT_BX + (b % 3) * TTT_CELL + TTT_CELL / 2,
              TTT_BY + (b / 3) * TTT_CELL + TTT_CELL / 2,
              g_tttWinner == 1 ? C_ORANGE_LO : C_INK, 1, 40);
  }
  if (g_tttState == 2 && g_tttWinner == 1) tttSparkles();

  // status line — the panel's own voice; the ticker isn't on this screen.
  // These carry the pet's NAME now (the name rule): a long name steps the
  // font down instead of ever clipping a letter of it.
  tft.setTextColor(C_INK, C_BONE);
  char msg[48];
  if (g_tttState == 0)       snprintf(msg, sizeof(msg), "your turn - tap a square");
  else if (g_tttState == 1)  fmtPet(msg, sizeof(msg), "%s is thinking...");
  else if (g_tttWinner == 1) fmtPet(msg, sizeof(msg), "you won! %s cheers for you");
  else if (g_tttWinner == 2) fmtPet(msg, sizeof(msg), "%s won - rematch?");
  else                       snprintf(msg, sizeof(msg), "it's a tie - friends!");
  {
    int mf = fitFont(msg, UI_W - 8, 2);
    tft.drawCentreString(msg, UI_W / 2, mf == 2 ? 254 : 258, mf);
  }
  if (g_tttJustWoke) {
    // Copy shortened with the name rule: the old line ran 46 chars with a
    // 12-char name and fell off the panel.
    char wk[48];
    fmtPet(wk, sizeof(wk), "%s woke up - clever now!");
    tft.setTextColor(C_ORANGE_LO, C_BONE);
    tft.drawCentreString(wk, UI_W / 2, 272, 1);
  }

  // bottom row: AGAIN once a game is over, recentred where DONE used to
  // crowd it — BACK in the header is the way out now.
  if (g_tttState == 2) {
    tft.fillRoundRect(72, 286, 96, 30, 5, C_ORANGE);
    tft.drawRoundRect(72, 286, 96, 30, 5, C_INK);
    tft.setTextColor(C_PAPER, C_ORANGE);
    tft.drawCentreString("AGAIN", 120, 294, 2);
  }
}

// -1 nothing, 0-8 a cell, 100 BACK, 101 AGAIN
static int gamePanelHit(int x, int y) {
  if (y < 36 && x < 68) return 100;              // BACK, top-left
  if (y >= 283) {
    if (x >= 68 && x < 172) return 101;          // AGAIN, recentred
    return -1;
  }
  if (x >= TTT_BX && x < TTT_BX + TTT_CELL * 3 &&
      y >= TTT_BY && y < TTT_BY + TTT_CELL * 3)
    return (y - TTT_BY) / TTT_CELL * 3 + (x - TTT_BX) / TTT_CELL;
  return -1;
}

static bool g_tttBunStarts = false;   // Jon 8/11: alternate who opens each game
static void tttReset() {
  memset(g_tttBoard, 0, sizeof(g_tttBoard));
  g_tttWinner = 0; g_tttWinLine = -1;
  g_tttJustWoke = false;
  g_tttSeed = esp_random() | 1;
  g_tttTouchMs = millis();
  // Turns alternate game to game: if bunbun opens, he takes his move on his
  // own clock (state 1 = thinking); otherwise the kid leads (state 0).
  if (g_tttBunStarts) {
    g_tttState = 1;
    g_tttMoveAt = millis() + 650 + esp_random() % 600;
  } else {
    g_tttState = 0;
  }
  g_tttBunStarts = !g_tttBunStarts;
}

// The third cell of a line the given player could complete right now, or -1.
static int tttLineTake(int who) {
  for (int l = 0; l < 8; l++) {
    int mine = 0, empty = -1;
    for (int k = 0; k < 3; k++) {
      int c = TTT_LINES[l][k];
      if (g_tttBoard[c] == who) mine++;
      else if (!g_tttBoard[c]) empty = c;
    }
    if (mine == 2 && empty >= 0) return empty;
  }
  return -1;
}

// Did anyone finish a line? Fills g_tttWinLine. 0 = nobody (yet).
static int tttCheckWin() {
  for (int l = 0; l < 8; l++) {
    int a = g_tttBoard[TTT_LINES[l][0]];
    if (a && a == g_tttBoard[TTT_LINES[l][1]] && a == g_tttBoard[TTT_LINES[l][2]]) {
      g_tttWinLine = l;
      return a;
    }
  }
  return 0;
}

static bool tttBoardFull() {
  for (int c = 0; c < 9; c++) if (!g_tttBoard[c]) return false;
  return true;
}

static void tttGameOver(int winner) {
  g_tttState = 2; g_tttWinner = winner;
  S.fun = min(100.0f, S.fun + 15);       // a finished game is a good time (Jon)
  if (winner == 1) {
    // Piper's rule, adopted double-weight: bunbun is never a sore loser —
    // one small oh-no beat, then the fanfare is all for the kid.
    if (g_tttKidWins < 999) g_tttKidWins++;
    if (g_tttKidWins == 3) g_tttJustWoke = true;
    // DEFER the NVS write to game-exit (Jon 8/11: "music sometimes drops
    // while playing"). A flash write freezes BOTH cores for tens of ms - the
    // old comment only weighed the RENDER, but AirPlay music hitches on the
    // same freeze. Mark dirty; flush once when the player leaves the game.
    g_tttWinsDirty = true;
    sfxWin();
    // 0.1.133 (Piper's advisory, double-weighted): the clever-bunny unlock
    // announced itself only in text a five-year-old can't read. The growth
    // sparkle rides up right behind the fanfare — waking up IS growing.
    if (g_tttJustWoke) sfxGrow();
    hapticThump();                       // self-gated on a live motor
  } else if (winner == 2) {
    sfxDroop();                          // gracious, not gloating
  } else {
    sfxPurr();                           // a draw is friends
  }
}

// Sleepy bunny: a random legal move, with a 20% chance he notices your two-in-
// a-row and blocks — beatable by a five-year-old (Piper's calibration).
// Clever bunny (after three kid wins): win > block > center > corner > side —
// beatable, never minimax (Tempo: "a toy that cannot lose is a toy nobody
// plays twice").
static void tttBunMove() {
  int mv = -1;
  // Opening variety (Jon 8/12: "it just goes in the middle"): on an empty
  // board the bunny opens center only sometimes — corners are equally strong
  // openings and make him feel like a playmate with moods, not a lookup table.
  {
    bool empty = true;
    for (int c = 0; c < 9 && empty; c++) if (g_tttBoard[c]) empty = false;
    if (empty) {
      if ((esp_random() % 10) < 4) mv = 4;                     // center, 40%
      else { static const int8_t op[4] = {0, 2, 6, 8};         // a corner, 60%
             mv = op[esp_random() % 4]; }
    }
  }
  if (mv < 0 && tttClever()) {
    mv = tttLineTake(2);
    if (mv < 0) mv = tttLineTake(1);
    if (mv < 0 && !g_tttBoard[4]) mv = 4;
    if (mv < 0) {
      static const int8_t corners[4] = {0, 2, 6, 8};
      int r = esp_random() % 4;
      for (int i = 0; i < 4 && mv < 0; i++)
        if (!g_tttBoard[corners[(i + r) % 4]]) mv = corners[(i + r) % 4];
    }
    if (mv < 0) {
      static const int8_t sides[4] = {1, 3, 5, 7};
      int r = esp_random() % 4;
      for (int i = 0; i < 4 && mv < 0; i++)
        if (!g_tttBoard[sides[(i + r) % 4]]) mv = sides[(i + r) % 4];
    }
  } else {
    int blk = tttLineTake(1);
    if (blk >= 0 && (esp_random() % 100) < 20) mv = blk;
  }
  if (mv < 0) {
    int empties = 0;
    for (int c = 0; c < 9; c++) if (!g_tttBoard[c]) empties++;
    if (!empties) { tttGameOver(0); return; }
    int pick = esp_random() % empties;
    for (int c = 0; c < 9; c++)
      if (!g_tttBoard[c] && pick-- == 0) { mv = c; break; }
  }
  g_tttBoard[mv] = 2;
  sfxPeep();                             // a happy peep on his move
  int w = tttCheckWin();
  if (w)                   tttGameOver(w);
  else if (tttBoardFull()) tttGameOver(0);
  else                     g_tttState = 0;
}

// -1 nothing, 0-4 music level, 10-14 effects level, 20 BACK (to the room)
static int soundPanelHit(int x, int y) {
  if (titleBarBackHit(x, y)) return 20;                       // BACK, in the bar
  // (The SETUP door is gone — the gear on the room screen opens the shelf
  // now. The AirPlay footer below SONGS is display only, no zone.)
  if (x < SP_LEFT || x >= SP_LEFT + 5 * SP_CELL_W) return -1;
  int col = (x - SP_LEFT) / SP_CELL_W;
  if (col > 4) col = 4;
  if (y >= SP_MUS_Y && y < SP_MUS_Y + SP_CELL_H) return col;
  if (y >= SP_FX_Y  && y < SP_FX_Y  + SP_CELL_H) return 10 + col;
  if (y >= SP_HAP_Y && y < SP_HAP_Y + SP_CELL_H) return 30 + col;   // W-055
  if (y >= SP_DONE_Y && y < SP_DONE_Y + 30) return 21;        // SONGS, full row
  return -1;
}

static int setupPanelHit(int x, int y) {
  // Same codes the old scatter used, so the handler logic stays put: 23
  // WIFI, 22 CLOCK, 25 UPDATE, 27 HAPTICS (only live when the motor is
  // declared, W-022), 28/29 BEDTIME, 20 BACK (to the ROOM — the shelf is
  // the gear's child now, not SOUND's). New in P2: 40 start over?, 41
  // RESTART, the RESET pin's two orphans rehomed at the very bottom.
  if (titleBarBackHit(x, y)) return 20;
  if (y >= SU_BOT_Y - 3 && y < SU_BOT_Y + 33) {               // bottom pair
    if (x >= SU_SO_X && x < SU_SO_X + SU_SO_W) return 40;     // start over?
    if (x >= SU_RS_X && x < SU_RS_X + SU_RS_W) return 41;     // RESTART
    return -1;
  }
  if (x < SU_X - 4 || x >= SU_X + SU_W + 4) return -1;
  if (y >= SU_WIFI_Y - 3 && y < SU_WIFI_Y + SU_ROW_H + 3) return 23;   // WIFI
  if (y >= SU_CLK_Y - 3  && y < SU_CLK_Y + SU_ROW_H + 3)  return 22;   // CLOCK
  // W-059 BEDTIME: left button = bed hour, right = wake hour.
  if (y >= SU_BED_Y - 3 && y < SU_BED_Y + SU_ROW_H + 3)
    return (x < SU_X + SU_W / 2) ? 28 : 29;
  if (hapticAvailable() && y >= SU_HAPT_Y - 3 && y < SU_HAPT_Y + SU_ROW_H + 3)
    return 27;                                                // HAPTICS
  if (y >= SU_UPD_Y - 3 && y < SU_UPD_Y + SU_ROW_H + 3) return 25;     // UPDATE
  return -1;
}

// ---------------- input ----------------
// ---- set-time screen ----
// Shown on cold boot, exactly as the original Tamagotchi did: time of day only, never a
// date. UP/DOWN adjust, MENU confirms; MENU also steps hours -> minutes -> done.
// ---------------- naming ----------------
// A tapped keyboard rather than the clock screen's cycle-a-value pattern. Three buttons work
// for a number that only has to move a few steps; spelling a name that way is 26 presses per
// letter in the worst case, which is not a thing anyone should be asked to do.
static const char *KB_ROWS[4] = {
  "ABCDEFG",
  "HIJKLMN",
  "OPQRSTU",
  "VWXYZ- ",
};
static const int KB_COLS = 7, KB_ROWN = 4;
static const int KB_X0 = 3, KB_Y0 = 118, KB_W = 33, KB_H = 30;
static const int KB_DEL_X = 6,   KB_OK_X = 126, KB_BTN_Y = 246, KB_BTN_W = 108, KB_BTN_H = 44;

static void drawNameScreen() {
  tft.fillScreen(C_BONE);
  tft.setTextColor(C_INK, C_BONE);
  tft.drawCentreString("NAME YOUR BUNBUN", UI_W / 2, 16, 4);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  tft.drawCentreString("also its AirPlay name", UI_W / 2, 44, 1);

  // The name so far, with the MAC suffix shown greyed. Showing the suffix here is the point:
  // it is what will actually appear in Control Centre, so it should not be a surprise later.
  tft.fillRoundRect(20, 60, UI_W - 40, 30, 5, C_PAPER);
  tft.drawRoundRect(20, 60, UI_W - 40, 30, 5, C_INK);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "-%02X%02X", mac[4], mac[5]);
  tft.setTextColor(C_INK, C_PAPER);
  tft.setTextDatum(TL_DATUM);
  int tw = tft.textWidth(g_petName, 4);
  int sw = tft.textWidth(suffix, 2);
  int x0 = (UI_W - (tw + sw)) / 2;
  if (g_petName[0]) tft.drawString(g_petName, x0, 66, 4);
  tft.setTextColor(C_INK_SOFT, C_PAPER);
  tft.drawString(suffix, x0 + tw, 72, 2);

  for (int r = 0; r < KB_ROWN; r++) {
    for (int c = 0; c < KB_COLS; c++) {
      char ch = KB_ROWS[r][c];
      int x = KB_X0 + c * KB_W, y = KB_Y0 + r * KB_H;
      tft.fillRoundRect(x + 1, y + 1, KB_W - 2, KB_H - 2, 3, C_BONE_LO);
      tft.drawRoundRect(x + 1, y + 1, KB_W - 2, KB_H - 2, 3, C_INK);
      tft.setTextColor(C_INK, C_BONE_LO);
      char lbl[2] = {ch == ' ' ? '_' : ch, 0};
      tft.drawCentreString(lbl, x + KB_W / 2, y + 8, 2);
    }
  }

  tft.fillRoundRect(KB_DEL_X, KB_BTN_Y, KB_BTN_W, KB_BTN_H, 8, C_BONE_LO);
  tft.drawRoundRect(KB_DEL_X, KB_BTN_Y, KB_BTN_W, KB_BTN_H, 8, C_INK);
  tft.setTextColor(C_INK, C_BONE_LO);
  tft.drawCentreString("DELETE", KB_DEL_X + KB_BTN_W / 2, KB_BTN_Y + 14, 2);

  bool can = g_petName[0] != 0;
  tft.fillRoundRect(KB_OK_X, KB_BTN_Y, KB_BTN_W, KB_BTN_H, 8, can ? C_ORANGE : C_BONE_LO);
  tft.drawRoundRect(KB_OK_X, KB_BTN_Y, KB_BTN_W, KB_BTN_H, 8, C_INK);
  tft.setTextColor(can ? C_PAPER : C_INK_SOFT, can ? C_ORANGE : C_BONE_LO);
  tft.drawCentreString("DONE", KB_OK_X + KB_BTN_W / 2, KB_BTN_Y + 14, 2);
  tft.setTextDatum(TL_DATUM);
}

// ---- W-036: the mode chooser (new game only, right after naming) ----
static void drawModeScreen() {
  tft.fillScreen(C_BONE);
  tft.setTextColor(C_INK, C_BONE);
  // Luna's copy, adopted 10-0 (council 8/7): the question is about love,
  // not bravery — the word the ghost owns appears nowhere on this screen.
  char t[44];
  snprintf(t, sizeof(t), "how should %s love you?", g_petName[0] ? g_petName : "bunbun");
  // A 12-char name in font 4 blows past 240 px — measure and step down.
  if (tft.textWidth(t, 4) > UI_W - 8)
    tft.drawCentreString(t, UI_W / 2, 28, 2);
  else
    tft.drawCentreString(t, UI_W / 2, 24, 4);
  // Buttons are 212 px wide (was 180): Luna's BRAVE line is 32 chars and
  // must sit INSIDE the card, not straddle its edges. Touch tests only ty,
  // so the widening costs the hit-test nothing.
  // COZY — the default world: bunbun never leaves, the floor is mercy.
  tft.fillRoundRect(14, 78, UI_W - 28, 56, 8, C_ORANGE);
  tft.drawRoundRect(14, 78, UI_W - 28, 56, 8, C_INK);
  tft.setTextColor(C_PAPER, C_ORANGE);
  tft.drawCentreString("COZY", UI_W / 2, 88, 4);
  tft.setTextColor(C_PAPER, C_ORANGE);
  tft.drawCentreString("always by my side", UI_W / 2, 116, 1);
  // BRAVE — consequence mode. Luna's line verbatim, and the always-comes-
  // home seal stays on the card: choosing this is informed consent.
  tft.fillRoundRect(14, 150, UI_W - 28, 56, 8, C_PAPER);
  tft.drawRoundRect(14, 150, UI_W - 28, 56, 8, C_INK);
  tft.setTextColor(C_INK, C_PAPER);
  tft.drawCentreString("BRAVE", UI_W / 2, 154, 4);
  tft.setTextColor(C_INK_SOFT, C_PAPER);
  tft.drawCentreString("he has feelings and follows them", UI_W / 2, 183, 1);
  tft.drawCentreString("(he always comes home)", UI_W / 2, 194, 1);
}

// Returns true once a name has been accepted.
static bool nameScreenTouch(int tx, int ty) {
  if (ty >= KB_Y0 && ty < KB_Y0 + KB_ROWN * KB_H && tx >= KB_X0 && tx < KB_X0 + KB_COLS * KB_W) {
    int c = (tx - KB_X0) / KB_W, r = (ty - KB_Y0) / KB_H;
    if (r >= 0 && r < KB_ROWN && c >= 0 && c < KB_COLS) {
      size_t n = strlen(g_petName);
      // Leave room for the 5-char "-XXXX" suffix inside g_airName as well as the terminator.
      if (n < sizeof(g_petName) - 1) {
        char ch = KB_ROWS[r][c];
        // A leading space or dash would make an odd mDNS name and reads as a mistake.
        if (!(n == 0 && (ch == ' ' || ch == '-'))) {
          g_petName[n] = ch;
          g_petName[n + 1] = 0;
          sfxTick();
          drawNameScreen();
        }
      }
    }
    return false;
  }
  if (ty >= KB_BTN_Y && ty < KB_BTN_Y + KB_BTN_H) {
    if (tx >= KB_DEL_X && tx < KB_DEL_X + KB_BTN_W) {
      size_t n = strlen(g_petName);
      if (n) { g_petName[n - 1] = 0; sfxTick(); drawNameScreen(); }
      return false;
    }
    if (tx >= KB_OK_X && tx < KB_OK_X + KB_BTN_W && g_petName[0]) {
      // Trim trailing spaces — easy to leave one on and it looks like a typo on the network.
      for (int i = (int)strlen(g_petName) - 1; i >= 0 && g_petName[i] == ' '; i--) g_petName[i] = 0;
      if (!g_petName[0]) { drawNameScreen(); return false; }
      saveName();
      sfxTick();
      return true;
    }
  }
  return false;
}

static int g_setStage = 0;            // 0 = hours, 1 = minutes, 2 = done
int g_setH = 9, g_setM = 0;
static void drawSetTime() {
  tft.fillScreen(C_BONE);
  tft.setTextColor(C_INK, C_BONE);
  tft.drawCentreString("SET THE TIME", UI_W / 2, 40, 4);
  tft.setTextColor(C_INK_SOFT, C_BONE);
  // Named (the name rule) and shortened with it: the old copy ran 40 chars
  // against a 12-char name. This screen can pre-date naming — petName()
  // falls back to the species, which is correct for a pet with no name yet.
  {
    char nc[40];
    fmtPet(nc, sizeof(nc), "%s has no clock inside");
    tft.drawCentreString(nc, UI_W / 2, 70, 1);
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", g_setH, g_setM);
  tft.setTextColor(C_INK, C_BONE);
  tft.drawCentreString(buf, UI_W / 2, 100, 7);
  tft.setTextColor(C_ORANGE, C_BONE);
  tft.drawCentreString(g_setStage == 0 ? "set HOURS" : "set MINUTES", UI_W / 2, 160, 2);
  // Real buttons. The first version only printed "UP / DOWN to change", which refers to the
  // round keys â€” and those aren't drawn on this screen, so there was nothing to press.
  const char *lbl[3] = {"-", g_setStage == 0 ? "NEXT" : "START", "+"};
  int bw = 68, by = 200, bh = 44;
  int bx[3] = {6, 86, 166};
  for (int i = 0; i < 3; i++) {
    tft.fillRoundRect(bx[i], by, bw, bh, 8, i == 1 ? C_ORANGE : C_BONE_LO);
    tft.drawRoundRect(bx[i], by, bw, bh, 8, C_INK);
    tft.setTextColor(i == 1 ? C_PAPER : C_INK, i == 1 ? C_ORANGE : C_BONE_LO);
    tft.drawCentreString(lbl[i], bx[i] + bw / 2, by + 14, i == 1 ? 2 : 4);
  }
}
static bool setTimeInput(int key) {   // 0 = up, 1 = mid, 2 = down
  if (key == 1) {
    if (g_setStage == 0) g_setStage = 1;
    else {
      g_clockBaseMin = g_setH * 60 + g_setM;
      g_clockBaseMs = millis();
      g_clockSet = true;
      g_clockPrompt = false;
      g_clockWaitUntil = 0;
      ds3231Write(g_clockBaseMin);    // so this is the LAST time it ever has to be entered
      prefs.begin("bunbun", false);   // and a fallback for when no chip is fitted
      prefs.putInt("clkMin", g_clockBaseMin);
      prefs.putUInt("clkAt", (unsigned)(millis() / 1000));
      prefs.end();
      // W-015/W-019: this manual set is also a LESSON — a human just told us
      // local time, and if internet UTC is known, the difference IS the
      // timezone. Shared with the serial path via tzStore().
      tzStore(g_clockBaseMin);
      g_clockProvisional = false;     // a human said so; that's not provisional
      return true;                    // done
    }
  } else if (g_setStage == 0) g_setH = (g_setH + (key == 0 ? 1 : 23)) % 24;
  // Minutes step by ONE, not five. Five was a reasonable trade when the clock had to be
  // re-entered on every power cut and precision would have been lost within the day anyway.
  // With the DS3231 holding it, this is set once and then kept for years — so it is worth the
  // extra presses to land on the exact minute. Confirming also writes seconds = 0, so pressing
  // OK as the minute turns over syncs it properly rather than approximately.
  else                       g_setM = (g_setM + (key == 0 ? 1 : 59)) % 60;
  drawSetTime();
  return false;
}

// THE EGG (menu redesign P3). The UP/OK/DOWN keys are gone, and with them the only thing
// that ever hatched an egg: "press MENU to warm the egg" named a button that no longer
// exists. So the egg answers the finger directly now — three taps ON THE EGG, which is the
// gesture every kid tried first and the old build ignored. The counter, the one-minute
// grace, the crack sequence and the save are the exact code the OK key ran.
//
// The whole shell is a target, not a pixel: the egg region is a generous box around the
// sprite, and a tap on the room OUTSIDE it says where to aim rather than doing nothing.
static void eggTap(int tx, int ty) {
  if (S.stage == STAGE_HATCHING) return;      // let the crack sequence finish
  int ex = gx2s(S.x), ey = gy2s(S.y);
  if (tx < ex - 40 || tx > ex + 40 || ty < ey - 64 || ty > ey + 10) {
    sfxNo(); say("tap the egg to warm it"); return;
  }
  g_eggTaps++;
  if (g_eggTaps >= 3 || ageMin() >= 1) {
    S.stage = STAGE_HATCHING;                 // now the shell actually cracks
    g_anim = &EGG_ANIM; g_animT = 0;
    say("the egg is hatching!");
  } else say("the egg wiggles...");
  uiTick();                                   // the same chirp every other door gets, fx-gated
  saveState();
}

void setup() {
  Serial.begin(115200); delay(300);
  // NEVER block the game on the USB console. Serial here is the USB-JTAG CDC, and its writes
  // BLOCK when the FIFO is full — which is the normal state whenever no PC is reading. With
  // ~10 diagnostic lines a second, closing the serial port froze the game for as long as 12.2
  // SECONDS per loop pass (measured: iterMax=12217ms with draw and I2C both idle), and it ran
  // fine again the moment a capture reopened the port. A pet that only works while a debugger
  // watches it is the purest possible form of logging the intent instead of the result.
  // Timeout 0 = drop output when nobody is draining it, which is exactly what diagnostics are
  // worth when nobody is listening.
  Serial.setTxTimeoutMs(0);
  Serial.println(F("\n=== bunbun ==="));
  // Scan I2C once at boot: settles whether this board carries a real RTC (DS3231 @0x68,
  // PCF8563 @0x51, DS1307 @0x68) rather than assuming from the silkscreen.
  {
    Wire.begin(PIN_SDA, PIN_SCL, 100000);
    Serial.print("i2c:");
    for (uint8_t a = 1; a < 127; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
    }
    Serial.println();
  }
  analogReadResolution(12);
  batteryBegin();
  // 160MHz rather than 240. The loop was idling at ~3200Hz against a 10fps screen, so there
  // is enormous headroom, and the MP3 decoder needs well under half of one core at 160.
  // 240, not 160: the audio path needs the headroom. See exitStandby() for the reasoning —
  // musicBegin() runs later in setup and will settle this to 160 if music ends up disabled.
  setCpuFrequencyMhz(240);
  // Look for a charger status line. TP4056-class chargers expose CHRG/STDBY as open-drain:
  // pulled LOW while charging, high-Z once done. With an internal pullup a real status pin
  // therefore reads 0 on the charger and 1 off it — a two-state test no voltage heuristic
  // can match. Anything that stays 1 in both states is just an unconnected pin.
  Serial.printf("battery: %.3fV  charging=%d (GPIO%d)\n", batteryVolts(),
                (int)(digitalRead(PIN_CHRG) == LOW), PIN_CHRG);
  tft.init(); tft.setRotation(0); tft.setSwapBytes(false);
  // MUST come after tft.init(). TFT_eSPI is built with -DTFT_BL=45 -DTFT_BACKLIGHT_ON=HIGH, so
  // its init() does pinMode(45,OUTPUT); digitalWrite(45,HIGH) — which pulls the pin out of the
  // LEDC matrix and drives it statically high. Running backlightBegin() first meant every
  // ledcWrite() afterwards updated a duty register connected to nothing: the panel sat at 100%
  // permanently, no dim level had any visible or electrical effect, and the largest load on the
  // board could never be reduced. Everything downstream of this ordering looked like a
  // mysteriously high current draw.
  backlightBegin();
  hapticBegin();                     // W-022: no-op until the motor is declared (serial 'v')
  prefs.begin("bunbun", true);
  g_love = prefs.getFloat("love", 70.0f);
  g_modeBrave = prefs.getUChar("brave", 0) != 0;
  // W-072: after a SOFTWARE reset (an OTA reboot, or a crash) restore the
  // sleep state the pet held - a 2am auto-update must not wake a sleeping
  // bunny. A POWER-ON is a human pulling the plug and is left to wake fresh.
  // Applied in loop()'s first pass, once the clock is known.
  {
    esp_reset_reason_t rr = esp_reset_reason();
    if (rr != ESP_RST_POWERON && rr != ESP_RST_BROWNOUT) {
      g_pendingSleepRestore = prefs.getUChar("sleepst", 0);
    }
  }
  // W-059: the family's bedtime, clamped to the menu's own choices so a
  // corrupt read can never invent a 3am bedtime.
  g_bedStartMin = prefs.getInt("bedstart", 22 * 60);
  g_bedEndMin   = prefs.getInt("bedend", 6 * 60);
  // Ranges widened again (Jon 8/14): BED 5:00pm..12:00am, WAKE 5:00..10:00.
  // Snap to the half-hour so an older :00-only value stays legal, and clamp so
  // a corrupt read can never invent a 3am bedtime.
  g_bedStartMin -= g_bedStartMin % 30;
  g_bedEndMin   -= g_bedEndMin % 30;
  if (g_bedStartMin < 17 * 60 || g_bedStartMin > 24 * 60) g_bedStartMin = 22 * 60;
  if (g_bedEndMin < 5 * 60 || g_bedEndMin > 10 * 60) g_bedEndMin = 6 * 60;
  prefs.end();
  // The name loads BEFORE the splash is painted (the name rule): the very
  // first thing a kid reads on a reboot is whose device this is. It's a pure
  // NVS read, and it still runs ahead of loadState() — freshState() writes
  // the name too, so that ordering stands.
  loadName();
  tft.fillScreen(C_BONE);
  // The version answers on every restart, not just in SETUP: painted onto the
  // blank bone screen and left there for the seconds it takes the art pak and
  // audio to come up, then the first scene push covers it. A parent watching a
  // reboot can now read what's running without touching anything (8/10, after
  // a rollback made "which version am I on?" a real question).
  tft.setTextColor(C_INK, C_BONE);
  {
    char splash[48];   // 12-ch name + the app desc's full 32-ch version field
    snprintf(splash, sizeof(splash), "%s %s", petName(),
             esp_app_get_description()->version);
    tft.drawCentreString(splash, UI_W / 2, 300,
                         fitFont(splash, UI_W - 8, 2));   // long name + tag can pass 240px
  }
  touchBegin();                      // FT6336U over I2C, not the old XPT2046 on SPI
  scene.setColorDepth(16);
  if (!scene.createSprite(UI_W, SCENE_H)) { Serial.println(F("FATAL: no RAM for scene sprite")); }
  scene.setSwapBytes(true);          // the packed art is little-endian RGB565
  tickSpr.setColorDepth(16);
  tickSpr.createSprite(UI_W, TICKER_H);
  if (!pakBegin()) {
    // W-044: a pak that won't mount is now a REPAIRABLE state, not a dead
    // end. An interrupted art update leaves exactly this (the magic sector
    // is written last, on purpose), so tell the updater to re-fetch the art
    // on the next check and say so on screen instead of "no assets" — the
    // one screen a parent can't act on.
    fw_assets_report_missing();
    tft.setTextColor(TFT_RED, C_BONE);
    tft.drawCentreString("art missing", UI_W / 2, 138, 4);
    tft.setTextColor(C_INK, C_BONE);
    tft.drawCentreString("connect to wifi and", UI_W / 2, 176, 2);
    tft.drawCentreString("tap UPDATE to repair", UI_W / 2, 198, 2);
    return;
  }
  // Needs the index, so it runs here rather than inside pakBegin(); the walkable model reads
  // it from the first frame drawn onward.
  charMeasureBodies();
  // A UNIT MUST KNOW HOW LOUD IT IS (bench, 2026-08-09: a freshly flashed
  // board made no sound at all). hostSetMusicVolume was only ever called
  // when a human tapped the SND panel — its own comment says "boots never
  // call this (the DAC wakes at its default)". On every unit in the family
  // that default had been overwritten by hand months ago, so nobody noticed
  // that a NEW board inherits whatever the codec powers up with, which can
  // be silence. Restore the saved level (default 0 = music off, which since
  // W-040 still means a healthy -12dB floor so chirps speak) and tell the
  // codec about it before the first frame is drawn.
  prefs.begin("bunbun", true);
  g_musicLevel = prefs.getUChar("snd", 0);
  g_fxLevel    = prefs.getUChar("fx", 2);
  g_tttKidWins = prefs.getUShort("tttwins", 0);   // W-061: clever bunny remembers
  g_snkHigh    = prefs.getUShort("snkhi", 0);     // Carrot Chase best
  g_bbHigh     = prefs.getUShort("bbhi", 0);      // Basket Bounce best
  g_ggHigh     = prefs.getUShort("gghi", 0);      // Garden Guard best
  g_bkHigh     = prefs.getUShort("bkhi", 0);      // Burrow Blocks best
  g_bkStart    = prefs.getUChar("bkstart", 0);    // Burrow start level (GB level-select)
  g_ccHigh     = prefs.getUShort("cchi", 0);      // Carrot Crossing best
  prefs.end();
  hostSetMusicVolume(g_musicLevel);
  g_musicOn = (g_musicLevel > 0);
  Serial.printf("audio: codec set from saved levels - music=%d fx=%d\n",
                g_musicLevel, g_fxLevel);

  // (loadName() moved up above the splash paint — the name rule.)
  // The copy follows the gesture (P3, R4d): there is no MENU key to press anymore, and the
  // shell has always been the thing kids actually reach for.
  if (!loadState()) { freshState(); say("tap the egg to warm it"); }
  // A RESTART OPENS ON HIM AT HOME (Jon: "restart should just put him in the main
  // room idle in the center"): the save's position could be a side room's tub or a
  // corner mid-errand, and restoring it read as him materialising somewhere odd.
  // The main scene loads fresh anyway, so he simply starts at its centre.
  S.x = 160; S.y = FLOOR_Y;                       // centre of the 320-wide room
  g_fx = (float)S.x; g_fy = (float)S.y;
  // An existing pet that predates naming gets asked once, rather than being stuck as "bunbun"
  // with no way to reach the screen.
  if (!g_petName[0]) { g_nameAsk = true; g_namePainted = false; }
#if defined(TEST_ADULT) || defined(TEST_TEEN)
  // WORK/SCHOOL refuses at 96 and each go adds 28, so a topped-up meter allows barely two runs
  // before it locks out. Start it low, with the energy to actually do the job. Energy also
  // gates the teen's radio-and-dance beat (it needs >= 45), so a full tank exercises that too.
  S.disc = 10;
  S.energy = 100;
  S.food = max(S.food, 60.0f);
  S.fun = max(S.fun, 70.0f);      // contentMood() needs food/fun/energy >= 35 for the dance
#endif
  bcSnapshot();                  // read last boot's marker before anything stamps over it
  roomLoad(phaseRoom());
  buildLightMap();               // BEFORE anything else takes the heap, or the alloc fails
  updateDimPalette();            // so the palette is valid before the first frame is drawn
                                 // (it was all zeros while the set-time screen was up)
  musicBegin(732961);            // music.mp3, flashed after the pak
  powerLogDump();                // the previous battery run, printed the moment USB returns
  // Start a clean log for THIS power-on session. Reboots are the only gaps the uniform time
  // axis cannot represent, so a session is exactly the right unit to log continuously.
  powerLogReset();
  // Waking from the backpack must not re-ask the time: the RTC domain kept counting, so the
  // clock is restored from it and only a true cold boot gets the prompt.
  int restoredMin = 0, sleptMin = 0;
  // A fitted DS3231 outranks everything: it is right across power cuts, flat batteries and
  // reflashes, so the prompt never appears again once the chip has been set.
  // RETRY the probe. A single failed read used to drop straight through to asking for the time
  // — which is exactly what happened after a night in deep sleep: waking re-runs setup(), the
  // bus had not settled, one read failed, and it put the set-time screen up despite a perfectly
  // good chip sitting there with OSF clear. Five attempts over ~100ms costs nothing on a cold
  // boot and removes the whole failure mode.
  int probeMin = 0, probeSec = 0;
  g_rtcChip = false;
  bool rtcRead = false;
  for (int attempt = 0; attempt < 5 && !rtcRead; attempt++) {
    if (attempt) delay(20);
    Wire.beginTransmission(DS3231_ADDR);
    if (Wire.endTransmission() != 0) continue;
    g_rtcChip = true;
    rtcRead = ds3231Read(&probeMin, &probeSec);
  }
  if (rtcRead) {
    g_clockBaseMin = probeMin;
    g_clockBaseMs = millis() - (uint32_t)probeSec * 1000;   // keep the rollover aligned
    g_clockSet = true;
    Serial.printf("clock: DS3231 says %02d:%02d:%02d\n", probeMin / 60, probeMin % 60, probeSec);
    redrawRoomChrome();
  } else if (time(NULL) > 1672531200 &&
             [&]() {
               prefs.begin("bunbun", true);
               int off = prefs.getInt("tzOffMin", TZ_UNSET);
               prefs.end();
               if (off == TZ_UNSET) return false;
               // W-015 addendum (Jon, from the car 2026-08-07: "when a
               // restart before it reconnects, [shouldn't it] be current
               // time?"). After a SOFT restart -- OTA, update, crash -- the
               // system epoch survives in the RTC timer and the timezone is
               // already learned, so the pet knows the time before WiFi
               // even reconnects. A real power cut resets the epoch to 1970
               // and falls through to the old paths.
               time_t bt = time(NULL);
               long daySec = (long)((bt + (long)off * 60) % 86400);
               if (daySec < 0) daySec += 86400;
               g_clockBaseMin = (int)(daySec / 60);
               g_clockBaseMs = millis() - (uint32_t)(daySec % 60) * 1000;
               g_clockSet = true;
               Serial.printf("clock: carried epoch says %02d:%02d\n",
                             g_clockBaseMin / 60, g_clockBaseMin % 60);
               return true;
             }()) {
    redrawRoomChrome();
  } else if (restoreAfterDeepSleep(&restoredMin, &sleptMin)) {
    // ANY deep-sleep carry restores silently now (Jon, launch eve — the
    // RETIME_AFTER_MIN cutoff used to reopen the prompt after long sleeps).
    // The RC oscillator can wake a long sleep minutes off, so the carry is
    // marked provisional and the 5-minute sync (W-037) corrects it at the
    // first sight of WiFi. The prompt is for cold-boot-with-no-internet,
    // nothing else.
    Serial.printf("clock: RTC domain, slept %d min\n", sleptMin);
    g_clockBaseMin = restoredMin;
    g_clockBaseMs = millis();
    g_clockSet = true;
    g_clockProvisional = true;   // W-019: carried, not human — sync may correct it
    redrawRoomChrome();
  } else {
    if (g_rtcChip) Serial.println("clock: DS3231 present but oscillator stopped - set it once");
    // Either a real power cut, or a sleep long enough that the carried clock is worth
    // correcting. Seed the prompt with the restored time so it is a confirmation, not a
    // question from scratch.
    if (sleptMin) { g_setH = restoredMin / 60; g_setM = restoredMin % 60; }
    else {
      // No chip and no RTC domain — a real power cut. Seed from NVS so the prompt opens at
      // roughly the right time rather than 09:00, which makes it a nudge instead of a chore.
      prefs.begin("bunbun", true);
      int savedMin = prefs.getInt("clkMin", -1);
      prefs.end();
      if (savedMin >= 0) { g_setH = savedMin / 60; g_setM = savedMin % 60; }
    }
    // W-015 (Jon): don't ask yet — the internet usually knows. The pet boots
    // straight into the room on the provisional time; if no sync (or no
    // learned timezone) lands within 30s, the prompt appears as the
    // fallback it always should have been. The CLOCK button remains the
    // manual door at any time.
    g_clockBaseMin = g_setH * 60 + g_setM;
    g_clockBaseMs = millis();
    g_clockProvisional = true;   // W-019: an NVS seed is a guess with a memory
    g_clockWaitUntil = millis() + 30000;
    Serial.println("clock: waiting up to 30s for internet time before asking");
    redrawRoomChrome();
  }
  // W-019: no reboot is ever invisible again. Tonight's "mystery rebooter"
  // took an hour to identify as bench tooling because nothing logged WHY the
  // chip came up.
  Serial.printf("boot: reset reason %d, wakeup cause %d\n",
                (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause());
  Serial.printf("heap %u\n", (unsigned)ESP.getFreeHeap());
}

static bool gamesLive() {
  return g_gamePanel || g_snakePanel || g_bbPanel || g_ggPanel || g_bkPanel ||
         g_ccPanel || g_gameRoster;
}

void loop() {
  static uint32_t last = millis(), lastDraw = 0, lastUI = 0, lastSave = 0;
  static bool wasDown = false;
  uint32_t now = millis();
  float dt = (now - last) / 1000.0f; if (dt > 0.25f) dt = 0.25f;
  last = now;
  if (!g_assets) { delay(500); return; }

  // W-072: put the device back in the state it held before the update (Jon:
  // "updates should put devices back in the same state"). FULL restore now -
  // screen-off, nap, and night-sleep - made safe by the AirPlay-wake added to
  // the screen-off/standby handlers: a device restored asleep still wakes and
  // plays the instant music is cast to it, so the amp-mute can't strand a
  // stream anymore (the 8/11 break). Only after a SOFTWARE reset (OTA/crash),
  // never a power-on. Time-gated so a stale byte past morning wakes normally.
  // Only NIGHT-SLEEP restores (it keeps the amp live, so AirPlay plays through
  // a sleeping bunny). Screen-off and nap MUTE the amp, and restoring them
  // needs a wake-on-active-stream that the current host signal can't provide
  // (audio_receiver_is_playing() reports connected, not streaming - it woke
  // every nap instantly, 8/11). Until a "samples flowing" signal exists, an
  // OTA during screen-off/nap wakes normally - amp-on is the property that
  // matters. Time-gated so a stale night byte past morning wakes.
  if (g_pendingSleepRestore && g_clockSet) {
    uint8_t want = g_pendingSleepRestore;
    g_pendingSleepRestore = 0;
    saveSleepState(0);                // consume the byte
    if (want == 3 && (clockNowMin() >= 18 * 60 || clockNowMin() < g_bedEndMin)) {
      S.lights = 0;
      g_nightSleep = true;
      g_quietGreet = true;            // W-059: wake silent, greeted-first
      redrawRoomChrome();
    }
  }

  // Outside the !g_paused block on purpose: the ball should still finish retracting while
  // paused rather than freezing half way down the screen.
  discoTick(dt);

  // W-024: silence turning into sound is also a "song started" — metadata
  // isn't guaranteed (some senders never send it), and the invitation
  // shouldn't depend on politeness. But audioLive() is amplitude-based, so a
  // QUIET PASSAGE flaps it on every beat; only a silence that lasted like a
  // real pause (8s — longer than any musical breath, shorter than a between-
  // songs shuffle) makes the next sound count as a beginning.
  {
    static bool wasLive = false;
    static uint32_t deadSince = 0;
    bool live = audioLive();
    if (live) g_lastAudioLiveMs = now;      // W-043: playing audio holds off the auto-nap
    if (!live && wasLive) deadSince = now;
    if (live && !wasLive && (deadSince == 0 || now - deadSince >= 8000)) {
      danceBtnInvite();   // deadSince==0 = first sound since boot: always invite
    }
    wasLive = live;
  }

  // W-015: the 30s grace ran out with no internet time (or no learned
  // timezone) — fall back to asking, exactly as boots always used to.
  if (!g_clockSet && !g_clockPrompt && g_clockWaitUntil &&
      millis() >= g_clockWaitUntil) {
    g_clockWaitUntil = 0;
    g_clockPrompt = true;
    g_clockPromptAt = millis();
    g_setStage = 0;
    Serial.println("clock: no internet time within 30s - asking");
    drawSetTime();
  }

  // Wish recorder chatter: countdown on the ticker while listening, verdict when the clip
  // lands. The recorder itself runs on core 1; all UI stays here.
  if (wish_recorder_active()) {
    static int lastLeft = -1;
    if (wish_recorder_saving()) {
      // Countdown stops the instant the stop lands — continuing to count
      // was half of the "did it hear me?" confusion (W-008). One line,
      // then quiet until the verdict.
      if (lastLeft != -2) {
        lastLeft = -2;
        say("packing your wish...");
      }
    } else {
      int left = 15 - wish_recorder_seconds();
      if (left != lastLeft) {
        lastLeft = left;
        char t[48];
        snprintf(t, sizeof(t), "listening... %ds  (hold to finish)", left);
        say(t);
      }
    }
  } else if (g_wishDone) {
    if (g_wishDone > 0) {
      // Offline saves are safe on the shelf and fly on reconnect — say that,
      // instead of promising "sending" with no radio (council session #6).
      say(wish_uploader_online() ? "wish saved! sending it now..."
                                 : "wish saved! it flies when wifi is back");
      sfxOK();
    }
    else {
      // Say WHY (W-011): the generic "got away" line covered four different
      // failures and was wrong about half of them in the field.
      switch (wish_recorder_fail_reason()) {
        case WISH_FAIL_STORAGE:   say("bunbun's shelf jammed - tidied it, try again"); break;
        case WISH_FAIL_MIC:       say("bunbun's ears are stuck - try once more"); break;
        case WISH_FAIL_MEMORY:    say("bunbun's head is too full - try again in a moment"); break;
        case WISH_FAIL_TOO_SHORT: say("too quick! say your wish before tapping done"); break;
        default:                  say("the wish got away - try again"); break;
      }
      sfxNo();
    }
    g_wishDone = 0;
  }

  // Delivery visibility (by request): the ticker carries the wish's whole
  // journey — sending progress, the moment everything is delivered (the cue
  // that music can come back), or a note that it will finish after the music.
  {
    if (wish_uploader_busy()) {
      static int lastPct = -1;
      int pct = wish_uploader_pct();
      if (pct / 10 != lastPct / 10) {           // update the ticker per 10%
        lastPct = pct;
        char t[48];
        snprintf(t, sizeof(t), "sending wish... %d%%", pct);
        say(t);
      }
    }
    int ev = wish_uploader_take_event();
    if (ev == 1)      { say("wish delivered! music can come back now"); sfxOK(); }
    else if (ev == 2) { say("wish will finish sending after the music"); }
  }
  whatsNewTick();

  // UPDATE bar liveness: repaint the panel while an update works so the
  // percent moves, and make the verdict audible — success chirps, failure
  // buzzes — so "did it work?" never needs squinting.
  {
    static fw_update_state_t fwPrev = FW_IDLE;
    static uint32_t fwDrew = 0;
    fw_update_state_t st = fw_update_state();
    if (st != fwPrev) {
      fwPrev = st;
      if (st == FW_UP_TO_DATE) { sfxOK(); say("bunbun is up to date!"); }
      else if (st == FW_FAILED) { sfxNo(); say("update failed - tap the gear to see why"); }
      else if (st == FW_SUCCESS) { sfxOK(); say("updated! restarting..."); }
      if (g_setupPanel) drawSetupPanel();     // the update bar lives on the shelf now (IVY-4)
    } else if (g_setupPanel && st == FW_DOWNLOADING && millis() - fwDrew > 400) {
      fwDrew = millis();
      drawUpdateRow();     // the row, not the screen (review 8/14, no-flash law)
    }
    // Hold the panel open while a check or download runs: a real network
    // round-trip outlives the ~4s menu timeout, so the panel used to bail
    // to the room mid-check and the verdict landed in a ticker nobody was
    // watching (field report: "checking... then flashes and goes back to
    // the main screen"). The person who tapped CHECK is owed the answer
    // on the screen they tapped it on.
    if (g_setupPanel && (st == FW_CHECKING || st == FW_DOWNLOADING)) {
      wakeMenu();
    }
  }
  // There is deliberately NO auto-off here. An earlier version switched dance mode off after 15s
  // of silence, which broke it twice over: g_pcmMs only advances when bin energy clears
  // BD_FLOOR, a threshold set for detecting BEATS and far too high for "is anything playing",
  // so a quiet passage or a track change tripped it. And once it had switched itself off during
  // a quiet moment, danceBtnVisible() went false, the button vanished, and the next tap fell
  // through to the menu — which looked exactly like a toggle that would not turn off.
  // The button is the only thing that controls dance mode.

  // think() must NOT run while he's still an egg: it calls setAnim() every frame, which
  // overwrote the egg animation with baby poses and reset the frame timer each time, so the
  // hatch sequence could never progress past frame 0.
  if (!g_paused) {
    BC(BC_SIMULATE); simulate(dt);
    if (alive()) { updateWork(dt); BC(BC_THINK); think(dt); }
    BC(BC_KEEPLEGAL); keepLegal();   // the floor is an invariant — see the note on it
    BC(BC_CLOUDS); cloudsUpdate(dt);
    BC(BC_RAIN);   updateRain(dt);
    BC(BC_BIRD);   updateBird(dt);
    // BEFORE updateCat AND BEFORE catKeepSave, and the order is the whole feature. catKeepSave()
    // clears the keep whenever the cat is not in the room, which is correct — it stops a reboot
    // during the gap between visits resurrecting her. But at boot she is not in the room YET, so
    // running the save first wiped the very state the restore was about to read, every time.
    // Measured before this moved: she came back mid-nap at x202 as a fresh arrival at x126 in
    // phase 2, which is only reachable by walking in through a door.
    if (!g_catRestored) {
      g_catRestored = true;
      catKeepRestore();
      if (catHere()) g_nextCatAt = 0;         // she is already here; do not schedule an arrival
    }
    BC(BC_CAT);    updateCat(dt);
    catKeepSave();                    // the last frame before any reset is always the saved one
    BC(BC_LOOSE);  looseStep(dt);    // whatever is rolling keeps rolling, cat or no cat
    // She lets herself in on the builder's schedule, not one invented here. startCat() still
    // holds the "is the room available" conditions — asleep, away, mid-game, poorly.
    if (!catHere() && g_nextCatAt && millis() >= g_nextCatAt) {
      if (startCat()) g_nextCatAt = 0;
      else g_nextCatAt = millis() + 5000;        // busy: keep asking, as the builder's timer does
    }
    // R(2,6) — placer.html simInit: `cat:{st:'away',t:R(2,6),...}`. She is meant to turn up
    // almost straight away. Was 5-15 minutes here, on top of a reboot for every flash, which is
    // most of why she was never caught.
    if (!g_nextCatAt && !catHere())
      g_nextCatAt = millis() + 2000 + (esp_random() % 4000);
    BC(BC_EVENTS); updateEvents(dt);   // the old fleet-beacon suspect lives in here -
                                       // stamped so the next panic names it or clears it
    if (millis() >= g_holdUntil) g_animT += dt;   // held still for a beat after arriving
  }

  int tx, ty;
  bool down = touchRead(&tx, &ty);

  // THE MOTOR GATE LIVES HERE, ahead of every early return (Jon, 8/10,
  // second report: "the haptics are still sometimes getting stuck and keep
  // going when i go into the menu"). The first fix put the gag late in the
  // loop — but every menu page below returns before reaching it, so while
  // a page was up neither the gag NOR hapticTick ran, and a purr mid-pulse
  // froze at its duty with nothing left alive to turn it off. The only
  // placement that can't be dodged by a return is before all of them:
  // any non-main-page state = motor forced off, every pass. One DELIBERATE
  // exception: the W-061 game panel stays out of this list — its block runs
  // every pass and hapticTick() keeps ticking there, so the win thump can
  // play and any live envelope still finishes. Nothing can stick on it.
  {
    fw_update_state_t fs = fw_update_state();
    bool offMain = g_soundPanel || g_setupPanel || g_trackPanel ||
                   g_wishScreen ||   // the P3 WISH screen joins the list (spec law 6)
                   // The SLEEP surface LEAVES it: as a P2 screen it was off-main, but a P3
                   // sheet lives on the main-loop path by design, so hapticTick() must keep
                   // running under it — a purr started by a cuddle just before the sheet
                   // opened has to be allowed to finish rather than freeze at its duty.
                   g_resetPanel || g_nameAsk || g_modeAsk || g_standby ||
                   g_screenOff || g_gameRoster || g_gamePanel || g_snakePanel || g_bbPanel || g_ggPanel || g_bkPanel || g_ccPanel ||   // Jon 8/11: no motor mid-game
                   (!g_clockSet && g_clockPrompt) ||
                   fs == FW_CHECKING || fs == FW_DOWNLOADING ||
                   wish_recorder_active();   // a running motor buzzes into the mic
    if (offMain) {
      hapticPurrStop();
      hapticOffNow();
    } else {
      hapticTick();
    }
  }

  // Screen-off: any touch anywhere brings it back. Checked before everything else so nothing
  // can swallow the wake.
  if (g_screenOff) {
    // (W-072 AirPlay-wake removed 8/11: audio_receiver_is_playing() reports
    // a CONNECTED session, not an ACTIVE stream, so it fired constantly and
    // instantly woke every nap. Needs a real "samples flowing" signal before
    // it can come back - see the revert note on the restore block.)
    // W-043: an auto-nap that fell to screen-off on a low battery still owes
    // the 06:00 wake — from HERE, not just from standby (owner's spec, Q5).
    if (g_autoNap && g_clockSet && !autoNapWindowNow()) {
      autoNapWakeCredit();
      quietMorningBegin();               // W-059: nobody is here yet
      exitScreenOff();
      wasDown = down;
      return;
    }
    // W-057 (Jon, 8/10): waking takes TWO touches now, a second apart —
    // the same tap-tap rhythm dance mode taught every kid. One brush of a
    // sleeve, one cat, one W-023 ghost can no longer light the room at
    // 2am; a deliberate tap-tap always does. (The panel and amp are off
    // in this state, so the first tap has no way to answer — the rhythm
    // IS the interface.)
    // POCKET LOCK (Jon 8/14: "the idea is someone can put it in screen off mode
    // and put it in your pocket"). Tap-tap defeats a single brush, but a pocket
    // is not one brush — fabric holds contact and drums on the glass, which
    // manufactures the two edges the rhythm asks for. A finger and a pocket
    // differ in a way the panel CAN see: a finger touches briefly and lets go,
    // a pocket leans. So two tells arm the lock, and while it is armed no tap
    // counts at all:
    //   1. any single contact held longer than 1.2s — no one taps that long
    //   2. more than 5 contacts inside 4s — drumming, not knocking
    // The lock clears only after 2.5s of GENUINE quiet, and every further
    // contact pushes that horizon back, so a pocketed unit simply stays dark
    // for the whole walk. Pulling it out gives the quiet the lock needs.
    {
      static uint32_t offArmAt = 0;       // first tap of the rhythm
      static uint32_t downSince = 0;      // when the current contact began
      static uint32_t pocketUntil = 0;    // lock horizon
      static uint8_t  recent = 0;         // contacts in the current window
      static uint32_t windowAt = 0;
      uint32_t nowMs = millis();

      if (down && !wasDown) {             // a contact began
        downSince = nowMs;
        if (nowMs - windowAt > 4000) { windowAt = nowMs; recent = 0; }
        if (++recent > 5) pocketUntil = nowMs + 2500;      // tell 2: drumming
      }
      if (down && downSince && nowMs - downSince > 1200) {
        pocketUntil = nowMs + 2500;                        // tell 1: leaning
      }
      if (down) pocketUntil = pocketUntil ? nowMs + 2500 : 0;  // hold it open
      if (!down) downSince = 0;

      if (pocketUntil && nowMs >= pocketUntil) {
        pocketUntil = 0;                  // the quiet arrived — listen again
        offArmAt = 0;
        recent = 0;
      }
      if (!pocketUntil && down && !wasDown) {
        if (nowMs - offArmAt < 1600 && nowMs - offArmAt > 120) {
          offArmAt = 0;
          if (g_autoNap) autoNapWakeCredit();   // Frankie credit here too
          greetAnswer();                 // W-059: this hand IS the greeting
          exitScreenOff();
        } else {
          offArmAt = nowMs;
        }
      }
    }
    wasDown = down;
    delay(80);
    return;
  }


  // Battery sampling, the 1s trace and the serial commands ALL run here, ahead of every early
  // return below — the set-time screen, standby, and each of the touch panels. Exactly the same
  // reason the diagnostics moved up: a logger that only runs on one screen records nothing on
  // the others, and silence then looks identical to a broken logger. This cost a whole
  // afternoon of testing, because the clock prompt comes up after every reflash and swallowed
  // the first minute of every run.
  //
  // backlightTick is deliberately NOT here: standby sets its own 2% level directly, and ticking
  // it from up here would immediately override that.
  batteryUpdate();
  traceTick();
  netTick();               // no-op while the radio is off; never blocks
  airReport();             // prints the airplay stats; callbacks only record them
  beatReport();            // ditto for the beat detector
  beatVisTick();           // slew the phase-continuous visual anchor toward the beat grid

  // Kick an NTP sync shortly after joining, then once a day. Once a day is plenty: the DS3231
  // holds ~5s/month on its own, so this is correcting the SET POINT, not chasing drift.
  {
    static NetState lastN = NET_OFF;
    static uint32_t nextNtp = 0, nextAir = 0;
    if (g_netState == NET_ONLINE && lastN != NET_ONLINE) {
      nextNtp = millis() + 3000;
      // AirPlay follows the network. Requiring a serial command to start it meant every reflash
      // silently took bunbun off the AirPlay list with no way to tell from the device — if it is
      // on wifi, it should be findable.
      nextAir = millis() + 5000;             // let the join settle before advertising
    }
    if (g_netState != NET_ONLINE && airPlayOn()) airPlayEnd();
    // Held back while the naming screen is up. Starting first and renaming afterwards means the
    // service is advertised as plain "bunbun" at least once, and senders cache mDNS names — so
    // the old one lingers in Control Centre long after the rename. Publishing the right name
    // the first time avoids the whole problem.
    if (nextAir && millis() >= nextAir && g_netState == NET_ONLINE && !g_nameAsk) {
      nextAir = 0;
      airPlayBegin();
    }
    lastN = g_netState;
    if (nextNtp && millis() >= nextNtp && g_netState == NET_ONLINE) {
      nextNtp = millis() + 86400000UL;       // placeholder; tuned on completion
      ntpStart();
    }
    // Result comes back from the task; apply it here where the chip write belongs.
    // W-015: the task reports UTC now. It only becomes the pet's clock through
    // the LEARNED timezone offset — never raw (council A9: a bunbun that
    // confidently displays London time in Jon's kitchen is worse than one
    // that asks).
    if (g_ntpDone) {
      g_ntpDone = false;
      if (g_ntpMin >= 0) {
        prefs.begin("bunbun", true);
        int off = prefs.getInt("tzOffMin", TZ_UNSET);
        prefs.end();
        if (off != TZ_UNSET) {
          int local = ((g_ntpMin + off) % 1440 + 1440) % 1440;
          int before = clockNowMin();
          g_clockBaseMin = local;
          g_clockBaseMs  = millis() - (uint32_t)g_ntpSec * 1000;
          g_clockSet = true;
          g_clockProvisional = false;   // synced through a taught offset
          ds3231Write(g_clockBaseMin, g_ntpSec);
          Serial.printf("clock: internet set %02d:%02d:%02d (was %+d min out)\n",
                        local / 60, local % 60, g_ntpSec, local - before);
          // If the fallback prompt is up but untouched, the answer arrived
          // late — withdraw the question rather than making someone answer
          // what the internet just answered. A prompt opened from the
          // shelf's CLOCK row hands back to the shelf (§2's nav stack).
          if (g_clockPrompt && g_setStage == 0) {
            g_clockPrompt = false;
            if (g_setTimeFromSetup) {
              g_setTimeFromSetup = false;
              g_setupPanel = true;
              drawSetupPanel();
            } else redrawRoomChrome();
          }
          g_clockWaitUntil = 0;
        } else {
          Serial.println("clock: internet time known but timezone never taught"
                         " - one manual set fixes every future boot");
        }
      }
      // W-037 (Jon, hotspot afternoon): a 5-minute-off clock waiting hours
      // for its 6h maintenance sync is technically fine and practically
      // wrong. While WiFi is up the clock now re-checks EVERY 5 MINUTES,
      // trusted or not — SNTP stays a visitor (start, sync, deinit) so the
      // resident-RAM cost is still zero, and a wrong clock can never
      // outlive one kettle of tea. (W-019's rules unchanged: only genuine
      // fresh syncs count, and raw UTC never touches the pet without the
      // taught offset.)
      nextNtp = millis() + 300000UL;
    }
  }
  // Repaint the SETUP panel's status line as the connection progresses, so "connecting" turning
  // into an IP address is visible without having to leave and come back (moved with WIFI, IVY-4).
  if (g_setupPanel) {
    static NetState lastNet = NET_OFF;
    if (g_netState != lastNet) { lastNet = g_netState; drawSetupPanel(); }
  }
  // Same idea for the SLEEP surface: simulate() runs under every panel, so
  // self-bedtime (or the 06:00 wake) can flip the lights while this screen
  // is open. Its row-1 label was bound at DRAW time while the tap re-read
  // live state — so at 6:05pm the button said LIGHTS OUT and the tap woke
  // him instead (review 8/14). Rows repaint on change; no full screen.
  if (g_sleepSheet) {
    static bool lastL = true, lastN = false;
    if (S.lights != lastL || g_nightSleep != lastN) {
      lastL = S.lights; lastN = g_nightSleep;
      drawSleepRows();
    }
  }
  // And the CARE sheet's cards, for the same reason: a mess arriving, a fever passing, or
  // bunbun coming home from an away day all change itemDim() under an open sheet. The cards
  // are repainted individually — the sheet BODY is still painted exactly once (no flash).
  if (g_careSheet) {
    static uint8_t lastDim = 0xFF;
    static uint8_t lastPh  = 0xFF;
    static uint8_t lastAway = 0xFF;
    // AWAY OWNS THE WHOLE SHEET (Jon 8/14: "when I hit care for the first time the other
    // buttons are still there with leave a treat in the background"). This block repaints
    // the six cards individually, which drew the normal grid straight back over the away
    // layout the moment any dim state moved. While he is gone the sheet is one button, so
    // the whole sheet repaints — or nothing does.
    uint8_t awNow = (uint8_t)((bunAway() ? 1 : 0) | (g_treatsOutMs ? 2 : 0));
    if (bunAway()) {
      // NOT a return: this block sits above the touch dispatch, and returning from loop()
      // here meant the sheet drew but nothing on it could be pressed (Jon 8/14, "the treat
      // showed up but i cant click anything"). Repaint, then fall through to the rest of
      // the frame like every other branch.
      if (awNow != lastAway) { lastAway = awNow; lastDim = 0xFF; drawCareSheet(); }
    } else {
    uint8_t d = 0;
    for (int c = 0; c < 6; c++) if (itemDim(CARE_MENU[c])) d |= (1 << c);
    // AWAY is part of the dirty state (review 8/14, RISK-5): leaving flips card 3 from
    // "MEDS, dim unless sick" to "TREAT, dim unless treats are out" — and because he only
    // ever leaves WHILE sick, both sides evaluate the same and the dim mask never moved.
    // The card kept saying MEDS for the whole away day, and the homecoming was equally
    // invisible. The label depends on more than dimness, so the check has to as well.
    uint8_t aw = (uint8_t)((bunAway() ? 1 : 0) | (g_treatsOutMs ? 2 : 0));
    if (d != lastDim || (uint8_t)S.phase != lastPh || aw != lastAway) {
      lastDim = d; lastPh = (uint8_t)S.phase; lastAway = aw;
      for (int c = 0; c < 6; c++) drawCareCard(c);
    }
    }
  }
  // W-021: DORMANT for launch (honest-button rule, applied to a task).
  // The analogRead-paced capture starved the idle task at 8kHz — task WDT
  // panics inside btCaptureTask (backtrace decoded launch eve), heard from
  // the couch as "the speaker is making a weird sound" while the unit
  // crash-cycled. v2 needs a timer-driven sampler or the ES8311 mic-pad
  // line-in (the hi-fi path) — built after launch, not at 10pm before it.
  // btAudioTick();
  // NO SD polling here. Probing a missing card blocks for ~1s inside the driver, and doing
  // that from this task froze the whole game while the card was out. The audio task owns
  // detection and recovery now — it can afford to block, the render loop cannot.
  // Keep the clock honest. The DS3231 is the authority when fitted, so re-adopt it every 10
  // minutes and millis() drift can never accumulate; without a chip, keep NVS roughly current
  // so a power cut opens the prompt near the right time instead of at 09:00.
  {
    static uint32_t lastClk = 0;
    if (g_clockSet && millis() - lastClk > 600000UL) {
      lastClk = millis();
      if (!clockSyncFromChip()) {
        prefs.begin("bunbun", false);
        prefs.putInt("clkMin", clockNowMin());
        prefs.end();
      }
    }
  }
  //   p = 5s power log   t = live 1s trace   s = trace snapshot saved at the last plug event
  //   z = zero the power log
  if (Serial.available()) {
    int c = Serial.read();
    // The 'a' (force portal), 'j' (transmit test) and 'k' (scan) diagnostics are gone here.
    // They were written to diagnose a board whose WiFi transmitter turned out to be dead, and
    // they drive Arduino's WiFi class — which this build deliberately does not compile, because
    // the host application owns the radio. The host's own web UI and log stream cover this.
    // 'd' toggles dance mode from the host PC — added so dance-under-streaming performance can
    // be measured over serial without needing a finger on the panel.
    if (c == 'd' || c == 'D') {
      g_danceMode = !g_danceMode;
      if (g_danceMode) danceBegin();
      Serial.printf("dance: %d\n", (int)g_danceMode);
    }
    else if (c == 'p' || c == 'P') powerLogPrint();
    else if (c == 't' || c == 'T') traceDump();
    else if (c == 's' || c == 'S') traceDumpSaved();
    else if (c == 'o' || c == 'O') traceDumpPrev();   // the run BEFORE the last reboot
    else if (c == 'z' || c == 'Z') { powerLogReset(); Serial.println("plog: reset"); }
    // h = hop to the next remembered wifi network (W-054): the escape
    // hatch for isolation-cursed networks. Announces where it went.
    else if (c == 'h') {
      char nssid[33] = {0};
      if (wifi_switch_next_known(nssid, sizeof(nssid)) == 0) {
        Serial.printf("wifi: hopping to '%s'\n", nssid);
        say("bunbun is changing wifi...");
      } else {
        Serial.println("wifi: only one network on file - nowhere to hop");
      }
    }
    // k/K = declare/undeclare the BT bridge wired (W-049). Declaring starts
    // the listener immediately; undeclaring frees its RAM at next boot.
    else if (c == 'k') {
      prefs.putUChar("btbr", 1);
      Serial.println("btbridge: DECLARED WIRED - listener starting");
      btBridgeBegin();
    }
    else if (c == 'K') {
      prefs.putUChar("btbr", 0);
      Serial.println("btbridge: undeclared - RAM returns at next boot");
    }
    // j = the jukebox (W-047): each press plays the next voice in the sound
    // book, with its name and the kid's spec - so Piper and Maya can sit at
    // the bench and audition their own designs.
    else if (c == 'j' || c == 'J') {
      static int jj = 0;
      switch (jj) {
        case 0:  Serial.println("jukebox: tummy - 'a frog in a sock'");        sfxTummy();   break;
        case 1:  Serial.println("jukebox: yawn - 'a slide, slower at the bottom'"); sfxYawn(); break;
        case 2:  Serial.println("jukebox: stinky - 'the EW-ew song'");         sfxStinky();  break;
        case 3:  Serial.println("jukebox: lonely - 'a question that droops'"); sfxLonely();  break;
        case 4:  Serial.println("jukebox: plop - 'PLOP. hehehehe.'");          sfxPlop();    break;
        case 5:  Serial.println("jukebox: grow - 'three sparkles going up'");  sfxGrow();    break;
        case 6:  Serial.println("jukebox: tuck-in - 'a music box'");           sfxTuckIn();  break;
        case 7:  Serial.println("jukebox: home again - 'BOING BOING!'");       sfxHomeAgain(); break;
        case 8:  Serial.println("jukebox: droop - 'a balloon going sad'");     sfxDroop();   break;
        case 9:  Serial.println("jukebox: purr - the love, audible");          sfxPurr();    break;
        default: Serial.println("jukebox: excited - the thrill trill");        sfxExcited(); break;
      }
      jj = (jj + 1) % 11;
    }
    // b steps the backlight through a sweep so the real brightness curve can be checked by eye
    // without a reflash per level. It parks g_blNow there too, so backlightTick ramps from the
    // right place rather than snapping when it next runs.
    // wHHMMSS sets the clock exactly from the host, seconds included. Setting it by hand can
    // only ever be as accurate as the moment you press OK; this lands it properly, which is what
    // makes "is it a minute out, or just seconds out?" answerable rather than a guess.
    else if (c == 'w' || c == 'W') {
      char b[6]; int n = 0; uint32_t t0 = millis();
      while (n < 6 && millis() - t0 < 300) {
        if (!Serial.available()) continue;
        int d = Serial.read();
        if (d < '0' || d > '9') break;
        b[n++] = (char)d;
      }
      if (n >= 4) {
        int hh = (b[0] - '0') * 10 + (b[1] - '0');
        int mm = (b[2] - '0') * 10 + (b[3] - '0');
        int ss = (n >= 6) ? (b[4] - '0') * 10 + (b[5] - '0') : 0;
        if (hh < 24 && mm < 60 && ss < 60) {
          g_clockBaseMin = hh * 60 + mm;
          g_clockBaseMs = millis() - (uint32_t)ss * 1000;   // so the next rollover lands right
          g_clockSet = true;
          g_clockProvisional = false;
          ds3231Write(g_clockBaseMin, ss);
          prefs.begin("bunbun", false);
          prefs.putInt("clkMin", g_clockBaseMin);
          prefs.end();
          tzStore(g_clockBaseMin);   // W-019: the bench teaches too
          Serial.printf("clock: set to %02d:%02d:%02d%s\n", hh, mm, ss,
                        g_rtcChip ? " (written to DS3231)" : " (no chip - RAM only)");
        } else Serial.println("clock: out of range, use wHHMM or wHHMMSS");
      } else Serial.println("clock: use wHHMM or wHHMMSS");
    }
    // c re-opens the set-time screen. With a DS3231 fitted the prompt never appears on its own
    // again — which is the point — so there has to be some way back in to correct the time.
    // r reports the DS3231's own view of itself — the oscillator-stop flag is the thing that
    // says "I lost power and my time is meaningless", which is what makes the set-time prompt
    // appear. A chip that reports OSF set after every power cut has no working backup cell.
    else if (c == 'a' || c == 'A') { netForcePortal(); }   // force the setup portal
    // x reboots into USB card-reader mode (W-020) from the bench
    else if (c == 'x' || c == 'X') {
      if (!usb_msc_mode_available()) {
        Serial.println("usb: not in this build yet (W-020 session 2)");
      } else {
        usb_msc_mode_set_flag(true);
        Serial.println("usb: rebooting as a card reader - tap screen to return");
        delay(200);
        esp_restart();
      }
    }
    // u triggers the repo update check from the bench
    else if (c == 'u' || c == 'U') {
      Serial.printf("fw_update: start -> %d (state %d)\n", (int)fw_update_start(),
                    (int)fw_update_state());
    }
    // q records a wish exactly as a screen-hold would — from the bench, the screen is out of
    // arm's reach, and the nightly pipeline needed an end-to-end test clip.
    else if (c == 'q' || c == 'Q') {
      if (wish_recorder_active()) { wish_recorder_stop(); Serial.println("wish: stopping"); }
      else if (audioLive()) Serial.println("wish: refused, audio live");
      else Serial.printf("wish: start -> %d\n", (int)wish_recorder_start(wishDoneCb));
    }
    // m = the W-068 MIC LAB (bench only, serial only). The 28:84:85 board
    // batch records full-scale hiss on the standard analog-mic config while
    // its registers match a healthy unit byte for byte - so the experiment
    // cycles the ES8311's INPUT configuration over raw I2C (same bus bunbun
    // already drives) and each press announces the new config. Record with
    // 'q' after each press; 'm' four times returns to stock.
    //   cfg1: DMIC mode ON  (REG14 0x1A|0x40 - the digital-mic hypothesis)
    //   cfg2: analog, PGA gain floor (REG16 0x00 - does the hiss scale?)
    //   cfg3: analog, PGA gain max   (REG16 0x0A)
    //   cfg0: stock (REG14 0x1A, REG16 0x05)
    //   cfg4: internal PULLDOWN on the I2S DI pin (GPIO6). The gain ladder
    //   (8/11) proved the noise is gain-independent - born after the PGA.
    //   A floating data line + pulldown reads ZEROS; a driven line ignores
    //   the pull. This one press separates "codec not driving SDOUT /
    //   broken trace" from everything else, no oscilloscope required.
    // Capital M: the pulldown test alone, straight from stock - it must
    // never pass through cfg1, whose DMIC toggle wedges the ADC into
    // zeros until reboot and would fake the floating-line verdict.
    else if (c == 'M') {
      static bool pd = false;
      pd = !pd;
      gpio_set_pull_mode((gpio_num_t)6, pd ? GPIO_PULLDOWN_ONLY : GPIO_FLOATING);
      Serial.printf("miclab: DI (GPIO6) pulldown %s\n", pd ? "ON" : "off");
    }
    else if (c == 'm') {
      static int micCfg = 0;
      micCfg = (micCfg + 1) % 5;
      auto esw = [](uint8_t reg, uint8_t val) {
        Wire.beginTransmission(0x18);
        Wire.write(reg); Wire.write(val);
        return (int)Wire.endTransmission();
      };
      int r = 0;
      switch (micCfg) {
        case 1: r = esw(0x14, 0x5A);            Serial.println("miclab cfg1: DMIC ON (REG14=5A)"); break;
        case 2: r = esw(0x14, 0x1A) + esw(0x16, 0x00); Serial.println("miclab cfg2: analog, gain floor (REG16=00)"); break;
        case 3: r = esw(0x16, 0x0A);            Serial.println("miclab cfg3: analog, gain max (REG16=0A)"); break;
        case 4:
          gpio_set_pull_mode((gpio_num_t)6, GPIO_PULLDOWN_ONLY);
          Serial.println("miclab cfg4: DI pin (GPIO6) pulldown ON - floating line reads zeros");
          break;
        default:
          gpio_set_pull_mode((gpio_num_t)6, GPIO_FLOATING);
          r = esw(0x14, 0x1A) + esw(0x16, 0x05);
          Serial.println("miclab cfg0: stock (REG14=1A REG16=05, DI pull off)");
          break;
      }
      if (r) Serial.printf("miclab: i2c write err %d\n", r);
    }
    // g forces a shower ("gloom"; r was already the RTC report). Weather is otherwise random
    // with 7-13 minute gaps, which is a miserable way to check the overcast rendering.
    else if (c == 'g' || c == 'G') {
      g_rainStart = millis();
      g_rainUntil = millis() + 45000;
      say("rain patters on the window");
      Serial.printf("rain: forced for 45s (amount ramps over 3s)\n");
    }
    // y toggles the AirPlay receiver. Serial-only for now: the point of this build is to find
    // out whether RAOP fits in DRAM alongside the game and WiFi, and a UI button can wait until
    // that question is answered.
    else if (c == 'y' || c == 'Y') {
      Serial.printf("airplay: heap %u, psram %u, largest block %u\n",
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
      if (airPlayOn()) airPlayEnd(); else airPlayBegin();
      Serial.printf("airplay: now %s, heap %u\n", airStatus(), (unsigned)ESP.getFreeHeap());
    }
    else if (c == 'r' || c == 'R') {
      Wire.beginTransmission(DS3231_ADDR);
      Wire.write(0x0F);
      if (Wire.endTransmission() != 0 || Wire.requestFrom((int)DS3231_ADDR, 1) != 1) {
        Serial.println("rtc: no DS3231 on the bus");
      } else {
        uint8_t st = Wire.read();
        int m = 0, s = 0;
        bool got = ds3231Read(&m, &s);
        Serial.printf("rtc: status=0x%02X OSF=%d (%s), reads %s",
                      st, (st >> 7) & 1,
                      (st & 0x80) ? "time NOT trustworthy - lost power with no backup cell"
                                  : "time held since it was last set",
                      got ? "" : "FAILED");
        if (got) Serial.printf("%02d:%02d:%02d\n", m / 60, m % 60, s);
        else     Serial.println();
      }
    }
    else if (c == 'c' || c == 'C') {
      g_setH = clockNowMin() / 60; g_setM = clockNowMin() % 60; g_setStage = 0;
      g_clockSet = false;
      drawSetTime();
      Serial.println("clock: set-time screen opened");
    }
    else if (c == 'v') {
      // W-022 bench: declare the motor wired and run the test sequence —
      // one thump, then a 2s purr. Feel it or fix the wiring.
      hapticDeclare(true);
      Serial.println("haptics: DECLARED PRESENT - thump, then 2s purr");
      hapticThump();
      delay(300);
      hapticPurrStart(2000);
    }
    else if (c == 'V') {
      hapticDeclare(false);
      Serial.println("haptics: declared absent - all behaviours dormant");
    }
    else if (c == 'b') {
      // Bench diagnostic: 1s FULL duty through LEDC. Separates "motor
      // needs more than 40% to spin" from "nothing reaches the module".
      Serial.println("haptics: BLAST - 1s at 100% duty via LEDC");
      ledcWrite(PIN_HAPT, 255); delay(1000); ledcWrite(PIN_HAPT, 0);
      Serial.println("haptics: blast done");
    }
    else if (c == 'p') {
      // W-032 experiment: 10 motor-pulse rail-stiffness probes. Run once
      // plugged, once unplugged; compare the dip columns.
      if (!hapticLive()) Serial.println("W-032 probe: needs a declared, enabled motor ('v')");
      else { g_probeLeft = 10; g_probePhase = 0; g_probeAt = 0;
             Serial.println("W-032 probe: 10 pulses, 1.5s apart - hold still"); }
    }
    else if (c == 'n') {
      // Bench diagnostic: raw digital HIGH, no LEDC at all. If THIS spins
      // the motor and 'b' does not, the LEDC matrix never won the pad.
      Serial.println("haptics: RAW HIGH - 1s digitalWrite, LEDC bypassed");
      ledcDetach(PIN_HAPT);
      pinMode(PIN_HAPT, OUTPUT); digitalWrite(PIN_HAPT, HIGH);
      delay(1000);
      digitalWrite(PIN_HAPT, LOW);
      ledcAttach(PIN_HAPT, 200, 8); ledcWrite(PIN_HAPT, 0);
      Serial.println("haptics: raw done, LEDC restored");
    }
    else if (c == 'b' || c == 'B') {
      static const uint8_t sweep[] = {255, 128, 64, 32, 16, 8, 4, 1};
      static uint8_t si = 0;
      si = (si + 1) % (sizeof(sweep) / sizeof(sweep[0]));
      g_blManual = true;
      g_blNow = g_blTarget = sweep[si];
      backlightSet(sweep[si]);
      Serial.printf("backlight = %u (%.1f%% duty)\n", sweep[si], sweep[si] * 100.0f / 255.0f);
    }
    else if (c == 'n' || c == 'N') {          // hand control back to the automatic curve
      g_blManual = false;
      Serial.println("backlight = auto");
    }
  }

  // Diagnostics run BEFORE the set-time return. They were after it, so while stuck on that
  // screen nothing was reported at all — which is exactly the state we needed to see into.
  {
    static uint32_t diagT2 = 0;
    if (now - diagT2 >= 2000) {
      diagT2 = now;
      uint8_t z = 0; ftRead(0x02, &z, 1);      // touch point count, from the capacitive part
      int rx = g_rawX, ry = g_rawY;            // RAW controller coords, for calibration
      // The lamp glow and the night sky both key off the CLOCK, and the clock resets on every
      // reflash — so report it, rather than assuming the time is what you think it is.
      int cm = clockNowMin();
      // Report what a sky palette entry actually becomes, so "the sky isn't night" can be
      // checked rather than argued about.
      int si = -1;
      for (int i = 0; i < 256; i++) if (g_isSky[i]) { si = i; break; }
      if (si >= 0) {
        uint16_t o = g_pal[si], dm = g_palDim[si];
        Serial.printf("sky[%d] raw=(%d,%d,%d) dim=(%d,%d,%d) night=%.2f\n", si,
                      (o >> 11) & 0x1F, (o >> 5) & 0x3F, o & 0x1F,
                      (dm >> 11) & 0x1F, (dm >> 5) & 0x3F, dm & 0x1F, nightAmount());
      }
        // anim=<key> f=<frame>/<count> t=<animT> hold=<ms left> — the frozen-animation bug has
        // now been chased three times by inferring these from side effects. Print them.
        Serial.printf("pos=(%d,%d) tgt=(%d,%d) visit=%d work=%d settle=%ld wander=%.1f "
                      "anim=%s f=%d/%d t=%.2f fps=%.1f hold=%ld act=%d actEnd=%ld pause=%d "
                      "hapt=%d purr=%ld freq=%lu\n",
                    S.x, S.y, g_tx, g_ty, g_visit, g_workStage,
                    (long)(g_settleUntil - millis()), g_wanderT,
                    g_anim ? g_anim->key : "?", currentFrame(), g_anim ? g_anim->frames : -1,
                    g_animT, g_anim ? g_anim->fps : 0.0f,
                    (long)(g_holdUntil - millis()), (int)g_action,
                    (long)(g_actionEnd - millis()), (int)g_paused,
                    g_haptLastDuty, (long)(g_purrUntil - millis()),
                    (unsigned long)(g_haptPresent ? ledcReadFreq(PIN_HAPT) : 0));
      Serial.printf("power: %.3fV %d%% chrg=%d ref=%.3f d=%+.4f bl=%u cpu=%uMHz proj=%.1fh\n",
                    g_vbat, batteryPercent(), (int)g_charging, g_vbatRef, g_vbat - g_vbatRef,
                    g_blNow, (unsigned)getCpuFrequencyMhz(), projectedHours());
    Serial.printf("touchZ=%u raw=(%u,%u) clockSet=%d | time=%02d:%02d day=%.2f lamp=%.2f "
                    "lightMap=%s sky=%d,%d..%d,%d\n",
                    z, rx, ry, (int)g_clockSet, cm / 60, cm % 60,
                    daylight(), lampLevel(), g_light ? "ok" : "NULL",
                    g_skyX0, g_skyY0, g_skyX1, g_skyY1);
    }
  }

  // cold boot asks for the time before anything else runs
  // Naming comes before the clock prompt, so a brand-new device asks who this is and then what
  // time it is, in that order. Deliberately NOT asked on every restart — being re-prompted after
  // every power cycle is exactly the annoyance the RTC was fitted to remove for the clock, and
  // the same reasoning applies here. It appears when there is no name stored, and when STARTING
  // OVER clears it.
  if (g_nameAsk) {
    if (!g_namePainted) { drawNameScreen(); g_namePainted = true; }
    if (down && !wasDown) {
      if (nameScreenTouch(tx, ty)) {
        g_nameAsk = false;
        // Re-advertise under the new name. AirPlay publishes its name at init, so a rename only
        // reaches Control Centre if the service is torn down and brought back up.
        if (airPlayOn()) { airPlayEnd(); airPlayBegin(); }
        // W-036: a new game chooses its world right after its name (Jon's
        // ruling — the chooser exists HERE and nowhere else).
        g_modeAsk = true;
        g_modePainted = false;
      }
    }
    wasDown = down;
    return;
  }

  // W-036: the COZY / BRAVE chooser, shown exactly once per new game.
  if (g_modeAsk) {
    if (!g_modePainted) { drawModeScreen(); g_modePainted = true; }
    if (down && !wasDown) {
      int pick = -1;
      if (ty >= 78 && ty < 134) pick = 0;          // COZY
      else if (ty >= 150 && ty < 206) pick = 1;    // BRAVE
      if (pick >= 0) {
        g_modeBrave = (pick == 1);
        prefs.begin("bunbun", false);
        prefs.putUChar("brave", g_modeBrave ? 1 : 0);
        prefs.end();
        g_modeAsk = false;
        uiTick();
        redrawRoomChrome();
        char m[48];
        snprintf(m, sizeof(m), "hello, %s!%s", g_petName,
                 g_modeBrave ? " (brave mode)" : "");
        say(m);
      }
    }
    wasDown = down;
    return;
  }

  if (!g_clockSet && g_clockPrompt) {   // W-015: only when the prompt is truly up
    if (down && !wasDown && ty >= 200) {          // only the button row is live
      // W-019 ghost-tap guard: tonight a wrong time got COMMITTED through
      // this prompt with nobody in the room, teaching the unit a poisoned
      // timezone (UTC+8:45). No tap counts in the prompt's first 3 seconds,
      // and taps must be human-spaced — a glitch burst can't walk NEXT+START.
      static uint32_t lastSetTap = 0;
      if (millis() - g_clockPromptAt < 3000 || millis() - lastSetTap < 600) {
        wasDown = down;
        return;
      }
      lastSetTap = millis();
      int key = (tx < 80) ? 2 : (tx < 160 ? 1 : 0);   // "-" decrements, "+" increments
      if (setTimeInput(key)) {
        // START lands you where you came from: the shelf's CLOCK row hands
        // back to the shelf, the cold-boot prompt still opens on the room.
        if (g_setTimeFromSetup) {
          g_setTimeFromSetup = false;
          g_setupPanel = true;
          drawSetupPanel();
        } else redrawRoomChrome();
      }
    }
    wasDown = down;
    return;
  }

  // Standby: nothing renders, nothing simulates. Only the wake button is live, and the loop
  // idles hard between polls so the CPU spends most of its time doing nothing at all.
  if (g_standby) {
    // (W-072 AirPlay-wake removed 8/11 - see the screen-off note.)
    // W-043: at 06:00 an auto-nap ends by itself — back to the state he was
    // in, per the owner's spec. Manual naps keep waiting for a hand.
    if (g_autoNap && g_clockSet && !autoNapWindowNow()) {
      autoNapWakeCredit();
      quietMorningBegin();               // W-059: nobody is here yet
      exitStandby();
      wasDown = down;
      return;
    }
    // W-057 (Jon, 8/10): both nap-screen buttons take TWO taps now. The
    // screen is alive here, so the first tap answers: a small "again?"
    // under the buttons, erased if the second tap never comes. WAKE
    // mis-taps cost a night's sleep credit; OFF mis-taps cost the 06:00
    // wake — both too dear for one ghost.
    static uint32_t napArmAt = 0;
    static int napArmBtn = 0;                   // 1 = WAKE armed, 2 = OFF armed
    if (napArmBtn && millis() - napArmAt >= 1600) {
      napArmBtn = 0;
      tft.fillRect(WAKE_X, OFF_Y + OFF_H + 4, WAKE_W, 18, C_INK);   // erase the hint
    }
    if (down && !wasDown && tx >= WAKE_X && tx < WAKE_X + WAKE_W
        && ty >= WAKE_Y && ty < WAKE_Y + WAKE_H) {
      if (napArmBtn == 1) {
        napArmBtn = 0;
        // W-043 "Frankie credit" (council 2026-08-09, blocking): a hand that
        // wakes an AUTO-napped bunny early still gets the rested bunny — the
        // credit belongs to the nap, not to the clock reaching six.
        if (g_autoNap) autoNapWakeCredit();
        greetAnswer();                   // W-059: this hand IS the greeting
        exitStandby();
        wasDown = down;
        return;
      }
      napArmBtn = 1; napArmAt = millis();
      tft.setTextColor(C_BONE_LO, C_INK);
      tft.drawCentreString("again?", WAKE_X + WAKE_W / 2, OFF_Y + OFF_H + 6, 2);
      wasDown = down;
      return;
    }
    if (down && !wasDown && tx >= OFF_X && tx < OFF_X + OFF_W
        && ty >= OFF_Y && ty < OFF_Y + OFF_H) {
      if (napArmBtn == 2) {
        napArmBtn = 0;
        g_autoNap = false;               // deliberate OFF: no 06:00 surprise wake
        g_standby = false;
        enterScreenOff();                // tap-tap anywhere brings it back
        wasDown = down;
        return;
      }
      napArmBtn = 2; napArmAt = millis();
      tft.setTextColor(C_BONE_LO, C_INK);
      tft.drawCentreString("again?", WAKE_X + WAKE_W / 2, OFF_Y + OFF_H + 6, 2);
      wasDown = down;
      return;
    }
    wasDown = down;
    // Deep sleep is held back until the pack is genuinely low. While there is charge to spare
    // the nap screen stays up, which is the nicer behaviour and keeps the wake button visible;
    // below 40% that stops being affordable and it drops to ~50uA instead. Requiring a low
    // battery also means an unreliable charging read can never sleep a full device by mistake.
    // Screen-off rather than deep sleep, for the same reason the button does it: deep sleep
    // can only be woken by BOOT, and in a case that button does not exist. Being stranded with
    // a nearly-flat pack and no way to reach it is a far worse outcome than the extra ~30mA.
    if (!g_charging && batteryPercent() < DEEP_SLEEP_BELOW_PCT
        && now - g_standbySince > DEEP_SLEEP_AFTER_MS) { g_standby = false; enterScreenOff(); }
    static uint32_t lastFace = 0;
    if (now - lastFace > 20000) {         // refresh the clock occasionally
      lastFace = now;
      drawStandbyScreen();                // batteryUpdate now runs at the top of loop()
    }
    delay(90);
    return;
  }

  // track picker takes the whole screen while it is up
  if (g_trackPanel) {
    if (down && !wasDown) {
      int hit = trackPanelHit(tx, ty);
      if (hit == 101) {                                   // BACK — one level, to SOUND
        // The F2 fix: SONGS is a child of SOUND, so its exit lands there —
        // it used to jump straight to the room, the one place BACK's
        // stack-of-screens model said it couldn't be.
        uiTick();
        g_trackPanel = false;
        g_soundPanel = true;
        drawSoundPanel();
      } else if (hit == 102 || hit == 103) {              // PREV / NEXT song
        uiTick();
        int sel = g_trackSel + ((hit == 103) ? 1 : -1);
        if (sel < 0) sel = g_trackN - 1;                  // wrap like a jukebox
        if (sel >= g_trackN) sel = 0;
        g_rotate = false;
        // Choosing a song MEANS play it (field: with music level 0 every
        // selection was silently ignored — "can't select", "arrows doing
        // weird things"). If music is off, it comes on at a gentle 2.
        if (g_musicLevel == 0) { g_musicLevel = 2; hostSetMusicVolume(2); }
        g_musicOn = true;
        g_trackSel = sel;                 // highlight NOW, not when the task catches up
        prefs.begin("bunbun", false);
        prefs.putUChar("rot", 0); prefs.putUChar("snd", g_musicLevel);
        prefs.end();
        playTrack(sel);
        // keep the playing song visible: view follows the selection
        if (sel < g_trackScroll) g_trackScroll = sel;
        if (sel >= g_trackScroll + TP_ROWS_SHOWN) g_trackScroll = sel - TP_ROWS_SHOWN + 1;
        drawTrackPanel();
      } else if (hit == 0) {                              // LOOP ALL chip
        uiTick();
        g_rotate = true;
        if (g_musicLevel == 0) { g_musicLevel = 2; hostSetMusicVolume(2); }
        g_musicOn = true;
        prefs.begin("bunbun", false);
        prefs.putUChar("rot", 1); prefs.putUChar("snd", g_musicLevel);
        prefs.end();
        drawTrackPanel();
      } else if (hit > 0) {                               // a specific track
        uiTick();                                         // selections chirp like buttons do
        g_rotate = false;
        if (g_musicLevel == 0) { g_musicLevel = 2; hostSetMusicVolume(2); }
        g_musicOn = true;
        g_trackSel = hit - 1;
        prefs.begin("bunbun", false);
        prefs.putUChar("rot", 0); prefs.putUChar("snd", g_musicLevel);
        prefs.end();
        playTrack(hit - 1);
        drawTrackPanel();
      }
    }
    wasDown = down;
    return;
  }

  // W-061 tic-tac-toe. Sits with the other panels, ahead of the auto-nap
  // entry, so a mid-game nap is impossible by construction. Deliberately NOT
  // in the motor gate's offMain list: hapticTick() keeps running here, so the
  // win thump plays and any envelope still finishes — nothing can stick.
  // Games return early from loop(), which froze the backlight ramp wherever
  // it was (Jon 8/13: "hit play while it was still dim and it stayed dim").
  // Presence + ramp keep running on every game surface.
  if (g_gameRoster || g_gamePanel || g_snakePanel || g_bbPanel || g_ggPanel ||
      g_bkPanel || g_ccPanel) {
    if (down) g_lastTouchMs = now;
    backlightTick(g_charging);
    // 8/13 panic investigation: stamp WHERE we are into fw_update.c's
    // RTC-noinit crumb every pass — it survives a panic reboot, so a family
    // unit with no serial cable can still tell us whether a game owned the
    // glass when it crashed, and which one. Nothing stamps outside the
    // arcade on purpose: the last value standing IS the answer.
    g_fw_crumb = g_gameRoster ? 40
               : g_gamePanel  ? 51
               : g_snakePanel ? 52
               : g_bbPanel    ? 53
               : g_ggPanel    ? 54
               : g_bkPanel    ? 55
               :                56;
  } else if (wish_recorder_active()) {
    g_fw_crumb = 60;   // a wish is being spoken (evening-panic suspect list)
  } else if (g_fw_crumb >= 40 && g_fw_crumb <= 69) {
    // back in the room: clear the arcade/wish stamp, or a panic three hours
    // after the kid closed a game would still convict it (review 8/13 #3)
    g_fw_crumb = 0;
  }

  if (g_gameRoster) {
    if (down && !wasDown) {
      int hit = gameRosterHit(tx, ty);
      if (hit == 100) {                          // BACK - to the room
        uiTick();
        g_gameRoster = false;
        tttFlushWins();
        redrawRoomChrome();
      } else if (hit >= 0 && hit < GAME_ROSTER_N && GAME_ROSTER[hit].ready) {
        uiTick();
        g_gameRoster = false;
        if (hit == 0) {                          // TIC-TAC-TOE
          tttReset(); g_gamePanel = true; drawGamePanel();
        } else if (hit == 1) {                   // CARROT CHASE
          snakeReset(); g_snakePanel = true; drawSnakePanel();
        } else if (hit == 2) {                   // BASKET BOUNCE
          breakoutReset(); g_bbPanel = true; drawBreakoutPanel();
        } else if (hit == 3) {                   // GARDEN GUARD
          gardenReset(); g_ggPanel = true; drawGardenPanel();
        } else if (hit == 4) {                   // BURROW BLOCKS
          burrowReset(); g_bkPanel = true; drawBurrowPanel();
        } else if (hit == 5) {                   // CARROT CROSSING
          crossingReset(); g_ccPanel = true; drawCrossingPanel();
        }
      }
      // a tap on a "soon" game is silently ignored - no scold for curiosity
    }
    wasDown = down;
    return;
  }

  if (g_gamePanel) {
    // 0.1.133 (Grim's advisory): this panel sits ahead of the auto-nap entry
    // on purpose, which also means a board abandoned at dinner would hold
    // bedtime open all night. After 4 quiet minutes bunbun tidies the game
    // away himself and the room — and the nap ladder — get their screen back.
    if (millis() - g_tttTouchMs > 240000UL) {
      g_gamePanel = false;
      tttFlushWins();
      sfxYawn();
      redrawRoomChrome();
      say("bunbun tidied the game away");
      wasDown = down;
      return;
    }
    // bunbun takes his turn on his own clock, touch or no touch
    if (g_tttState == 1 && millis() >= g_tttMoveAt) {
      tttBunMove();
      g_tttTouchMs = millis();           // his move keeps the board alive too
      drawGamePanel();
    }
    if (down && !wasDown) {
      g_tttTouchMs = millis();
      int hit = gamePanelHit(tx, ty);
      if (hit == 100) {                            // BACK - up to the roster
        uiTick();
        g_gamePanel = false;
        tttFlushWins();
        g_gameRoster = true;
        drawGameRoster();
      } else if (hit == 101 && g_tttState == 2) {  // AGAIN
        uiTick();
        tttReset();
        drawGamePanel();
      } else if (hit >= 0 && hit < 9) {
        if (g_tttState == 2) {                     // tapping the old board deals fresh
          uiTick();
          tttReset();
          drawGamePanel();
        } else if (g_tttState == 0 && !g_tttBoard[hit]) {
          g_tttBoard[hit] = 1;
          sfxTick();
          S.fun = min(100.0f, S.fun + 5);      // Jon: playing games is fun
          int w = tttCheckWin();
          if (w)                   tttGameOver(w);
          else if (tttBoardFull()) tttGameOver(0);
          else {
            // a beat of thought keeps him a playmate, not a vending machine
            g_tttState = 1;
            g_tttMoveAt = millis() + 650 + esp_random() % 600;
          }
          drawGamePanel();
        }
        // a tap on a taken square while it's your turn, or any square while
        // he thinks, is silently ignored — kids double-tap, and a refusal
        // beep for every bounce would turn the board into a scold
      }
    }
    wasDown = down;
    return;
  }

  if (g_snakePanel) {
    // same tidy-away as tic-tac-toe: a board left running at dinner would
    // hold bedtime open all night, so after 4 quiet minutes bunbun packs it in.
    if (millis() - g_snkTouchMs > 240000UL) {
      g_snakePanel = false;
      snkFlushHigh();
      sfxYawn();
      redrawRoomChrome();
      say("bunbun tidied the game away");
      wasDown = down;
      return;
    }
    // the bun runs on its own clock while alive; only changed cells repaint
    if (g_snkState == 0 && millis() >= g_snkStepAt) {
      snkStepAndDraw();
      g_snkStepAt = millis() + snkStepMs();
    }
    if (down && !wasDown) {
      g_snkTouchMs = millis();
      char h = snkBtnHit(tx, ty);
      if (h == 'X') {                         // BACK, up to the roster
        uiTick();
        g_snakePanel = false;
        snkFlushHigh();
        g_gameRoster = true;
        drawGameRoster();
      } else if (g_snkState == 1) {           // any tap on the game-over card replays
        uiTick();
        snakeReset();
        drawSnakePanel();
      } else if (h == 'L') { snkSetDir(-1, 0); S.fun = min(100.0f, S.fun + 2); }
      else if   (h == 'U') { snkSetDir(0, -1); S.fun = min(100.0f, S.fun + 2); }
      else if   (h == 'D') { snkSetDir(0,  1); S.fun = min(100.0f, S.fun + 2); }
      else if   (h == 'R') { snkSetDir(1,  0); S.fun = min(100.0f, S.fun + 2); }
    }
    wasDown = down;
    return;
  }

  if (g_bbPanel) {
    if (millis() - g_bbTouchMs > 240000UL) {
      g_bbPanel = false;
      bbFlushHigh();
      sfxYawn();
      redrawRoomChrome();
      say("bunbun tidied the game away");
      wasDown = down;
      return;
    }
    // HOLD to slide the basket: read the held button every frame, not per tap
    int heldDir = 0;
    if (down) {
      g_bbTouchMs = millis();
      char h = bbBtnHit(tx, ty);
      if (h == 'X' && !wasDown) {              // BACK, up to the roster
        uiTick();
        g_bbPanel = false;
        bbFlushHigh();
        g_gameRoster = true;
        drawGameRoster();
        wasDown = down;
        return;
      }
      if (g_bbState != 0 && !wasDown && h != 'X') {   // replay after win/loss
        uiTick();
        breakoutReset();
        drawBreakoutPanel();
      } else if (h == 'P' && !wasDown) {              // start/stop (Jon 8/12)
        g_bbPaused = !g_bbPaused;
        uiTick();
        bbPauseButton();
      } else if (h == 'L') heldDir = -1;
      else if (h == 'R') heldDir = 1;
    }
    if (g_bbState == 0 && !g_bbPaused && millis() >= g_bbStepAt) {
      breakoutStep(heldDir);
      g_bbStepAt = millis() + bbStepMs();
      if (heldDir) S.fun = min(100.0f, S.fun + 0.05f);   // gentle: it steps often
    }
    wasDown = down;
    return;
  }

  if (g_ggPanel) {
    if (millis() - g_ggTouchMs > 240000UL) {
      g_ggPanel = false;
      ggFlushHigh();
      sfxYawn();
      redrawRoomChrome();
      say("bunbun tidied the game away");
      wasDown = down;
      return;
    }
    int heldDir = 0; bool fire = false;
    if (down) {
      g_ggTouchMs = millis();
      char h = ggBtnHit(tx, ty);
      if (h == 'X' && !wasDown) {
        uiTick();
        g_ggPanel = false;
        ggFlushHigh();
        g_gameRoster = true;
        drawGameRoster();
        wasDown = down;
        return;
      }
      if (g_ggState != 0 && !wasDown && h != 'X') {
        uiTick();
        gardenReset();
        drawGardenPanel();
      } else if (h == 'L') heldDir = -1;
      else if (h == 'R') heldDir = 1;
      // FIRE while HELD, not just on the press edge — ggFire() already blocks a
      // second seed until the first clears, so holding = auto-fire as it frees.
      // (Jon 8/12: "space invaders doesn't always shoot.")
      else if (h == 'F') { fire = true; if (!wasDown) S.fun = min(100.0f, S.fun + 1); }
    }
    if (g_ggState == 0 && millis() >= g_ggStepAt) {
      gardenStep(heldDir, fire);
      g_ggStepAt = millis() + ggStepMs();
    }
    if (g_ggState == 0 && millis() >= g_ggMarchAt) {
      ggMarch();
      g_ggMarchAt = millis() + ggMarchMs();
    }
    wasDown = down;
    return;
  }

  if (g_bkPanel) {
    if (millis() - g_bkTouchMs > 240000UL) {
      g_bkPanel = false;
      bkFlushHigh();
      sfxYawn();
      redrawRoomChrome();
      say("bunbun tidied the game away");
      wasDown = down;
      return;
    }
    g_bkSoft = false;
    if (down) {
      g_bkTouchMs = millis();
      char h = bkBtnHit(tx, ty);
      if (h == 'X' && !wasDown) {
        uiTick();
        g_bkPanel = false;
        bkFlushHigh();
        g_gameRoster = true;
        drawGameRoster();
        wasDown = down;
        return;
      }
      if (g_bkState == 2) {                    // the level picker
        if (!wasDown) {
          int pick = bkPickHit(tx, ty);
          if (pick >= 0) {
            uiTick();
            g_bkStart = pick;
            prefs.begin("bunbun", false);
            prefs.putUChar("bkstart", (uint8_t)pick);
            prefs.end();
            bkBeginRun();
            drawBurrowPanel();
          }
        }
        wasDown = down;
        return;
      }
      // DAS (panel 8/13, the one real control defect): tap moves once, HOLD
      // repeats — 260ms to arm, then every 100ms. Six taps to cross the well
      // was losing races against gravity by level 5.
      static uint32_t dasAt = 0;
      static char dasKey = 0;   // audit M3: a slide-in from another button must re-arm, not instant-repeat
      if (h != dasKey) { dasKey = h; if (h == 'L' || h == 'R') dasAt = millis() + 260; }
      if (g_bkState == 1 && !wasDown && h != 'X') {
        uiTick();
        burrowReset();
        drawBurrowPanel();
      } else if (!wasDown && h == 'L') { bkMove(-1); dasAt = millis() + 260; S.fun = min(100.0f, S.fun + 1); }
      else if   (!wasDown && h == 'R') { bkMove(1);  dasAt = millis() + 260; S.fun = min(100.0f, S.fun + 1); }
      else if   (wasDown && h == 'L' && millis() >= dasAt) { bkMove(-1); dasAt = millis() + 100; }
      else if   (wasDown && h == 'R' && millis() >= dasAt) { bkMove(1);  dasAt = millis() + 100; }
      else if   (!wasDown && h == 'T') { bkRotate(); S.fun = min(100.0f, S.fun + 1); }
      else if   (h == 'D') g_bkSoft = true;   // held: soft drop
    }
    if (g_bkState == 0 && millis() >= g_bkFallAt) {
      burrowGravity();
      g_bkFallAt = millis() + bkFallMs();
    }
    wasDown = down;
    return;
  }

  if (g_ccPanel) {
    if (millis() - g_ccTouchMs > 240000UL) {
      g_ccPanel = false;
      ccFlushHigh();
      sfxYawn();
      redrawRoomChrome();
      say("bunbun tidied the game away");
      wasDown = down;
      return;
    }
    if (down && !wasDown) {
      g_ccTouchMs = millis();
      char h = ccBtnHit(tx, ty);
      if (h == 'X') {
        uiTick();
        g_ccPanel = false;
        ccFlushHigh();
        g_gameRoster = true;
        drawGameRoster();
        wasDown = down;
        return;
      }
      if (g_ccState == 1 && h != 'X') {         // game over: instant rerun
        uiTick();
        crossingReset();
        drawCrossingPanel();
      } else if (h) {
        crossingHop(h);
        S.fun = min(100.0f, S.fun + 1);
      }
    }
    if (g_ccState == 0 && millis() >= g_ccTickAt) {
      crossingTick();
      g_ccTickAt = millis() + 33;
    }
    wasDown = down;
    return;
  }

  // start-over confirmation takes the whole screen while it is up
  if (g_resetPanel) {
    // The 2s press-and-hold (menu redesign P2, M9): a tap on "start over"
    // does NOTHING. Pressing it arms the countdown dial; the spokes thin as
    // the hold matures; lifting early erases the dial and that is the whole
    // cost of a slip. Only a hold that goes the full two seconds reaches
    // the wipe — whose handler below is byte-for-byte the old one.
    static uint32_t rpHoldAt = 0;
    static int rpSpokes = -1;                 // -1 = not holding
    if (down && !wasDown) {
      int hit = resetPanelHit(tx, ty);
      if (hit == 1) {
        rpHoldAt = millis();
        rpSpokes = 24;
        rpDrawRing(24);
      } else if (hit == 0) {
        // KEEP (or BACK) — instant, and one level up to the shelf it
        // opened from. Nothing about the pet was ever touched.
        uiTick();
        rpSpokes = -1;
        g_resetPanel = false;
        g_setupPanel = true;
        drawSetupPanel();
      }
    } else if (rpSpokes >= 0 && down && resetPanelHit(tx, ty) == 1) {
      // the finger must STAY on the button (review 8/14): position was
      // sampled only on the press edge, so a press-and-drag — onto KEEP,
      // even — still wiped the pet, and a stuck touch (W-023) qualified too
      uint32_t held = millis() - rpHoldAt;
      if (held >= 2000) {
        rpSpokes = -1;
        freshState(); saveState();
        g_resetPanel = false;
        redrawRoomChrome();
        say("a new egg appeared");
      } else {
        int left = 24 - (int)(held * 24 / 2000);
        if (left != rpSpokes) { rpSpokes = left; rpDrawRing(left); }
      }
    } else if (rpSpokes >= 0) {
      // released OR slid off the button: both cancel, both cost only the dial
      rpSpokes = -1;
      rpEraseRing();
    }
    wasDown = down;
    return;
  }

  // settings panel (IVY-4, the grown-up shelf) — one tap deep, behind the
  // gear on the room screen (menu redesign P2)
  if (g_setupPanel) {
    if (down && !wasDown) {
      int hit = setupPanelHit(tx, ty);
      if (hit == 25) {                          // UPDATE bar
        fw_update_state_t st = fw_update_state();
        // One tap always RE-CHECKS (Jon 8/12: "worked once then stopped /
        // flashes like no change"). The old code needed two taps - the first
        // only dismissed a stale "up to date"/"failed" verdict with no visible
        // action, which read as a dead button. Now: clear any verdict AND
        // kick a fresh check in the same tap. A check already running is left
        // alone.
        if (st != FW_CHECKING && st != FW_DOWNLOADING) {
          if (st == FW_FAILED || st == FW_UP_TO_DATE) fw_update_ack();
          if (audioLive()) { sfxNo(); say("after the music - updating restarts bunbun"); }
          else if (wish_recorder_active()) { sfxNo(); say("after the wish!"); }
          else { fw_update_start(); sfxTick(); }
        }
        drawSetupPanel(); wasDown = down; return;
      }
      if (hit == 23) {
        // WIFI cycles OFF -> connect -> setup -> OFF, rather than a plain toggle. Without the
        // middle step there is no way to reach the portal once it has a network it can see: it
        // would rejoin that one every time and never offer the choice, so moving it to a
        // different network would be impossible from the device.
        uiTick();
        if (g_netState == NET_OFF)     { netBegin();       say("wifi: connecting"); }
        else if (g_netState == NET_AP) { netEnd();         say("wifi off"); }
        else                           { netForcePortal(); say("wifi: setup mode");
          // Give the radio a beat to actually drop STA and raise the AP before the panel
          // repaints, so the status line below shows "setup mode + <this unit's network>"
          // instead of a stale "online" — that line is the whole point of setup mode when
          // four units sit in one room and you need to know WHICH network is this one.
          delay(250);
        }
        drawSetupPanel(); wasDown = down; return;
      }
      if (hit == 22) {                          // CLOCK — hand over to the set-time screen
        uiTick();
        g_setupPanel = false;
        g_setTimeFromSetup = true;              // START returns here, not the room (§2 nav)
        g_setH = clockNowMin() / 60; g_setM = clockNowMin() % 60; g_setStage = 0;
        g_clockSet = false;                     // the loop's prompt branch drives it now
        g_clockPrompt = true;                   // opened by hand = a real prompt (W-015)
        g_clockPromptAt = millis();
        drawSetTime(); wasDown = down; return;
      }
      if (hit == 27) {                          // HAPTICS switch (W-022)
        uiTick();
        hapticSetOn(!g_haptOn);
        if (g_haptOn) hapticThump();            // the switch demos itself
        drawSetupPanel();
        wasDown = down; return;
      }
      if (hit == 28 || hit == 29) {             // W-059 BEDTIME row
        uiTick();
        // 30-minute steps. Ranges widened (Jon 8/14): BED 5:00pm..12:00am and
        // WAKE 5:00am..10:00am — early enough for a toddler's bedtime, late
        // enough for a weekend lie-in.
        if (hit == 28) {                        // bed: 30-min steps, wraps
          g_bedStartMin += 30;
          if (g_bedStartMin > 24 * 60) g_bedStartMin = 17 * 60;
        } else {                                // morning: 30-min steps, wraps
          g_bedEndMin += 30;
          if (g_bedEndMin > 10 * 60) g_bedEndMin = 5 * 60;
        }
        // Event-only persist (the NVS flash-freeze rule): a settings tap is
        // an event, and there is no periodic writer to lean on.
        prefs.begin("bunbun", false);
        prefs.putInt("bedstart", g_bedStartMin);
        prefs.putInt("bedend", g_bedEndMin);
        prefs.end();
        drawSetupPanel();
        wasDown = down; return;
      }
      if (hit == 40) {                          // start over? — the M9 confirm guards it
        uiTick();
        g_setupPanel = false; g_resetPanel = true;
        drawResetPanel(); wasDown = down; return;
      }
      if (hit == 41) {                          // RESTART (moved off the confirm screen, P2)
        // Same as the button on the back, but save first — a bare reset would drop whatever
        // has happened since the last periodic save, and the clock/power logs with it.
        tft.fillScreen(C_BONE);
        tft.setTextColor(C_INK, C_BONE);
        tft.drawCentreString("restarting...", UI_W / 2, 110, 4);
        saveState(); powerLogSave(); traceFlushSD();
        delay(400);
        ESP.restart();
      }
      if (hit == 20) {                          // BACK — to the ROOM: the shelf is the gear's child now
        uiTick();
        g_setupPanel = false;
        redrawRoomChrome();
      }
    }
    wasDown = down;
    return;
  }

  // sound panel takes the whole screen while it is up
  if (g_soundPanel) {
    if (down && !wasDown) {
      int hit = soundPanelHit(tx, ty);
      if (hit == 21) {                          // SONGS
        uiTick();
        g_soundPanel = false; g_trackPanel = true;
        drawTrackPanel(); wasDown = down; return;
      }
      if (hit == 20) {                          // BACK — to the room
        uiTick();
        g_soundPanel = false;
        redrawRoomChrome();
      } else if (hit >= 30) {                   // haptics strength (W-055)
        if (hapticAvailable()) {
          g_haptLevel = hit - 30;
          drawSoundPanel();
          if (g_haptLevel > 0) hapticPulse();   // the new strength demos itself
        }
      } else if (hit >= 10) {                   // effects + rain
        g_fxLevel = hit - 10;
        drawSoundPanel();
        if (g_fxLevel > 0) sfxTick();           // audition the new level
      } else if (hit >= 0) {                    // music
        g_musicLevel = hit;
        g_musicOn = (hit > 0);
        hostSetMusicVolume(hit);
        drawSoundPanel();
      }
      if (hit >= 0) {
        // Kept outside GameState on purpose: adding fields there changes the struct size and
        // every existing save would be rejected as corrupt.
        prefs.begin("bunbun", false);
        prefs.putUChar("snd", g_musicLevel);
        prefs.putUChar("fx", g_fxLevel);
        prefs.putUChar("haptLv", (uint8_t)g_haptLevel);   // W-055
        prefs.end();
      }
    }
    wasDown = down;
    return;
  }

  // THE WISH SCREEN (menu redesign P3). It is the input lock, drawn. While the mic is live
  // the only control on the whole device is the red circle — the same total lock the room
  // used to enforce invisibly ("not let us get off that screen until unselected"), except
  // now the screen agrees with it. The countdown and every verdict land HERE instead of on
  // the ticker the recording was hiding.
  if (g_wishScreen) {
    if (down) g_lastTouchMs = now;   // the backlight must not fade out mid-wish
    backlightTick(g_charging);
    // Live repaints, never the whole screen: the circle when its state or its second
    // changes, the verdict band when bunbun says something new.
    {
      static int lastS = -99; static int8_t lastMode = -1;
      bool rec = wish_recorder_active(), sav = rec && wish_recorder_saving();
      int8_t mode = sav ? 2 : rec ? 1 : 0;
      int s = rec && !sav ? wish_recorder_seconds() : -1;
      if (mode != lastMode || s != lastS) { lastMode = mode; lastS = s; drawWishButton(); }
      static char lastMsg[64] = "\x01";
      const char *m = (millis() < g_tickUntil && g_ticker[0]) ? g_ticker : "";
      if (strcmp(m, lastMsg)) { strlcpy(lastMsg, m, sizeof(lastMsg)); drawWishStatus(); }
    }
    if (down && !wasDown) {
      int dx = tx - WS_CX, dy = ty - WS_CY;
      bool onBtn = (dx * dx + dy * dy) <= (WS_R + 6) * (WS_R + 6);
      if (wish_recorder_active()) {
        if (onBtn) wishTapStop();
        else       sfxNo();              // total lock: nothing else on the glass is live
      } else if (onBtn) {
        wishTapStart();
      } else if (titleBarBackHit(tx, ty)) {
        // BACK goes exactly ONE level — to the CARE sheet the star lives on, which is what
        // the chip says and therefore what it must do. The room also un-quiets to precisely
        // the state the wish found it in: entering a wish from an already-paused room and
        // leaving it must not silently resume the pet's decay.
        uiTick();
        g_wishScreen = false;
        g_paused = g_wishPrevPaused;
        tft.fillScreen(C_BONE);      // this screen owned the whole glass, so the wipe is right
        statsInvalidate();
        g_sleepSheet = false;
        g_careSheet = true;
        drawCareSheet();
        lastDraw = 0;                // the room above the sheet comes back on the next pass
      }
    }
    wasDown = down;
    return;
  }

  if (down) g_lastTouchMs = now;      // any contact brings the backlight back up
  // W-059: the day's first touch ends the quiet morning and answers with
  // the purr. Before the edge handlers so no page can swallow the hello.
  if (down && !wasDown) greetAnswer();
  // (g_touchBeganWithMenu retired with the icon row — it was set and never read. This one
  // replaces it and IS read.) Captured BEFORE the edge handlers, because a card tap closes
  // the sheet inside this same pass: the petting gate below only knows "is the finger in
  // the scene area", and rows 138..179 of the CARE grid live inside that area. Without
  // this, a long press on FEED would feed him and then pet him for holding still.
  static bool g_touchBeganOnSheet = false;
  if (down && !wasDown) g_touchBeganOnSheet = sheetOpen() && ty >= SHEET_TOP;
  if (!down) g_touchBeganOnSheet = false;

  // W-043: the auto-nap entry. Every modal screen already returned above,
  // so this only ever fires from the plain room — a pause, a panel, or the
  // clock prompt each hold it off by construction. Paused stays excluded on
  // purpose: exitStandby() redraws the game shell, and waking into a PAUSE
  // screen it didn't redraw would leave the two disagreeing.
  if (!g_standby && !g_screenOff && !g_paused && !g_danceMode &&
      g_clockSet && autoNapWindowNow() &&
      now - g_lastTouchMs >= AUTONAP_IDLE_MS &&
      (g_lastAudioLiveMs == 0 || now - g_lastAudioLiveMs >= AUTONAP_QUIET_MS)) {
    g_autoNap = true;
    saveSleepState(2);
    enterStandby();
    wasDown = down;
    return;
  }

  if (down && !wasDown) {
    // While a wish is RECORDING the lock is total: the red FEATURE button is the only live
    // control on the whole screen (by request — "not let us get off that screen until
    // unselected"). No PLAY, no chips, no sheets, no panels; the recording owns the device
    // until it is tapped off or the 15s runs out. (This is the PAUSE-OVERLAY entry, which
    // the coverage table keeps; the WISH screen enforces the same lock on its own surface.)
    if (g_paused && wish_recorder_active()) {
      if (tx >= WISH_BX - 4 && tx < WISH_BX + WISH_BW + 4 &&
          ty >= WISH_BY - 3 && ty < WISH_BY + WISH_BH + 3) {
        wishTapStop();
      } else {
        sfxNo();
      }
      wasDown = down;
      return;
    }
    // ================== RED FLAG R2: THE PAUSE WHITELIST ==================
    // PAUSED locks everything. While paused the animation clock, movement and ambient
    // events are all stopped, so any action taken here would change state invisibly and
    // then sit frozen — which is what made a paused pet look like a crashed one.
    //
    // ONE control stays live, and it is the one that un-pauses. Until P3 that was "the
    // bottom pin row", because PAUSE lived in it; the pin row is gone, so the whitelist
    // MOVED TO THE CHIP IN THE SAME COMMIT. pauseChipHit() is the single definition of
    // where that control is — the whitelist below and the handler beneath it both ask it,
    // so the two can never disagree and the device can never become unresumable.
    // The wish button's exception is GONE with the button (Jon 8/14): while paused, the
    // chip is now the ONLY live pixel on the glass. Wishing has its own door on the CARE
    // sheet, which quiets the room by itself.
    bool onPauseChip = pauseChipHit(tx, ty);
    if (g_paused && !onPauseChip) {
      sfxNo();
      wasDown = down;
      return;
    }
    if (onPauseChip) {
      g_paused = !g_paused;
      say(g_paused ? "paused" : "resumed");
      sfxTick();
      // Pausing puts any open sheet away. The paused banner and its WISH button live at
      // y66..150 — below SHEET_TOP, i.e. inside exactly the rows a sheet's clipped push
      // discards — so a pause under an open sheet would hide the controls the pause exists
      // to show. And a pause is a request to look at the pet, which is the sheet's cue to go.
      if (sheetOpen()) { redrawRoomBelowScene(); lastDraw = 0; }
      wasDown = down;
      return;
    }
    // ---- an open sheet owns the glass from SHEET_TOP down ----
    if (sheetOpen()) {
      if (ty >= TAB_SHEET_Y) {                    // the short tab bar under the sheet
        int t = tabHit(tx, ty, TAB_SHEET_Y);
        if (t >= 0 && tabOpen(t)) { redrawRoomBelowScene(); lastDraw = 0; }
        wasDown = down;
        return;
      }
      if (ty >= SHEET_TOP) {
        int closed = g_careSheet ? careSheetTap(tx, ty) : sleepSheetTap(tx, ty);
        if (closed) { redrawRoomBelowScene(); lastDraw = 0; }
        wasDown = down;
        return;
      }
      // Above the sheet is the live room, and the chip row is part of it — DANCE and the
      // gear stay reachable so a song starting under an open sheet can still be answered.
      if (danceBtnVisible() && tx >= DANCE_BX - 4 && tx < DANCE_BX + DANCE_BW + 4 &&
                               ty >= DANCE_BY - 3 && ty < DANCE_BY + DANCE_BH + 3) {
        g_danceMode = !g_danceMode;
        if (g_danceMode) danceBegin();
        sfxTick();
        say(g_danceMode ? "dance mode!" : "dance mode off");
        wasDown = down;
        return;
      }
      if (tx >= GEAR_BX - 3 && tx < GEAR_BX + GEAR_BW + 4 &&
          ty >= GEAR_BY - 3 && ty < GEAR_BY + GEAR_BH + 3) {
        uiTick();
        g_careSheet = g_sleepSheet = false;       // the shelf will own the whole glass
        g_setupPanel = true;
        drawSetupPanel();
        wasDown = down;
        return;
      }
      // Anywhere else on the room: put the sheet away and give the pet back. Tapping the
      // thing you came for is the most natural dismissal there is.
      uiTick();
      redrawRoomBelowScene();
      lastDraw = 0;
      wasDown = down;
      return;
    }
    // ---- the tab bar: four doors, always in the same four places ----
    if (ty >= TAB_Y) {
      int t = tabHit(tx, ty, TAB_Y);
      if (t >= 0 && tabOpen(t)) { redrawRoomBelowScene(); lastDraw = 0; }
      wasDown = down;
      return;
    }
    if (danceBtnVisible() && tx >= DANCE_BX - 4 && tx < DANCE_BX + DANCE_BW + 4 &&
                             ty >= DANCE_BY - 3 && ty < DANCE_BY + DANCE_BH + 3) {
      // MUST be tested before the `ty < SCENE_H` catch-all below. A little slop around the
      // drawn rectangle, since this is a small target near the edge of a resistive-feeling
      // touch panel.
      g_danceMode = !g_danceMode;
      if (g_danceMode) danceBegin();
      sfxTick();
      say(g_danceMode ? "dance mode!" : "dance mode off");
    } else if (tx >= GEAR_BX - 3 && tx < GEAR_BX + GEAR_BW + 4 &&
               ty >= GEAR_BY - 3 && ty < GEAR_BY + GEAR_BH + 3) {
      // THE GEAR (menu redesign P2): the one door to everything grown-up.
      // Same slop as the dance chip, same must-precede-the-catch-all rule.
      // Deliberately NOT gated on alive(): the clock, wifi and updates
      // matter to an egg too. Unreachable while paused — that branch
      // returned above, and the PAUSE chip keeps the pause whitelist.
      uiTick();
      g_setupPanel = true;
      drawSetupPanel();
      wasDown = down;
      return;
    } else if (ty < SCENE_H) {
      // THE EGG ANSWERS THE FINGER (menu redesign P3). "press MENU to warm the egg" named a
      // key that no longer exists, so the shell takes taps directly now — three of them, the
      // exact counter and grace period the OK key ran. Before the ripple and the double-tap:
      // during the egg stage there is nothing else in the room to mean.
      if (S.stage == STAGE_EGG || S.stage == STAGE_HATCHING) {
        eggTap(tx, ty);
        wasDown = down;
        return;
      }
      // W-003 (Maya: the screen should answer her finger): while it rains, a
      // tap on the pane makes a ripple instead of anything else. The window is
      // a small target and the room answers everywhere else, so consuming the
      // tap costs nothing — and it keeps a ripple-tap from counting as half a
      // dance double-tap.
      if (weatherRippleStart(tx, ty)) {
        sfxTick();
        wasDown = down;
        return;
      }
      // DOUBLE TAP anywhere on the room toggles dance mode, so it is reachable with no music
      // playing at all — the top chip only exists while something is audible, which made the
      // whole feature unavailable in a quiet room.
      static uint32_t lastSceneTap = 0;
      uint32_t tnow = millis();
      if (lastSceneTap && tnow - lastSceneTap < 450 && alive()) {
        lastSceneTap = 0;                    // consumed, so a third tap starts a fresh pair
        g_danceMode = !g_danceMode;
        if (g_danceMode) danceBegin();
        sfxTick();
        say(g_danceMode ? "dance mode!" : "dance mode off");
      } else {
        lastSceneTap = tnow;
      }
    }
  }

  // Jon's ruling (launch night): CUDDLES ARE FOR ALL AGES. Holding a finger
  // on the room pets bunbun — he purrs for as long as the finger stays and
  // loves it a little. Taps above are untouched (menu, double-tap dance);
  // only a hold past 600ms becomes petting, and the menu the initial press
  // opened politely puts itself away. Not while asleep (the heartbeat owns
  // sleeping touches) and not while paused (that path returned above).
  {
    static uint32_t petStart = 0;
    static bool petting = false;
    static bool petBlocked = false;
    static int petX = 0, petY = 0;
    bool sceneHold = down && !g_touchBeganOnSheet && ty < SCENE_H && alive() &&
                     S.lights && !bunAway();
    if (sceneHold && !wasDown) {
      petStart = now; petting = false;
      petX = tx; petY = ty;
      // Petting must be UNMISTAKABLY deliberate (field, launch night: at
      // 600ms every lingering tap became affection — love popping in and
      // out, menus eaten, buttons seemingly dead). The 1.2s motionless
      // hold, scene-area-only, is now the whole gate. The extra locks from
      // the 600ms era are gone (bench 8/7, Jon's ruling: love "no matter
      // what should always work"): the menu-was-visible lock stole every
      // hold for ~5s after ANY button press, and the any-action lock
      // refused a second love inside the first one's 700ms tail. Only the
      // work sequence still blocks — think() ignores actions entirely
      // while g_workStage drives the stage, so a love here would purr with
      // no picture and strand g_action until the errand ends.
      petBlocked = g_workStage != 0;
    }
    if (petBlocked) { if (!down) { petStart = 0; petting = false; petBlocked = false; } }
    else
    if (sceneHold && petStart && !petting &&
        (abs(tx - petX) > 26 || abs(ty - petY) > 26)) {
      petBlocked = true;               // it moved: a gesture, not a pet
    }
    else
    if (sceneHold && petStart && now - petStart > 1200) {
      if (!petting) {
        petting = true;
        g_menuUntil = 0;                 // the hold was affection, not a menu request
        say("bunbun loves this");
        sfxPurr();                       // W-047: petting murmurs too (once,
                                         // at the start - not per refresh)
        S.fun = min(100.0f, S.fun + 2);
        // Jon: petting is LOVE, not cuddles — the hearts animation at every
        // age (pa() picks each phase's own love clip), for as long as the
        // finger stays. The CUDL button keeps the cuddle clips for itself.
        startAction(pa("love"), 1.2f);
      }
      g_actionEnd = now + 700;           // rolling: the love clip holds while held
      g_love = min(100.0f, g_love + 2.0f * dt);
      hapticPurrStart(400);              // rolling refresh: purrs while held
    }
    if (!down) {
      if (petting) loveSave();
      petStart = 0; petting = false;
    }
  }

  wasDown = down;

  // The job happens OUTSIDE — the HTML swaps the whole room to the farm for its duration, and
  // without that he reads as miming a harvest in his own bedroom.
  // School happens at school, work happens at the farm — same scene-swap either way.
  roomLoad(g_workStage ? (teenSchool() && pakFind("rooms/room-school") ? "rooms/room-school"
                                                                      : "rooms/room-farm")
                       : phaseRoom());

  // Redraw cadence follows the backlight: pushing 86KB over SPI ten times a second is real
  // current, and there is no point spending it on a screen that has faded to a glow nobody
  // is watching. 10fps when bright, 4fps once dimmed.
  // Back to g_charging. This was briefly keyed to (bool)Serial to break the dim->"charging"->
  // bright feedback loop, but (bool)Serial means "a host has the CDC port OPEN", not "USB is
  // connected" — so the screen changed brightness every time a serial monitor was attached or
  // closed, which is visible and baffling to anyone holding it. The loop is now broken properly
  // by the load-steady gate in batteryUpdate(), which re-bases the reference across a backlight
  // change instead of reading the step as a charger.
  backlightTick(g_charging);
  // W-022: the envelope engine — purr wobble, dance thump, sleep heartbeat.
  // ...never while a wish is being spoken (P3, spec 1.8): a running motor buzzes straight
  // into the mic, which is the same reason the motor gate already carries this check.
  if (g_danceMode && hapticLive() && !wish_recorder_active()) {
    static float prevPulse = 0;
    float p = beatPulse();
    if (p > 0.75f && prevPulse < 0.4f) hapticThump();
    prevPulse = p;
  }
  // W-052: the idle peeps — bunbun is audibly ALIVE. Every 15-35s while
  // he's awake and present, one tiny randomized chirp (Jon's spec: "a
  // chirp every 20 seconds or so"). Sacred silences preserved: night
  // sleep, naps, away, screen-off worlds, and never over another voice.
  {
    static uint32_t peepAt = 0;
    uint32_t pnow = millis();
    if (peepAt == 0) peepAt = pnow + 15000;
    if (pnow >= peepAt && g_fxLevel > 0 && alive() && S.lights &&
        !g_nightSleep && !g_autoNap && !g_screenOff && !bunAway() &&
        !g_quietGreet &&               // W-059: silent until the morning hello
        !gamesLive() &&                // 8/13: the arcade owns the speaker
        !wish_recorder_active() &&     // P3: never chirp into an open mic (spec 1.8)
        !discoDown()) {
      bool busyVoice = false;
      for (int i = 0; i < SFX_VOICES; i++)
        if (g_voice[i].active) { busyVoice = true; break; }
      if (!busyVoice) sfxPeep();
      peepAt = pnow + 15000 + (esp_random() % 20000);
    }
  }

  // The motor gate moved to the TOP of the loop, ahead of every menu page's
  // early return — a gag placed here never ran while a page was up, which
  // is exactly when the motor needed gagging. See the block above touchRead.
  chargeProbeTick();                 // W-032 experiment: log-only, inert unless 'p'
  // Dance mode runs the scene faster. The beat pulse decays over roughly half a second, so at
  // the usual 10fps a flash gets five frames and reads as a stutter rather than a strobe. Only
  // while the ball is actually down, so the extra redraw cost is not paid the rest of the time.
  uint32_t frameMs = (g_blTarget == BL_BRIGHT) ? 100 : 250;
  if (discoVisible() && g_blTarget == BL_BRIGHT) frameMs = 50;   // 20fps target, matching idle
  // A full-screen panel owns the glass; the pet scene must not paint over it.
  // The panels return early on later passes, but on the OPENING pass the menu
  // tap set the flag mid-loop and control still reaches here — which drew the
  // pet's scene sprite across the top of a freshly-opened game (Jon 8/11:
  // "sometimes only half the screen shows the game"). Guard it.
  // SHEETS ARE NOT PANELS and are deliberately absent from this list: the whole point of a
  // sheet is that the room keeps composing above it (see the clipped push below).
  bool panelOwnsScreen = g_gamePanel || g_snakePanel || g_bbPanel || g_ggPanel || g_bkPanel || g_ccPanel || g_gameRoster || g_soundPanel ||
                         g_setupPanel || g_trackPanel || g_resetPanel || g_wishScreen;
  if (!panelOwnsScreen && now - lastDraw >= frameMs) {
    lastDraw = now;
    { uint32_t t0 = micros();
      drawScene();
      uint32_t d = micros() - t0;
      if (d > g_drawMaxUs) g_drawMaxUs = d;
      if (g_tPakAcc  > g_tPakUs)  g_tPakUs  = g_tPakAcc;
      if (g_tPixAcc  > g_tPixUs)  g_tPixUs  = g_tPixAcc;
      if (g_tPushAcc > g_tPushUs) g_tPushUs = g_tPushAcc;
      g_tPakAcc = g_tPixAcc = g_tPushAcc = 0;
      if (g_discoUsFrame > g_discoUsMax) g_discoUsMax = g_discoUsFrame;
      g_discoUsFrame = 0;
      g_drawCount++;
      // Rain-onset frame audit (W-012 acceptance): a rolling frame-time
      // average, reported at shower start and every 2s for the first 10s —
      // so "rain cut the FPS" is a number on the canary ('g' forces a
      // shower) instead of an impression in a moving car.
      static float emaUs = 0;
      emaUs += ((float)d - emaUs) * 0.1f;
      static uint32_t rainRepUntil = 0, rainRepNext = 0, lastRainStart = 0;
      if (isRaining() && g_rainStart != lastRainStart) {
        lastRainStart = g_rainStart;
        rainRepUntil = now + 10000; rainRepNext = now;
      }
      if (rainRepUntil && now < rainRepUntil && now >= rainRepNext) {
        rainRepNext = now + 2000;
        Serial.printf("rain-audit: frame avg %.1fms (rain %.2f)\n",
                      emaUs / 1000.0f, rainAmount());
      } else if (rainRepUntil && now >= rainRepUntil) {
        rainRepUntil = 0;
        Serial.printf("rain-audit: settled at %.1fms\n", emaUs / 1000.0f);
      } }
    drawWifi();                      // left of the battery, only when the radio is up
    drawDanceButton();               // top-centre, only while something is audible
    drawGearChip();                  // the grown-up door, right of DANCE (P2)
    drawSleepZs();                   // rising Z's whenever bunbun sleeps
    drawBattery();                   // into the scene sprite, before it goes out
    drawPausedBanner();              // dims everything above it, then states why
    drawPauseChip();                 // AFTER the dim: the one control that can resume must
                                     // never be drawn at half brightness (R2)
    // The clock goes LAST, after the pause pad it shares a column with (Jon 8/14: "the time
    // is now missing or behind the pause button"). Drawing it first put the digits under
    // every chip composed after them — the pad simply covered the time. The pad sits above
    // the digits now, so they no longer overlap at all, but the order stays: chrome that
    // shares a column should be painted in the order it must be read.
    drawClock();
    // THE CLIPPED PUSH — the single render change P3 makes, and the only new viewport pair
    // in the whole redesign. Three adjacent statements with no branch between them, so
    // set/reset are paired on every path including every early return above (there are
    // none between these three lines by construction). While a sheet is open only rows
    // 0..111 go to glass: 112 of 180, which is CHEAPER per frame than today — the sheet
    // costs the frame budget nothing and gives some back.
    if (sheetOpen()) {
      tft.setViewport(0, 0, UI_W, SHEET_TOP);
      scene.pushSprite(0, 0);
      tft.resetViewport();
    } else {
      scene.pushSprite(0, 0);        // the room, unobstructed
    }
    // The stats strip lives at y182, which a sheet covers. Painting it under one would put
    // seven bars through the middle of the card grid.
    // The CARE sheet carries the bars at its top; every other sheet hides them (Jon 8/14).
    if (!sheetOpen())      drawStats();
    else if (g_careSheet)  drawStats(SHEET_STATS_Y);
    // SCHEDULED cadence, not completion-stamping. Perceived smoothness is regularity more than
    // rate: stamping at completion made every slow draw stretch the following interval, so the
    // frame spacing visibly breathed under streaming load — "the old build was much smoother"
    // even at similar average fps, because standalone frames were evenly spaced.
    //
    // Advancing the schedule by exactly frameMs keeps the spacing metronomic while draws are
    // faster than the period. The rebase handles the draws that are not: when a draw overruns
    // the whole period we are late, and re-basing to "now" makes the next frame due one full
    // period later — sustained 90ms draws against a 60ms period settle into a REGULAR 90ms
    // cadence instead of either back-to-back drawing (the old 10Hz death spiral, stamped
    // before) or lurching 150ms gaps (stamped after completion).
    lastDraw += frameMs;
    if ((int32_t)(millis() - lastDraw) >= 0) lastDraw = millis();
  }
  // Ticker stays off panel surfaces (Jon 8/13: "I'm sometimes getting the
  // text bar when I select play game") — it was the one draw not gated by
  // panelOwnsScreen, so a say() landing mid-roster painted a bar across it.
  // The SLEEP sheet is the one surface with no strip: its rows carry their own sub-line, and
  // a ticker pushed at y252 would land across row 3. The CARE sheet keeps it, at y252.
  if (now - lastUI >= 60 && !panelOwnsScreen && !g_sleepSheet) { lastUI = now; drawTicker(); }
  if (now - lastSave > 20000) {
    lastSave = now; saveState();
    // mirror to the backup key every ~15th save (~5 min) so a corrupt primary
    // has a recent fallback, without doubling flash wear on the 20s cadence.
    static uint8_t bkTick = 0;
    if (++bkTick >= 15) { bkTick = 0; saveStateBackup(); }
  }

  // The loop was free-running at ~3200Hz to service a 10fps screen and a touch panel that
  // cannot report faster than ~100Hz. Yielding hands the core to the idle task, which parks
  // it on WFI instead of spinning — the same work for a fraction of the current.
  delay(4);

  // ---- diagnostics ----
  // Reports what the board is actually doing, so we stop inferring from symptoms:
  // loop rate, whether the decoder task is being scheduled, raw touch pressure, and where
  // in the game we are. A dead loop, a starved decoder and an unresponsive panel look the
  // same from the outside but produce very different numbers here.
  static uint32_t diagT = 0, loops = 0;
  loops++;
  if (now - diagT >= 2000) {
    Serial.printf("loop=%luHz  touch=%d  clockSet=%d stage=%d music=%d bt=%d fx=%d love=%.0f heap=%u psram=%u\n",
                  loops / 2, (int)down, (int)g_clockSet, (int)S.stage, (int)g_musicOn,
                  (int)g_btActive, (int)g_fxLevel, g_love,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    Serial.printf("touch: ok=%u failTx=%u failRx=%u lastStatus=0x%02X lastErr=%u\n",
                  (unsigned)g_ftOk, (unsigned)g_ftFail1, (unsigned)g_ftFail2,
                  g_ftLastStatus, g_ftLastErr);
    Serial.printf("ghost: bad=%u range=%u jump=%u\n",
                  (unsigned)g_ghostBad, (unsigned)g_ghostRange, (unsigned)g_ghostJump);
    Serial.printf("perf: fps=%.1f iterMax=%lums drawMax=%lums discoMax=%lums i2cMax=%lums\n",
                  g_drawCount / 2.0f,
                  (unsigned long)(g_iterMaxUs / 1000), (unsigned long)(g_drawMaxUs / 1000),
                  (unsigned long)(g_discoUsMax / 1000), (unsigned long)(g_ftMaxUs / 1000));
    g_fpsLast = g_drawCount / 2.0f;
    g_drawMaxLast = g_drawMaxUs;
    g_iterMaxLast = g_iterMaxUs;
    g_tPakLast = g_tPakUs; g_tPixLast = g_tPixUs; g_tPushLast = g_tPushUs;
    g_tPakUs = g_tPixUs = g_tPushUs = 0;
    g_iterMaxUs = g_drawMaxUs = g_ftMaxUs = 0;
    g_discoUsMax = 0;
    g_drawCount = 0;
    loops = 0; diagT = now;
  }
}

// ---------------- entry point, hosted ----------------
// Arduino's setup()/loop() do not exist here: CONFIG_AUTOSTART_ARDUINO is off, because this app
// has its own app_main that brings up NVS, WiFi, PTP and the AirPlay services long before the
// pet should appear. bunbun therefore runs as an ordinary FreeRTOS task the host starts when it
// is ready.
//
// Pinned to core 1. Core 0 carries the WiFi stack and the RTP/PTP threads, and the render loop
// is the one thing here that can afford to be preempted — putting it opposite the audio path is
// the same split bunbun already used for its own decoder.
extern "C" void bunbun_start(void) {
  xTaskCreatePinnedToCore([](void *) {
    initArduino();          // Arduino-as-a-component never runs this for us
    setup();
    for (;;) {
      uint32_t t0 = micros();
      loop();
      uint32_t d = micros() - t0;
      if (d > g_iterMaxUs) g_iterMaxUs = d;
      // The render loop is not a busy-wait, but it has no natural blocking point either. Without
      // this the idle task on this core never runs and the watchdog fires.
      vTaskDelay(1);
    }
  //
  // BACK TO PRIORITY 2. Raising this to 5 did smooth the render, but measurement showed it was
  // taking the time from the audio pipeline: playout error went from 0..-5ms to -4..-34ms and
  // late-frame drains became constant. The stall it appeared to fix had a different cause —
  // the scene sprite landing in PSRAM — which is addressed by starting this task before the
  // network stack fragments the internal heap. Audio timing outranks frame rate here.
  //
  // 12KB of stack, not 8: TFT_eSPI's sprite and font paths are stack-hungry and this task now
  // shares a core with the audio pipeline rather than owning it.
  //
  // CORE 0, not 1 — measured, not guessed. The host pins its whole audio hot path to core 1
  // (playback pri 7, RTP receive pri 8), and bunbun at pri 2 on that core froze for up to
  // 407ms mid-draw whenever streaming got busy (the discoMax attribution caught it: no
  // arithmetic runs 407ms; that is preemption). Core 0 carries WiFi, whose bursts are short;
  // sharing with WiFi costs bunbun far less than sharing with a continuous audio pipeline.
  }, "bunbun", 12288, nullptr, 2, nullptr, 0);
}

// ---------------- bridges the host calls ----------------
// C linkage because the callers are the host's C files. These are the LAST link in restoring
// the two features that died with bunbun's own audio pipeline: the beat detector had nothing
// listening to the music, so dance mode pulsed on its 75 BPM fallback instead of the song.

// From the host's audio output task, with the exact PCM block that is about to reach I2S —
// 16-bit interleaved stereo. beat.h's contract fits this caller exactly: record into volatiles,
// no printf, no allocation, no blocking. (Rate note: the detector assumes 44.1kHz; if the host
// ever resamples a source to 48k the detected tempo reads ~9% fast, which the octave-folding
// absorbs in practice. AirPlay ALAC is 44.1 and the output is initialised at 44100.)
extern "C" void bunbun_audio_tap(const void *pcm, size_t bytes) {
  beatFeedBlock((const uint8_t *)pcm, bytes);
}

// From the RTSP task whenever track metadata arrives (either protocol generation). Writes the
// standing now-playing slot AND announces the change once — the same behaviour the AirPlay 1
// build had, finally fed by a metadata path that actually delivers.
extern "C" void bunbun_now_playing(const char *artist, const char *title) {
  if (!title || !*title) return;
  char np[80];
  if (artist && *artist) snprintf(np, sizeof(np), "%s - %s", artist, title);
  else                   snprintf(np, sizeof(np), "%s", title);
  // Cross-thread text write into buffers the game task reads. Worst case is one garbled ticker
  // frame during a track change, which is not worth a mutex in the render path.
  setNowPlaying(np);
  say(np);
}

// The host mixes bunbun's sound effects and rain into EVERY outgoing block — music, SD
// playback or silence — which is what makes a bleep audible in a quiet room. Runs on the
// host's audio task; sfxMixInto keeps that task's discipline (no printf/alloc/blocking).
extern "C" void bunbun_mix_sfx(int16_t *pcm, size_t frames, int allow_rain) {
  // A wish is the one moment the room must be silent: chirps, rain, mood
  // sounds and button bleeps would all be recorded straight off the
  // speaker and buried under the child's voice. While the mic is live,
  // mix NOTHING (the motor is stopped in the loop's gate, the same reason).
  if (wish_recorder_active()) {
    return;
  }
  sfxMixInto(pcm, frames, allow_rain != 0);
}

// The host's playback loop calls this in the branch that used to write pure silence — i.e.
// exactly and only when no AirPlay stream is delivering samples. SD music therefore plays
// when nothing is streaming and pauses to the sample when a stream starts, with the
// precedence enforced by the loop's structure rather than by state anyone could get wrong.
extern "C" size_t bunbun_local_pcm(int16_t *dst, size_t frames) {
  // W-021: the BT line feeds the same silence branch. It can only be
  // active while SD music is off (the probe enforces it), so there is no
  // moment where both sources contend for these frames.
  // The mic is live during a wish — hold the speaker silent so SD music or
  // a paired phone can't bleed into the recording (the SFX/rain mixer and
  // the haptic motor gate on this same flag).
  if (wish_recorder_active()) {
    memset(dst, 0, frames * 2 * sizeof(int16_t));
    return 0;
  }
  size_t got;
  if (g_btActive && btRingAvail())  got = btRingPull(dst, frames);
  else if (!g_sdRing) { memset(dst, 0, frames * 2 * sizeof(int16_t)); return 0; }
  else                              got = sdRingPull(dst, frames);
  // W-048: the music level lives HERE now, on the music samples alone —
  // not on the DAC master, where it was strangling the effects too.
  // (BT deliberately included: bunbun's SND level is the room's ceiling
  // for local music of either kind; the phone rides beneath it.)
  float mg = g_musicGain;
  if (mg < 0.999f) {
    size_t n = frames * 2;
    for (size_t i = 0; i < n; i++) dst[i] = (int16_t)(dst[i] * mg);
  }
  return got;
}

// ---- W-020: the USB card-reader mode screen ----
// Runs INSTEAD of the pet for a whole boot (see main/usb_msc_mode.c). Owns
// only what a mode screen needs: TFT, backlight, touch, and one exit road.
// Never returns; every path out is esp_restart() into the normal firmware.
extern "C" void bunbun_msc_screen_run(void) {
  initArduino();
  tft.init(); tft.setRotation(0); tft.setSwapBytes(false);
  pinMode(45, OUTPUT); digitalWrite(45, HIGH);   // backlight full, no PWM needed
  touchBegin();

  // This is a SEPARATE boot path — loadName() never ran here, so the name
  // comes straight off NVS, once (the name rule reaches every screen, even
  // the one that isn't the pet). A pet that predates naming still reads
  // "bunbun", which is the species doing its job.
  {
    Preferences p;
    p.begin("bunbun", true);
    String n = p.getString("petname", "");
    p.end();
    strncpy(g_petName, n.c_str(), sizeof(g_petName) - 1);
    g_petName[sizeof(g_petName) - 1] = 0;
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(0xFDF7 /*bone*/, TFT_BLACK);
  {
    char t1[24];
    fmtPet(t1, sizeof(t1), "(( %s ))");
    tft.drawCentreString(t1, tft.width() / 2, 46, fitFont(t1, tft.width() - 8));
  }
  tft.setTextColor(0xFD20 /*orange*/, TFT_BLACK);
  tft.drawCentreString("I'm being a USB drive!", tft.width() / 2, 96, 2);
  tft.setTextColor(0xFDF7, TFT_BLACK);
  tft.drawCentreString("add or remove songs on the computer", tft.width() / 2, 126, 1);
  tft.drawCentreString("eject me there first, please", tft.width() / 2, 142, 1);
  tft.setTextColor(TFT_BLACK, 0xFD20);
  tft.fillRoundRect(60, 180, tft.width() - 120, 40, 8, 0xFD20);
  {
    // "wake <name>" inside a 120px button: fitFont is capped at font 2 so a
    // short name doesn't come back headline-sized in a chip this small, and
    // a long one steps down to font 1 rather than losing a letter.
    char wk[24];
    fmtPet(wk, sizeof(wk), "wake %s");
    int wf = fitFont(wk, tft.width() - 128, 2);
    tft.drawCentreString(wk, tft.width() / 2, wf == 2 ? 192 : 196, wf);
  }

  // Exit: a real tap, honored only after a 3s guard (the W-019 ghost lesson)
  // and requiring the same finger twice, ~50ms apart (the W-023 lesson,
  // restated locally because the game's filtered path isn't running here).
  uint32_t armedAt = millis();
  int lx = -1000, ly = -1000;
  uint32_t firstSeen = 0;
  for (;;) {
    delay(40);
    int x, y;
    if (!touchRead(&x, &y)) { firstSeen = 0; continue; }
    if (millis() - armedAt < 3000) continue;
    if (firstSeen == 0) { firstSeen = millis(); lx = x; ly = y; continue; }
    if (abs(x - lx) < 24 && abs(y - ly) < 24 && millis() - firstSeen >= 50) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(0xFDF7, TFT_BLACK);
      tft.drawCentreString("waking up...", tft.width() / 2, 110, 4);
      delay(300);
      esp_restart();
    }
    firstSeen = millis(); lx = x; ly = y;
  }
}
