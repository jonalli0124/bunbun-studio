#include "wall_clock.h"

#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

static const char *TAG = "wall_clock";
static bool s_started = false;

static bool s_fresh = false;   // a REAL server answer arrived this visit

void wall_clock_start(void) {
  if (s_started) {
    return;
  }
  s_fresh = false;
  esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_err_t err = esp_netif_sntp_init(&cfg);
  if (err == ESP_OK) {
    s_started = true;
    ESP_LOGI(TAG, "SNTP started");
  } else {
    ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
  }
}

// Block up to timeout_ms for a GENUINE server response. The system epoch
// survives soft reboots, so "time() looks plausible" is NOT evidence of a
// sync — the canary spent an evening rebooting through gate cycles and its
// 'synced' clock drifted 80+ minutes as a photocopy of a photocopy (Jon,
// 2026-08-05 ~11pm: "the clock is off"). Only a fresh answer counts.
int wall_clock_wait_fresh(int timeout_ms) {
  if (!s_started) {
    return -1;
  }
  if (s_fresh) {
    return 0;
  }
  esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
  if (err == ESP_OK) {
    s_fresh = true;
    return 0;
  }
  // The 21:47 timeout (Jon, 2026-08-05) left "DNS? UDP 123? pool choice?"
  // unanswerable from serial — the bench PC on the SAME LAN resolved and got
  // an NTP answer instantly, so the failure is in this box. One lookup here
  // splits the fault: DNS failing too means resolver/boot-race, DNS fine
  // means the UDP leg (blocked port or a dead pool pick) ate the window.
  {
    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    int rc = getaddrinfo("pool.ntp.org", "123", &hints, &res);
    if (rc != 0 || res == NULL) {
      ESP_LOGW(TAG, "sync timed out and DNS fails too (rc=%d): resolver or "
                    "boot race, not the pool", rc);
    } else {
      char ip[16] = "?";
      inet_ntoa_r(((struct sockaddr_in *)res->ai_addr)->sin_addr, ip,
                  sizeof(ip));
      ESP_LOGW(TAG, "sync timed out but DNS is fine (%s): UDP 123 leg "
                    "suspect", ip);
      freeaddrinfo(res);
    }
  }
  return -1;
}

void wall_clock_stop(void) {
  if (!s_started) {
    return;
  }
  esp_netif_sntp_deinit();
  s_started = false;
  s_fresh = false;
  ESP_LOGI(TAG, "SNTP stopped (clock keeps ticking; footprint returned)");
}

int wall_clock_utc(int *sec) {
  time_t t = time(NULL);
  // Anything before ~2023 is the epoch default, not a sync. The check keeps
  // this callable at any moment: it simply says "not yet" until SNTP lands.
  if (t < 1700000000) {
    return -1;
  }
  if (sec) {
    *sec = (int)(t % 60);
  }
  return (int)((t / 60) % 1440);
}
