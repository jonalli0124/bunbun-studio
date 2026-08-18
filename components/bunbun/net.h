// Network state, READ from the host application.
//
// bunbun's original net.h was a full provisioning stack: scanning, a captive portal, up to five
// remembered networks, blind-retry for hidden SSIDs. All of that is duplicated — and done better,
// with a proper web UI — by the app this now lives inside. Keeping both would mean two things
// driving one radio, and this project has already learned what two owners of one resource costs.
//
// Uses the IDF WiFi API rather than Arduino's WiFi class, for two reasons. Arduino's WiFi library
// is deliberately not compiled into this build (the host owns the radio), so the class does not
// exist. And `#include <WiFi.h>` does not even reach Arduino on a case-insensitive filesystem —
// it resolves to this project's own main/network/WiFi.h, which is a completely different header.
#pragma once
#include <Arduino.h>
#include "esp_wifi.h"
#include "esp_netif.h"

// The host's user-facing radio switch (main/network/wifi.c): suppresses every auto-reconnect
// path, because esp_wifi_stop() alone loses the argument with the retry machinery.
extern "C" void wifi_user_set(bool on);
extern "C" bool wifi_user_is_off(void);
extern "C" void wifi_user_force_portal(void);

enum NetState : uint8_t {
  NET_OFF = 0,
  NET_SCANNING,
  NET_JOINING,
  NET_ONLINE,
  NET_AP,
};

// Derived on every call rather than cached. A mirror of someone else's state is exactly the kind
// of instrument that reports intent instead of result, which has already cost this project four
// separate bugs.
static inline NetState netStateNow() {
  if (wifi_user_is_off()) return NET_OFF;    // stopped radio still reports its configured mode
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) return NET_OFF;
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return NET_ONLINE;
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) return NET_AP;
  return NET_JOINING;
}

#define g_netState (netStateNow())

static inline const char *netStateName() {
  switch (netStateNow()) {
    case NET_SCANNING: return "looking";
    case NET_JOINING:  return "connecting";
    case NET_ONLINE:   return "online";
    case NET_AP:       return "setup mode";
    default:           return "off";
  }
}

static inline String netIp() {
  NetState s = netStateNow();
  const char *key = (s == NET_AP) ? "WIFI_AP_DEF" : "WIFI_STA_DEF";
  esp_netif_t *nif = esp_netif_get_handle_from_ifkey(key);
  esp_netif_ip_info_t ip;
  if (!nif || esp_netif_get_ip_info(nif, &ip) != ESP_OK || ip.ip.addr == 0) return String();
  char buf[16];
  snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
  return String(buf);
}

static inline String netName() {
  NetState s = netStateNow();
  if (s == NET_ONLINE) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return String((const char *)ap.ssid);
  } else if (s == NET_AP) {
    wifi_config_t cfg;
    if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK)
      return String((const char *)cfg.ap.ssid);
  }
  return String();
}

// The WIFI button is real again: off for battery (a feature the standalone pet had), on to
// rejoin, and setup mode genuinely forces the host's configuration AP. That last one earned
// its keep at an office: WiFi with client isolation auto-joins forever while blocking the web
// UI, and without an on-device escape the unit is unreachable on the very network it's on.
static inline void netBegin()       { wifi_user_set(true); }
static inline void netEnd()         { wifi_user_set(false); }
static inline void netForcePortal() { wifi_user_force_portal(); }
static inline void netTick()        {}
