// bunbun sound effects — a direct port of the HTML's beep()/chirp* set.
//
// The browser builds these with Web Audio oscillators. Here they are mixed sample-by-sample
// into the MP3 stream through ESP32-audioI2S's `audio_process_i2s` weak hook, which the
// library calls once per stereo frame just before the i2s_write. That is the only place the
// two can meet: the library owns the I2S channel outright, so a second writer would fight it.
//
// Everything here runs in the audio task. The game task only ever sets fields and then flips
// `active` last, so a half-written voice can never be picked up.
#pragma once
#include <Arduino.h>
#include <math.h>

static const int SFX_VOICES = 8;
static const float SFX_SR = 44100.0f;

enum SfxWave : uint8_t { SFX_SQUARE = 0, SFX_TRIANGLE = 1 };

struct SfxVoice {
  volatile bool  active;
  uint32_t delay;        // frames still to wait before it sounds
  uint32_t left;         // frames remaining once sounding
  float    phase, step;  // 0..1 phase accumulator
  float    amp, decay;   // per-frame exponential decay, matching the browser's ramp
  uint8_t  wave;
};
static SfxVoice g_voice[SFX_VOICES];

// Music and effects are levelled INDEPENDENTLY, because they do not pass through the same
// attenuation. The library applies its own Gain() to the music before calling our hook, so
// music at volume 4/21 arrives already tiny while a beep mixed in afterwards is at full
// scale — which is why effects drowned the track when both ran off one setting.
// 0 = off .. 4 = loudest. Boots at 0 (W-005, Jon, council-approved session #2):
// after any restart the music stays off until someone asks for it — "the 3am
// power-blip concert is a support call." Fun fact found implementing this: the
// SND panel saves the chosen level to NVS but nothing ever restored it, so the
// old behavior was actually "always level 3 after boot" regardless of choice.
static volatile int g_musicLevel = 0;
static volatile int g_fxLevel = 2;         // effects + rain
static volatile bool g_sfxEnabled = true;
// The bottom of this scale is deliberately near the noise floor — level 1 is meant to be
// "barely there", not "quiet". Going much below 0.005 stops being quiet and starts being
// quantisation noise, since a beep peaks at 0.05 before this multiplier is applied.
// W-041 (launch night, "the chirps have still never worked"): this table
// was calibrated for the STANDALONE build, where the MP3 decoder delivered
// music already attenuated to single-digit percent and a beep needed to be
// tiny to match. The AirPlay port runs full-scale digital audio with volume
// applied at the DAC — against that, 0.095 x a 0.05-amplitude beep is
// -46dBFS: one hundred and fifty counts, physically inaudible. Rain never
// noticed because its own gain divides this table back out (see RAIN_GAIN
// compensation below); every BEEP on the port has been microscopic since
// the day of the merge. Scaled ~50x: a tick at fx 4 now lands ~-12dBFS.
static const float SFX_GAIN[5] = {0.0f, 0.6f, 1.2f, 2.4f, 4.8f};
static const float RAIN_GAIN = 0.035f;     // continuous, so quieter again than a one-shot

// Music maps onto the library's own 0-21 scale.
//
// Capped at 12 — what used to be level 3 — because 17 was louder than this thing ever wants to
// be. It is an ambient pet sitting in the corner of a room, not a speaker; the top of the range
// should be "clearly there", not "filling the room".
//
// Level 1 is now 1, the quietest non-zero step the library offers, because 4 was still too
// present for something meant to be barely audible. That makes the gap from 1 to 2 the widest
// on the scale, which is the right way round: the interesting resolution is at the quiet end,
// where you are choosing between "almost silent" and "just noticeable".
static const uint8_t MUSIC_VOL[5] = {0, 1, 4, 8, 12};

// freq Hz, dur seconds, wave, vol 0-1, delay seconds — the same argument list as the HTML's
// beep(), so the chirps below can be transcribed one for one.
// HOW DARK IT IS, 0 day to 1 deep night. Set once a frame from the room's own clock; nothing
// else in this file knows about time. The picture already regrades globally — 256 palette
// entries, once, when the level changes — and this is the same idea for sound: after dark every
// square becomes a triangle and everything quietens. Same melodies, edges rounded off. Doing it
// here rather than at forty call sites is what keeps it from drifting the way per-behaviour
// cooldowns have.
static float g_sfxNight = 0.0f;

static void beep(float freq, float dur = 0.07f, uint8_t wave = SFX_SQUARE,
                 float vol = 0.05f, float when = 0.0f) {
  if (!g_sfxEnabled || g_fxLevel <= 0 || freq <= 0) return;
  if (g_sfxNight > 0.6f)      { wave = SFX_TRIANGLE; vol *= 0.55f; }
  else if (g_sfxNight > 0.3f) { vol *= 0.80f; }
  for (int i = 0; i < SFX_VOICES; i++) {
    if (g_voice[i].active) continue;
    SfxVoice &v = g_voice[i];
    v.delay = (uint32_t)(when * SFX_SR);
    v.left  = (uint32_t)(dur * SFX_SR);
    if (!v.left) return;
    v.phase = 0;
    v.step  = freq / SFX_SR;
    v.amp   = vol;
    // Web Audio ramps to 0.001 over dur; the equivalent per-frame factor is that ratio
    // raised to 1/frames, which keeps the decay curve identical without a pow() per sample.
    v.decay = powf(0.001f / max(vol, 0.0011f), 1.0f / (float)v.left);
    v.wave  = wave;
    v.active = true;                 // set last: the audio task reads this to claim the voice
    return;
  }
}

// The HTML's named cues, transcribed exactly.
// Was 0.035 — a whisper, tuned when the tick was only the level-audition
// sound. W-030 made it the voice of every navigation tap and the field
// verdict (Jon, day one) was "a bit too quiet": brought up to the same
// 0.05 the rest of the cue family speaks at.
static inline void sfxTick()   { beep(660, 0.045f, SFX_SQUARE, 0.05f); }
static inline void sfxOK()     { beep(880); beep(1320, 0.09f, SFX_SQUARE, 0.05f, 0.08f); }
static inline void sfxNo()     { beep(220, 0.12f); }
static inline void sfxHatch()  { beep(660);
                                 beep(880,  0.07f, SFX_SQUARE, 0.05f, 0.09f);
                                 beep(1320, 0.14f, SFX_SQUARE, 0.05f, 0.18f); }
static inline void sfxDead()   { beep(440, 0.20f, SFX_TRIANGLE);
                                 beep(330, 0.20f, SFX_TRIANGLE, 0.05f, 0.22f);
                                 beep(220, 0.40f, SFX_TRIANGLE, 0.05f, 0.46f); }
static inline void sfxWin()    { const float f[4] = {523, 659, 784, 1047};
                                 for (int i = 0; i < 4; i++)
                                   beep(f[i], 0.09f, SFX_SQUARE, 0.05f, i * 0.09f); }
static inline void sfxLose()   { beep(330, 0.12f); beep(262, 0.20f, SFX_SQUARE, 0.05f, 0.13f); }
static inline void sfxCall()   { for (int i = 0; i < 3; i++)
                                   beep(1046, 0.06f, SFX_SQUARE, 0.05f, i * 0.12f); }
static inline void sfxSick()   { beep(180, 0.15f); }
static inline void sfxMess()   { beep(140, 0.08f); }

// ---- W-046: the passive voices, described by Piper (5) and Maya (9) ----
// (sound session 2026-08-10 — each comment is the kid's own spec.)
// "His tummy TALKS! brrp-brrp, like a frog in a sock."
static inline void sfxTummy()  { beep(180, 0.09f, SFX_TRIANGLE, 0.04f);
                                 beep(140, 0.11f, SFX_TRIANGLE, 0.04f, 0.10f); }
// "A tiny yawn that slides down like a slide — slower at the bottom."
static inline void sfxYawn()   { beep(660, 0.10f, SFX_TRIANGLE, 0.04f);
                                 beep(440, 0.12f, SFX_TRIANGLE, 0.04f,  0.11f);
                                 beep(330, 0.16f, SFX_TRIANGLE, 0.035f, 0.24f); }
// "The stinky song!! It goes EW-ew."
static inline void sfxStinky() { beep(500, 0.06f, SFX_SQUARE, 0.04f);
                                 beep(350, 0.08f, SFX_SQUARE, 0.04f, 0.07f); }
// "A little question that droops. Like he says 'boo?' and nobody answers."
static inline void sfxLonely() { beep(523, 0.10f, SFX_TRIANGLE, 0.04f);
                                 beep(494, 0.16f, SFX_TRIANGLE, 0.035f, 0.12f); }
// "PLOP. hehehehe." — the old thud keeps its job; the plop lands after.
static inline void sfxPlop()   { beep(140, 0.08f);
                                 beep(90,  0.10f, SFX_SQUARE, 0.05f, 0.07f); }
// "That's the BIGGEST thing that ever happens to him — sparkles going up."
static inline void sfxGrow()   { beep(659,  0.08f, SFX_SQUARE, 0.045f);
                                 beep(784,  0.08f, SFX_SQUARE, 0.045f, 0.09f);
                                 beep(1047, 0.12f, SFX_SQUARE, 0.045f, 0.18f); }
// "A music box. Three notes going down and the last one takes forever."
// The night's LAST sound (Ivy): nothing chirps after this until morning.
static inline void sfxTuckIn() { beep(784, 0.14f, SFX_TRIANGLE, 0.04f);
                                 beep(659, 0.16f, SFX_TRIANGLE, 0.038f, 0.16f);
                                 beep(523, 0.22f, SFX_TRIANGLE, 0.035f, 0.34f); }
// "BOING BOING! Because he hops when he's happy!"
static inline void sfxHomeAgain() { beep(660, 0.06f, SFX_SQUARE, 0.045f);
                                    beep(880, 0.09f, SFX_SQUARE, 0.045f, 0.08f); }
// "A droopy sound. Like a balloon going sad. Not scary."
static inline void sfxDroop()  { beep(220, 0.10f, SFX_TRIANGLE, 0.045f);
                                 beep(180, 0.16f, SFX_TRIANGLE, 0.04f, 0.11f); }
// THE LOVE (Jon, 8/10: "people need to fall in love and want to care for
// bunbun"). A contented hum for cuddles and petting — three low triangles
// pulsing like a slow heartbeat, the audible twin of the haptic purr.
// Quietest voice in the book on purpose: love murmurs, it never announces.
static inline void sfxPurr()   { beep(196, 0.16f, SFX_TRIANGLE, 0.035f);
                                 beep(196, 0.16f, SFX_TRIANGLE, 0.035f, 0.22f);
                                 beep(220, 0.20f, SFX_TRIANGLE, 0.032f, 0.44f); }
// THE IDLE PEEPS (W-052, Jon: "I would expect a chirp every 20 seconds
// or so") — the ambient aliveness layer. A tiny randomized chirp, one or
// two notes from a happy little scale, different every time like a real
// pet's ambient noises. Quietest-but-one voice; the variety is what
// keeps it charming instead of mechanical.
static inline void sfxPeep() {
  static const float SCALE[6] = {659, 784, 880, 988, 1047, 1175};
  uint32_t r = esp_random();
  float f1 = SCALE[r % 6];
  beep(f1, 0.05f, SFX_SQUARE, 0.035f);
  if ((r >> 8) & 1)                              // half the time, a second hop
    beep(SCALE[(r >> 4) % 6], 0.06f, SFX_SQUARE, 0.032f, 0.07f);
}

// Excitement — a quick rising trill for the genuinely thrilling moments
// (a meal when truly hungry, a new song to dance to). Starts 0.18s late
// so it rides just behind sfxOK instead of mushing into it.
static inline void sfxExcited() { beep(523, 0.06f, SFX_SQUARE, 0.045f, 0.18f);
                                  beep(659, 0.06f, SFX_SQUARE, 0.045f, 0.26f);
                                  beep(880, 0.10f, SFX_SQUARE, 0.045f, 0.34f); }

// Rain: continuous filtered noise, the same shape as the HTML's looped noise buffer through a
// lowpass. Faded in and out over ~1.5s so a shower arrives and leaves rather than snapping on.
static volatile bool g_rainAudio = false;
static float g_rainLevel = 0, g_rainLP = 0;
static uint32_t g_rainSeed = 0x1234567;

// ---- pacing ----
// The single tempo every timed thing in the game is built from: pauses, lingers, and the
// school/work stages all measure themselves in beats and bars so the whole thing moves at one
// consistent, unhurried rate.
//
// This used to be a live beat detector reading the music through the mixer hook. It worked,
// but tying the pacing to whatever track happened to be playing made the game's feel lurch
// between tracks — a slow one stretched every pause with it, and a 5-beat linger became a
// 10-second motionless character. A fixed medium tempo is steadier and is what the pet
// actually wants; the music plays underneath it rather than driving it.
static const uint32_t BEAT_MS = 800;               // 75 BPM — a middle-of-the-road lofi pace

// Seconds per beat, and per bar (4 beats). Callable from the game task.
static inline float beatSecs() { return BEAT_MS / 1000.0f; }
static inline float barSecs()  { return beatSecs() * 4.0f; }

// Soft clip, replacing a hard clamp at +/-32767. Once the music is near full scale, anything
// mixed on top runs straight into the rail, and a hard clamp squares off the waveform — which
// is audible as crackle rather than as "loud". Above the knee this compresses roughly 3:1
// instead, so overload rounds over rather than shearing off. Cheap enough for a per-sample hook:
// two compares and a divide, no floating point.
static inline int16_t softClip(int32_t v) {
  const int32_t KNEE = 26000;
  if (v > KNEE)  return (int16_t)min((int32_t)32767,  KNEE + (v - KNEE) / 3);
  if (v < -KNEE) return (int16_t)max((int32_t)-32768, -KNEE + (v + KNEE) / 3);
  return (int16_t)v;
}

// Called by ESP32-audioI2S once per stereo frame, immediately before it writes to I2S.
// `sample` packs the two int16 channels; mix the voices into both and hand it back.
void audio_process_i2s(uint32_t *sample, bool *continueI2S) {
  *continueI2S = true;

  // Tap the music for the beat detector BEFORE anything else. This has to sit above the
  // `if (!any) return` below: that early-out fires whenever no sound effect and no rain are
  // playing, which is almost all the time, so a tap placed after it would only ever see the
  // music during a sound effect. Reads the incoming sample, so it hears the track as decoded
  // and not our own bleeps mixed on top.
  beatFeedFrame((int16_t)(*sample & 0xFFFF), (int16_t)(*sample >> 16));

  float mix = 0;
  bool any = false;
  for (int i = 0; i < SFX_VOICES; i++) {
    SfxVoice &v = g_voice[i];
    if (!v.active) continue;
    if (v.delay) { v.delay--; any = true; continue; }
    if (!v.left) { v.active = false; continue; }
    float s = (v.wave == SFX_SQUARE) ? (v.phase < 0.5f ? 1.0f : -1.0f)
                                     : (4.0f * fabsf(v.phase - 0.5f) - 1.0f);
    mix += s * v.amp;
    v.amp *= v.decay;
    v.phase += v.step;
    if (v.phase >= 1.0f) v.phase -= 1.0f;
    v.left--;
    any = true;
  }

  float target = (g_rainAudio && g_sfxEnabled && g_fxLevel > 0) ? 1.0f : 0.0f;
  g_rainLevel += (target - g_rainLevel) * 0.000015f;      // ~1.5s fade at 44.1kHz
  if (g_rainLevel > 0.001f) {
    g_rainSeed = g_rainSeed * 1664525u + 1013904223u;     // cheap white noise
    float n = ((int32_t)(g_rainSeed >> 8) / 8388608.0f) - 1.0f;
    g_rainLP += (n - g_rainLP) * 0.22f;                   // lowpass into a soft hiss
    mix += g_rainLP * (RAIN_GAIN / max(SFX_GAIN[4], 0.001f)) * g_rainLevel;
    any = true;
  }
  if (!any) return;

  mix *= SFX_GAIN[constrain(g_fxLevel, 0, 4)];
  int32_t add = (int32_t)(mix * 32767.0f);
  int32_t l = (int16_t)(*sample & 0xFFFF), r = (int16_t)(*sample >> 16);

  // DUCK the music while rain is audible. Rain is continuous, not a one-shot, so its level
  // sits on top of the music for minutes at a time — and with the volume up the track already
  // peaks near full scale, leaving nothing to add it to. Pulling the music down slightly makes
  // the room for it. Proportional to the fade, so it comes and goes with the shower rather
  // than stepping.
  if (g_rainLevel > 0.001f) {
    float duck = 1.0f - 0.22f * g_rainLevel;
    l = (int32_t)(l * duck);
    r = (int32_t)(r * duck);
  }
  *sample = ((uint32_t)(uint16_t)softClip(r + add) << 16) | (uint16_t)softClip(l + add);
}

// The same mixer as audio_process_i2s above, reshaped for the AirPlay 2 port: the host owns
// I2S and calls this (via bunbun_mix_sfx) on every outgoing block — music, SD playback, or
// silence alike. That last one matters: bleeps and rain are audible in a quiet room because
// the host's playback loop never stops writing frames, it merely writes zeros between songs.
//
// Runs on the HOST's audio task: no printf, no allocation, no blocking — the discipline every
// audio-thread lesson in this project converges on. The early-out keeps the idle cost of an
// silent, effect-less frame at two comparisons.
// allowRain=false while a stream is live: rain's duck-and-softclip pass audibly compresses
// full-scale streamed music (heard as clipping). Voices — short, quiet bleeps — stay allowed
// everywhere. With rain disallowed its level decays toward silence on its own.
static void sfxMixInto(int16_t *pcm, size_t frames, bool allowRain) {
  bool anyVoice = false;
  for (int i = 0; i < SFX_VOICES; i++)
    if (g_voice[i].active) { anyVoice = true; break; }
  bool rainOn = (allowRain && g_rainAudio && g_sfxEnabled && g_fxLevel > 0);
  if (!anyVoice && !rainOn && g_rainLevel < 0.001f) return;

  for (size_t n = 0; n < frames; n++) {
    float mix = 0;
    bool any = false;
    bool voiceOn = false;   // a BEEP is sounding this sample (not just rain)
    for (int i = 0; i < SFX_VOICES; i++) {
      SfxVoice &v = g_voice[i];
      if (!v.active) continue;
      if (v.delay) { v.delay--; any = true; continue; }
      if (!v.left) { v.active = false; continue; }
      float sv = (v.wave == SFX_SQUARE) ? (v.phase < 0.5f ? 1.0f : -1.0f)
                                        : (4.0f * fabsf(v.phase - 0.5f) - 1.0f);
      mix += sv * v.amp;
      v.amp *= v.decay;
      v.phase += v.step;
      if (v.phase >= 1.0f) v.phase -= 1.0f;
      v.left--;
      any = true;
      voiceOn = true;
    }

    float target = rainOn ? 1.0f : 0.0f;
    g_rainLevel += (target - g_rainLevel) * 0.000015f;
    if (g_rainLevel > 0.001f) {
      // Hiss at quarter rate (W-012): the full-rate noise+lowpass ran three
      // float ops on EVERY sample of the host's never-stopping output — a
      // constant tax landing on the render core the moment a shower starts,
      // in exactly the quiet-room case where rain is audible. A lowpassed
      // hiss regenerated every 4th sample and held is aurally identical
      // (the 0.22 pole already buries everything above ~2kHz) at a quarter
      // of the cost.
      if ((n & 3) == 0) {
        g_rainSeed = g_rainSeed * 1664525u + 1013904223u;
        float nz = ((int32_t)(g_rainSeed >> 8) / 8388608.0f) - 1.0f;
        g_rainLP += (nz - g_rainLP) * 0.22f;
      }
      mix += g_rainLP * (RAIN_GAIN / max(SFX_GAIN[4], 0.001f)) * g_rainLevel;
      any = true;
    }
    if (!any) continue;

    mix *= SFX_GAIN[constrain(g_fxLevel, 0, 4)];
    int32_t add = (int32_t)(mix * 32767.0f);
    int32_t l = pcm[n * 2], r = pcm[n * 2 + 1];
    float duck = 1.0f;
    if (g_rainLevel > 0.001f) duck *= 1.0f - 0.22f * g_rainLevel;
    // Chirps get the same courtesy rain always had (field: ticks were
    // buried ~24dB under full-scale music — "the chirp is quiet"): while
    // any beep voice sounds, the music steps aside briefly. Beeps are
    // tens of milliseconds, so this reads as the chirp punching through,
    // not the song dipping.
    if (voiceOn) duck *= 0.45f;
    if (duck < 0.999f) {
      l = (int32_t)(l * duck);
      r = (int32_t)(r * duck);
    }
    pcm[n * 2]     = softClip(l + add);
    pcm[n * 2 + 1] = softClip(r + add);
  }
}
