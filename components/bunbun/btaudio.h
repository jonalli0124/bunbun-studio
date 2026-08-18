#pragma once
// ---- W-021: Bluetooth module audio in (the lofi ADC path) ----
//
// The Aideepen BT5.0 module's line-out arrives on IO2 (ADC1_CH1) through a
// bias network in the cable (L+R summed via 1K each, 1uF coupling, 100K/100K
// divider centring the node at ~1.65V). Whatever a phone streams to the
// module, bunbun digitises and plays through his own speaker — Android and
// everything else can finally stream (the wish's own words).
//
// LIFECYCLE IS THE WHOLE DESIGN. The ADC-DMA driver wants ~20KB of internal
// RAM for its pool, and the steady-state floor (40KB, gate-enforced) cannot
// pay rent for a feature that is idle almost always. So capture is a
// VISITOR, exactly like SNTP (W-019) and the USB mode (W-020):
//
//   probe (cheap, oneshot, no task)  ->  signal?  ->  engage (task + DMA)
//        ^                                                 |
//        +----------- 5s of line silence: tear down -------+
//
// The probe runs from loop() a few times a second, ONLY while eligible:
// SD music off (level 0) and no stream in the last 2s — the same precedence
// the silence branch already enforces. Eight quick oneshot reads; if the
// line wiggles more than noise, the visitor moves in. On teardown every
// internal-RAM byte goes back. The PSRAM ring (88KB) is allocated once on
// first engage and kept — PSRAM is plentiful and refree'ing fragments it.
//
// Precedence, spelled out: AirPlay beats everything (structural — this only
// feeds the silence branch). SD music beats BT (level > 0 disables the
// probe; no arbitration code, no surprises). BT volume rides the phone's
// slider plus the DAC's level-0 floor (-12dB, W-040).

// Driver politics (learned the hard way, two gates in a row): ADC1 belongs
// to the ARDUINO CORE — the battery divider reads it via analogRead(), and
// the core's oneshot unit is created lazily and kept. A second claim on
// ADC1 (esp_adc oneshot OR continuous) either loses ("adc1 is already in
// use", this build's probe spam) or wins and crash-loops the battery path
// (the previous build's uptime-went-backwards). ADC2 is no refuge: it
// shares arbitration with WiFi on the S3. So bunbun goes THROUGH Arduino
// instead of around it: analogRead() for probe and capture both, sharing
// the core's own arbitration. That caps capture around 8kHz — telephone
// quality — which is exactly what "the lofi path" was always specced to
// be; the ES8311 mic-pad wiring remains the hi-fi upgrade for later.

static const int BT_ADC_PIN = 2;                // IO2 on the Expanded IO port
#define BT_SR 8000                              // capture rate, Hz
#define BT_RING_FRAMES (BT_SR / 2)              // half a second, mono
static int16_t *g_btRing = nullptr;             // PSRAM, mono at BT_SR
static volatile uint32_t g_btHead = 0, g_btTail = 0;
static volatile bool g_btActive = false;        // capture task alive
static volatile bool g_btStop = false;          // teardown requested

static inline size_t btRingAvail() { return (size_t)(g_btHead - g_btTail); }

// Pull with linear interpolation from BT_SR mono up to the output's 44.1k
// stereo. Phase carries across calls so track pitch stays true.
static size_t btRingPull(int16_t *dst, size_t frames) {
  memset(dst, 0, frames * 2 * sizeof(int16_t));
  static float phase = 0.0f;
  const float step = (float)BT_SR / 44100.0f;
  size_t out = 0;
  while (out < frames && btRingAvail() >= 2) {
    size_t i0 = (size_t)(g_btTail % BT_RING_FRAMES);
    size_t i1 = (size_t)((g_btTail + 1) % BT_RING_FRAMES);
    float a = (float)g_btRing[i0], b = (float)g_btRing[i1];
    int16_t s = (int16_t)(a + (b - a) * phase);
    dst[out * 2] = s;
    dst[out * 2 + 1] = s;
    out++;
    phase += step;
    while (phase >= 1.0f) { phase -= 1.0f; g_btTail++; }
  }
  return out;
}

// The capture visitor: a task pacing analogRead on a BT_SR grid for exactly
// as long as the line carries signal. All CPU cost is transient; the
// steady-state floor pays nothing.
static void btCaptureTask(void *) {
  float bias = 2048.0f;
  uint32_t quietMs = 0, lastMs = millis();
  const uint32_t budgetUs = 1000000UL / BT_SR;
  uint32_t nextUs = micros();
  int peak = 0, peakN = 0;
  while (!g_btStop) {
    int v = analogRead(BT_ADC_PIN);
    // Track the true DC centre (divider tolerance moves it); the -315
    // clock taught what trusting a nominal value costs.
    bias += (v - bias) * 0.002f;
    int s = (int)((v - bias) * 14.0f);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    if (btRingAvail() < BT_RING_FRAMES) {
      g_btRing[g_btHead % BT_RING_FRAMES] = (int16_t)s;
      g_btHead++;
    }
    int a = s < 0 ? -s : s;
    if (a > peak) peak = a;
    if (++peakN >= BT_SR / 10) {                // judge silence 10x a second
      uint32_t nowMs = millis();
      if (peak > 900) quietMs = 0;
      else quietMs += nowMs - lastMs;
      lastMs = nowMs;
      peak = 0; peakN = 0;
      if (quietMs > 5000) break;                // 5s of line silence: leave
    }
    nextUs += budgetUs;
    int32_t wait = (int32_t)(nextUs - micros());
    if (wait > 0) {
      if (wait > 1500) vTaskDelay(1);           // long gap: yield properly
      else delayMicroseconds(wait);
    } else if (wait < -(int32_t)(budgetUs * 8)) {
      nextUs = micros();                        // fell far behind: re-anchor
    }
  }
  g_btActive = false;
  vTaskDelete(NULL);
}

// The cheap presence probe: eight analogReads through Arduino's own ADC
// ownership (no second driver claim, no fight with the battery reads).
// Real audio swings tens of counts inside a millisecond; a quiet biased
// node moves low single digits.
static bool btLineWiggling() {
  int lo = 4096, hi = 0;
  for (int i = 0; i < 8; i++) {
    int v = analogRead(BT_ADC_PIN);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    delayMicroseconds(120);
  }
  return (hi - lo) > 120;
}

// Called from loop() a few times a second. Handles the whole lifecycle.
static void btAudioTick() {
  static uint32_t nextProbe = 0;
  if (g_btActive || millis() < nextProbe) return;
  nextProbe = millis() + 300;
  // Eligibility mirrors the silence branch: SD music off, nothing audible
  // for 2s (audioLive is amplitude-based; a stream's quiet passage may
  // slip through, which is harmless — the ring just fills unheard and the
  // visitor leaves after 5s of line silence).
  if (g_musicLevel > 0) return;
  if (audioLive()) return;
  if (!btLineWiggling()) return;
  // Signal on a quiet unit: the visitor moves in. (The probe's oneshot
  // unit already vacated ADC1 — it lives only inside the call.)
  if (!g_btRing) {
    g_btRing = (int16_t *)heap_caps_malloc(BT_RING_FRAMES * sizeof(int16_t),
                                           MALLOC_CAP_SPIRAM);
    if (!g_btRing) return;
  }
  g_btHead = g_btTail = 0;
  g_btStop = false;
  g_btActive = true;
  Serial.println("bt: line signal - capture engaged");
  if (xTaskCreatePinnedToCore(btCaptureTask, "bunbun-bt", 4096, nullptr, 5,
                              nullptr, 0) != pdPASS) {
    g_btActive = false;
  }
}
