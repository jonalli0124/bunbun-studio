// bunbun — the browser build's lofi engine, ported as a synth.
//
// Streaming an MP3 was the wrong shape for this hardware: 716KB of flash, a decoder
// competing with the render loop, and dense recorded audio showing up every bit of the 8-bit
// PWM's quantisation noise. Synthesised tones have none of those problems — simple waveforms
// at 8 bits sound clean, cost almost nothing, and it's the actual composition rather than a
// recording of it.
//
// Straight from bunbun_adult_test.html's scheduler:
//   16th-note sequencer with swing (off-beats laid back by spb*swing)
//   8-bar phrase: six bars of groove, then a two-bar refrain with busier drums
//   last bar ends in a snare roll that turns it back around
//   hatArc swells the hi-hats in and out across the phrase
//   a lowpass that opens from 2000Hz to 3200Hz through the refrain
#pragma once
#include <Arduino.h>

static const int SYNTH_RATE = 22050;

struct Song {
  int   bpm;
  float keys, bassv, swing;
  bool  drums;
  const float *hatArc;                 // 8 entries, or nullptr
  int8_t chords[4][4];                 // one chord per bar, voiced low to high
};
static const float HAT_DAY[8] = {0, 0.28f, 0.55f, 0.8f, 1.0f, 0.62f, 1.0f, 0.85f};

// day: Cmaj7 Am7 Dm7 G7   |   night: Fmaj7 Em7 Dm7 Cmaj7
static const Song SONG_DAY = {
    78, 0.030f, 0.055f, 0.17f, true, HAT_DAY,
    {{60, 64, 67, 71}, {57, 60, 64, 67}, {62, 65, 69, 72}, {55, 59, 62, 66}}};
static const Song SONG_NIGHT = {
    66, 0.024f, 0.042f, 0.14f, false, nullptr,
    {{65, 69, 72, 76}, {64, 67, 71, 74}, {62, 65, 69, 72}, {60, 64, 67, 71}}};

static inline float noteHz(int midi) { return 440.0f * powf(2.0f, (midi - 69) / 12.0f); }

// ---- voices ----
// Keys are a pair of slightly detuned saws (the browser's "soft detuned keys"), bass a sine,
// drums shaped noise and a pitch-swept sine.
struct Voice {
  float phase = 0, inc = 0, phase2 = 0, inc2 = 0;
  float amp = 0, decay = 0;
  uint8_t kind = 0;                    // 0 keys, 1 bass, 2 noise, 3 kick
  float sweep = 0;
  void note(float hz, float a, float dec, uint8_t k) {
    inc = hz / SYNTH_RATE; inc2 = hz * 1.006f / SYNTH_RATE;   // ~10 cent detune
    amp = a; decay = dec; kind = k; phase = 0; phase2 = 0;
    if (k == 3) sweep = hz;
  }
  float next() {
    if (amp <= 0.0005f) { amp = 0; return 0; }
    float v = 0;
    switch (kind) {
      case 0:                                        // detuned saws, soft
        v = (phase - 0.5f) + (phase2 - 0.5f) * 0.7f;
        v *= 0.5f;
        break;
      case 1: v = sinf(phase * 6.2831853f); break;   // bass sine
      case 2: v = ((int)(esp_random() & 0xFF) - 128) / 128.0f; break;   // noise
      case 3:                                        // kick: sine with a fast pitch drop
        v = sinf(phase * 6.2831853f);
        sweep *= 0.9992f;
        inc = sweep / SYNTH_RATE;
        break;
    }
    phase += inc;  if (phase >= 1) phase -= 1;
    phase2 += inc2; if (phase2 >= 1) phase2 -= 1;
    float out = v * amp;
    amp *= decay;
    return out;
  }
};

class LofiSynth {
 public:
  void begin(bool night) { setSong(night); reset(); }
  void setSong(bool night) {
    const Song *s = night ? &SONG_NIGHT : &SONG_DAY;
    if (s == song) return;
    song = s;
    samplesPer16th = (int)(60.0f / song->bpm / 4 * SYNTH_RATE);
  }
  void reset() { step = 0; acc = 0; lp = 0; for (auto &v : keys) v.amp = 0;
                 bass.amp = 0; kick_.amp = 0; snare_.amp = 0; hat.amp = 0; }

  // Produces one 8-bit unsigned sample.
  uint8_t next() {
    if (acc-- <= 0) { advance(); }
    float mix = bass.next() * 1.0f;
    for (auto &v : keys) mix += v.next();
    mix += kick_.next() + snare_.next() * 0.7f + hat.next() * 0.35f;
    // one-pole lowpass, standing in for the browser's musFilter; opens up in the refrain
    float cut = refrain ? 3200.0f : 2000.0f;
    float k = cut / (cut + SYNTH_RATE * 0.15f);
    lp += (mix - lp) * k;
    int v = (int)(lp * 90.0f) + 128;                 // headroom: voices can sum
    return (uint8_t)constrain(v, 0, 255);
  }

 private:
  void advance() {
    int st = step % 16, bar = step / 16;
    int phase = bar % 8;
    refrain = song->drums && phase >= 6;
    bool lastBar = (phase == 7);
    const int8_t *chord = song->chords[bar % 4];

    if (st == 0 || st == 8 || (refrain && st == 12)) {
      for (int i = 0; i < 4; i++)
        keys[i].note(noteHz(chord[i]), song->keys * 9.0f, 0.99985f, 0);
    }
    if (st == 0)            bass.note(noteHz(chord[0] - 24), song->bassv * 11.0f, 0.99990f, 1);
    else if (st == 10)      bass.note(noteHz(chord[0] - 24 + 7), song->bassv * 9.0f, 0.99988f, 1);
    else if (refrain && st == 6) bass.note(noteHz(chord[0] - 24 + 5), song->bassv * 8.0f, 0.99988f, 1);

    if (song->drums) {
      float lvl = song->hatArc ? song->hatArc[phase] : 1.0f;
      bool sparse = lvl < 0.3f;
      if (refrain) {
        if (st == 0 || st == 6 || st == 10 || st == 14) kick_.note(110, 0.55f, 0.9995f, 3);
        if (st == 4 || st == 12) snare_.note(0, 0.42f, 0.9975f, 2);
        if (st == 7 || st == 11) snare_.note(0, 0.14f, 0.9970f, 2);   // ghost notes
      } else {
        if (st == 0 || st == 10) kick_.note(110, 0.55f, 0.9995f, 3);
        if (sparse && (st == 6 || st == 13)) kick_.note(110, 0.45f, 0.9995f, 3);
        if (st == 4 || st == 12) snare_.note(0, 0.42f, 0.9975f, 2);
        if (sparse && st == 14) snare_.note(0, 0.16f, 0.9970f, 2);
      }
      if (lastBar && st >= 12) snare_.note(0, 0.18f + (st - 12) * 0.08f, 0.9972f, 2);  // roll
      if (lvl > 0.05f && (st % 2 == 0) && !(lastBar && st >= 12))
        hat.note(0, 0.10f * lvl, 0.9955f, 2);
    }

    // swing: lay the off-beats back, exactly as the browser does
    acc = samplesPer16th + ((st % 2) ? (int)(samplesPer16th * song->swing) : 0);
    step++;
  }

  const Song *song = nullptr;
  Voice keys[4], bass, kick_, snare_, hat;
  int step = 0, acc = 0, samplesPer16th = 0;
  bool refrain = false;
  float lp = 0;
};
