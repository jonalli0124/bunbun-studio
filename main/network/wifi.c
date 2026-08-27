#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "wifi.h"
#include "settings.h"
#include "mdns_airplay.h"
#include "wish_uploader.h"

static const char *TAG = "wifi";

// Event group to signal WiFi connection
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Re-enable AP after this many consecutive failures
#define AP_REENABLE_THRESHOLD 5
// lwIP DHCP hostnames are limited to 31 characters plus the trailing NUL.
#define DHCP_HOSTNAME_MAX_LEN 31

static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static bool s_wifi_initialized = false;
static bool s_sta_connected = false;
static bool s_bssid_set = false;
static esp_timer_handle_t s_retry_timer = NULL;

// Saved AP config from init, used to re-enable AP without duplication
static wifi_config_t s_ap_config;

static int wifi_select_best_ap(const char *ssid);
static void scan_and_connect_task(void *arg);
// defined below with their stories; the rescue path reads them early
static volatile bool s_user_off;
static volatile bool s_force_portal;

/* ------------------------------------------------------------------------
 * Known-network memory (W-010): the last 5 networks that ever WORKED, most
 * recent first, in their own NVS namespace. The settings SSID stays the
 * single "configured" network (portal semantics unchanged); this list is a
 * safety net underneath it — a mobile speaker that boots at home shouldn't
 * need the setup portal just because it was on a phone hotspot yesterday.
 * Slots hold ssid ("n<i>s") and password ("n<i>p"); index 0 is newest.
 * ---------------------------------------------------------------------- */
#define KNOWN_NET_MAX WIFI_KNOWN_MAX   /* the shelf's picker sizes its rows off wifi.h */
#define KNOWN_NVS_NS "wifinets"

static volatile bool s_rescue_running = false;

static int known_get(nvs_handle_t h, int i, char *ssid, size_t sc, char *pass,
                     size_t pc) {
  char key[8];
  snprintf(key, sizeof(key), "n%ds", i);
  size_t len = sc;
  if (nvs_get_str(h, key, ssid, &len) != ESP_OK || !ssid[0]) {
    return -1;
  }
  snprintf(key, sizeof(key), "n%dp", i);
  len = pc;
  if (nvs_get_str(h, key, pass, &len) != ESP_OK) {
    pass[0] = '\0';
  }
  return 0;
}

static void known_store(const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) {
    return;
  }
  nvs_handle_t h;
  if (nvs_open(KNOWN_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
    return;
  }
  // Pull the current list, drop any entry matching this ssid, then reinsert
  // at the front — MRU order means eviction always hits the stalest network.
  char ss[KNOWN_NET_MAX][33] = {0};
  char pp[KNOWN_NET_MAX][65] = {0};
  int n = 0;
  for (int i = 0; i < KNOWN_NET_MAX && n < KNOWN_NET_MAX - 1; i++) {
    char s[33], p[65];
    if (known_get(h, i, s, sizeof(s), p, sizeof(p)) == 0 &&
        strcmp(s, ssid) != 0) {
      strlcpy(ss[n], s, sizeof(ss[n]));
      strlcpy(pp[n], p, sizeof(pp[n]));
      n++;
    }
  }
  char key[8];
  snprintf(key, sizeof(key), "n0s");
  nvs_set_str(h, key, ssid);
  snprintf(key, sizeof(key), "n0p");
  nvs_set_str(h, key, pass ? pass : "");
  for (int i = 0; i < n; i++) {
    snprintf(key, sizeof(key), "n%ds", i + 1);
    nvs_set_str(h, key, ss[i]);
    snprintf(key, sizeof(key), "n%dp", i + 1);
    nvs_set_str(h, key, pp[i]);
  }
  nvs_commit(h);
  nvs_close(h);
  ESP_LOGI(TAG, "Known networks: '%s' remembered (%d on file)", ssid, n + 1);
}

// W-054 (Jon, 8/10, stuck on office wifi with client isolation): hop to
// the NEXT remembered network. The auto-join always picks by its own
// logic and there was no way to say "not this one" — an office network
// that blocks AirPlay and peer traffic became a roach motel. Finds the
// current SSID in the known list, promotes the next entry to the active
// credentials, and reconnects. Returns 0 and copies the new SSID out.
int wifi_switch_next_known(char *out_ssid, size_t out_len) {
  nvs_handle_t h;
  if (nvs_open(KNOWN_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
    return -1;
  }
  char ss[KNOWN_NET_MAX][33] = {0};
  char pp[KNOWN_NET_MAX][65] = {0};
  int n = 0;
  for (int i = 0; i < KNOWN_NET_MAX; i++) {
    if (known_get(h, i, ss[n], sizeof(ss[n]), pp[n], sizeof(pp[n])) == 0) {
      n++;
    }
  }
  nvs_close(h);
  if (n < 2) {
    return -1; /* nowhere else to go */
  }
  wifi_ap_record_t ap;
  int cur = -1;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    for (int i = 0; i < n; i++) {
      if (strcmp(ss[i], (const char *)ap.ssid) == 0) {
        cur = i;
        break;
      }
    }
  }
  int next = (cur + 1) % n;
  settings_set_wifi_credentials(ss[next], pp[next]);
  if (out_ssid) {
    strlcpy(out_ssid, ss[next], out_len);
  }
  ESP_LOGI(TAG, "Known networks: switching to '%s' (slot %d of %d)", ss[next],
           next, n);
  wifi_config_t wc = {0};
  strlcpy((char *)wc.sta.ssid, ss[next], sizeof(wc.sta.ssid));
  strlcpy((char *)wc.sta.password, pp[next], sizeof(wc.sta.password));
  esp_wifi_disconnect();
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  esp_wifi_connect();
  return 0;
}

static int known_lookup(const char *ssid, char *pass, size_t pc);

/* W-054 phase 2 (approved 8/10, spec ratified by council 8/14): the picker's
 * backend. The shelf lists what is remembered, joins a named one, forgets a
 * named one. No scan anywhere in this path - the list is only ever what the
 * family already taught the unit. */
int wifi_known_list(char out[][33], int max) {
  nvs_handle_t h;
  if (nvs_open(KNOWN_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
    return 0;
  }
  int n = 0;
  for (int i = 0; i < KNOWN_NET_MAX && n < max; i++) {
    char p[65];
    if (known_get(h, i, out[n], 33, p, sizeof(p)) == 0) {
      n++;
    }
  }
  nvs_close(h);
  return n;
}

int wifi_switch_to_known(const char *ssid) {
  char pass[65] = {0};
  if (!ssid || !ssid[0] || known_lookup(ssid, pass, sizeof(pass)) != 0) {
    return -1;
  }
  /* Same three calls the hop has always used - promote to the active
   * credentials, then reconnect at the named network. */
  settings_set_wifi_credentials(ssid, pass);
  ESP_LOGI(TAG, "Known networks: joining '%s' by name", ssid);
  wifi_config_t wc = {0};
  strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
  strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
  esp_wifi_disconnect();
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  esp_wifi_connect();
  return 0;
}

int wifi_forget_known(const char *ssid) {
  if (!ssid || !ssid[0]) {
    return -1;
  }
  nvs_handle_t h;
  if (nvs_open(KNOWN_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
    return -1;
  }
  char ss[KNOWN_NET_MAX][33] = {0};
  char pp[KNOWN_NET_MAX][65] = {0};
  int n = 0, had = 0;
  for (int i = 0; i < KNOWN_NET_MAX; i++) {
    char s[33], p[65];
    if (known_get(h, i, s, sizeof(s), p, sizeof(p)) != 0) {
      continue;
    }
    if (strcmp(s, ssid) == 0) {
      had = 1;
      continue;
    }
    strlcpy(ss[n], s, sizeof(ss[n]));
    strlcpy(pp[n], p, sizeof(pp[n]));
    n++;
  }
  if (had) {
    /* clear every slot, then rewrite compacted - a forget in the middle must
     * not leave a hole that known_get() reads as end-of-list */
    char key[12];   /* 8 fits, but -Wformat-truncation can't see the loop bound */
    for (int i = 0; i < KNOWN_NET_MAX; i++) {
      snprintf(key, sizeof(key), "n%ds", i);
      nvs_erase_key(h, key);
      snprintf(key, sizeof(key), "n%dp", i);
      nvs_erase_key(h, key);
    }
    for (int i = 0; i < n; i++) {
      snprintf(key, sizeof(key), "n%ds", i);
      nvs_set_str(h, key, ss[i]);
      snprintf(key, sizeof(key), "n%dp", i);
      nvs_set_str(h, key, pp[i]);
    }
    nvs_commit(h);
    ESP_LOGI(TAG, "Known networks: '%s' forgotten (%d on file)", ssid, n);
  }
  nvs_close(h);
  return had ? 0 : -1;
}

esp_err_t wifi_apply_credentials_now(const char *ssid, const char *password) {
  if (!ssid || !ssid[0]) {
    return ESP_ERR_INVALID_ARG;
  }
  const char *pw = password ? password : "";
  settings_set_wifi_credentials(ssid, pw);
  known_store(ssid, pw);           /* so the escape hatch can come back here */
  ESP_LOGI(TAG, "Improv: joining '%s' without a restart", ssid);
  wifi_config_t wc = {0};
  strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
  strlcpy((char *)wc.sta.password, pw, sizeof(wc.sta.password));
  esp_wifi_disconnect();
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
  if (err != ESP_OK) {
    return err;
  }
  return esp_wifi_connect();
}

static int known_lookup(const char *ssid, char *pass, size_t pc) {
  nvs_handle_t h;
  if (nvs_open(KNOWN_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
    return -1;
  }
  int rc = -1;
  for (int i = 0; i < KNOWN_NET_MAX; i++) {
    char s[33], p[65];
    if (known_get(h, i, s, sizeof(s), p, sizeof(p)) == 0 &&
        strcmp(s, ssid) == 0) {
      if (pass) {
        strlcpy(pass, p, pc);
      }
      rc = 0;
      break;
    }
  }
  nvs_close(h);
  return rc;
}

// Scan everything in range and point the STA config at the strongest network
// we have credentials for (skipping `avoid`, the one that just failed).
// Returns true if the config was swapped; the caller still owns the
// esp_wifi_connect() call. Runs a blocking scan — task context only.
static bool known_rescue_try(const char *avoid) {
  wifi_scan_config_t sc = {
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
      .scan_time = {.active = {.min = 0, .max = 0}}, // BT co-exist safe
  };
  if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
    return false;
  }
  uint16_t n = 0;
  esp_wifi_scan_get_ap_num(&n);
  if (n == 0) {
    esp_wifi_scan_get_ap_records(&n, NULL);
    return false;
  }
  wifi_ap_record_t *aps = malloc(sizeof(wifi_ap_record_t) * n);
  if (!aps) {
    esp_wifi_scan_get_ap_records(&n, NULL);
    return false;
  }
  esp_wifi_scan_get_ap_records(&n, aps);
  int best = -1;
  for (int i = 0; i < n; i++) {
    const char *s = (const char *)aps[i].ssid;
    if (!s[0] || (avoid && strcmp(s, avoid) == 0)) {
      continue;
    }
    if (known_lookup(s, NULL, 0) != 0) {
      continue;
    }
    if (best < 0 || aps[i].rssi > aps[best].rssi) {
      best = i;
    }
  }
  bool swapped = false;
  if (best >= 0) {
    char pass[65] = {0};
    if (known_lookup((const char *)aps[best].ssid, pass, sizeof(pass)) == 0) {
      wifi_config_t c = {0};
      strlcpy((char *)c.sta.ssid, (const char *)aps[best].ssid,
              sizeof(c.sta.ssid));
      strlcpy((char *)c.sta.password, pass, sizeof(c.sta.password));
      c.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
      memcpy(c.sta.bssid, aps[best].bssid, 6);
      c.sta.bssid_set = true;
      if (esp_wifi_set_config(WIFI_IF_STA, &c) == ESP_OK) {
        ESP_LOGI(TAG, "Known-network rescue: joining '%s' (rssi=%d)",
                 (const char *)aps[best].ssid, aps[best].rssi);
        swapped = true;
      }
    }
  }
  free(aps);
  return swapped;
}

// One-shot: read the live config and remember it. Runs in its own task
// because a wifi_config_t is 628 bytes and anything placed in the event
// handler's locals is charged to the sys_evt task's (tight) stack on EVERY
// event — putting it inline boot-looped the canary at the first WiFi event
// (caught by the regression gate, 2026-08-05).
static void known_store_task(void *arg) {
  wifi_config_t c;
  if (esp_wifi_get_config(WIFI_IF_STA, &c) == ESP_OK && c.sta.ssid[0]) {
    known_store((const char *)c.sta.ssid, (const char *)c.sta.password);
  }
  vTaskDelete(NULL);
}

static void known_rescue_task(void *arg) {
  // The failing SSID comes from the live config, not settings: a previous
  // rescue may already have swapped it once.
  char avoid[33] = {0};
  wifi_config_t cfg;
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
    strlcpy(avoid, (const char *)cfg.sta.ssid, sizeof(avoid));
  }
  if (!s_user_off && !s_force_portal && !s_sta_connected &&
      known_rescue_try(avoid[0] ? avoid : NULL)) {
    esp_wifi_connect();
  }
  s_rescue_running = false;
  vTaskDelete(NULL);
}

static void sanitize_hostname(const char *name, char *out, size_t out_len) {
  size_t j = 0;
  for (size_t i = 0; name[i] && j < out_len - 1; i++) {
    char c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
      out[j++] = c;
    } else if (j > 0 && out[j - 1] != '-') {
      out[j++] = '-';
    }
  }
  while (j > 0 && out[j - 1] == '-') {
    j--;
  }
  if (j == 0) {
    strlcpy(out, "esp32-airplay", out_len);
    return;
  }
  out[j] = '\0';
}

void wifi_set_hostname(const char *device_name) {
  if (!s_sta_netif || !device_name) {
    return;
  }
  char hostname[DHCP_HOSTNAME_MAX_LEN + 1];
  sanitize_hostname(device_name, hostname, sizeof(hostname));
  esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set hostname '%s': %s", hostname,
             esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Hostname set to: %s", hostname);
  }
}

// bunbun: the on-screen WIFI button turns the radio off for battery — a feature the pet had
// standalone. This flag suppresses every auto-reconnect path while the user has said "off",
// because esp_wifi_stop() alone just loses an argument with the retry machinery.
static volatile bool s_user_off = false;

// Setup-mode hold: the STA link is deliberately dropped and every reconnect
// path suppressed while the device's own AP stays up. This is the escape
// hatch for a network you can join but whose web UI you cannot reach (office
// WiFi with client isolation auto-joins forever and locks you out of your own
// configuration page). RAM-only on purpose: saving a new network reboots the
// device (web_server.c), and a plain power cycle also returns to normal.
static volatile bool s_force_portal = false;
static void enable_ap_mode(void);

bool wifi_user_is_off(void) { return s_user_off; }

void wifi_user_set(bool on) {
  s_force_portal = false;
  if (on) {
    s_user_off = false;
    esp_wifi_start();
    esp_wifi_connect();
  } else {
    s_user_off = true;
    esp_wifi_stop();
  }
}

void wifi_user_force_portal(void) {
  s_force_portal = true;
  s_user_off = false;
  esp_wifi_start(); // no-op if already running; revives a user-off radio
  enable_ap_mode();
  esp_wifi_disconnect();
  ESP_LOGI(TAG, "Setup mode forced: STA dropped, AP up for configuration");
}

static void retry_timer_callback(void *arg) {
  if (!s_sta_connected && !s_user_off && !s_force_portal) {
    ESP_LOGI(TAG, "Retry timer fired, reconnecting (attempt %d)...",
             s_retry_num + 1);
    esp_wifi_connect();
  }
}

static void schedule_retry(void) {
  // Exponential backoff: 5s, 10s, 20s, 30s (max)
  int delay_s = 5;
  if (s_retry_num > AP_REENABLE_THRESHOLD) {
    int backoff_count = s_retry_num - AP_REENABLE_THRESHOLD;
    delay_s = 5 * (1 << (backoff_count > 3 ? 3 : backoff_count));
    if (delay_s > 30) {
      delay_s = 30;
    }
  }
  ESP_LOGI(TAG, "Scheduling retry in %d seconds", delay_s);
  esp_timer_start_once(s_retry_timer, (uint64_t)delay_s * 1000000);
}

static void enable_ap_mode(void) {
  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_APSTA) {
    ESP_LOGI(TAG, "Re-enabling AP mode for configuration access");
    if (!s_ap_netif) {
      s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &s_ap_config);
  }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    // Defer scan+connect to a separate task — the blocking scan uses too
    // much stack to run inside the sys_evt event loop (2–4 KB).
    // 5120 since W-010: the boot path can now fall through to
    // known_rescue_try (wifi_config_t + nvs reads) on top of the scan.
    xTaskCreate(scan_and_connect_task, "wifi_scan", 5120, NULL, 3, NULL);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_sta_connected = false;
    if (s_user_off) return;              // the user turned the radio off; stay off
    if (s_force_portal) return;          // setup mode: stay on our own AP, no rejoining
    wifi_event_sta_disconnected_t *disconnected =
        (wifi_event_sta_disconnected_t *)event_data;
    ESP_LOGI(TAG, "Disconnected from AP, reason: %d", disconnected->reason);

    s_retry_num++;

    if (s_retry_num < AP_REENABLE_THRESHOLD) {
      // Fast retries — reconnect immediately
      ESP_LOGI(TAG, "Retrying connection (%d/%d)...", s_retry_num,
               AP_REENABLE_THRESHOLD);
      esp_wifi_connect();
    } else {
      if (s_retry_num == AP_REENABLE_THRESHOLD) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        ESP_LOGW(TAG,
                 "WiFi connection failed after %d attempts, switching to "
                 "backoff retries",
                 AP_REENABLE_THRESHOLD);
        enable_ap_mode();
        // W-010: before settling into backoff on a network that isn't
        // answering, look around — maybe we simply came home. Blocking scan,
        // so never in the event loop task.
        if (!s_rescue_running) {
          s_rescue_running = true;
          if (xTaskCreate(known_rescue_task, "wifi_rescue", 5120, NULL, 3,
                          NULL) != pdPASS) {
            s_rescue_running = false;
          }
        }
      }
      // Delayed retries with backoff
      schedule_retry();
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    s_sta_connected = true;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    // W-010: a network that just handed us an IP has earned a memory slot.
    // Deferred to a task — see known_store_task for the stack story.
    // 4608: wifi_config_t (628) + slot buffers (~500) + nvs_set_str/commit
    // internals (~2KB) blew through 3072 on the bench.
    xTaskCreate(known_store_task, "wifi_known", 4608, NULL, 3, NULL);

    // Disable AP mode when STA connects
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_APSTA) {
      ESP_LOGI(TAG, "STA connected, disabling AP mode");
      esp_wifi_set_mode(WIFI_MODE_STA);
    }

    // Every new IP gets a fresh mDNS advertisement. Joining a different
    // network without rebooting (home WiFi -> phone hotspot, the mobile
    // speaker case) otherwise leaves discovery relying on the mdns
    // component's own event handling, which proved unreliable in the field.
    mdns_airplay_reannounce();

    // Wishes recorded offline fly the moment a network appears instead of
    // waiting out the uploader's 60s sweep — walking in the door with a
    // carful of wishes should deliver them before the coat is off. The
    // uploader still applies its own gates (token, music-quiet), so this
    // is a wake-up, not a bypass.
    wish_uploader_poke();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
    ESP_LOGI(TAG, "AP started");
  }
}

// One-shot task: scan for best AP then connect — runs outside the event loop
// to avoid overflowing the sys_evt stack.
static void scan_and_connect_task(void *arg) {
  wifi_config_t cfg;
  bool have_cfg = esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK &&
                  strlen((char *)cfg.sta.ssid) > 0;
  int seen = 0;
  if (have_cfg) {
    seen = wifi_select_best_ap((char *)cfg.sta.ssid);
  }
  // W-010: configured network nowhere in sight (booted at home while set to
  // yesterday's hotspot), or nothing configured at all but we've been on
  // networks before — join the strongest remembered one instead of making
  // someone find the setup portal again.
  if (seen == 0 && !s_user_off && !s_force_portal) {
    known_rescue_try(have_cfg ? (const char *)cfg.sta.ssid : NULL);
  }
  if (!s_user_off && !s_force_portal) esp_wifi_connect();
  vTaskDelete(NULL);
}

// Scan for the best AP matching our SSID and set its BSSID in the STA
// config. Returns the number of matching APs seen (0 = nowhere in range,
// which is the known-network rescue's cue).
static int wifi_select_best_ap(const char *ssid) {
  wifi_scan_config_t scan_config = {
      .ssid = (uint8_t *)ssid,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = false,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
      .scan_time = {.active = {.min = 0,
                               .max = 0}}, // 0, 0 needed for BT co-exist
  };

  esp_err_t err = esp_wifi_scan_start(&scan_config, true);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Best-AP scan failed: %s", esp_err_to_name(err));
    // Scan machinery down, not "network absent" — don't trigger a rescue.
    return -1;
  }

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);
  if (ap_count == 0) {
    ESP_LOGW(TAG, "Best-AP scan: no APs found for SSID %s", ssid);
    esp_wifi_scan_get_ap_records(&ap_count, NULL);
    return 0;
  }

  wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
  if (!ap_list) {
    esp_wifi_scan_get_ap_records(&ap_count, NULL);
    return -1;
  }

  esp_wifi_scan_get_ap_records(&ap_count, ap_list);

  // Find AP with strongest signal
  int best_idx = 0;
  for (int i = 1; i < ap_count; i++) {
    if (ap_list[i].rssi > ap_list[best_idx].rssi) {
      best_idx = i;
    }
  }

#if CONFIG_WIFI_PREFER_5GHZ
  // Prefer the 5 GHz band when the same SSID is present on both bands.
  // 5 GHz APs use channels above 14; 2.4 GHz uses channels 1-14. Pick the
  // strongest AP in each band, then favour 5 GHz unless it is too weak and a
  // 2.4 GHz AP is available (in which case 2.4 GHz is the more reliable link).
  int best_5g = -1;
  int best_24 = -1;
  for (int i = 0; i < ap_count; i++) {
    if (ap_list[i].primary > 14) {
      if (best_5g < 0 || ap_list[i].rssi > ap_list[best_5g].rssi) {
        best_5g = i;
      }
    } else {
      if (best_24 < 0 || ap_list[i].rssi > ap_list[best_24].rssi) {
        best_24 = i;
      }
    }
  }
  if (best_5g >= 0 && (best_24 < 0 || ap_list[best_5g].rssi >=
                                          CONFIG_WIFI_PREFER_5GHZ_MIN_RSSI)) {
    best_idx = best_5g;
    ESP_LOGI(TAG, "Preferring 5 GHz AP (rssi=%d, ch=%d)", ap_list[best_5g].rssi,
             ap_list[best_5g].primary);
  } else if (best_5g >= 0) {
    ESP_LOGI(TAG, "5 GHz AP too weak (rssi=%d < %d), falling back to 2.4 GHz",
             ap_list[best_5g].rssi, CONFIG_WIFI_PREFER_5GHZ_MIN_RSSI);
    best_idx = best_24;
  }
#endif

  ESP_LOGI(TAG, "Found %d APs for SSID '%s', best: " MACSTR " (rssi=%d, ch=%d)",
           ap_count, ssid, MAC2STR(ap_list[best_idx].bssid),
           ap_list[best_idx].rssi, ap_list[best_idx].primary);

  for (int i = 0; i < ap_count; i++) {
    if (i != best_idx) {
      ESP_LOGI(TAG, "  Other AP: " MACSTR " (rssi=%d, ch=%d)",
               MAC2STR(ap_list[i].bssid), ap_list[i].rssi, ap_list[i].primary);
    }
  }

  // Set BSSID in the STA config to lock to the best AP
  wifi_config_t sta_cfg;
  esp_wifi_get_config(WIFI_IF_STA, &sta_cfg);
  memcpy(sta_cfg.sta.bssid, ap_list[best_idx].bssid, 6);
  sta_cfg.sta.bssid_set = true;
  esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  s_bssid_set = true;

  free(ap_list);
  return (int)ap_count;
}

static void wifi_init_base(void) {
  if (s_wifi_initialized) {
    return;
  }

  s_wifi_event_group = xEventGroupCreate();

  esp_err_t ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(ret);
  }

  // Create event loop if it doesn't exist
  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(ret);
  }

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

  // Create one-shot retry timer (no background task needed)
  const esp_timer_create_args_t timer_args = {
      .callback = retry_timer_callback,
      .name = "wifi_retry",
  };
  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_retry_timer));

  s_wifi_initialized = true;
}

void wifi_init_apsta(const char *ap_ssid, const char *ap_password) {
  wifi_init_base();

  if (!s_sta_netif) {
    s_sta_netif = esp_netif_create_default_wifi_sta();
    char dev_name[65];
    settings_get_device_name(dev_name, sizeof(dev_name));
    wifi_set_hostname(dev_name);
  }
  if (!s_ap_netif) {
    s_ap_netif = esp_netif_create_default_wifi_ap();
  }

  // Configure STA
  char ssid[33] = {0};
  char password[65] = {0};
  bool has_credentials = false;

  if (settings_get_wifi_ssid(ssid, sizeof(ssid)) == ESP_OK &&
      settings_get_wifi_password(password, sizeof(password)) == ESP_OK &&
      strlen(ssid) > 0) {
    has_credentials = true;
  }

  wifi_config_t sta_config = {0};
  strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
  strlcpy((char *)sta_config.sta.password, password,
          sizeof(sta_config.sta.password));
  sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  // Configure AP and save for later re-enable. The AP SSID is the DEVICE NAME
  // (bunbun-XXXX by default, or whatever the naming screen chose), not the
  // build-wide constant: several units in one room in setup mode broadcasting
  // identical "ESP32-AirPlay-Setup" networks are indistinguishable.
  char ap_name[33] = {0}; // 802.11 SSID limit is 32 bytes
  const char *default_ssid = ap_ssid;
  if (!default_ssid) {
    char dev_name[65] = {0};
    settings_get_device_name(dev_name, sizeof(dev_name));
    if (strlen(dev_name) > 0) {
      strlcpy(ap_name, dev_name, sizeof(ap_name));
      default_ssid = ap_name;
    } else {
      default_ssid = CONFIG_DEFAULT_AP_SSID;
    }
  }
  const char *default_password =
      ap_password ? ap_password : CONFIG_DEFAULT_AP_PASSWORD;

  memset(&s_ap_config, 0, sizeof(s_ap_config));
  strncpy((char *)s_ap_config.ap.ssid, default_ssid,
          sizeof(s_ap_config.ap.ssid) - 1);
  s_ap_config.ap.ssid_len = strlen(default_ssid);
  s_ap_config.ap.channel = 1;
  s_ap_config.ap.max_connection = 4;

  // A SHORT PASSWORD MUST NEVER BECOME AN OPEN AP. WPA2-PSK needs 8+ characters; below that
  // esp_wifi_set_config refuses and the AP does not come up. Say so loudly rather than let a
  // well-meant edit ("make it bunbun") silently reopen the API at 192.168.4.1 to anyone in
  // radio range - which is what an empty password does today.
  if (strlen(default_password) > 0 && strlen(default_password) < 8) {
    ESP_LOGE(TAG,
             "AP password is %d chars; WPA2 needs 8. The setup AP will NOT start. "
             "Fix CONFIG_DEFAULT_AP_PASSWORD.",
             (int)strlen(default_password));
  }
  if (strlen(default_password) == 0) {
    ESP_LOGW(TAG, "AP password is empty - the setup AP is OPEN and the whole API is reachable "
                  "on it. Set CONFIG_DEFAULT_AP_PASSWORD.");
    s_ap_config.ap.authmode = WIFI_AUTH_OPEN;
  } else {
    strncpy((char *)s_ap_config.ap.password, default_password,
            sizeof(s_ap_config.ap.password) - 1);
    s_ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  }

  s_retry_num = 0;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &s_ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "AP+STA mode started: AP SSID=%s", default_ssid);
  if (has_credentials) {
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
  }
}

bool wifi_wait_connected(uint32_t timeout_ms) {
  if (!s_wifi_event_group) {
    return false;
  }

  TickType_t timeout_ticks =
      timeout_ms > 0 ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY;
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, timeout_ticks);

  if (bits & WIFI_CONNECTED_BIT) {
    return true;
  }
  if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "Failed to connect to WiFi");
  }
  return false;
}

void wifi_get_mac_str(char *mac_str, size_t len) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(mac_str, len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
}

bool wifi_is_connected(void) {
  return s_sta_connected;
}

esp_err_t wifi_get_ip_str(char *ip_str, size_t len) {
  if (!s_sta_netif || !ip_str || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_netif_ip_info_t ip_info;
  esp_err_t err = esp_netif_get_ip_info(s_sta_netif, &ip_info);
  if (err == ESP_OK) {
    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
  }
  return err;
}

esp_err_t wifi_scan(wifi_ap_record_t **ap_list, uint16_t *ap_count) {
  if (!ap_list || !ap_count) {
    return ESP_ERR_INVALID_ARG;
  }

  // Stop any pending retry and disconnect cleanly
  esp_timer_stop(s_retry_timer);
  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));

  // Clear BSSID lock so next connect can use a fresh scan result
  if (s_bssid_set) {
    wifi_config_t sta_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &sta_cfg) == ESP_OK) {
      memset(sta_cfg.sta.bssid, 0, sizeof(sta_cfg.sta.bssid));
      sta_cfg.sta.bssid_set = false;
      esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    }
    s_bssid_set = false;
  }

  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = true,
  };

  esp_err_t err = esp_wifi_scan_start(&scan_config, true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
    return err;
  }

  uint16_t number = 0;
  err = esp_wifi_scan_get_ap_num(&number);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(err));
    return err;
  }

  if (number == 0) {
    *ap_list = NULL;
    *ap_count = 0;
    return ESP_OK;
  }

  wifi_ap_record_t *aps = malloc(sizeof(wifi_ap_record_t) * number);
  if (!aps) {
    return ESP_ERR_NO_MEM;
  }

  err = esp_wifi_scan_get_ap_records(&number, aps);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
    free(aps);
    return err;
  }

  *ap_list = aps;
  *ap_count = number;
  return ESP_OK;
}

void wifi_stop(void) {
  if (s_wifi_initialized) {
    esp_timer_stop(s_retry_timer);
    esp_wifi_stop();
    esp_wifi_deinit();
    s_wifi_initialized = false;
    s_sta_connected = false;
    s_retry_num = 0;
    if (s_wifi_event_group) {
      xEventGroupClearBits(s_wifi_event_group,
                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }
  }
}
