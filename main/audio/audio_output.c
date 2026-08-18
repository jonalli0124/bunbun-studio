#include "audio_output.h"

// bunbun (components/bunbun): the pet's beat detector taps the outgoing PCM, its sound
// effects and rain ambience mix into every block, and its SD player fills the frames this
// loop would otherwise write as silence — which is what makes AirPlay outrank SD playback
// sample-accurately, by structure instead of by arbitration state.
extern void bunbun_audio_tap(const void *pcm, size_t bytes);
extern void bunbun_mix_sfx(int16_t *pcm, size_t frames, int allow_rain);
extern size_t bunbun_local_pcm(int16_t *dst, size_t frames);

// ~2 seconds of continuous receiver silence before bunbun's SD audio may engage. Only a
// genuinely stopped stream stays silent that long; mid-song buffer dips last a frame or two.
#define IDLE_FRAMES_FOR_LOCAL 250
#include "rtsp_server.h"

#include "audio_resample.h"
#include "dac.h"
#include "led.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_receiver.h"
#include <inttypes.h>
#include <stdlib.h>

// SIDE NOTE; providing power from GPIO pins is capped ~20mA.
#if CONFIG_I2S_GND_IO >= 0
#define I2S_GND_PIN CONFIG_I2S_GND_IO
#endif
#if CONFIG_I2S_VCC_IO >= 0
#define I2S_VCC_PIN CONFIG_I2S_VCC_IO
#endif

#define TAG           "audio_output"
#define I2S_SCK_PIN   CONFIG_I2S_SCK_IO
#define I2S_BCK_PIN   CONFIG_I2S_BCK_IO
#define I2S_LRCK_PIN  CONFIG_I2S_WS_IO
#define I2S_DOUT_PIN  CONFIG_I2S_DO_IO
#define OUTPUT_RATE   CONFIG_OUTPUT_SAMPLE_RATE_HZ
#define FRAME_SAMPLES 352

// DMA ring-buffer configuration.  Total DMA latency (in samples) is
//   I2S_DMA_DESC_NUM × I2S_DMA_FRAME_NUM
// which at OUTPUT_RATE gives the hardware pipeline delay in µs.
// Keep these in sync with the i2s_chan_config_t initialisation below.
#define I2S_DMA_DESC_NUM  8
#define I2S_DMA_FRAME_NUM 256

/* Max output frames after resampling one input frame */
#define MAX_RESAMPLE_FRAMES \
  ((size_t)((FRAME_SAMPLES + 2) * ((double)OUTPUT_RATE / 44100) + 16))

#if CONFIG_FREERTOS_UNICORE
#define PLAYBACK_CORE 0
#else
#define PLAYBACK_CORE 1
#endif

static i2s_chan_handle_t tx_handle;
static i2s_chan_handle_t rx_handle; // codec ADC/mic; exists only while recording
static volatile bool s_tx_pause = false;      // mic-release owns the channel
static volatile bool s_tx_paused_ack = false; // writer confirms it stepped out
static volatile uint32_t s_last_stream_ms = 0; // last frame of REAL stream audio

uint32_t audio_output_last_stream_ms(void) { return s_last_stream_ms; }

i2s_chan_handle_t audio_output_rx_acquire(void) {
#if CONFIG_I2S_DI_IO >= 0
  if (rx_handle) {
    return rx_handle;
  }
  // DMA ring: 2 x 512 frames (~23ms at 44.1k) double-buffers the recorder's
  // 1024-frame reads. Trimmed 3->2 descriptors (6KB->4KB of contiguous
  // internal DMA RAM) after the fleet hit "ears are stuck" (WISH_FAIL_MIC)
  // once W-061's tic-tac-toe raised the steady-state footprint: connected
  // units idle at ~8KB largest block, and the old 6KB alloc could no longer
  // land. 4KB fits; the read cadence is unchanged.
  // DMA ring: 4 x 256 frames (Jon 8/12). This restores the original ~23ms of
  // buffering depth (4KB total) that a 2x256 trim had cut to ~11ms — too little
  // slack for the read to drain before the DMA wraps, which OVERRUNS and reads
  // as static (Jon's catch: "if they're all static it may be something you're
  // doing"). But it's 4 separate 1KB descriptor buffers, not one 4KB block, so
  // it still fits a fragmented unit — and the mic DMA is now opened before the
  // recorder's task stack (wish_recorder_start), so both find room.
  // 4 x 256 frames (4x1KB descriptor buffers): ~23ms of buffering that fits a
  // fragmented unit, opened on demand before the recorder's task stack.
  i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cfg.dma_desc_num = 4;
  cfg.dma_frame_num = 256;
  // One retry: an allocation this size can lose a fragmentation race to a
  // transient (a draw sprite, a TLS buffer). A 120ms wait lets the momentary
  // holder free, turning a hard "ears stuck" into a blink the child never
  // sees.
  esp_err_t nc = i2s_new_channel(&cfg, NULL, &rx_handle);
  if (nc != ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(120));
    nc = i2s_new_channel(&cfg, NULL, &rx_handle);
  }
  if (nc != ESP_OK) {
    rx_handle = NULL;
    return NULL;
  }
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(OUTPUT_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      // ONLY the data-in pin. Declaring the shared clock pins here was the
      // whole silent-speaker saga: deleting the RX channel UN-ROUTED the
      // codec's MCLK from its pin, BCLK kept running (so every firmware
      // counter looked healthy), and the codec sat clockless and mute until
      // the next recording re-claimed the pin — which is why the bench's
      // self-hearing test could never catch it: the mic revives the clock
      // every time it listens. The controller's clocks feed both channels
      // internally; RX needs no pin claims at all.
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = I2S_GPIO_UNUSED,
              .ws = I2S_GPIO_UNUSED,
              .dout = I2S_GPIO_UNUSED,
              .din = CONFIG_I2S_DI_IO,
          },
  };
  if (i2s_channel_init_std_mode(rx_handle, &std_cfg) != ESP_OK ||
      i2s_channel_enable(rx_handle) != ESP_OK) {
    i2s_del_channel(rx_handle);
    rx_handle = NULL;
    return NULL;
  }
  return rx_handle;
#else
  return NULL;
#endif
}

void audio_output_rx_release(void) {
  if (!rx_handle) {
    return;
  }
  i2s_channel_disable(rx_handle);
  i2s_del_channel(rx_handle);
  rx_handle = NULL;
  // Re-assert the TX clock after tearing down the shared RX side (the codec can
  // otherwise lose its master clock and play silent).
  s_tx_pause = true;
  for (int i = 0; i < 100 && !s_tx_paused_ack; i++) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (s_tx_paused_ack) {
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(OUTPUT_RATE);
    i2s_std_gpio_config_t gpio = {
        .mclk = I2S_SCK_PIN,
        .bclk = I2S_BCK_PIN,
        .ws = I2S_LRCK_PIN,
        .dout = I2S_DOUT_PIN,
        .din = I2S_GPIO_UNUSED,
    };
    i2s_channel_disable(tx_handle);
    i2s_channel_reconfig_std_gpio(tx_handle, &gpio);
    i2s_channel_reconfig_std_clock(tx_handle, &clk);
    i2s_channel_enable(tx_handle);
    dac_on_i2s_started();
  }
  s_tx_pause = false;
}
static volatile bool flush_requested = false;
static volatile bool playback_running = false;
static TaskHandle_t playback_task_handle = NULL;
static volatile int source_rate = 44100;
static volatile bool resample_reinit_needed = false;
static volatile audio_channel_mode_t channel_mode = AUDIO_CHANNEL_STEREO;

static void apply_volume(int16_t *buf, size_t n) {
#ifndef CONFIG_DAC_CONTROLS_VOLUME
  // Ramp toward the target gain instead of applying volume changes
  // instantly.  An abrupt gain step mid-waveform is a discontinuity scaled
  // by the signal's current amplitude — the classic volume "zipper" click,
  // audible on every step of the sender's volume slider.  Approach the
  // target exponentially, stepping once per stereo frame (even indices) so
  // both channels always carry the same gain; the /256 divisor gives a
  // ~3 ms time constant and a worst-case per-frame gain step of ~0.4%,
  // with a minimum step of 1 so the ramp always completes.
  static int32_t cur_q15 = -1;
  int32_t target = airplay_get_volume_q15();
  if (cur_q15 < 0) {
    cur_q15 = target; // first call: no audio has played yet, jump silently
  }
  for (size_t i = 0; i < n; i++) {
    if ((i & 1) == 0 && cur_q15 != target) {
      int32_t diff = target - cur_q15;
      int32_t step = diff / 256;
      if (step == 0) {
        step = diff > 0 ? 1 : -1;
      }
      cur_q15 += step;
    }
    buf[i] = (int16_t)(((int32_t)buf[i] * cur_q15) >> 15);
  }
#endif
}

// Apply the selected channel mode to an interleaved stereo buffer (L,R,...).
// LEFT/RIGHT route the chosen source channel to BOTH outputs so the selected
// track is heard from both speakers; STEREO leaves the buffer untouched.
static void apply_channel_mode(int16_t *buf, size_t frames) {
  audio_channel_mode_t mode = channel_mode;
  if (mode == AUDIO_CHANNEL_STEREO) {
    return;
  }
  size_t src = (mode == AUDIO_CHANNEL_RIGHT) ? 1 : 0;
  for (size_t i = 0; i < frames; i++) {
    int16_t s = buf[i * 2 + src];
    buf[i * 2] = s;
    buf[i * 2 + 1] = s;
  }
}

static void playback_task(void *arg) {
  int16_t *pcm = malloc((size_t)(FRAME_SAMPLES + 1) * 2 * sizeof(int16_t));
  int16_t *silence = calloc((size_t)FRAME_SAMPLES * 2, sizeof(int16_t));
  int16_t *resample_buf = malloc(MAX_RESAMPLE_FRAMES * 2 * sizeof(int16_t));
  if (!pcm || !silence || !resample_buf) {
    ESP_LOGE(TAG, "Failed to allocate buffers");
    free(pcm);
    free(silence);
    playback_task_handle = NULL;
    free(resample_buf);
    vTaskDelete(NULL);
    return;
  }

  size_t written;
  int idle_frames = 0;
  while (playback_running) {
    // Cooperative pause: the mic-release path must briefly disable/reconfig
    // this channel, and i2s_channel_disable DEADLOCKS against a writer
    // blocked in i2s_channel_write(portMAX_DELAY) — each waits on the other
    // forever (bench-caught: three wish cycles, three stuck tasks). While
    // paused we sleep instead of write; the pauser owns the channel.
    if (s_tx_pause) {
      s_tx_paused_ack = true;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    s_tx_paused_ack = false;
    if (resample_reinit_needed) {
      resample_reinit_needed = false;
      audio_resample_init((uint32_t)source_rate, OUTPUT_RATE, 2);
    }
    if (flush_requested) {
      flush_requested = false;
      audio_resample_reset();
      i2s_channel_disable(tx_handle);
      i2s_channel_enable(tx_handle);
    }
    size_t samples = audio_receiver_read(pcm, FRAME_SAMPLES + 1);
    if (samples > 0) {
      // Ground truth for "is music ACTUALLY streaming": stream samples
      // reaching the speaker this frame. audio_receiver_is_playing() was a
      // liar — true on a fresh-booted idle unit — and everything gated on it
      // (wish uploads) waited forever ("it's like it is never sending").
      // Record what the speaker is doing, not what a state machine intends.
      s_last_stream_ms = (uint32_t)(esp_timer_get_time() / 1000);
      int16_t *play_buf = pcm;
      size_t play_samples = samples;
      if (audio_resample_is_active()) {
        play_samples = audio_resample_process(pcm, samples, resample_buf,
                                              MAX_RESAMPLE_FRAMES);
        play_buf = resample_buf;
      }
      apply_volume(play_buf, play_samples * 2);
      apply_channel_mode(play_buf, play_samples);
      led_audio_feed(play_buf, play_samples);
      // bunbun listens to the outgoing PCM here — this feeds its beat detector, which is what
      // makes the disco ball and the dance follow the actual song rather than a fixed 75 BPM
      // fallback. The tap only records into volatiles (no printf, no blocking), so it is safe
      // on this task; see bunbun_audio_tap in components/bunbun.
      idle_frames = 0;
      bunbun_audio_tap(play_buf, play_samples * 2 * sizeof(int16_t));
      // Mix AFTER the tap, so the beat detector hears the music clean rather than bunbun's
      // own bleeps layered on top. Rain is EXCLUDED over a live stream: its duck-and-softclip
      // pass audibly compressed full-scale streamed music — the user heard it as clipping.
      bunbun_mix_sfx(play_buf, play_samples, 0);
      i2s_channel_write(tx_handle, play_buf, play_samples * 2 * sizeof(int16_t),
                        &written, portMAX_DELAY);
      taskYIELD();
    } else {
      // No stream samples THIS frame — but that is not the same as "nothing is streaming".
      // AirPlay's buffer runs dry for a frame or two routinely mid-song, and handing those
      // gaps to the SD player spliced 8ms shards of local MP3 into streamed music — heard as
      // crackling/clipping. Hysteresis: bunbun's SD audio only engages after ~2 seconds of
      // continuous silence, which only a genuinely stopped stream produces; any real frame
      // resets the clock. Momentary underruns therefore stay what they always were: silence.
      if (idle_frames < IDLE_FRAMES_FOR_LOCAL) {
        idle_frames++;
        memset(pcm, 0, (size_t)FRAME_SAMPLES * 2 * sizeof(int16_t));
      } else {
        bunbun_local_pcm(pcm, FRAME_SAMPLES);
        bunbun_audio_tap(pcm, (size_t)FRAME_SAMPLES * 2 * sizeof(int16_t));
      }
      bunbun_mix_sfx(pcm, FRAME_SAMPLES, 1);
      led_audio_feed(pcm, FRAME_SAMPLES);
      i2s_channel_write(tx_handle, pcm,
                        (size_t)FRAME_SAMPLES * 2 * sizeof(int16_t), &written,
                        portMAX_DELAY);
    }
  }

  free(pcm);
  free(silence);
  playback_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t audio_output_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = I2S_DMA_DESC_NUM;
  chan_cfg.dma_frame_num = I2S_DMA_FRAME_NUM;
  // Zero each DMA descriptor after it is sent.  Without this, a writer
  // stall longer than the DMA ring (~46 ms — e.g. an NVS/flash write
  // disabling the cache, or a CPU burst from the web server) makes the
  // hardware REPLAY the stale ring contents in a loop: a loud stutter, then
  // a second discontinuity on recovery.  With auto_clear an underrun
  // degrades to plain silence.
  chan_cfg.auto_clear = true;

  ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_handle, NULL), TAG,
                      "channel create failed");

  // The mic RX channel is created ON DEMAND (audio_output_rx_acquire) and
  // deleted after each recording. It existed permanently at first, sharing
  // the TX channel's DMA sizing — and that internal-RAM squeeze surfaced in
  // the field as "Failed to create receiver task" at 12KB free: the AirPlay
  // session died at RECORD because a microphone nobody was using had eaten
  // the receiver task's stack. A wish records only when nothing else runs,
  // so the mic's memory and AirPlay's memory can safely be the same bytes.

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(OUTPUT_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_SCK_PIN,
              .bclk = I2S_BCK_PIN,
              .ws = I2S_LRCK_PIN,
              .dout = I2S_DOUT_PIN,
              .din = I2S_GPIO_UNUSED,
          },
  };
#ifdef I2S_GND_PIN
  gpio_reset_pin(I2S_GND_PIN);
  gpio_set_direction(I2S_GND_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(I2S_GND_PIN, 0);
#endif
#ifdef I2S_VCC_PIN
  gpio_reset_pin(I2S_VCC_PIN);
  gpio_set_direction(I2S_VCC_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(I2S_VCC_PIN, 1);
#endif

  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &std_cfg), TAG,
                      "std mode init failed");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG,
                      "channel enable failed");
  ESP_LOGI(TAG, "I2S initialized: Rate=%u, DMA_Desc=%d, DMA_Frame=%d",
           (unsigned int)OUTPUT_RATE, I2S_DMA_DESC_NUM, I2S_DMA_FRAME_NUM);

  // MCLK/BCLK/LRCK are now running. Some codecs need this edge to finish their
  // clock setup; amplifiers that manage power from board RTSP events can ignore
  // the hook.
  dac_on_i2s_started();

  audio_resample_init(44100, OUTPUT_RATE, 2);

  return ESP_OK;
}

void audio_output_start(void) {
  if (playback_task_handle != NULL) {
    return; // already running
  }
  playback_running = true;
  xTaskCreatePinnedToCore(playback_task, "audio_play", 4096, NULL, 7,
                          &playback_task_handle, PLAYBACK_CORE);
}

void audio_output_stop(void) {
  if (playback_task_handle == NULL) {
    return;
  }
  playback_running = false;
  // Wait for task to exit cleanly
  int timeout = 40;
  while (playback_task_handle != NULL && timeout-- > 0) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (playback_task_handle != NULL) {
    ESP_LOGW(TAG, "Playback task did not exit within timeout");
  } else {
    ESP_LOGI(TAG, "Playback task stopped");
  }
}

esp_err_t audio_output_write(const void *data, size_t bytes, TickType_t wait) {
  size_t written = 0;
  return i2s_channel_write(tx_handle, data, bytes, &written, wait);
}

void audio_output_set_sample_rate(uint32_t rate) {
  // Only safe to call when no writer task is actively using I2S
  // (AirPlay playback task must be stopped, BT calls this before
  // the I2S writer task starts consuming data)
  ESP_LOGI(TAG, "Setting sample rate to %" PRIu32 " Hz", rate);
  i2s_channel_disable(tx_handle);
  i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);
  i2s_channel_enable(tx_handle);
}

void audio_output_flush(void) {
  flush_requested = true;
}

void audio_output_set_source_rate(int rate) {
  if (rate > 0 && rate != source_rate) {
    source_rate = rate;
    resample_reinit_needed = true;
  }
}

uint32_t audio_output_get_hardware_latency_us(void) {
  // Delay between i2s_channel_write() accepting a sample and that sample
  // leaving the DAC.  This is the DMA ring occupancy AHEAD of the newly
  // written data, which is NOT the full ring: i2s_channel_write() blocks
  // only until space frees, so the writer refills as soon as a descriptor
  // completes and steady-state occupancy oscillates between
  // (DESC_NUM - 1) and DESC_NUM descriptors.
  //
  // Using the full ring (DESC_NUM) overstates the delay by half a
  // descriptor on average — 2.9 ms at 44.1 kHz with the config below — and
  // that bias lands directly in compute_early_us(), pushing every frame
  // toward the "late" side of the threshold.  Model the midpoint instead:
  //   (DESC_NUM - 0.5) x FRAME_NUM == (2*DESC_NUM - 1) x FRAME_NUM / 2
  // The residual +/-2.9 ms swing is real jitter that the drift servo in
  // audio_timing.c absorbs; only the constant bias is removed here.
  return (uint32_t)((((uint64_t)(2 * I2S_DMA_DESC_NUM - 1) * I2S_DMA_FRAME_NUM *
                      1000000ULL) /
                     2) /
                    OUTPUT_RATE);
}

audio_channel_mode_t audio_output_cycle_channel_mode(void) {
  audio_channel_mode_t next;
  switch (channel_mode) {
  case AUDIO_CHANNEL_STEREO:
    next = AUDIO_CHANNEL_LEFT;
    break;
  case AUDIO_CHANNEL_LEFT:
    next = AUDIO_CHANNEL_RIGHT;
    break;
  default:
    next = AUDIO_CHANNEL_STEREO;
    break;
  }
  channel_mode = next;
  ESP_LOGI(TAG, "Channel mode: %s",
           next == AUDIO_CHANNEL_LEFT    ? "LEFT only"
           : next == AUDIO_CHANNEL_RIGHT ? "RIGHT only"
                                         : "STEREO");
  return next;
}

audio_channel_mode_t audio_output_get_channel_mode(void) {
  return channel_mode;
}
