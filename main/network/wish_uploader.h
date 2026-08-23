#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the background uploader task (idles until a token is configured). */
void wish_uploader_init(void);

/** Store/replace the GitHub token in NVS (NULL or "" clears it). */
esp_err_t wish_uploader_set_token(const char *token);

/** True if a token is configured. Never exposes the token itself. */
bool wish_uploader_configured(void);

/** Wake the uploader now (called when a wish finishes saving). */
void wish_uploader_poke(void);

/** True while a clip is actively uploading; pct is its progress. */
bool wish_uploader_busy(void);
int wish_uploader_pct(void);

/** Consume-once event: 0 none, 1 = all wishes delivered, 2 = yielded to music. */
int wish_uploader_take_event(void);

/** Human-readable last action / wait reason, for the status API. */
const char *wish_uploader_status(void);

/** False when this image has no uploader at all (the public build).
 *
 * The screen used to ask wish_uploader_online() and, on a false, promise the wish would
 * "fly when wifi is back". In a public build there is no uploader to fly it: online() is a
 * hardcoded false, so the promise was made on WiFi, off WiFi, and for ever. Callers that
 * word a message for a child must ask THIS first - it separates "no radio right now" from
 * "this bunbun keeps its wishes at home". */
bool wish_uploader_present(void);

/** True if WiFi is up — lets the save-verdict tell the truth offline. */
bool wish_uploader_online(void);

/* Panic hunt (8/14): the smallest stack headroom, in WORDS, this task has
 * ever had — sampled at the two deepest points of the fleet beacon's TLS
 * frames. 0 = never sampled yet. A number near zero means the beacon is
 * overflowing, which is the 8/10 field panic returning now that the freed
 * internal RAM lets every unit clear the beacon's heap gate. */
uint32_t wish_up_stack_low(void);

/** Number of recorded wishes still waiting on the shelf. */
int wish_uploader_pending(void);

#ifdef __cplusplus
}
#endif
