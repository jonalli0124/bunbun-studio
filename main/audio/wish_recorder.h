#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Why the last recording failed — so the UI can say something truthful
 *  instead of a generic "the wish got away". Valid after done_cb(false). */
typedef enum {
  WISH_FAIL_NONE = 0,
  WISH_FAIL_MIC,       /* mic channel could not be opened */
  WISH_FAIL_MEMORY,    /* RAM for the capture buffers */
  WISH_FAIL_TOO_SHORT, /* under 0.5s captured */
  WISH_FAIL_STORAGE,   /* flash refused the bytes; husk deleted */
} wish_fail_t;

wish_fail_t wish_recorder_fail_reason(void);

/**
 * Start a ~15s mic capture to /spiffs/wishes/w<ms>.wav (11kHz mono WAV).
 * done_cb fires from the recorder task when the clip is on flash (ok=true)
 * or the recording was discarded/failed. One recording at a time.
 *
 * Returns ESP_ERR_NOT_SUPPORTED on boards without an I2S RX pin.
 */
esp_err_t wish_recorder_start(void (*done_cb)(bool ok));

/** True while a capture is in flight. */
bool wish_recorder_active(void);

/** Self-hearing probe telemetry from the last capture attempt(s): what the
 *  mic heard of the unit's own probe beep, per attempt. For threshold
 *  calibration over HTTP. Returns chars written. */
int wish_recorder_probe_stats(char *out, size_t cap);

/** True from the moment a stop is requested (or the 15s runs out) until the
 *  clip is on flash. The mic is done listening but the save burst is still
 *  running — the UI should acknowledge the press NOW and show "saving", not
 *  keep a red RECORDING up through two seconds of flash writes (W-008: the
 *  silent lag read as a missed press and taught kids to double-tap). */
bool wish_recorder_saving(void);

/** Whole seconds captured so far (for a countdown UI). */
int wish_recorder_seconds(void);

/** Ask the in-flight capture to finish early (clip is kept if >0.5s). */
void wish_recorder_stop(void);

#ifdef __cplusplus
}
#endif
