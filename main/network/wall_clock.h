#pragma once

/**
 * Wall-clock time from the internet (W-015).
 *
 * Not to be confused with ntp_clock.c, which is AirPlay-session timing
 * against the SENDER's clock — a comment in bunbun believed for months that
 * it was an SNTP client, and it never was. This is the real one.
 */

/** Start SNTP (idempotent; call once the network is up). */
void wall_clock_start(void);

/**
 * Stop SNTP and free its internal-RAM footprint (~a few hundred bytes of
 * sockets and service state — measured as the exact 57 bytes the regression
 * gate failed 0.1.32 by). The system clock keeps ticking after deinit;
 * wall_clock_utc() stays valid. Re-start for the daily re-sync.
 */
void wall_clock_stop(void);

/**
 * UTC minutes-of-day (0..1439), or -1 before the first successful sync.
 * Seconds-in-minute via *sec when non-NULL. Safe from any task.
 * NOTE: reflects the system epoch, which SURVIVES soft reboots — plausible
 * is not synced. Gate on wall_clock_wait_fresh() before trusting it.
 */
int wall_clock_utc(int *sec);

/**
 * Block up to timeout_ms for a genuine SNTP server response in THIS
 * start/stop visit. 0 = fresh answer arrived; -1 = not started or timeout.
 */
int wall_clock_wait_fresh(int timeout_ms);
