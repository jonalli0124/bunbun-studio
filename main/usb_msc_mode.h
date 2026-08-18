#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True when this build actually contains the USB machinery. The scaffolding
 *  ships dormant while the TinyUSB stack conflict is unresolved; every entry
 *  point must check this and say so, not reboot into nothing. */
bool usb_msc_mode_available(void);

/** True if the next boot was asked to be a USB card reader (W-020). */
bool usb_msc_mode_flagged(void);

/** Arm (or disarm) card-reader mode for the NEXT boot; caller reboots. */
void usb_msc_mode_set_flag(bool on);

/**
 * Become a USB drive for the rest of this boot: raw SD + TinyUSB MSC + the
 * bunbun mode screen. Clears the flag first (crash-safe), never returns —
 * every exit road is a reboot into the normal pet.
 */
void usb_msc_mode_run(void);

#ifdef __cplusplus
}
#endif
