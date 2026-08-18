#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mdns.h"
#include <stdio.h>
#include <string.h>

#include "hap.h"
#include "mdns_airplay.h"
#include "rtsp_handlers.h"
#include "wifi.h"
#include "settings.h"

static const char *TAG = "mdns_airplay";

// Feature flags are defined in rtsp_handlers.h (shared with /info handler)

// Protocol version
#ifdef CONFIG_AIRPLAY_FORCE_V1
#define AIRPLAY_PROTOCOL_VERSION "1"
#else
#define AIRPLAY_PROTOCOL_VERSION "2"
#endif
#define AIRPLAY_SOURCE_VERSION "377.40.00"

// Flags: 0x4 = audio receiver
#define AIRPLAY_FLAGS "0x4"

// Metadata types advertised in the "md" txt record:
//   0 = text (track title/artist/album), 1 = artwork (cover art images),
//   2 = progress.
// When artwork is disabled, drop "1" so senders do not transmit cover art
// (which can stall the audio pipeline and cause drop-outs on realtime
// streams) while still sending text and progress metadata.
#ifdef CONFIG_ENABLE_AIRPLAY_ARTWORK
#define AIRPLAY_METADATA_TYPES "0,1,2"
#else
#define AIRPLAY_METADATA_TYPES "0,2"
#endif

// Model identifier - AudioAccessory for speaker appearance
// AppleTV3,2 = Apple TV, AudioAccessory5,1 = HomePod mini (speaker)
#define AIRPLAY_MODEL "AudioAccessory5,1"

// Set once init has published the services; lets a network-change re-announce
// distinguish "restart mDNS" from "mDNS was never started" (the device may get
// an IP before AirPlay services come up at all).
static bool s_mdns_ready = false;

bool airplay_classic_mode(void) {
#ifdef CONFIG_AIRPLAY_FORCE_V1
  return true;
#else
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip;
  if (!sta || esp_netif_get_ip_info(sta, &ip) != ESP_OK) {
    return false;
  }
  // addr is network byte order: first octet in the low byte on this LE core.
  // 172.20.10.0/28 == Apple's fixed Personal Hotspot subnet.
  return (ip.ip.addr & 0x00FFFFFF) ==
         (((uint32_t)10 << 16) | ((uint32_t)20 << 8) | 172);
#endif
}

void mdns_airplay_init(void) {
  char mac_str[18];
  char device_id[18];
  char features_str[32];
  char service_name[80];
  char pk_str[65]; // 32 bytes = 64 hex chars + null
  char device_name[65];

  // Get device name from settings
  settings_get_device_name(device_name, sizeof(device_name));

  // Get MAC address
  wifi_get_mac_str(mac_str, sizeof(mac_str));
  strncpy(device_id, mac_str, sizeof(device_id));

  // Get real Ed25519 public key from HAP module
  const uint8_t *pk = hap_get_public_key();
  for (int i = 0; i < 32; i++) {
    snprintf(pk_str + (size_t)i * 2, 3, "%02x", pk[i]);
  }

  // Format features as "hi,lo" hex string. Classic mode strips the
  // pairing/encryption bits so iOS senders choose RAOP over AirPlay 2.
  bool classic = airplay_classic_mode();
  if (classic) {
    snprintf(features_str, sizeof(features_str), "0x%X,0x%X",
             AIRPLAY_FEATURES_V1_LO, AIRPLAY_FEATURES_V1_HI);
  } else {
    snprintf(features_str, sizeof(features_str), "0x%X,0x%X",
             AIRPLAY_FEATURES_V2_LO, AIRPLAY_FEATURES_V2_HI);
  }
  ESP_LOGI(TAG, "Advertising as %s",
           classic ? "classic AirPlay (RAOP) - hotspot/V1 mode" : "AirPlay 2");

  // Create service name for RAOP: <mac>@<name>
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(service_name, sizeof(service_name), "%02X%02X%02X%02X%02X%02X@%s",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], device_name);

  // Initialize mDNS
  ESP_ERROR_CHECK(mdns_init());

  // Set hostname
  ESP_ERROR_CHECK(mdns_hostname_set(device_name));

  // ========================================
  // _airplay._tcp service (port 7000)
  // Only registered in AirPlay 2 mode — classic (V1 build or hotspot) skips
  // it so iOS never even sees an AirPlay 2 target to fail against.
  // ========================================
  if (!classic) {
    mdns_txt_item_t airplay_txt[] = {
        {"deviceid", device_id},
        {"features", features_str},
        {"flags", AIRPLAY_FLAGS},
        {"model", AIRPLAY_MODEL},
        {"pk", pk_str},
        {"pi", "00000000-0000-0000-0000-000000000000"}, // Pairing identity UUID
        {"srcvers", AIRPLAY_SOURCE_VERSION},
        {"vv", AIRPLAY_PROTOCOL_VERSION},
        {"acl", "0"},
    };

    esp_err_t err =
        mdns_service_add(device_name, "_airplay", "_tcp", 7000, airplay_txt,
                         sizeof(airplay_txt) / sizeof(airplay_txt[0]));
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to add _airplay._tcp service: %s",
               esp_err_to_name(err));
    }
  }

  // ========================================
  // _raop._tcp service (port 7000)
  // RAOP = Remote Audio Output Protocol
  // Service name format: <MAC>@<DeviceName>
  // ========================================
  // Classic RAOP records: match squeezelite-esp32 txt record format.
  // No features, no pk, no HAP pairing — just classic RAOP fields.
  mdns_txt_item_t raop_classic_txt[] = {
      {"am", AIRPLAY_MODEL},
      {"tp", "UDP"},                  // Transport protocol
      {"sm", "false"},                // Sharing mode
      {"sv", "false"},                // Server version (unused)
      {"ek", "1"},                    // Encryption key available
      {"et", "0,1"},                  // Encryption types: none, RSA
      {"md", AIRPLAY_METADATA_TYPES}, // Metadata types
      {"cn", "0,1"},                  // Audio codecs: PCM, ALAC
      {"ch", "2"},                    // Channels
      {"ss", "16"},                   // Sample size (bits)
      {"sr", "44100"},                // Sample rate
      {"vn", "3"},                    // Version number
      {"txtvers", "1"},               // TXT record version
  };
  // Dual-mode: include et=1 (RSA) so RAOP-only clients (TuneBlade, AirMusic,
  // shairtunes2, etc.) accept the advertisement, while keeping et=3,5 for
  // AirPlay 2 FairPlay/MFi-SAP. ek=1 advertises that an RSA-encrypted key
  // can be supplied via SDP rsaaeskey: at ANNOUNCE time.
  mdns_txt_item_t raop_txt[] = {
      {"am", AIRPLAY_MODEL},
      {"cn", "0,1,2,3"},              // Audio codecs: PCM, ALAC, AAC, AAC-ELD
      {"da", "true"},                 // Digest auth
      {"ek", "1"},                    // Encryption key available (RSA)
      {"et", "0,1,3,5"},              // Encryption types
      {"ft", features_str},           // Features (same as airplay)
      {"md", AIRPLAY_METADATA_TYPES}, // Metadata types
      {"pk", pk_str},                 // Public key
      {"sf", AIRPLAY_FLAGS},          // Status flags
      {"tp", "UDP"},                  // Transport protocol
      {"vn", "65537"},                // Version number
      {"vs", AIRPLAY_SOURCE_VERSION},
      {"vv", AIRPLAY_PROTOCOL_VERSION},
  };

  esp_err_t err_raop = mdns_service_add(
      service_name, "_raop", "_tcp", 7000,
      classic ? raop_classic_txt : raop_txt,
      classic ? sizeof(raop_classic_txt) / sizeof(raop_classic_txt[0])
              : sizeof(raop_txt) / sizeof(raop_txt[0]));
  if (err_raop != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add _raop._tcp service: %s",
             esp_err_to_name(err_raop));
  }

  s_mdns_ready = true;
}

void mdns_airplay_reannounce(void) {
  // Tear down and republish. The mdns component is supposed to follow IP
  // events on its own, but "supposed to" left one of these units invisible on
  // a phone hotspot: a full restart makes the advertisement's freshness a
  // fact rather than a hope. Cheap (tens of ms), and connected senders are
  // unaffected — established sessions ride TCP, not discovery.
  if (!s_mdns_ready) {
    return; // AirPlay services not up yet; init will announce when they are
  }
  ESP_LOGI(TAG, "Network changed - restarting mDNS advertisement");
  mdns_free();
  s_mdns_ready = false;
  mdns_airplay_init();
}
