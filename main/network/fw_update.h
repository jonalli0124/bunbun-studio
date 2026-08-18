#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FW_IDLE = 0,
  FW_CHECKING,
  FW_DOWNLOADING, // fw_update_pct() is live
  FW_UP_TO_DATE,  // terminal until fw_update_ack()
  FW_FAILED,      // terminal until fw_update_ack(); fw_update_reason() says why
  FW_SUCCESS,     // shown ~3s, then the device reboots itself
} fw_update_state_t;

/** Kick off a check-and-apply against the repo's approved manifest. */
esp_err_t fw_update_start(void);

fw_update_state_t fw_update_state(void);
int fw_update_pct(void);
const char *fw_update_reason(void);

/** Dismiss a terminal UP_TO_DATE/FAILED state back to idle (UI tap). */
void fw_update_ack(void);

/** W-044: the game calls this when pakBegin() found no valid pak, so the
 *  next update check re-fetches the art even if NVS claims it is current.
 *  An interrupted art write is thereby self-healing on the next check. */
void fw_assets_report_missing(void);
bool fw_assets_repair_wanted(void);

/* Crash breadcrumbs (8/13): RTC-noinit marker stamped along the update/
 * selfcheck paths; snapshot at boot preserves the previous life's last
 * position for /api/system/info ("crumb"). 0 = died outside these paths. */
void fw_crumb_snapshot(void);
uint32_t fw_crumb_prev(void);

/** True while the art partition is being erased/rewritten. The pak is
 *  memory-mapped, so every reader MUST stand down for this window. */
bool fw_assets_writing(void);

#ifdef __cplusplus
}
#endif
