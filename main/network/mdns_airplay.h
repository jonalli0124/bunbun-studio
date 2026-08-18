#pragma once

#include <stdbool.h>

/**
 * True when the receiver should present itself as a CLASSIC AirPlay (RAOP)
 * device rather than AirPlay 2.
 *
 * Always true when built with CONFIG_AIRPLAY_FORCE_V1. Otherwise true exactly
 * when the device's own IP sits in 172.20.10.0/28 — the subnet Apple
 * hardcodes for iPhone Personal Hotspots. iOS refuses to run AirPlay 2's PTP
 * timing over its own hotspot (verified in the field: discovery succeeds,
 * then the sender drops the session ~50ms after the initial SETUP), while
 * classic RAOP needs no PTP and works. Evaluated per call, so the mDNS
 * re-announce on every GOT_IP flips the advertisement automatically as the
 * device moves between home WiFi and a hotspot.
 */
bool airplay_classic_mode(void);

/**
 * Initialize mDNS and advertise AirPlay 2 services
 *
 * This publishes:
 * - _airplay._tcp service (AirPlay 2)
 * - _raop._tcp service (Remote Audio Output Protocol)
 *
 * With all required TXT records for iOS to recognize the device
 */
void mdns_airplay_init(void);

/**
 * Restart the mDNS advertisement after a network change (new IP / new AP).
 *
 * No-op if mdns_airplay_init() has not run yet. Called from the WiFi GOT_IP
 * handler so the services are re-published on whatever network the device
 * just joined (e.g. moving from home WiFi to a phone hotspot without reboot).
 */
void mdns_airplay_reannounce(void);
