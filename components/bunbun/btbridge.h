#pragma once
// ---- W-021: the Bluetooth bridge's LISTENING side ----
//
// The D1 Mini (ESP-WROOM-32) does everything the S3 cannot: pairs as a
// Bluetooth Classic A2DP speaker named "bunbun", decodes SBC, and clocks
// the PCM out of three wires as I2S MASTER:
//
//     D1 IO26 (BCK)  -> IO14      D1 IO25 (WS) -> IO3      D1 IO22 -> IO2
//
// This side listens on I2S1 as SLAVE — deliberately. The D1 owns the bit
// clock, so capture is drift-free by construction and the ONLY place the
// two crystals disagree is the ring below: the D1 fills it at its 44.1kHz,
// the host's output drains it at the S3's 44.1kHz, and the tens-of-ppm
// difference becomes a slow fill creep handled by dropping or repeating a
// single frame at the watermarks. Through this speaker playing lofi, a
// one-frame nip a few times a second is inaudible — the same trick every
// cheap BT speaker ships with (assessment co-signed 8/8; zero-crossing
// deferral is the noted upgrade if a better speaker ever hears a zipper).
//
// Interface kept IDENTICAL to the retired analog capture (btaudio.h):
// g_btActive + btRingAvail() + btRingPull(), because bunbun_local_pcm()
// already routes BT-before-SD on exactly those names. AirPlay precedence
// costs nothing here either — the host only calls bunbun_local_pcm() in
// its silence branch, so a live stream simply stops draining us.
//
// No NVS declare needed: with nothing wired, a slave port sees no BCK and
// every read times out. Silence is the honest default.

#include "driver/i2s_std.h"

static const size_t BT_RING_FRAMES = 16384;       // ~371ms at 44.1k, like the SD ring
static int16_t *g_btRing = nullptr;               // PSRAM
static volatile uint32_t g_btHead = 0, g_btTail = 0;
static volatile bool g_btActive = false;          // frames arriving right now
static volatile uint32_t g_btLastRxMs = 0;
static volatile uint32_t g_btDrops = 0, g_btDups = 0;   // drift ledger, for the trace
// Chop forensics (added when ground/wires/buffers were all cleared and the
// chop survived): gapRuns counts stretches of >=128 near-zero frames INSIDE
// an active stream — silence gaps that ARRIVED on the wire. clicks counts
// sample-to-sample jumps too violent for music — the sound of bit-slip or
// corruption. gaps high = sender/wire starves; clicks high = receiver
// misreading; both ~0 while ears still hear chop = the fault is downstream
// of this ring entirely.
static volatile uint32_t g_btGapRuns = 0, g_btClicks = 0;
static i2s_chan_handle_t g_btRx = nullptr;

static inline size_t btRingAvail() { return (size_t)(g_btHead - g_btTail); }

// Consumer side — the host's playback task, via bunbun_local_pcm(). The
// drift handler lives here, at the drain: one frame nipped or repeated per
// call, only past the watermarks, so correction can never gallop.
static size_t btRingPull(int16_t *dst, size_t frames) {
  size_t avail = btRingAvail();
  // High watermark: the D1's crystal is a whisker fast — skip ONE frame.
  if (avail > BT_RING_FRAMES * 3 / 4) { g_btTail++; g_btDrops++; avail--; }
  size_t n = avail < frames ? avail : frames;
  for (size_t i = 0; i < n; i++) {
    size_t idx = (size_t)((g_btTail + i) % BT_RING_FRAMES) * 2;
    dst[i * 2]     = g_btRing[idx];
    dst[i * 2 + 1] = g_btRing[idx + 1];
  }
  g_btTail += n;
  // Low watermark: the D1 is a whisker slow — repeat the last frame rather
  // than hand the mixer a click of silence.
  if (n < frames) {
    g_btDups += frames - n;
    int16_t l = n ? dst[(n - 1) * 2] : 0, r = n ? dst[(n - 1) * 2 + 1] : 0;
    for (size_t i = n; i < frames; i++) { dst[i * 2] = l; dst[i * 2 + 1] = r; }
  }
  return frames;
}

// Producer: blocks on i2s_channel_read, which in slave mode simply waits
// for the D1's clock. No clock, no data, 200ms timeout — and that timeout
// IS the source-gone detector (the RX-timeout fallback from the 8/8
// review): half a second without frames and the SD player inherits the
// silence branch again.
static void btBridgeTask(void *) {
  static int16_t chunk[512 * 2];
  for (;;) {
    size_t got = 0;
    esp_err_t err = i2s_channel_read(g_btRx, chunk, sizeof(chunk), &got,
                                     pdMS_TO_TICKS(200));
    uint32_t now = millis();
    if (err == ESP_OK && got > 0) {
      size_t frames = got / 4;
      static int zeroRun = 0;
      static int16_t lastL = 0;
      for (size_t i = 0; i < frames; i++) {
        int16_t L = chunk[i * 2], R = chunk[i * 2 + 1];
        if (g_btActive) {
          if (L > -50 && L < 50 && R > -50 && R < 50) {
            if (++zeroRun == 128) g_btGapRuns++;
          } else {
            zeroRun = 0;
            int d = (int)L - (int)lastL;
            if (d > 16000 || d < -16000) g_btClicks++;
          }
          lastL = L;
        }
        // Depth cap, PRE-ENGAGE ONLY. Its one job is stopping the ring
        // filling to the brim during the host's 2s silence hysteresis
        // (pinned-full + the trimmer = the original chop). But shedding
        // while the stream is LIVE is a guaranteed pop every time a BT
        // burst kisses the cap — which the stats showed (fill parked at
        // exactly 8192) and the ears confirmed ("little pops"). Once
        // engaged, the drain holds the level and the fill must wander
        // freely; only the 3/4 watermark trims, at crystal pace.
        if (!g_btActive && btRingAvail() >= 8192) continue;
        // THE CAPACITY INVARIANT. The forensics build caught fill=310,697
        // in a 16,384-frame ring: between engage and the host's first
        // drain, the writer LAPPED the reader ~19x and then permanently
        // overwrote the very frames being read — every second of output a
        // collage of two moments of the song. That was the chop, from the
        // first night to this one. If the ring is truly full, jump the
        // playhead forward to keep the newest half — one audible skip,
        // once, instead of corruption forever.
        if ((uint32_t)(g_btHead - g_btTail) >= BT_RING_FRAMES) {
          g_btTail = g_btHead - BT_RING_FRAMES / 2;
        }
        size_t idx = (size_t)(g_btHead % BT_RING_FRAMES) * 2;
        g_btRing[idx]     = chunk[i * 2];
        g_btRing[idx + 1] = chunk[i * 2 + 1];
        g_btHead++;
      }
      g_btLastRxMs = now;
      // Engage only once ~100ms is banked, so the first note doesn't
      // stutter while the link jitters its way up to speed.
      // Engage at 150ms banked — MUST sit below the producer's 8192 depth
      // cap, which the first cut of the cap forgot: engage was left at
      // 11025, a line the capped ring could never cross, and "playing but
      // no sound" was the whole build (2026-08-09, late). The cap and this
      // threshold are a pair; move them together or not at all.
      if (!g_btActive && btRingAvail() > 6615) {
        g_btActive = true;
        Serial.println("btbridge: stream STARTED (frames arriving on I2S1)");
      }
    }
    // Living stats while streaming, every 5s: ring fill says whether the
    // drift handler is loafing or fighting; drops/dups say which way the
    // crystals lean. This is the instrument the choppiness was diagnosed
    // without, added so the next tuning pass has numbers.
    static uint32_t statAt = 0;
    if (g_btActive && now - statAt > 5000) {
      statAt = now;
      Serial.printf("btbridge: fill=%u drops=%lu dups=%lu gaps=%lu clicks=%lu\n",
                    (unsigned)btRingAvail(), (unsigned long)g_btDrops,
                    (unsigned long)g_btDups, (unsigned long)g_btGapRuns,
                    (unsigned long)g_btClicks);
    }
    if (g_btActive && now - g_btLastRxMs > 500) {
      g_btActive = false;
      g_btHead = g_btTail = 0;            // stale tail must not replay later
      Serial.printf("btbridge: stream ended (drops=%lu dups=%lu)\n",
                    (unsigned long)g_btDrops, (unsigned long)g_btDups);
    }
  }
}

static void btBridgeBegin() {
  g_btRing = (int16_t *)ps_malloc(BT_RING_FRAMES * 2 * sizeof(int16_t));
  if (!g_btRing) { Serial.println("btbridge: no ring memory - disabled"); return; }

  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
  // DMA sizing is a BUDGET question, not a comfort question: DMA buffers
  // live in internal RAM, and 8x1023 frames (~32KB) crash-looped a board
  // that had ~14KB spare (first tuning pass — the cure was worse than the
  // chop). ~46ms (4x511, ~8KB) plus the task priority below is the actual
  // fix: the producer only needs to OUTLIVE one long render draw (~17ms)
  // between drains, because the 371ms PSRAM ring absorbs everything else.
  cc.dma_desc_num = 4;
  cc.dma_frame_num = 511;
  if (i2s_new_channel(&cc, nullptr, &g_btRx) != ESP_OK) {
    Serial.println("btbridge: no I2S1 channel - disabled");
    return;
  }
  i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),   // nominal; the D1's clock rules
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = GPIO_NUM_14,
          .ws   = GPIO_NUM_3,
          .dout = I2S_GPIO_UNUSED,
          .din  = GPIO_NUM_2,
          .invert_flags = {false, false, false},
      },
  };
  if (i2s_channel_init_std_mode(g_btRx, &sc) != ESP_OK ||
      i2s_channel_enable(g_btRx) != ESP_OK) {
    Serial.println("btbridge: I2S1 init failed - disabled");
    return;
  }
  // Core 1, but ABOVE the renderer (prio 10 vs the game task): this is a
  // real-time producer, and the chop fix is preempting a long draw to drain
  // DMA promptly — not buying enormous DMA. The task runs ~4% duty; the
  // renderer barely notices.
  xTaskCreatePinnedToCore(btBridgeTask, "bunbun-bt", 4096, nullptr, 10, nullptr, 1);
  Serial.println("btbridge: listening on I2S1 slave (BCK 14, WS 3, DATA 2)");
}
