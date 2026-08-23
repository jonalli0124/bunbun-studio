// Improv Wi-Fi Serial — hand a device its wifi over the USB cable, from the browser.
//
// WHY: a freshly flashed unit comes up as an access point, and whoever set it up has to leave
// their own wifi, join bunbun-XXXX, type their password into a captive page, and hope. That is
// four steps and two devices for something the flasher page could have asked while the cable was
// still plugged in. ESP Web Tools speaks this protocol already; implementing the device half
// makes "Connect to Wi-Fi" appear in the same window that just did the flashing.
//
// AND IT FAILS SAFE BY DESIGN: a client that gets no answer to the handshake simply does not
// offer the step, so a build without this — or a device nobody asks — still comes up in AP mode
// exactly as before. Nothing here is on the path of a device that is already set up.
//
// WIRE FORMAT (Improv Serial v1). Every packet:
//     'I','M','P','R','O','V', 0x01, type, len, data[len], checksum
// checksum is the sum of every byte from 'I' through the last data byte, mod 256.
//
// The cable is shared with the debug log, which is fine and intended: the client scans for the
// header and ignores everything else, which is why the pet can keep printing while this runs.
#pragma once

#include "esp_app_desc.h"

// The bunbun component deliberately does NOT require `main` — main requires bunbun, and a
// component cycle would not resolve (see CMakeLists.txt). So the host's few entry points are
// declared by hand here, exactly as net.h already does for wifi_user_set().
extern "C" bool wifi_is_connected(void);
extern "C" esp_err_t wifi_get_ip_str(char *ip_str, size_t len);
extern "C" esp_err_t wifi_apply_credentials_now(const char *ssid, const char *password);
extern "C" esp_err_t settings_get_device_name(char *name, size_t len);

// ---- protocol constants ----
static const uint8_t IMPROV_VER = 1;
enum {  // packet types
  IMPROV_T_CURRENT_STATE = 0x01, IMPROV_T_ERROR_STATE = 0x02,
  IMPROV_T_RPC = 0x03, IMPROV_T_RPC_RESULT = 0x04,
};
enum {  // states
  IMPROV_S_READY = 0x02,          // authorized: send me credentials
  IMPROV_S_PROVISIONING = 0x03, IMPROV_S_PROVISIONED = 0x04,
};
enum {  // errors
  IMPROV_E_NONE = 0x00, IMPROV_E_INVALID_RPC = 0x01, IMPROV_E_UNKNOWN_RPC = 0x02,
  IMPROV_E_UNABLE_TO_CONNECT = 0x03, IMPROV_E_UNKNOWN = 0xFF,
};
enum {  // RPC commands
  IMPROV_C_WIFI_SETTINGS = 0x01, IMPROV_C_IDENTIFY = 0x02,
  IMPROV_C_GET_STATE = 0x03, IMPROV_C_GET_INFO = 0x04, IMPROV_C_GET_NETWORKS = 0x05,
};

static const char IMPROV_HDR[6] = {'I', 'M', 'P', 'R', 'O', 'V'};

// ---- receive state ----
static uint8_t  g_impBuf[192];
static uint16_t g_impLen = 0;      // bytes of a packet gathered so far, header included
static uint8_t  g_impNeed = 0;     // data length, once known
static bool     g_impPending = false;      // a join is in flight
static uint32_t g_impDeadline = 0;
static bool     g_impIdentify = false;     // set for the UI to flash something; read by main
static uint32_t g_impIdentifyUntil = 0;

// ONE WRITE, NEVER A BYTE AT A TIME. The pet prints to this same port continuously, and a log
// line landing between two bytes of a packet corrupts it: the first version of this wrote the
// header, type, length and each data byte separately, and the very first live test came back
// with a broken checksum and "bsky[20] ra" sitting where the device name should have been —
// debug output spliced into the middle of the frame. Building the whole packet first and
// handing it over in a single call closes that window.
static void improvSend(uint8_t type, const uint8_t *data, uint8_t len) {
  uint8_t p[10 + 255];
  uint16_t n = 0;
  for (int i = 0; i < 6; i++) p[n++] = (uint8_t)IMPROV_HDR[i];
  p[n++] = IMPROV_VER;
  p[n++] = type;
  p[n++] = len;
  for (uint8_t i = 0; i < len; i++) p[n++] = data[i];
  uint8_t sum = 0;
  for (uint16_t i = 0; i < n; i++) sum += p[i];
  p[n++] = sum;
  Serial.write(p, n);
  Serial.flush();
}

static void improvState(uint8_t s) { improvSend(IMPROV_T_CURRENT_STATE, &s, 1); }
static void improvError(uint8_t e) { improvSend(IMPROV_T_ERROR_STATE, &e, 1); }

// An RPC result is: command, payload length, then length-prefixed strings.
static void improvResult(uint8_t cmd, const char *const *strs, int n) {
  uint8_t d[160]; int p = 0;
  d[p++] = cmd; int lenAt = p++;             // payload length filled in below
  for (int i = 0; i < n; i++) {
    int sl = (int)strlen(strs[i]);
    if (p + 1 + sl > (int)sizeof(d)) break;  // never overrun; a truncated list beats a crash
    d[p++] = (uint8_t)sl;
    memcpy(d + p, strs[i], sl); p += sl;
  }
  d[lenAt] = (uint8_t)(p - 2);
  improvSend(IMPROV_T_RPC_RESULT, d, (uint8_t)p);
}

static void improvSendDeviceInfo() {
  char name[40] = {0};
  if (settings_get_device_name(name, sizeof(name)) != ESP_OK || !name[0])
    strlcpy(name, "bunbun", sizeof(name));
  const esp_app_desc_t *ad = esp_app_get_description();
  const char *strs[4] = {"bunbun", ad && ad->version[0] ? ad->version : "0", "ESP32-S3", name};
  improvResult(IMPROV_C_GET_INFO, strs, 4);
}

// Report where the device can now be reached. ESP Web Tools turns this into a link, which is
// the whole payoff: the page that flashed the toy hands you its address.
static void improvSendUrl() {
  char ip[20] = {0};
  char url[40];
  if (wifi_get_ip_str(ip, sizeof(ip)) == ESP_OK && ip[0])
    snprintf(url, sizeof(url), "http://%s/", ip);
  else
    strlcpy(url, "http://bunbun.local/", sizeof(url));
  const char *strs[1] = {url};
  improvResult(IMPROV_C_WIFI_SETTINGS, strs, 1);
}

// ---- network list ----
//
// The browser cannot scan wifi; only the toy can. wifi_scan() blocks for seconds AND
// disconnects the station first, so it must never run on the game loop — it goes on its own
// task, exactly as ntpTask does for SNTP, and the answer is posted back through these.
//
// Capped at 16 and deduplicated by name: a house with three mesh points is three BSSIDs with
// one SSID, and a list of duplicates is worse than a short list. Static rather than heap
// because internal RAM is the scarce thing on this board.
#define IMP_NET_MAX 16
struct ImpNet { char ssid[33]; int8_t rssi; bool locked; };
static ImpNet   g_impNets[IMP_NET_MAX];
static uint8_t  g_impNetN = 0;
static volatile bool g_impScanBusy = false;   // a scan task is running
static volatile bool g_impScanDone = false;   // results are ready to send
static bool     g_impScanWanted = false;      // the client asked and is still waiting

// HOLD THE LOG WHILE A CONVERSATION IS HAPPENING. The pet prints several lines a second to this
// same USB CDC port, and the buffer is finite: a burst of eight network packets went out and
// only three arrived, with zero bad checksums - not corrupted, DROPPED, squeezed out by the
// trace. Any valid Improv packet opens a quiet window; the periodic traces check it and stay
// silent. Nothing else is suppressed, so real events still print.
static uint32_t g_impQuietUntil = 0;
static inline bool improvQuiet() { return (int32_t)(millis() - g_impQuietUntil) < 0; }

extern "C" esp_err_t wifi_scan(void **ap_list, uint16_t *ap_count);

static void improvScanTask(void *) {
  wifi_ap_record_t *aps = nullptr;
  uint16_t n = 0;
  g_impNetN = 0;
  if (wifi_scan((void **)&aps, &n) == ESP_OK && aps) {
    for (uint16_t i = 0; i < n && g_impNetN < IMP_NET_MAX; i++) {
      const char *s = (const char *)aps[i].ssid;
      if (!s[0]) continue;                                   // hidden: nothing to show
      bool dup = false;
      for (uint8_t k = 0; k < g_impNetN; k++)
        if (!strcmp(g_impNets[k].ssid, s)) { dup = true; break; }
      if (dup) continue;
      strlcpy(g_impNets[g_impNetN].ssid, s, sizeof(g_impNets[0].ssid));
      g_impNets[g_impNetN].rssi = aps[i].rssi;
      g_impNets[g_impNetN].locked = (aps[i].authmode != WIFI_AUTH_OPEN);
      g_impNetN++;
    }
  }
  if (aps) free(aps);                                        // wifi_scan mallocs; we free
  g_impScanDone = true;
  g_impScanBusy = false;
  vTaskDelete(nullptr);
}

// ONE PACKET PER TICK, NOT A BURST.
//
// First attempt sent all of them back to back with a delay() between. The device reported
// "sent 8 network(s)" and the host received three - zero bad checksums, so they were not
// corrupted, they were DROPPED: the USB CDC buffer is finite and a burst overruns it. Blocking
// with delay() only made the pet stutter without fixing it.
//
// So the list is handed out one entry per loop tick (~60ms apart), which the buffer keeps up
// with easily and which costs the game nothing. -1 means idle; g_impNetN means "send the
// terminator next", which is the packet the client is actually waiting for.
static int g_impNetSend = -1;

static void improvNetPump() {
  if (g_impNetSend < 0) return;
  if (g_impNetSend < (int)g_impNetN) {
    char rssi[8];
    snprintf(rssi, sizeof(rssi), "%d", (int)g_impNets[g_impNetSend].rssi);
    const char *strs[3] = {g_impNets[g_impNetSend].ssid, rssi,
                           g_impNets[g_impNetSend].locked ? "YES" : "NO"};
    improvResult(IMPROV_C_GET_NETWORKS, strs, 3);
    g_impNetSend++;
    return;
  }
  improvResult(IMPROV_C_GET_NETWORKS, nullptr, 0);      // terminator
  Serial.printf("improv: sent %d network(s)\n", (int)g_impNetN);
  g_impNetSend = -1;
}

static void improvSendNetworks() { g_impNetSend = 0; }   // starts the paced hand-out

static void improvHandleRpc(const uint8_t *d, uint8_t len) {
  if (len < 2) { improvError(IMPROV_E_INVALID_RPC); return; }
  const uint8_t cmd = d[0];
  switch (cmd) {
    case IMPROV_C_GET_STATE:
      improvState(wifi_is_connected() ? IMPROV_S_PROVISIONED : IMPROV_S_READY);
      if (wifi_is_connected()) improvSendUrl();
      return;
    case IMPROV_C_GET_INFO:
      improvSendDeviceInfo();
      return;
    case IMPROV_C_IDENTIFY:
      // The pet is the indicator. main.cpp reads this and makes him do something visible.
      g_impIdentify = true;
      g_impIdentifyUntil = millis() + 4000;
      return;
    case IMPROV_C_GET_NETWORKS: {
      if (g_impScanDone) { improvSendNetworks(); return; }   // already have a list: answer now
      g_impScanWanted = true;                                // otherwise scan and answer later
      if (!g_impScanBusy) {
        g_impScanBusy = true;
        xTaskCreatePinnedToCore(improvScanTask, "improv-scan", 4096, nullptr, 1, nullptr, 1);
        Serial.println("improv: scanning for networks");
      }
      return;
    }
    case IMPROV_C_WIFI_SETTINGS: {
      const uint8_t plen = d[1];
      if (plen + 2 > len) { improvError(IMPROV_E_INVALID_RPC); return; }
      const uint8_t *p = d + 2;
      uint8_t sl = *p++;
      if (sl > 32 || (int)(sl + 1) > plen) { improvError(IMPROV_E_INVALID_RPC); return; }
      char ssid[33] = {0}; memcpy(ssid, p, sl); p += sl;
      uint8_t pl = *p++;
      if (pl > 64) { improvError(IMPROV_E_INVALID_RPC); return; }
      char pass[65] = {0}; memcpy(pass, p, pl);
      improvState(IMPROV_S_PROVISIONING);
      if (wifi_apply_credentials_now(ssid, pass) != ESP_OK) {
        improvError(IMPROV_E_UNABLE_TO_CONNECT);
        improvState(IMPROV_S_READY);
        return;
      }
      g_impPending = true;
      g_impDeadline = millis() + 25000;   // DHCP on a slow router is the long pole
      return;
    }
    default:
      improvError(IMPROV_E_UNKNOWN_RPC);
      return;
  }
}

// Feed one byte. Returns true if Improv consumed it, so the caller's own single-letter
// debug commands still work for every byte that is not part of a packet.
static bool improvFeed(uint8_t c) {
  if (g_impLen < 6) {                       // matching the header
    if (c == (uint8_t)IMPROV_HDR[g_impLen]) { g_impBuf[g_impLen++] = c; return true; }
    // A mismatch mid-header is not ours. Restart, but re-test this byte as a fresh 'I' so
    // "IIMPROV" still syncs.
    g_impLen = (c == (uint8_t)IMPROV_HDR[0]) ? 1 : 0;
    if (g_impLen) { g_impBuf[0] = c; return true; }
    return false;
  }
  if (g_impLen >= sizeof(g_impBuf)) { g_impLen = 0; return false; }
  g_impBuf[g_impLen++] = c;
  if (g_impLen == 9) g_impNeed = g_impBuf[8];          // ver, type, len are in
  if (g_impLen < 10u + g_impNeed) return true;         // still gathering data + checksum
  uint8_t sum = 0;
  for (uint16_t i = 0; i < g_impLen - 1; i++) sum += g_impBuf[i];
  const bool ok = (sum == g_impBuf[g_impLen - 1]) && (g_impBuf[6] == IMPROV_VER);
  if (ok) g_impQuietUntil = millis() + 15000;   // someone is talking: stop shouting
  if (ok && g_impBuf[7] == IMPROV_T_RPC) improvHandleRpc(g_impBuf + 9, g_impNeed);
  else if (ok) { /* a state/result packet from the other side: nothing to do */ }
  else improvError(IMPROV_E_UNKNOWN);
  g_impLen = 0; g_impNeed = 0;
  return true;
}

// Called once a loop. Only does anything while a join is in flight.
static void improvTick() {
  if (g_impScanWanted && g_impScanDone) { g_impScanWanted = false; improvSendNetworks(); }
  improvNetPump();
  if (!g_impPending) return;
  if (wifi_is_connected()) {
    g_impPending = false;
    improvSendUrl();
    improvState(IMPROV_S_PROVISIONED);
    Serial.println("improv: joined, credentials saved");
    return;
  }
  if ((int32_t)(millis() - g_impDeadline) >= 0) {
    g_impPending = false;
    improvError(IMPROV_E_UNABLE_TO_CONNECT);
    improvState(IMPROV_S_READY);
    Serial.println("improv: could not join - wrong password, or out of range");
  }
}

// SERVICE THE CABLE WHILE setup() IS STILL WORKING.
//
// ESP Web Tools gives the handshake 10s after a fresh install but only 1500ms on any other
// path (measured in install-dialog.js: `isNewInstall ? 10000 : 1500`). loop() does not begin
// until setup() has loaded an 858-entry pak, measured every character, read the scene, drawn
// the room and built the light map - about 3.4s on a warm device and longer on a cold one, so
// the short window was missed every time and the wifi step silently never appeared.
//
// Nothing here needs the game: answering "who are you" and "what is your state" only needs the
// serial port, which is up right after Serial.begin(). So setup() pumps this at each slow
// stage and the device is reachable within milliseconds of boot instead of seconds.
static void improvPump() {
  uint32_t guard = 0;
  while (Serial.available() && guard++ < 512) improvFeed((uint8_t)Serial.read());
  improvTick();
}
