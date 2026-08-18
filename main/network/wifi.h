#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>

/**
 * Initialize WiFi in both AP and STA modes
 * @param ap_ssid AP SSID (if NULL, uses default)
 * @param ap_password AP password (if NULL, uses default or open)
 */
void wifi_init_apsta(const char *ap_ssid, const char *ap_password);

/**
 * Block until WiFi is connected and has an IP address
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return true if connected, false if timeout
 */
bool wifi_wait_connected(uint32_t timeout_ms);

/**
 * Get the device MAC address as a string (XX:XX:XX:XX:XX:XX)
 */
void wifi_get_mac_str(char *mac_str, size_t len);

/**
 * Check if WiFi STA is connected
 */
bool wifi_is_connected(void);

/**
 * Get current IP address as string
 * @param ip_str Output buffer
 * @param len Buffer size
 * @return ESP_OK on success
 */
esp_err_t wifi_get_ip_str(char *ip_str, size_t len);

/**
 * Scan for available WiFi networks
 * @param ap_list Output array of AP info (caller must free)
 * @param ap_count Output: number of APs found
 * @return ESP_OK on success
 */
esp_err_t wifi_scan(wifi_ap_record_t **ap_list, uint16_t *ap_count);

/**
 * Disconnect and stop WiFi
 */
void wifi_stop(void);

/**
 * User-facing radio switch: on rejoins the saved network, off stops the radio
 * and suppresses every auto-reconnect path. Either direction cancels a forced
 * setup portal.
 */
void wifi_user_set(bool on);
bool wifi_user_is_off(void);

/**
 * Force setup mode: drop the STA link, suppress all reconnection, and bring
 * up the device's own configuration AP. Escape hatch for networks that
 * auto-join but block client-to-client traffic (office WiFi with client
 * isolation), which otherwise make the web UI permanently unreachable.
 * Cleared by wifi_user_set() or by the reboot that follows saving a network.
 */
void wifi_user_force_portal(void);

/**
 * Set the DHCP hostname from the given device name.
 * Sanitizes to a valid DNS label (lowercase, hyphens for spaces/symbols).
 * Takes effect on the next DHCP transaction.
 */
void wifi_set_hostname(const char *device_name);

/* W-054: hop to the next remembered network (escape hatch for networks
 * with client isolation). Returns 0 and copies the new SSID out. */
int wifi_switch_next_known(char *out_ssid, size_t out_len);
