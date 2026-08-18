// Wish recorder: the kids' feature-request microphone.
//
// Hold-to-wish on the bunbun screen starts a ~15s capture from the ES8311's
// onboard mic. The whole clip is captured into PSRAM first and written to
// SPIFFS only after the mic closes — SPIFFS writes are flash writes, and this
// project has already learned (the hard way, twice) that flash writes freeze
// both cores' caches; doing them WHILE also draining the mic's DMA ring would
// drop audio mid-sentence. One burst of writing at the end costs a brief
// visual hitch when nobody is performing.
//
// Format: 11025 Hz mono 16-bit WAV — a 4:1 averaged decimation of the codec's
// 44.1kHz stereo stream. Speech-band quality, ~330KB per 15s clip, and the
// nightly Whisper transcriber upsamples happily. Clips live at
// /spiffs/wishes/w<millis>.wav until the nightly collector pulls and deletes
// them; at most WISH_MAX_CLIPS are kept so the storage partition can't fill.

#include "wish_recorder.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_output.h"
#include "dac.h"
#include "spiffs_storage.h"
#include "wish_uploader.h"

static const char *TAG = "wish";

#define WISH_DIR "/spiffs/wishes"
#define WISH_SECONDS 15
#define WISH_SRC_RATE 44100
#define WISH_DECIM 4
#define WISH_RATE (WISH_SRC_RATE / WISH_DECIM) // 11025
#define WISH_SAMPLES (WISH_RATE * WISH_SECONDS)
#define WISH_MAX_CLIPS 6

static volatile bool s_recording = false;
static volatile bool s_stop_req = false;
static volatile bool s_saving = false;
static volatile int s_seconds_done = 0;
static volatile uint32_t s_last_end_tick = 0;   // tick a capture last finished (cooldown)

// Self-hearing probe telemetry (8/12): per attempt, what the mic heard of the
// unit's own 990Hz probe beep + the floor/flips that decided the attempt.
// Read over HTTP (debug/audio) so thresholds get set from MEASURED data.
#define PROBE_MAX_TRIES 6
static volatile unsigned s_probe_tries = 0;
static unsigned s_probe_chirp[PROBE_MAX_TRIES];
static unsigned s_probe_floor[PROBE_MAX_TRIES];
static unsigned s_probe_flips[PROBE_MAX_TRIES];

int wish_recorder_probe_stats(char *out, size_t cap) {
  int n = 0;
  for (unsigned i = 0; i < s_probe_tries && i < PROBE_MAX_TRIES && (size_t)n < cap - 24; i++) {
    n += snprintf(out + n, cap - n, "%st%u:chirp=%u,floor=%u,flips=%u", i ? " " : "",
                  i, s_probe_chirp[i], s_probe_floor[i], s_probe_flips[i]);
  }
  return n;
}
static volatile wish_fail_t s_fail = WISH_FAIL_NONE;

bool wish_recorder_active(void) { return s_recording; }
// s_stop_req counts as saving from the caller's point of view: the read loop
// honors it within one ~23ms chunk, so the distinction is invisible, and the
// press that set it deserves an immediate state change (W-008).
bool wish_recorder_saving(void) { return s_recording && (s_saving || s_stop_req); }
int wish_recorder_seconds(void) { return s_seconds_done; }
void wish_recorder_stop(void) { s_stop_req = true; }
wish_fail_t wish_recorder_fail_reason(void) { return s_fail; }

static void write_wav_header(FILE *f, uint32_t data_bytes) {
  uint32_t rate = WISH_RATE, byte_rate = WISH_RATE * 2, chunk = 36 + data_bytes;
  uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
  fwrite("RIFF", 1, 4, f);
  fwrite(&chunk, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  uint32_t fmtlen = 16;
  fwrite(&fmtlen, 4, 1, f);
  fwrite(&fmt, 2, 1, f);
  fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f);
  fwrite(&byte_rate, 4, 1, f);
  fwrite(&align, 2, 1, f);
  fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f);
  fwrite(&data_bytes, 4, 1, f);
}

// Keep the wish shelf bounded: if a new clip would exceed the cap, the OLDEST
// goes. Kids mash buttons; the collector usually empties this nightly anyway.
static void prune_clips(void) {
  DIR *d = opendir(WISH_DIR);
  if (!d) {
    return;
  }
  int n = 0;
  char oldest[64] = {0};
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    n++;
    if (!oldest[0] || strcmp(e->d_name, oldest) < 0) {
      strlcpy(oldest, e->d_name, sizeof(oldest));
    }
  }
  closedir(d);
  if (n >= WISH_MAX_CLIPS && oldest[0]) {
    char path[96];
    snprintf(path, sizeof(path), WISH_DIR "/%s", oldest);
    unlink(path);
    ESP_LOGW(TAG, "Wish shelf full - dropped oldest clip %s", oldest);
  }
}

static void wish_task(void *arg) {
  void (*done_cb)(bool ok) = (void (*)(bool))arg;
  i2s_chan_handle_t rx = audio_output_rx_acquire();
  bool ok = false;
  int16_t *clip = NULL;
  int16_t *chunk = NULL;

  if (!rx) {
    ESP_LOGE(TAG, "Mic channel unavailable");
    s_fail = WISH_FAIL_MIC;
    goto out;
  }

  // THE SEQUENTIAL-DEGRADATION FIX (Jon 8/12: "it is always sequential, it
  // gets worse"). Each capture inherited the codec clock state the previous
  // one left behind, so consecutive recordings drifted good -> static; only a
  // power cycle reset it. Force the codec's clock config back to known-good
  // before EVERY capture — the register-level equivalent of that power cycle.
  // dac_on_i2s_started() rewrites the ES8311 clock dividers from scratch (it
  // is the same hook the driver trusts after TX clock changes); the short
  // settle lets the ADC modulator relock before frames are kept, and the
  // 0.5s warm-up discard below drops anything from before the relock.
  // FULL codec soft-reset before every capture (8/12, the last piece): the
  // W-068 lab showed the analog input stage can wedge into full-scale hiss
  // while every register reads back healthy — so no register rewrite fixes
  // it and no register dump shows it. Only REG00's soft-reset sequence
  // clears that state. This is the true register-level power cycle.
  {
    extern int dac_es8311_capture_reset(void);
    if (dac_es8311_capture_reset() != 0) {
      dac_on_i2s_started();      // codec not up yet? at least reset clocks
    }
    vTaskDelay(pdMS_TO_TICKS(60));
  }

  clip = heap_caps_malloc(WISH_SAMPLES * sizeof(int16_t),
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  // 256 stereo frames per read, INTERNAL (Jon 8/12). Reverted from a 4KB PSRAM
  // buffer: a DMA->PSRAM copy can hand back cache-stale garbage that reads as
  // constant static, which is the leading suspect for the good/quiet/STATIC
  // clips. At 256 frames it's a 1KB internal alloc — small enough to fit a
  // fragmented unit now that the DMA is opened first (alloc reorder), and it
  // matches the DMA descriptor size so each read drains exactly one buffer.
  chunk = heap_caps_malloc(256 * 2 * sizeof(int16_t), MALLOC_CAP_DEFAULT);
  if (!clip || !chunk) {
    ESP_LOGE(TAG, "Out of memory for clip buffer");
    s_fail = WISH_FAIL_MEMORY;
    goto out;
  }

  // acquire() returned the channel already enabled

  // PROBE -> VALIDATE -> RETRY (Jon 8/12: "this is a key feature and needs to
  // always work"). The register dumps proved the ES8311 never drifts — every
  // degraded clip had a bit-identical codec config to every clean one. What
  // varies is HOW THE RX CHANNEL STARTS: a capture either locks onto real
  // audio or onto garbage in its first frames, and stays that way for the
  // whole clip (every bad clip is bad from sample 0). So: listen to a quarter
  // second FIRST, judge it by its sign-flip rate (static flips ~50% of
  // samples; audio, even loud music, stays well under 30%), and pick the
  // stereo slot that carries the cleaner signal — a misaligned start can land
  // the mic in the "right" slot. If BOTH slots read as static, tear the
  // channel down, reset the codec clock, and start over — up to 4 attempts.
  // A kid holding the button never notices a 0.4s retry; what they notice is
  // a wish that came out as noise.
  int slot = 0;                     // which stereo slot carries the mic
  bool aligned = false;
  s_probe_tries = 0;
  for (int attempt = 0; attempt < PROBE_MAX_TRIES && !aligned && !s_stop_req; attempt++) {
    if (attempt > 0) {              // re-make channel AND codec from scratch
      audio_output_rx_release();
      vTaskDelay(pdMS_TO_TICKS(80));
      rx = audio_output_rx_acquire();
      if (!rx) break;
      extern int dac_es8311_capture_reset(void);
      if (dac_es8311_capture_reset() != 0) {
        dac_on_i2s_started();
      }
      vTaskDelay(pdMS_TO_TICKS(60));
    }
    // drain ~0.2s of lead-in, then probe ~0.25s. Per 256-frame block (~6ms)
    // we also track each slot's QUIETEST block: a clean capture always has
    // near-silent gaps (room tone, inter-syllable dips), while a BIT-SHIFTED
    // capture never gets quiet — the corrupted low bits keep the floor high.
    // That floor is the tell that catches the "louder and worse" shifted
    // starts (Jon 8/12 late) that level/flip averages let through.
    uint32_t lvl[2] = {0, 0}, flips[2] = {0, 0}, n = 0;
    uint32_t minBlk[2] = {0xFFFFFFFF, 0xFFFFFFFF};
    uint32_t maxBlk[2] = {0, 0};      // loudest ~6ms block = the probe beep
    int16_t prev[2] = {0, 0};
    // The self-hearing beep: fired as the lead-in ends, so the mic should
    // hear the unit's OWN 990Hz tone inside the probe window at a known-ish
    // level. A quiet-slipped capture hears it tens of times too soft; a
    // garbled one hears mush. MEASURE-ONLY this build — thresholds come
    // after real numbers are collected over HTTP.
    extern int bunbun_probe_beep(void);
    int beepFired = 0;
    for (int blk = 0; blk < 64 && !s_stop_req; blk++) {   // 64 x 256 frames
      size_t got = 0;
      if (i2s_channel_read(rx, chunk, 256 * 2 * sizeof(int16_t), &got,
                           pdMS_TO_TICKS(500)) != ESP_OK) {
        break;
      }
      if (blk == 16) beepFired = bunbun_probe_beep();   // ~93ms into the drain
      if (blk < 18) continue;               // lead-in, discard
      size_t frames = got / (2 * sizeof(int16_t));
      if (!frames) continue;
      uint32_t blkLvl[2] = {0, 0};
      for (size_t i = 0; i < frames; i++) {
        for (int s = 0; s < 2; s++) {
          int16_t v = chunk[i * 2 + s];
          uint32_t av = (v < 0) ? -v : v;
          lvl[s] += av;
          blkLvl[s] += av;
          if ((v >= 0) != (prev[s] >= 0)) flips[s]++;
          prev[s] = v;
        }
        n++;
      }
      for (int s = 0; s < 2; s++) {
        blkLvl[s] /= frames;
        if (blkLvl[s] < minBlk[s]) minBlk[s] = blkLvl[s];
        if (blkLvl[s] > maxBlk[s]) maxBlk[s] = blkLvl[s];
      }
    }
    if (!n) continue;
    unsigned f0 = (unsigned)(flips[0] * 100 / n), f1 = (unsigned)(flips[1] * 100 / n);
    ESP_LOGI(TAG,
             "probe try %d: slot0 lvl=%u flips=%u%% floor=%u beep=%u | slot1 lvl=%u flips=%u%% floor=%u beep=%u",
             attempt, (unsigned)(lvl[0] / n), f0, (unsigned)minBlk[0],
             (unsigned)maxBlk[0], (unsigned)(lvl[1] / n), f1,
             (unsigned)minBlk[1], (unsigned)maxBlk[1]);
    // prefer the slot with the quieter floor; break ties by flip rate
    slot = (minBlk[1] < minBlk[0]) ? 1 : 0;
    // telemetry for threshold calibration (read via debug/audio)
    if (s_probe_tries < PROBE_MAX_TRIES) {
      s_probe_chirp[s_probe_tries] = (unsigned)maxBlk[slot];
      s_probe_floor[s_probe_tries] = (unsigned)minBlk[slot];
      s_probe_flips[s_probe_tries] = (slot == 1) ? f1 : f0;
      s_probe_tries++;
    }
    unsigned best = (slot == 1) ? f1 : f0;
    unsigned bestLvl = (unsigned)(lvl[slot] / n);
    unsigned bestFloor = (unsigned)minBlk[slot];
    // Four garbage signatures (all field-caught + CALIBRATED 8/12):
    //  - classic static: flips ~50% (threshold 25; measured static = 47-50).
    //  - the WALL: constant huge mean level (~9-12k). Cap 6500.
    //  - the LOUD bit-shift: floor never dips. Clean floors measured 78-345;
    //    garbage floors 7828-14976. Threshold 700 splits them by 10x each way.
    //  - the QUIET bit-shift: everything scaled down ("almost silent" clip,
    //    peak 90 vs 4935 same voice). Caught by SELF-HEARING: the unit beeps
    //    during the probe and must hear its own beep. Clean captures measured
    //    the beep at 261-1368; a /32 slip reads ~10-40. Threshold 150 — only
    //    enforced when the beep actually played (sound on), so silent-mode
    //    units can never stick their own ears.
    unsigned bestBeep = (unsigned)maxBlk[slot];
    bool beepHeard = !beepFired || bestBeep >= 150;
    if (best < 25 && bestLvl < 6500 && bestFloor <= 700 && beepHeard) {
      aligned = true;
    } else {
      ESP_LOGW(TAG,
               "probe try %d: garbage signature (flips=%u%% lvl=%u floor=%u beep=%u) - reinit",
               attempt, best, bestLvl, bestFloor, bestBeep);
    }
  }
  if (!aligned) {
    // Four fresh channels all read static: give an honest failure instead of
    // shipping 15s of noise as somebody's wish.
    ESP_LOGE(TAG, "mic never aligned - refusing to record noise");
    audio_output_rx_release();   // don't leave a bad channel for the next try
    s_fail = WISH_FAIL_MIC;
    goto out;
  }

  size_t written = 0; // mono samples accumulated
  int acc = 0, accN = 0;
  while (written < WISH_SAMPLES && !s_stop_req) {
    size_t got = 0;
    if (i2s_channel_read(rx, chunk, 256 * 2 * sizeof(int16_t), &got,
                         pdMS_TO_TICKS(500)) != ESP_OK) {
      break;
    }
    size_t frames = got / (2 * sizeof(int16_t));
    for (size_t i = 0; i < frames && written < WISH_SAMPLES; i++) {
      acc += chunk[i * 2 + slot]; // the slot the probe validated
      if (++accN == WISH_DECIM) {
        clip[written++] = (int16_t)(acc / WISH_DECIM);
        acc = 0;
        accN = 0;
      }
    }
    s_seconds_done = (int)(written / WISH_RATE);
  }
  // Register file at the END of the capture, before teardown — the diff
  // against REGS@start shows what the capture itself perturbed.
  {
    extern int dac_es8311_reg_dump(char *out, size_t cap);
    char regs[192];
    dac_es8311_reg_dump(regs, sizeof(regs));
    ESP_LOGI(TAG, "REGS@end   %s", regs);
  }
  s_saving = true; // mic is done; everything past here is the save burst
  audio_output_rx_release(); // give the DMA's internal RAM back immediately

  if (written < WISH_RATE / 2) { // under half a second: nothing worth keeping
    ESP_LOGW(TAG, "Recording too short (%u samples), discarded",
             (unsigned)written);
    s_fail = WISH_FAIL_TOO_SHORT;
    goto out;
  }

  // Mic closed — now the flash burst.
  mkdir(WISH_DIR, 0755); // no-op if it exists; SPIFFS treats paths as flat
  prune_clips();
  uint32_t bytes = written * sizeof(int16_t);
  // SPIFFS reclaims deleted pages lazily; after a day of record/upload/delete
  // churn a 330KB write comes back short even though logical free space is
  // fine (field, 2026-08-05: full 15s wishes reduced to 128-byte husks, and
  // the debt survives reboots). Reclaim BEFORE the write, not during.
  //
  // But ONLY when space is actually tight (launch eve, 2026-08-06): on a
  // near-empty partition with nothing reclaimable, esp_spiffs_gc degrades
  // into a full-partition scan that took 25s+ per save — five gate runs
  // failed "no verdict" chasing phantom music before the stall was traced
  // HERE. Plenty of logical headroom = the lazy-debt failure mode cannot
  // apply = skip straight to the write; the in-loop rescue below still
  // catches a lying free count.
  {
    size_t fs_total = 0, fs_used = 0;
    spiffs_storage_usage(&fs_total, &fs_used);
    size_t fs_free = (fs_total > fs_used) ? fs_total - fs_used : 0;
    if (fs_free < bytes + 65536) {
      spiffs_storage_gc(bytes + 8192);
    }
  }
  char path[64];
  snprintf(path, sizeof(path), WISH_DIR "/w%010u.wav",
           (unsigned)(xTaskGetTickCount() * portTICK_PERIOD_MS));
  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Cannot create %s", path);
    s_fail = WISH_FAIL_STORAGE;
    goto out;
  }
  write_wav_header(f, bytes);
  size_t put = 0;
  bool rescued = false;
  while (put < bytes) {
    size_t ask = bytes - put;
    if (ask > 16384) {
      ask = 16384;
    }
    size_t n = fwrite((const uint8_t *)clip + put, 1, ask, f);
    put += n;
    if (n < ask) {
      if (rescued) {
        break; // second stall: the partition is genuinely out of room
      }
      rescued = true;
      clearerr(f); // a short fwrite latches the stream error flag
      spiffs_storage_gc((bytes - put) + 8192);
    }
  }
  if (put < bytes) {
    // Keep the file honest: a header claiming 15s over 3s of audio made
    // partial clips look corrupt downstream. Stamp what actually landed.
    clearerr(f);
    fseek(f, 0, SEEK_SET);
    write_wav_header(f, (uint32_t)put);
  }
  fclose(f);
  // Anything with >=0.5s of real audio on flash is a deliverable wish — the
  // old all-or-nothing check told kids "the wish got away" about wishes that
  // uploaded fine seconds later. Below that it is a husk: delete it so the
  // sweep can never ship an empty shell as "delivered".
  ok = (put >= (WISH_RATE / 2) * sizeof(int16_t));
  if (ok) {
    ESP_LOGI(TAG, "Wish saved: %s (%u of %u bytes, %us)%s", path,
             (unsigned)put, (unsigned)bytes, (unsigned)(written / WISH_RATE),
             put == bytes ? "" : " PARTIAL - flash ran dry at the tail");
  } else {
    unlink(path);
    s_fail = WISH_FAIL_STORAGE;
    ESP_LOGE(TAG, "Storage refused the wish (%u of %u bytes) - husk deleted",
             (unsigned)put, (unsigned)bytes);
  }

out:
  free(chunk);
  free(clip);
  s_recording = false;
  s_stop_req = false;
  s_saving = false;
  s_last_end_tick = xTaskGetTickCount();   // for the inter-record cooldown
  if (ok) {
    wish_uploader_poke(); // ship it to the repo the moment it exists
  }
  if (done_cb) {
    done_cb(ok);
  }
  vTaskDelete(NULL);
}

esp_err_t wish_recorder_start(void (*done_cb)(bool ok)) {
  if (s_recording) {
    return ESP_ERR_INVALID_STATE;
  }
  // Cooldown (Jon 8/12): back-to-back captures — tearing the mic RX channel
  // down and re-acquiring it within a second — hung a unit hard (D424 needed a
  // power cycle). The shared-clock plumbing needs a beat to settle between
  // recordings. Refuse anything closer than 3s after the previous one ended.
  if (s_last_end_tick != 0 &&
      (xTaskGetTickCount() - s_last_end_tick) < pdMS_TO_TICKS(3000)) {
    return ESP_ERR_INVALID_STATE;
  }
#if CONFIG_I2S_DI_IO < 0
  return ESP_ERR_NOT_SUPPORTED; // no mic wired on this board
#endif
  // A full shelf REFUSES instead of dropping the oldest: field logs caught a
  // kid mashing the button into 1-second clips, and drop-oldest was flushing
  // real wishes to make room for taps. Refusal + a friendly ticker line
  // protects the wishes already waiting to fly.
  {
    DIR *d = opendir(WISH_DIR);
    int cnt = 0;
    if (d) {
      struct dirent *e;
      while ((e = readdir(d)) != NULL) {
        if (strstr(e->d_name, ".wav")) {
          cnt++;
        }
      }
      closedir(d);
    }
    if (cnt >= WISH_MAX_CLIPS) {
      return ESP_ERR_NO_MEM;
    }
  }
  s_recording = true;
  s_stop_req = false;
  s_saving = false;
  s_seconds_done = 0;
  s_fail = WISH_FAIL_NONE;
  // Open the mic DMA HERE, before the task's 4KB internal stack is allocated
  // (Jon 8/12). On a unit idling at ~6KB largest block, creating the stack
  // first left no 2KB-contiguous room for the DMA and the mic silently failed
  // (WISH_FAIL_MIC) fleet-wide. Allocating the small DMA first, while the heap
  // is least fragmented, then the stack in what remains, lets both fit. The
  // task's own acquire() call returns this same already-open channel.
  if (!audio_output_rx_acquire()) {
    s_fail = WISH_FAIL_MIC;
    s_recording = false;
    return ESP_ERR_NOT_FOUND;
  }
  // Core 1 beside the audio machinery; priority below the playback task so a
  // wish can never starve music output (recording is blocked during streaming
  // at the UI layer anyway, but layers of defense are free here).
  if (xTaskCreatePinnedToCore(wish_task, "wish_rec", 4096, (void *)done_cb, 4,
                              NULL, 1) != pdPASS) {
    audio_output_rx_release();  // give the DMA back — no task will
    s_recording = false;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
