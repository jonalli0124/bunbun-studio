// SD-card MP3 playback, restored for the AirPlay 2 port.
//
// The original bunbun used ESP32-audioI2S, which owns the I2S peripheral outright — the exact
// thing the host application also does, which is why SD music was cut from the port. This
// rebuild keeps the card and the decode but NOT the bus: helix (already in the tree as a host
// dependency) turns MP3 into PCM, the PCM goes into a ring, and the HOST's own playback loop
// drains the ring in the branch it previously filled with silence. Consequences, all wanted:
//
//   * SD audio plays ONLY while no AirPlay stream is delivering samples. The instant a stream
//     starts, the host's receiver branch takes over and the ring simply stops draining — the
//     precedence rule is enforced by the structure of the playback loop, sample-accurately,
//     with no arbitration code to have bugs in. When the stream ends, the track resumes from
//     where it paused.
//   * One I2S owner, one volume path, one place the beat detector taps.
//
// THE CARD HAS EXACTLY ONE OWNER: this task. Nothing else may call SD_MMC.begin()/end() or
// open files — the standalone build crashed with LoadProhibited twice from "helpful" SD access
// on other tasks, and the rule that fixed it carries over verbatim. Hot-swap works the same
// way it did there: a failed read means the card is gone, unmount and re-probe until it comes
// back, rescan, carry on. The game never blocks on any of it.
#pragma once

#include "FS.h"
#include "SD_MMC.h"
extern "C" {
#include "mp3dec.h"
}

// ---- the ring the host drains ----
// ~371ms of 44.1kHz stereo. Big enough that a slow SD read or a burst of decoding never
// audibly underruns; small enough that pausing for AirPlay leaves at most a third of a second
// of tail. Single producer (this task), single consumer (the host's playback task), lock-free
// with free-running indices.
static const size_t SD_RING_FRAMES = 16384;
static int16_t *g_sdRing = nullptr;                    // PSRAM; frames * 2 samples
static volatile uint32_t g_sdRingHead = 0, g_sdRingTail = 0;

static inline size_t sdRingAvail() { return (size_t)(g_sdRingHead - g_sdRingTail); }

// Consumer side — called from the HOST's playback task via bunbun_local_pcm(). Zero-fills the
// whole destination first, so a partly-empty ring degrades to leading silence rather than
// stale samples, then overlays whatever is available.
static size_t sdRingPull(int16_t *dst, size_t frames) {
  memset(dst, 0, frames * 2 * sizeof(int16_t));
  size_t n = sdRingAvail();
  if (n > frames) n = frames;
  for (size_t i = 0; i < n; i++) {
    size_t idx = (size_t)((g_sdRingTail + i) % SD_RING_FRAMES) * 2;
    dst[i * 2]     = g_sdRing[idx];
    dst[i * 2 + 1] = g_sdRing[idx + 1];
  }
  g_sdRingTail += n;
  return n;
}

// Producer side. Blocks (politely) while the ring is full — which is precisely what pauses SD
// playback for the whole duration of an AirPlay session, since nothing drains the ring while
// the receiver branch is feeding I2S.
static void sdRingPush(const int16_t *stereo, size_t frames) {
  size_t done = 0;
  while (done < frames) {
    size_t space = SD_RING_FRAMES - sdRingAvail();
    if (space == 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    size_t n = frames - done;
    if (n > space) n = space;
    for (size_t i = 0; i < n; i++) {
      size_t idx = (size_t)((g_sdRingHead + i) % SD_RING_FRAMES) * 2;
      g_sdRing[idx]     = stereo[(done + i) * 2];
      g_sdRing[idx + 1] = stereo[(done + i) * 2 + 1];
    }
    g_sdRingHead += n;
    done += n;
  }
}

// ---- state the UI reads ----
static volatile bool g_sdMounted = false;
static volatile bool g_sdRescan  = true;     // scan on first mount, and on demand

// ---- WiFi music mailbox (W-018) ----
// The card has exactly ONE owner: the player task. The web server collects a
// whole MP3 into PSRAM and posts it here; the write happens on this task,
// between tracks, so the single-owner invariant that keeps hot-swap sane is
// never bent. States: 0 idle, 1 queued, 2 writing, 3 done, 4 failed.
static char     g_upName[80] = {0};
static uint8_t *g_upBuf = nullptr;
static size_t   g_upLen = 0;
static volatile int g_upState = 0;
static char     g_upErr[48] = {0};
static char     g_delName[80] = {0};
static volatile bool g_delReq = false;

// ---- track scan, run ONLY on this task ----
static void sdScanTracks() {
  g_trackN = 0;
  File root = SD_MMC.open("/");
  if (!root) return;
  for (File f = root.openNextFile(); f && g_trackN < MAX_TRACKS; f = root.openNextFile()) {
    if (f.isDirectory()) { f.close(); continue; }
    String n = String(f.name());
    if (n.startsWith("/")) n = n.substring(1);
    if (n.startsWith(".") || n.startsWith("_")) { f.close(); continue; }
    String low = n; low.toLowerCase();
    if (!low.endsWith(".mp3")) { f.close(); continue; }
    strncpy(g_tracks[g_trackN], n.c_str(), sizeof(g_tracks[0]) - 1);
    g_tracks[g_trackN][sizeof(g_tracks[0]) - 1] = 0;
    g_trackN++;
    f.close();
  }
  root.close();
  Serial.printf("sd: %d track(s)\n", g_trackN);
}

// Same filename-tidying the old playTrack did, so the ticker reads as a title and not a slug.
static void sdAnnounce(int i) {
  char nice[80];
  strncpy(nice, g_tracks[i], sizeof(nice) - 1);
  nice[sizeof(nice) - 1] = 0;
  char *dot = strrchr(nice, '.');
  if (dot && dot != nice) *dot = 0;
  for (char *c = nice; *c; c++) if (*c == '-' || *c == '_') *c = ' ';
  setNowPlaying(nice);
  say(nice);
}

// ---- the player task ----
// States: unmounted -> mounted+idle -> playing. All transitions live here.
static void sdPlayerTask(void *) {
  HMP3Decoder dec = MP3InitDecoder();
  if (!dec) {
    Serial.println("sd: helix init failed - sd playback disabled");
    vTaskDelete(nullptr);
    return;
  }
  g_sdRing = (int16_t *)ps_malloc(SD_RING_FRAMES * 2 * sizeof(int16_t));
  if (!g_sdRing) { Serial.println("sd: no ring memory"); vTaskDelete(nullptr); return; }

  // Static so the task stack stays modest; only this task touches them.
  static uint8_t  inBuf[4096];
  static int16_t outBuf[1152 * 2];

  File     f;
  bool     playing = false;
  int      cur = -1;
  int      badFrames = 0;
  size_t   inLen = 0;                 // valid bytes at inBuf[0..]
  bool     announced = false;
  float    rsPos = 0.0f;              // W-042 resampler read cursor, carried across frames
  int16_t  rsPrevL = 0, rsPrevR = 0;  // last sample of the previous decoded frame
  uint32_t retryAt = 0;
  uint32_t probeDelay = 1500;   // A19: grows to 60s on an empty slot

  auto closeTrack = [&]() { if (f) f.close(); playing = false; inLen = 0; };
  auto cardGone = [&]() {
    probeDelay = 1500;   // a lost card usually means a hand is nearby — re-probe fast
    // A failed read IS the card-removal detector — there is no card-detect pin. Unmount and
    // fall back to the probe loop; the game keeps running throughout, because nothing outside
    // this task ever touches the card.
    closeTrack();
    SD_MMC.end();
    g_sdMounted = false;
    g_sdRescan = true;
    Serial.println("sd: card lost, waiting for reinsert");
    say("sd card removed");
  };

  for (;;) {
    // ---------- mount / hot-swap probe ----------
    if (!g_sdMounted) {
      uint32_t now = millis();
      if (now < retryAt) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
      // A19: back off toward a 60s ceiling instead of hammering an empty
      // slot every 1.5s forever — the driver's failed-init error lines were
      // a once-per-second log storm on slotless bench units, loud enough to
      // muddy a real investigation (W-017). A fresh insert after a LOST
      // card is still caught fast: cardGone() resets the pace.
      retryAt = now + probeDelay;
      probeDelay = min(probeDelay * 2, (uint32_t)60000);
      SD_MMC.setPins(38, 40, 39, 41, 48, 47);
      if (!SD_MMC.begin("/sdcard", true)) { SD_MMC.end(); continue; }
      probeDelay = 1500;
      g_sdMounted = true;
      Serial.println("sd: mounted");
    }

    if (g_sdRescan) {
      g_sdRescan = false;
      sdScanTracks();
      // Resume the remembered track if it survived the swap; otherwise start at the top.
      prefs.begin("bunbun", true);
      String want = prefs.getString("track", "");
      prefs.end();
      cur = -1;
      for (int i = 0; i < g_trackN; i++)
        if (want == g_tracks[i]) { cur = i; break; }
      if (cur < 0 && g_trackN > 0) cur = 0;
      g_trackSel = (cur < 0) ? 0 : cur;
      closeTrack();
    }

    // ---------- UI requests ----------
    if (g_trackReq >= 0) {
      int req = g_trackReq;
      g_trackReq = -1;
      if (req < g_trackN) { cur = req; g_trackSel = req; closeTrack(); }
    }

    // ---------- WiFi music mailbox (W-018) ----------
    // Runs on the card's only owner. The web side gates uploads behind
    // "music off", so closeTrack() here is belt-and-braces, not a stutter.
    if (g_upState == 1) {
      g_upState = 2;
      closeTrack();
      String path = "/" + String(g_upName);
      File out = SD_MMC.open(path.c_str(), FILE_WRITE);
      bool ok = false;
      if (out) {
        size_t put = 0;
        while (put < g_upLen) {
          size_t ask = g_upLen - put;
          if (ask > 16384) ask = 16384;
          size_t n = out.write(g_upBuf + put, ask);
          if (n == 0) break;
          put += n;
        }
        out.close();
        ok = (put == g_upLen);
        if (!ok) SD_MMC.remove(path.c_str());   // no half-songs on the shelf
      }
      free(g_upBuf);
      g_upBuf = nullptr;
      strlcpy(g_upErr, ok ? "" : "the card refused the bytes", sizeof(g_upErr));
      g_sdRescan = true;
      g_upState = ok ? 3 : 4;
      Serial.printf("sd: upload '%s' %s (%u bytes)\n", g_upName,
                    ok ? "saved" : "FAILED", (unsigned)g_upLen);
    }
    if (g_delReq) {
      g_delReq = false;
      closeTrack();
      String path = "/" + String(g_delName);
      SD_MMC.remove(path.c_str());
      g_sdRescan = true;
      Serial.printf("sd: deleted '%s'\n", g_delName);
    }

    // Music level 0 = off. Idle here rather than decode-and-discard.
    if (g_musicLevel == 0 || g_trackN == 0 || cur < 0) {
      closeTrack();
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    // ---------- open the current track ----------
    if (!playing) {
      String path = "/" + String(g_tracks[cur]);
      f = SD_MMC.open(path.c_str());
      if (!f) { cardGone(); continue; }
      playing = true;
      announced = false;
      badFrames = 0;
      inLen = 0;
      rsPos = 0.0f;
      rsPrevL = rsPrevR = 0;
      prefs.begin("bunbun", false);
      prefs.putString("track", g_tracks[cur]);
      prefs.end();
      Serial.printf("sd: playing '%s'\n", g_tracks[cur]);
    }

    // ---------- refill the input buffer ----------
    if (inLen < 1024) {
      int got = f.read(inBuf + inLen, sizeof(inBuf) - inLen);
      if (got < 0) { cardGone(); continue; }
      if (got == 0 && inLen == 0) {
        // End of file: next track, wrapping. This is the whole "playlist".
        closeTrack();
        cur = (cur + 1) % g_trackN;
        g_trackSel = cur;
        continue;
      }
      inLen += (size_t)got;
    }

    // ---------- decode one frame ----------
    int sync = MP3FindSyncWord(inBuf, (int)inLen);
    if (sync < 0) { inLen = 0; continue; }                 // no frame in buffer: refill
    if (sync > 0) { memmove(inBuf, inBuf + sync, inLen - sync); inLen -= (size_t)sync; }

    unsigned char *inptr = inBuf;
    int bytesLeft = (int)inLen;
    int err = MP3Decode(dec, &inptr, &bytesLeft, outBuf, 0);
    if (err == 0) {
      memmove(inBuf, inptr, (size_t)bytesLeft);
      inLen = (size_t)bytesLeft;
      badFrames = 0;

      MP3FrameInfo fi;
      MP3GetLastFrameInfo(dec, &fi);
      if (!announced) {
        announced = true;
        sdAnnounce(cur);
        if (fi.samprate != 44100)
          Serial.printf("sd: '%s' is %d Hz - resampling to 44100 (W-042)\n",
                        g_tracks[cur], fi.samprate);
      }

      int frames = fi.outputSamps / fi.nChans;
      if (fi.nChans == 1) {
        // mono: expand in place from the back so it can be pushed as stereo
        for (int i = frames - 1; i >= 0; i--) {
          outBuf[i * 2] = outBuf[i];
          outBuf[i * 2 + 1] = outBuf[i];
        }
      }
      if (fi.samprate != 44100) {
        // W-042: the host's I2S clock is pinned at 44.1kHz for AirPlay and
        // must stay there, so off-rate files used to be SKIPPED with an
        // auto-advance — which, the day a re-upload shuffled two 48kHz
        // files to the top of the card, read as "can't select the first
        // two, it plays the wrong song, sometimes it selects both" (the
        // advance moves g_trackSel while the panel is mid-draw). Now they
        // resample into the ring instead: linear interpolation, with the
        // read cursor and boundary sample carried across decode frames so
        // frame edges are seamless. (Not the tree's sinc resampler — that
        // one is wired 44.1->48 for the output path; linear is transparent
        // for lofi through this speaker.)
        static int16_t rs[2304 * 2];
        const float step = (float)fi.samprate / 44100.0f;
        int outN = 0;
        for (;;) {
          int i0 = (int)floorf(rsPos);
          if (i0 >= frames - 1 || outN >= 2304) break;
          float fr = rsPos - (float)i0;
          int16_t l0 = (i0 < 0) ? rsPrevL : outBuf[i0 * 2];
          int16_t r0 = (i0 < 0) ? rsPrevR : outBuf[i0 * 2 + 1];
          int16_t l1 = outBuf[(i0 + 1) * 2], r1 = outBuf[(i0 + 1) * 2 + 1];
          rs[outN * 2]     = (int16_t)((float)l0 + fr * (float)(l1 - l0));
          rs[outN * 2 + 1] = (int16_t)((float)r0 + fr * (float)(r1 - r0));
          outN++;
          rsPos += step;
        }
        rsPos -= (float)frames;
        if (rsPos < -1.0f) rsPos = -1.0f;   // absurd rates degrade, never index below -1
        rsPrevL = outBuf[(frames - 1) * 2];
        rsPrevR = outBuf[(frames - 1) * 2 + 1];
        if (outN > 0) sdRingPush(rs, (size_t)outN);
        continue;
      }
      sdRingPush(outBuf, (size_t)frames);      // blocks while AirPlay owns the output — the pause
    } else if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
      // keep what's left, top up on the next lap
      memmove(inBuf, inptr, (size_t)bytesLeft);
      inLen = (size_t)bytesLeft;
      if (inLen >= sizeof(inBuf) - 64) inLen = 0;          // wedged: hard resync
    } else {
      // Corrupt frame: skip a byte and resync. A run of these means the file is junk, not a
      // blip — move on rather than grinding.
      if (inLen > 1) { memmove(inBuf, inBuf + 1, inLen - 1); inLen -= 1; } else inLen = 0;
      if (++badFrames > 200) {
        say("unreadable mp3 - skipping");
        closeTrack();
        cur = (cur + 1) % g_trackN;
        g_trackSel = cur;
      }
    }
  }
}

// ---- the API the rest of bunbun already calls ----
static void sdPlayerBegin() {
  // Core 0, under the host's audio machinery in priority: decode runs in bursts and the ring
  // gives it a third of a second of slack, so it never needs to win a scheduling fight.
  // Core 1 with the audio pipeline: the decoder runs in short bursts at low priority, filling
  // the gaps audio leaves, and its ring gives it a third of a second of slack. Keeping it off
  // core 0 leaves that core to WiFi and the renderer.
  // 13312, was 16384: a seated card's FAT bookkeeping costs ~1KB of internal
  // RAM and tipped the 40KB gate floor the first night a card lived in the
  // canary. This task was sized generously at port time BEFORE its decode
  // buffers moved to statics; helix worst-case plus SD_MMC internals fit in
  // ~8KB, so 13KB keeps real margin while paying the card's rent.
  xTaskCreatePinnedToCore(sdPlayerTask, "bunbun-sd", 13312, nullptr, 3, nullptr, 1);
}

// ---- W-018: the bridge the host web server calls ----
// C linkage; all real card work still happens on the player task via the
// mailbox above. The web side owns sanitizing names and gating on music.
extern "C" {

int bunbun_music_card_present(void) { return g_sdMounted ? 1 : 0; }

int bunbun_music_upload_begin(const char *name, size_t len) {
  if (g_upState == 1 || g_upState == 2) return -1;   // one at a time
  if (!g_sdMounted) return -2;                        // no card
  if (len == 0 || len > 12u * 1024 * 1024) return -3; // size cap
  uint8_t *buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return -4;                                // PSRAM full
  strlcpy(g_upName, name, sizeof(g_upName));
  g_upBuf = buf;
  g_upLen = len;
  g_upErr[0] = 0;
  return 0;
}

unsigned char *bunbun_music_upload_buf(void) { return g_upBuf; }
void bunbun_music_upload_commit(void) { g_upState = 1; }
void bunbun_music_upload_abort(void) {
  if (g_upState != 1 && g_upState != 2) { free(g_upBuf); g_upBuf = nullptr; }
}
int bunbun_music_upload_state(void) { return g_upState; }
void bunbun_music_upload_ack(void) { if (g_upState >= 3) g_upState = 0; }
const char *bunbun_music_upload_err(void) { return g_upErr; }

int bunbun_music_delete(const char *name) {
  if (g_delReq) return -1;
  strlcpy(g_delName, name, sizeof(g_delName));
  g_delReq = true;
  return 0;
}

// {"card":true,"tracks":["a.mp3", ...]} into out; returns chars written.
int bunbun_music_list_json(char *out, size_t cap) {
  size_t n = 0;
  n += snprintf(out + n, cap - n, "{\"card\":%s,\"tracks\":[",
                g_sdMounted ? "true" : "false");
  for (int i = 0; i < g_trackN && n < cap - 4; i++) {
    n += snprintf(out + n, cap - n, "%s\"%s\"", i ? "," : "", g_tracks[i]);
  }
  n += snprintf(out + n, cap - n, "]}");
  return (int)n;
}

int bunbun_music_playing_now(void) { return (g_musicLevel > 0) ? 1 : 0; }

} // extern "C"
