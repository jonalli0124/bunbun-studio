/* W-060: post-OTA self-check. Call once after services start. If the
 * running image is PENDING_VERIFY it opens a 10-minute validation window
 * and marks the image valid only if it lives through it healthy; any
 * crash or failed check reverts to the previous firmware at the
 * bootloader. On a long-blessed image this is a no-op. */
#pragma once

void fw_selfcheck_begin(void);
