#pragma once
// ---- W-022: haptics — "a body, never a notification" ----
//
// A PWM vibration driver on GPIO44 (UART port RXD0 — quiet at boot; its
// neighbour TXD0 chatters the ROM log and would buzz every power-on).
// LEDC at 200Hz, 8-bit duty: duty IS intensity on these driver boards.
//
// The doctrine, chartered and amended (2026-08-06 haptics session):
// bunbun's motor exists so he feels alive in the hand. It never buzzes
// for attention, never accompanies a UI chirp, never signals. Four
// behaviours exist, and only these four:
//   1. PURR on cuddle — 20<->40% duty wobbling on a ~2Hz envelope, fading
//      out over the last 300ms (a motor that snaps off feels like a
//      machine; a fade feels like settling).
//   2. THUMP on the beat, dance mode only.
//   3. SLEEP HEARTBEAT, touch-gated: a finger resting on a night-sleeping
//      bunbun feels a soft double-thump; no finger, total stillness.
//   4. PASSIVE-EMOTION PULSE (Jon's addendum): one gentle pulse when a
//      need crosses its threshold — bunbun expressing himself, rate-
//      limited to once per need per 10 minutes, never at night, never
//      while paused.
//
// HONEST-BUTTON GATING: everything no-ops unless the motor is declared
// present (NVS "bunbun"/"hapt"). The SETUP shelf's HAPTICS switch and the
// MUSIC panel's strength row only draw themselves when the motor is
// declared, so no unit ever shows a control for a body part it lacks.
// 2026-08-23: the DEFAULT is now present, because every unit ships with a
// motor. Serial 'v' still declares one and runs the thump/purr test;
// 'V' declares absent and writes an explicit 0, which beats the default.

static const int PIN_HAPT = 44;
static bool g_haptPresent = false;   // NVS-declared: the motor is wired
static bool g_haptOn = true;         // the SETUP switch (NVS "haptOn")
// W-055: strength 0-4 (NVS "haptLv"). Maps to a duty CEILING - see the
// scale table in hapticTick. Level 2 ~ the 3V motor's spec (2.7-3.3V) on
// the 5V rail; level 4 = the old full-send for the 5V motors.
static int g_haptLevel = 3;
static float g_haptEnv = 0.0f;       // current envelope target bookkeeping
static uint32_t g_purrUntil = 0;     // purr active while millis() < this
static uint32_t g_thumpUntil = 0;    // one-shot thump window
static uint32_t g_hbNext = 0;        // next heartbeat double-thump
static uint8_t g_hbPhase = 0;

static inline bool hapticAvailable() { return g_haptPresent; }
// AN EMPTY HOUSE DOESN'T BUZZ (Jon 8/14: "all haptics should stop when he is away — he
// shouldn't be part of the mix essentially"). Every haptic in the build is bunbun's own
// body — a purr, a thump, a snore — so while he is gone there is nothing there to feel.
// One gate here covers all of them rather than a check at each call site.
static bool bunAway();
static inline bool hapticLive() { return g_haptPresent && g_haptOn && !bunAway(); }

static int g_haptLastDuty = -1;      // write cache; -1 forces the next ledcWrite

// The motor's LEDC attachment is not immortal: exitScreenOff() re-runs the
// full tft.init(), which is already known to strip LEDC from the backlight
// pin (that's why backlightBegin() is re-run there) — and a stripped haptics
// pin plus the write cache below meant the motor died silently until reboot
// ("the vibration just stops running"). Call this after any display re-init:
// detach + attach guarantees a live channel, and clearing the cache forces
// the next tick to rewrite the duty.
static void hapticReattach() {
  if (!g_haptPresent) return;
  ledcDetach(PIN_HAPT);
  ledcAttach(PIN_HAPT, 200, 8);
  ledcWrite(PIN_HAPT, 0);
  g_haptLastDuty = -1;
}

// Hard stop for paths where hapticTick will no longer run (screen off, deep
// sleep): whatever the envelope was doing, the motor goes still NOW, and the
// pending windows are cleared so a wake doesn't replay a stale pulse.
static void hapticOffNow() {
  g_purrUntil = g_thumpUntil = 0;
  if (!g_haptPresent) return;
  ledcWrite(PIN_HAPT, 0);
  g_haptLastDuty = 0;
}

static void hapticBegin() {
  prefs.begin("bunbun", true);
  // EVERY UNIT HAS A MOTOR NOW (Jon, 2026-08-23: "i plugged in a motor. all units will have
  // a motor going forward"), so the DEFAULT is present. The honest-button rule below is
  // unchanged in spirit - a unit still shows no control for a body part it lacks - but the
  // fleet's truth flipped, and bench-declaring ten boards by hand to reflect it is the
  // wrong way round. Serial 'V' remains the escape hatch for a board with no motor: it
  // writes 0 explicitly, and an explicit 0 still wins over this default.
  g_haptPresent = prefs.getUChar("hapt", 1) != 0;
  g_haptOn = prefs.getUChar("haptOn", 1) != 0;
  g_haptLevel = prefs.getUChar("haptLv", 3);
  prefs.end();
  if (g_haptPresent) {
    ledcAttach(PIN_HAPT, 200, 8);
    ledcWrite(PIN_HAPT, 0);
  }
}

static void hapticDeclare(bool present) {
  g_haptPresent = present;
  prefs.begin("bunbun", false);
  prefs.putUChar("hapt", present ? 1 : 0);
  prefs.end();
  if (present) { ledcAttach(PIN_HAPT, 200, 8); ledcWrite(PIN_HAPT, 0); }
  else ledcWrite(PIN_HAPT, 0);
}

static void hapticSetOn(bool on) {
  g_haptOn = on;
  prefs.begin("bunbun", false);
  prefs.putUChar("haptOn", on ? 1 : 0);
  prefs.end();
  if (!on) ledcWrite(PIN_HAPT, 0);
}

// Behaviour entry points — call freely; the gate lives here.
// Windows sized for a real ERM motor: it needs ~50-100ms just to spool up,
// so a 40ms pulse died before anything moved (bench, Jon's hand: "getting
// nothing"). Thumps are full-duty and long enough to actually happen.
static bool g_pulseSoft = false;     // the current thump window is a gentle pulse
static void hapticPurrStart(uint32_t ms) { if (hapticLive()) g_purrUntil = millis() + ms; }
static void hapticPurrStop()             { if (g_purrUntil > millis() + 300) g_purrUntil = millis() + 300; }
static void hapticThump()                { if (hapticLive()) { g_thumpUntil = millis() + 150; g_pulseSoft = false; } }
static void hapticPulse()                { if (hapticLive()) { g_thumpUntil = millis() + 200; g_pulseSoft = true; } }

// The envelope engine: one call per render tick. Cheap float math, one
// ledcWrite when the duty actually changes.
static void hapticTick() {
  if (!hapticLive()) return;
  uint32_t now = millis();
  // W-022 watchdog: something in the wider system occasionally evicts the
  // motor's pin from the LEDC matrix at runtime (bench 8/7, launch night:
  // the snore died twice mid-sleep with no nap-wake involved, and the 'n'
  // diagnostic's detach+attach revived it both times — 'b', which writes
  // through the existing route, did nothing). ledcReadFreq() returns 0 for
  // an evicted pin, so until the thief is caught by name: notice within
  // 2s, log the time of death for correlation, put the route back.
  // (Bench 8/7, 00:55 trace: ledcReadFreq reads 0 whenever the duty is 0 —
  // core 3 stops the channel on a zero write — so a dead route is only
  // distinguishable from ordinary idle WHILE the motor is being driven.)
  static uint32_t chkAt = 0;
  if (now >= chkAt) {
    chkAt = now + 2000;
    if (g_haptLastDuty > 0 && ledcReadFreq(PIN_HAPT) == 0) {
      Serial.printf("haptics: LEDC route lost at %lus - reattached (W-022 watchdog)\n",
                    (unsigned long)(now / 1000));
      hapticReattach();
    }
  }
  // AFFECTION IS WELDED TO THE PICTURE (Jon, launch morning: "cuddle
  // haptics keep going out... love as well" — the 01:17 trace showed 245
  // driven flawlessly through every cuddle; what he felt "go out" was the
  // fixed purr timer settling before the clip finished, plus the dead air
  // between chained presses). The rule now: while a love or cuddle clip is
  // on stage, the purr window keeps a 300ms tail ahead of itself — the
  // motor runs for exactly as long as the screen shows affection, and the
  // settle fires only as the clip truly ends. Hand and screen cannot
  // disagree.
  if (g_action && now < g_actionEnd && g_anim &&
      (strstr(g_anim->key, "love") || strstr(g_anim->key, "cuddle"))) {
    if (g_purrUntil < now + 300) g_purrUntil = now + 300;
  }
  int duty = 0;
  // The register map, COMPRESSED for the unmounted motor (bench truth: a
  // loose module barely registers below ~80% — the snore at 60% was
  // "not buzzing at all"). Tiers keep their ORDER but crowd the top of
  // the window; re-spread downward once the coin is glued to the v3
  // diaphragm back (and further with the 3V motors):
  //   thump (dance jump)      255
  //   cuddle / petting purr   245  constant, 300ms settle
  //   passive-emotion pulse   235
  //   action frame-buzz       220
  //   sleep snore             205  softest, still unmistakably there
  if (now < g_purrUntil) {
    float d = 245.0f;
    uint32_t left = g_purrUntil - now;
    if (left < 300) d *= (float)left / 300.0f;   // settle, never snap off
    duty = (int)d;
  } else if (now < g_thumpUntil) {
    duty = g_pulseSoft ? 235 : 255;
  } else if (g_action && now < g_actionEnd) {
    // EVERY action buzzes with its own animation — the body moves when the
    // picture moves, each at its clip's own tempo. (Cuddle stays constant
    // via the purr branch above, which outranks this one.)
    duty = (currentFrame() < (g_anim->frames + 1) / 2) ? 220 : 0;
  } else if (!S.lights) {
    // Sleep SNORE, riding the floating Z's own 3.6s clock (drawSleepZs:
    // (t/20 + i*60) % 180) — a buzz exactly as the lead Z launches, for as
    // long as bunbun sleeps. A 15-minute "snoring spell" used to gate this
    // (drift off, go still) and every single expiry got reported as "the
    // haptics just stopped" — the Z's don't stop, so the snore doesn't
    // either. The battery is already protected by the screen-off timer:
    // no lit screen, no tick, no motor.
    //
    // Jon's shape, third refinement: TEMPO, not amplitude. This motor's
    // felt range is too narrow for a swell (185 vs 255 both read as
    // "on"), but it renders on/off rhythm perfectly — so the snore is a
    // pulse train that ACCELERATES through each Z cycle: slow... slow..
    // quicker-quicker-quick, then a rest. One crescendo per breath,
    // like the indrawn half of a real snore.
    {
      static const uint16_t P[6] = {0, 800, 1450, 1950, 2350, 2650};
      uint32_t cyc = now % 3600;                    // the Z clock's period
      for (int i = 0; i < 6; i++) {
        if (cyc >= P[i] && cyc < (uint32_t)P[i] + 150) { duty = 255; break; }
      }
    }
  }
  // KICK-START (bench 8/7, 01:21: trace showed 245 driven through 100% of
  // cuddles while the hand felt ~85% — the missing ones are ERM start-up
  // failures, not firmware. This 5V motor has a ~3.7V start floor and the
  // bench rail sits ~4.3V, sagging further while the charger runs: a
  // RUNNING motor at 86-96% duty stays running, a STOPPED one sometimes
  // never breaks static friction. So every rest-to-drive transition gets
  // 180ms of full duty to break it loose, then settles to its tier —
  // textbook ERM overdrive. The 3V motor swap deletes the problem class.)
  // W-055: the strength slider (Jon: "sometimes it's a bit too loud" -
  // and the incoming 3V motors, 2.7~3.3V spec, run overdriven on the 5V
  // rail at these tiers). Each level is a duty CEILING; every tier
  // scales beneath it so the felt ORDER survives at any strength.
  // Level 2's ceiling (~170/255 of 5V ~ 3.3V) sits right at the 3V spec.
  static const uint8_t HAPT_CAP[5] = {0, 140, 170, 210, 255};
  int lv = g_haptLevel; if (lv < 0) lv = 0; if (lv > 4) lv = 4;
  duty = duty * HAPT_CAP[lv] / 255;
  static uint32_t kickUntil = 0;
  if (duty > 0 && g_haptLastDuty <= 0) kickUntil = now + 180;
  // The kick still overdrives to break static friction, but only halfway
  // to full - a 180ms spike is fine for an ERM, cruel to a 3V one at 5V.
  if (duty > 0 && now < kickUntil) duty = (255 + HAPT_CAP[lv]) / 2;
  if (duty != g_haptLastDuty) { ledcWrite(PIN_HAPT, duty); g_haptLastDuty = duty; }
}
