// Beat detection from whatever PCM is actually on its way to I2S.
//
// Fed from two places, because the two audio sources never run at once: the MP3 mixer hook (per
// stereo frame, from the audio task) and the AirPlay callback (per block, from the RTP thread).
// Both are audio threads, so everything here records into volatiles and does nothing else. No
// printf, no allocation, no blocking, no locks — a printf on the AirPlay thread already rebooted
// this board once, and its stack has not grown since.
//
// SCOPED TO THE DANCE ON PURPOSE. An earlier version of this drove the game's global tempo and
// had to be torn out: pacing every pause and linger off the current track made the whole game
// lurch whenever the track changed, and a slow song turned a five-beat linger into a ten-second
// motionless pet. BEAT_MS stays fixed and keeps owning the game's feel; this drives the disco
// ball and nothing else.
#pragma once

// ---- what the game loop reads ----
static volatile uint32_t g_beatCount  = 0;   // increments once per detected beat
static volatile uint32_t g_beatMs     = 0;   // millis() at the last beat
static volatile uint16_t g_beatPeriod = 0;   // ms between the last two beats (0 until two seen)
static volatile float    g_beatEnergy = 0;   // current bin energy, for diagnostics
static volatile uint32_t g_pcmMs      = 0;   // last time AUDIBLE pcm was seen, any source

// ---- audio-thread state, touched nowhere else ----
static float    g_bdLP   = 0.0f;    // one-pole lowpass -> bass band
static float    g_bdMean = 0.0f;    // running mean bin energy
static float    g_bdAcc  = 0.0f;    // |bass| accumulated across the current bin
static uint16_t g_bdN    = 0;
static int32_t  g_bdSum  = 0;       // boxcar accumulator feeding the decimator
static uint8_t  g_bdDec  = 0;
static bool     g_bdArmed = true;   // Schmitt-trigger arm state; see beatFeedMono

// Detection runs on the BASS BAND, where the kick lives. Above ~200Hz is mostly melody and
// vocals, which smear the onset and leave the threshold hunting.
//
// Samples are averaged in fours and then decimated 4:1 to 11025Hz. The averaging is not for
// speed — it is a 4-tap boxcar whose first null sits at 11025Hz, exactly the region that would
// otherwise alias down into the bass band and let hi-hats masquerade as kicks. Plain
// decimation without it is the cheap-looking mistake here.
static const uint16_t BD_BIN     = 128;    // 11.6ms per bin at 11025Hz
static const float    BD_LP_A    = 0.10f;  // one-pole, ~180Hz at 11025Hz
static const float    BD_THRESH  = 1.45f;  // a bin must exceed the running mean by this
static const float    BD_FLOOR   = 900.0f; // absolute floor: silence must not produce beats
static const uint32_t BD_REFRACT = 260;    // ms; 240 BPM ceiling, and stops one kick
                                           // registering twice as its envelope wobbles

// ---- tempo, as distinct from onsets ----
// Onset detection reliably fires on snares and off-beats as well as the kick, so the raw gap
// between onsets is usually HALF the musical beat: a measured 370ms is an 80 BPM track counted
// twice, and 160 BPM is a tempo almost nothing is actually written at.
//
// The lights want the raw onsets — every hit, tight and responsive. The DANCE wants the musical
// pulse, or bunbun looks like it is having a fit. So the two are separated: g_beatMs/beatPulse()
// stay raw, and this derives a folded tempo alongside them.
//
// Median of the last eight gaps, not a mean: a missed beat produces one double-length gap and a
// spurious one produces a short gap, and a mean drags toward both. The measured spread was
// 362-534ms around a true 370, which a median shrugs off and a mean does not.
static volatile uint16_t g_beatTempo = 0;      // ms per DANCE beat, folded into a musical range
static uint16_t g_bdGap[8] = {0};
static uint8_t  g_bdGapN = 0;

static const uint16_t BD_TEMPO_MIN = 400;      // 150 BPM — above this, fold up
static const uint16_t BD_TEMPO_MAX = 1000;     // 60 BPM  — below this, fold down

static inline void beatRetempo() {
  if (g_bdGapN < 4) return;                    // not enough evidence to claim a tempo yet
  uint16_t v[8];
  uint8_t n = (g_bdGapN < 8) ? g_bdGapN : 8;
  for (uint8_t i = 0; i < n; i++) v[i] = g_bdGap[i];
  for (uint8_t i = 1; i < n; i++) {            // insertion sort; n<=8, runs ~2/sec
    uint16_t k = v[i];
    int8_t j = i - 1;
    while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
    v[j + 1] = k;
  }
  float t = v[n / 2];
  // Fold by octaves into a range a body can actually move at.
  while (t < BD_TEMPO_MIN) t *= 2.0f;
  while (t > BD_TEMPO_MAX) t *= 0.5f;
  g_beatTempo = g_beatTempo ? (uint16_t)(g_beatTempo * 0.7f + t * 0.3f) : (uint16_t)t;
}

static inline void beatFeedMono(int32_t s) {
  g_bdLP += ((float)s - g_bdLP) * BD_LP_A;
  g_bdAcc += fabsf(g_bdLP);
  if (++g_bdN < BD_BIN) return;

  float e = g_bdAcc / BD_BIN;
  g_bdAcc = 0;
  g_bdN   = 0;
  g_beatEnergy = e;

  if (g_bdMean <= 0.0f) { g_bdMean = e; return; }   // first bin only seeds the mean

  // SCHMITT TRIGGER, not a level test. Firing on "energy is above the threshold" re-triggers on
  // every bin for as long as a passage stays loud, which is why the measured onset period sat at
  // 265ms against a 260ms refractory — it was firing as fast as it was permitted to, and the
  // refractory was hiding the flaw rather than fixing it. A beat is an EDGE: arm on the way up,
  // and do not re-arm until the energy has fallen back through the running mean. The mean is the
  // right lower rail because it chases the signal, so it is always crossed again shortly —
  // a fixed rail could leave dense material permanently disarmed and kill detection outright.
  if (e < g_bdMean) g_bdArmed = true;

  uint32_t now = millis();
  // The disco ball keys off THIS, not off g_musicOn or airStatus. Those record what the
  // firmware intended; this records sound that actually reached I2S, from whichever source,
  // which is the only thing that makes a dance look right. Four bugs in this project survived
  // because an instrument logged intent instead of result — not adding a fifth.
  if (e > BD_FLOOR) g_pcmMs = now;

  if (g_bdArmed && e > g_bdMean * BD_THRESH && e > BD_FLOOR && (now - g_beatMs) > BD_REFRACT) {
    g_bdArmed = false;
    uint32_t gap = now - g_beatMs;
    if (gap < 2000) {                               // >2s means we lost the thread, not a tempo
      g_beatPeriod = (uint16_t)gap;
      g_bdGap[g_bdGapN & 7] = (uint16_t)gap;
      g_bdGapN++;
      beatRetempo();
    }
    g_beatMs = now;
    g_beatCount++;
  }

  // Track the mean AFTER deciding, and fall faster than it rises. A loud chorus otherwise
  // raises the bar permanently and detection dies for the quiet section that follows it.
  g_bdMean += (e - g_bdMean) * ((e > g_bdMean) ? 0.06f : 0.15f);
}

// From the MP3 mixer hook: one stereo frame at 44.1kHz. Cost is an add, a shift and a compare
// on three frames out of four — this hook is already the heaviest work on the board, which is
// why the expensive part only runs at 11kHz.
static inline void beatFeedFrame(int16_t l, int16_t r) {
  g_bdSum += ((int32_t)l + r) >> 1;
  if (++g_bdDec < 4) return;
  int32_t avg = g_bdSum >> 2;
  g_bdSum = 0;
  g_bdDec = 0;
  beatFeedMono(avg);
}

// From the AirPlay callback: a block of 16-bit stereo. Same averaging and decimation, done a
// block at a time.
static inline void beatFeedBlock(const uint8_t *data, size_t len) {
  const int16_t *pcm = (const int16_t *)data;
  size_t frames = len / 4;
  for (size_t i = 0; i < frames; i++) beatFeedFrame(pcm[i * 2], pcm[i * 2 + 1]);
}

// Called when the audio source changes or stops. Without this the mean carries across from the
// previous track and the first seconds of the next one either miss every beat or fire on all
// of them.
static inline void beatReset() {
  g_bdLP = g_bdMean = g_bdAcc = 0.0f;
  g_bdN = g_bdDec = 0;
  g_bdArmed = true;
  g_bdSum = 0;
  g_beatPeriod = 0;
  g_beatMs = 0;
  g_pcmMs = 0;
  g_beatTempo = 0;
  g_bdGapN = 0;
  for (int i = 0; i < 8; i++) g_bdGap[i] = 0;
}

// Is a beat still being found? Music that stopped, or a passage with no percussion, both go
// quiet here — the ball should keep turning rather than freeze mid-swing.
static inline bool beatLive() { return g_beatMs && (millis() - g_beatMs) < 3000; }

// Is anything audible playing at all? True for music with no percussion too, where beatLive()
// would be false — the ball should still come down for that.
static inline bool audioLive() { return g_pcmMs && (millis() - g_pcmMs) < 2000; }

// Musical beats per minute, folded. 0 until a tempo is established.
static inline int beatBPM() { return g_beatTempo ? 60000 / g_beatTempo : 0; }

// 0..1 sawtooth advancing at the DANCE tempo, phase-locked to the most recent onset so the
// dance stays married to the music instead of drifting against it. Falls back to the game's own
// fixed tempo when nothing is playing, so bunbun keeps swaying rather than freezing mid-step.
// How far ahead of the DETECTED beat the visuals run. A kick heard in the PCM tap only reaches
// the eyes after the next scene draw completes and pushes — 60-150ms later under streaming
// load — so reacting to detections is structurally late, up to a quarter-beat at 120 BPM. Once
// the tempo is locked the beat is PREDICTABLE, and prediction has no causality problem: fire
// the flash this many ms before each predicted beat and the light lands with the sound. The
// value is the measured mid-range render latency minus the ~46ms of DMA the tap leads by.
static const uint32_t BEAT_VIS_LEAD_MS = 70;

// PHASE-CONTINUOUS anchor for everything visual. The raw g_beatMs resets on EVERY detected
// onset — and onsets fire on snares and off-beats too, roughly twice per musical beat — so any
// visual phased directly off it jumps discontinuously twice a beat: the lights restart their
// decay mid-flash and the dance hop yanks sideways by up to ~75ms. That is why dance mode was
// smooth in silence and twitchy with music: no onsets, no resets. (The user called it: "could
// it be the beat?" — it was.)
//
// This anchor never jumps. Onsets that land near the predicted beat grid pull it a quarter of
// their error; onsets near the half-beat (the snares) are ignored for phase entirely, since the
// folded tempo already owns the question of which pulses are the musical beat.
static uint32_t g_visAnchor = 0;
static uint32_t g_visSeen   = 0;

static inline void beatVisTick() {
  if (!g_beatTempo) { g_visAnchor = 0; g_visSeen = g_beatCount; return; }
  if (g_beatCount == g_visSeen) return;
  g_visSeen = g_beatCount;
  uint32_t per = g_beatTempo;
  uint32_t bm  = g_beatMs;
  if (!g_visAnchor) { g_visAnchor = bm; return; }
  int32_t err = (int32_t)((bm - g_visAnchor) % per);
  if (err > (int32_t)(per / 2)) err -= (int32_t)per;
  if (err < 0) err = -((-err) % (int32_t)per);          // keep the wrap symmetric
  if (err > (int32_t)(per / 2)) err -= (int32_t)per;
  if (err >= -(int32_t)(per / 4) && err <= (int32_t)(per / 4)) {
    g_visAnchor += err / 4;                             // slew, never snap
  }
}

// The anchor the visual accessors phase against: the continuous one when tempo is locked,
// the raw onset time otherwise.
static inline uint32_t beatVisAnchor() { return g_visAnchor ? g_visAnchor : g_beatMs; }

static inline float dancePhase() {
  // Deliberately a literal and not BEAT_MS: sfx.h is included AFTER this header, so the symbol
  // is not visible here. Kept equal to it by hand, which is fine — this is only the fallback for
  // when there is no music to lock onto.
  static const uint32_t BD_FALLBACK_MS = 800;
  uint32_t per = g_beatTempo ? g_beatTempo : BD_FALLBACK_MS;
  uint32_t a = beatVisAnchor();
  uint32_t since = a ? (millis() - a + BEAT_VIS_LEAD_MS) : millis();
  return (float)(since % per) / (float)per;
}

// Called from the game loop, where there is stack for printf — never from an audio thread.
// Reports the tempo it is actually tracking so "does this find the beat?" is a measurement
// against a song with a known BPM, not an impression formed by watching a light blink.
static void beatReport() {
  static uint32_t last = 0, lastCount = 0;
  if (millis() - last < 3000) return;
  last = millis();
  if (g_beatCount == lastCount) return;                  // no beats: stay quiet
  uint32_t got = g_beatCount - lastCount;
  lastCount = g_beatCount;
  Serial.printf("beat: %u in 3s | onset %u BPM (%ums) -> dance %u BPM (%ums) | e=%.0f mean=%.0f\n",
                (unsigned)got, (unsigned)(g_beatPeriod ? 60000 / g_beatPeriod : 0),
                (unsigned)g_beatPeriod, (unsigned)beatBPM(), (unsigned)g_beatTempo,
                g_beatEnergy, g_bdMean);
}

// 1.0 the instant a beat lands, falling to 0 by the time the next one is due. Squared so the
// attack reads as a flash rather than a fade — this is what the disco lights ride on.
static inline float beatPulse() {
  // No music to lock onto — dance mode can now be switched on in a silent room — so fall back
  // to a steady imaginary beat at the house tempo. Softer than a detected one, because it is a
  // stand-in rather than something actually happening in the audio.
  if (!beatLive()) {
    uint32_t per = 800;
    float t = 1.0f - (float)(millis() % per) / (float)per;
    return t * t * 0.55f;
  }
  // With a locked tempo, run PREDICTIVELY and led by the render latency: periodic pulses at the
  // folded tempo, each firing BEAT_VIS_LEAD_MS early so the flash lands with the audible kick
  // rather than a rendered-frame after it. The modulo form also keeps pulsing through a quiet
  // bar or a missed detection instead of freezing until the next onset.
  if (g_beatTempo) {
    uint32_t since = millis() - beatVisAnchor() + BEAT_VIS_LEAD_MS;
    float t = 1.0f - (float)(since % g_beatTempo) / (float)g_beatTempo;
    return t * t;
  }
  // No tempo lock yet: raw reactive decay from the last onset, as before.
  uint32_t age = millis() - g_beatMs;
  uint32_t per = g_beatPeriod ? g_beatPeriod : 500;
  if (age >= per) return 0.0f;
  float t = 1.0f - (float)age / (float)per;
  return t * t;
}
