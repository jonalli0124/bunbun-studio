#include "web_server.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_wifi.h"

#include "settings.h"
#include "led.h"
#include "wifi.h"
#include "ethernet.h"
#include "ota.h"
#include "fw_update.h"
#include "log_stream.h"
#include "rtsp_server.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "wish_uploader.h"
#include "wish_recorder.h"
#include "spiffs_storage.h"
#include "audio_output.h"
#include "audio_receiver.h"
#include "driver/gpio.h"
#ifdef CONFIG_DAC_ES8311
#include "dac_es8311.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_DAC_TAS58XX
#include "eq_events.h"
#include "dac_tas58xx_eq.h"
#endif

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;

#define SPIFFS_CHUNK_SIZE 1024

static esp_err_t serve_spiffs_file(httpd_req_t *req, const char *path,
                                   const char *content_type) {
  FILE *f = fopen(path, "r");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s", path);
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, content_type);
  char buf[SPIFFS_CHUNK_SIZE];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) {
      fclose(f);
      httpd_resp_send_chunk(req, NULL, 0);
      return ESP_FAIL;
    }
  }
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// API handlers
static esp_err_t root_handler(httpd_req_t *req) {
  // The MUSIC card is injected here rather than edited into index.html,
  // because index.html lives on SPIFFS and an app OTA cannot replace it —
  // a button added to the file would never reach a unit in the field
  // (Jon 8/14: "on the main ip webpage can we add an audio button" ->
  // the /music page, which has existed all along with nothing linking to it).
  // Served BEFORE the file so it lands near the top of the page, and styled
  // with the sheet's own .card/.btn classes so it looks native.
  static const char kMusicCard[] =
      "<div class='card'><h2>Music</h2>"
      "<p>Add songs to the SD card, or remove ones you are done with.</p>"
      "<a class='btn btn-primary btn-block' href='/music'"
      " style='display:block;text-align:center;text-decoration:none'>"
      "Open the music library</a></div>";
  httpd_resp_set_type(req, "text/html");
  FILE *f = fopen("/spiffs/www/index.html", "r");
  if (!f) {
    httpd_resp_send(req, kMusicCard, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;                       // the card alone still gets them there
  }
  // Read it whole (12KB) so the card can be placed INSIDE the body rather than
  // ahead of the doctype, where it would sit outside the document the page's
  // own stylesheet applies to. Buffer in PSRAM: internal RAM is the scarce one.
  fseek(f, 0, SEEK_END);
  long flen = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *page = (flen > 0 && flen < 64 * 1024)
                   ? heap_caps_malloc((size_t)flen + 1, MALLOC_CAP_SPIRAM)
                   : NULL;
  if (!page) {                           // no room: send the file unchanged
    fclose(f);
    return serve_spiffs_file(req, "/spiffs/www/index.html", "text/html");
  }
  size_t got = fread(page, 1, (size_t)flen, f);
  fclose(f);
  page[got] = 0;
  char *body = strstr(page, "<body");
  char *cut = body ? strchr(body, '>') : NULL;
  if (cut) {
    httpd_resp_send_chunk(req, page, (ssize_t)(cut - page + 1));
    httpd_resp_send_chunk(req, kMusicCard, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, cut + 1, HTTPD_RESP_USE_STRLEN);
  } else {                               // no body tag: append rather than lose it
    httpd_resp_send_chunk(req, page, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, kMusicCard, HTTPD_RESP_USE_STRLEN);
  }
  httpd_resp_send_chunk(req, NULL, 0);
  free(page);
  return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t logs_page_handler(httpd_req_t *req) {
  return serve_spiffs_file(req, "/spiffs/www/logs.html", "text/html");
}

static esp_err_t speedtest_page_handler(httpd_req_t *req) {
  return serve_spiffs_file(req, "/spiffs/www/speedtest.html", "text/html");
}

// The scene builder, served BY the device on purpose.
//
// A builder page hosted anywhere else cannot talk to a bunbun at all: this
// server sends no CORS headers and answers OPTIONS with 405, and an https
// page (a published artifact, say) can never reach an http LAN device no
// matter what headers we send. Same origin is the only route that works —
// and it avoids opening a wildcard CORS hole on an unauthenticated device
// sitting on a family's wifi. The child needs the device's IP and nothing
// else: no PC, no toolchain.
//
// The page itself arrives over POST /api/fs/upload and lives on SPIFFS,
// which a firmware OTA does not touch (OTA writes ota_0/ota_1 only), so an
// uploaded builder survives every update. A serial flash does rewrite
// SPIFFS from data/ — see the note above fs_upload_handler.
#define BUILDER_PAGE_PATH "/spiffs/www/builder.html"

static esp_err_t builder_page_handler(httpd_req_t *req) {
  // Nothing else on this server sends freshness headers, which leaves the
  // browser free to heuristically cache. For a tool a child re-uploads while
  // iterating, "you got your old page back and nothing said so" is exactly
  // the silent failure this project keeps paying for. Revalidate every time;
  // the page is local and streams in about a second.
  httpd_resp_set_hdr(req, "Cache-Control",
                     "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");

  // A bare 404 is a dead end for a child. If the page has not been uploaded
  // yet (fresh unit, or a serial flash wiped SPIFFS) say so in words, the
  // way root_handler still gets you somewhere when index.html is missing.
  FILE *probe = fopen(BUILDER_PAGE_PATH, "r");
  if (!probe) {
    static const char kNotYet[] =
        "<!doctype html><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Builder not installed</title>"
        "<body style='font:16px/1.5 system-ui;margin:2em;max-width:34em'>"
        "<h1>No builder here yet</h1>"
        "<p>This bunbun has no scene builder installed. Send one to it:</p>"
        "<pre style='background:#eee;padding:.6em;overflow-x:auto'>"
        "curl --data-binary @builder.html \\\n"
        "  \"http://DEVICE-IP/api/fs/upload?path=" BUILDER_PAGE_PATH "\""
        "</pre>"
        "<p>Then reload this page.</p>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kNotYet, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  fclose(probe);
  return serve_spiffs_file(req, BUILDER_PAGE_PATH, "text/html");
}

// Tiny endpoint used by JS for RTT timing. Returns minimal body.
static esp_err_t speedtest_ping_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_send(req, "ok", 2);
  return ESP_OK;
}

// Streams `bytes` octets of filler data so the browser can measure DL speed.
// Capped to avoid pathological requests starving audio.
#define SPEEDTEST_MAX_BYTES ((size_t)16 * 1024 * 1024)
#define SPEEDTEST_CHUNK     2048

static esp_err_t speedtest_download_handler(httpd_req_t *req) {
  size_t bytes = (size_t)1024 * 1024;
  char qbuf[64];
  if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
    char val[16];
    if (httpd_query_key_value(qbuf, "bytes", val, sizeof(val)) == ESP_OK) {
      long v = strtol(val, NULL, 10);
      if (v > 0) {
        bytes = (size_t)v;
      }
    }
  }
  if (bytes > SPEEDTEST_MAX_BYTES) {
    bytes = SPEEDTEST_MAX_BYTES;
  }

  // Reuse a single buffer of filler bytes. Static so we don't repeatedly
  // hammer the heap; content is irrelevant but non-zero to thwart any
  // compression along the way.
  static uint8_t filler[SPEEDTEST_CHUNK];
  static bool filler_init = false;
  if (!filler_init) {
    for (size_t i = 0; i < sizeof(filler); i++) {
      filler[i] = (uint8_t)(i * 37);
    }
    filler_init = true;
  }

  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  size_t remaining = bytes;
  while (remaining > 0) {
    ssize_t n =
        remaining < SPEEDTEST_CHUNK ? (ssize_t)remaining : SPEEDTEST_CHUNK;
    if (httpd_resp_send_chunk(req, (const char *)filler, n) != ESP_OK) {
      return ESP_FAIL;
    }
    remaining -= (size_t)n;
  }
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// Consumes a POST body and reports how many bytes were received.
static esp_err_t speedtest_upload_handler(httpd_req_t *req) {
  size_t total = req->content_len;
  size_t got = 0;
  uint8_t buf[SPEEDTEST_CHUNK];
  while (got < total) {
    size_t want = total - got;
    if (want > sizeof(buf)) {
      want = sizeof(buf);
    }
    int r = httpd_req_recv(req, (char *)buf, want);
    if (r <= 0) {
      if (r == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      return ESP_FAIL;
    }
    got += (size_t)r;
  }
  char reply[64];
  int n = snprintf(reply, sizeof(reply), "received=%u", (unsigned)got);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, reply, n);
  return ESP_OK;
}

// Captive portal detection handlers
// These endpoints are requested by various OS to detect captive portals
static esp_err_t captive_portal_redirect(httpd_req_t *req) {
  // Redirect to the configuration page
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// Apple devices (iOS/macOS) check these
static esp_err_t captive_apple_handler(httpd_req_t *req) {
  // Apple expects specific response, redirect instead
  return captive_portal_redirect(req);
}

// Android checks this
static esp_err_t captive_android_handler(httpd_req_t *req) {
  // Android expects 204 for no captive portal, anything else triggers portal
  return captive_portal_redirect(req);
}

// Windows checks this
static esp_err_t captive_windows_handler(httpd_req_t *req) {
  return captive_portal_redirect(req);
}

static esp_err_t wifi_scan_handler(httpd_req_t *req) {
  wifi_ap_record_t *ap_list = NULL;
  uint16_t ap_count = 0;

  cJSON *json = cJSON_CreateObject();
  esp_err_t err = wifi_scan(&ap_list, &ap_count);

  if (err == ESP_OK && ap_list) {
    cJSON *networks = cJSON_CreateArray();
    for (uint16_t i = 0; i < ap_count; i++) {
      cJSON *net = cJSON_CreateObject();
      cJSON_AddStringToObject(net, "ssid", (char *)ap_list[i].ssid);
      cJSON_AddNumberToObject(net, "rssi", ap_list[i].rssi);
      cJSON_AddNumberToObject(net, "channel", ap_list[i].primary);
      cJSON_AddItemToArray(networks, net);
    }
    cJSON_AddItemToObject(json, "networks", networks);
    cJSON_AddBoolToObject(json, "success", true);
    free(ap_list);
  } else {
    cJSON_AddBoolToObject(json, "success", false);
    cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
  }

  char *json_str = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);

  return ESP_OK;
}

static esp_err_t wifi_config_handler(httpd_req_t *req) {
  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  content[ret] = '\0';

  cJSON *json = cJSON_Parse(content);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
  cJSON *password_json = cJSON_GetObjectItem(json, "password");

  cJSON *response = cJSON_CreateObject();
  if (ssid_json && cJSON_IsString(ssid_json)) {
    const char *ssid = cJSON_GetStringValue(ssid_json);
    const char *password = password_json && cJSON_IsString(password_json)
                               ? cJSON_GetStringValue(password_json)
                               : "";

    esp_err_t err = settings_set_wifi_credentials(ssid, password);
    if (err == ESP_OK) {
      cJSON_AddBoolToObject(response, "success", true);
      ESP_LOGI(TAG, "WiFi credentials saved. We are restarting...");
      // Schedule restart
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_restart();
    } else {
      cJSON_AddBoolToObject(response, "success", false);
      cJSON_AddStringToObject(response, "error", esp_err_to_name(err));
    }
  } else {
    cJSON_AddBoolToObject(response, "success", false);
    cJSON_AddStringToObject(response, "error", "Invalid SSID");
  }

  char *json_str = cJSON_Print(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  cJSON_Delete(response);

  return ESP_OK;
}

static esp_err_t device_name_handler(httpd_req_t *req) {
  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  content[ret] = '\0';

  cJSON *json = cJSON_Parse(content);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *name_json = cJSON_GetObjectItem(json, "name");
  cJSON *response = cJSON_CreateObject();

  if (name_json && cJSON_IsString(name_json)) {
    const char *name = cJSON_GetStringValue(name_json);
    esp_err_t err = settings_set_device_name(name);
    if (err == ESP_OK) {
      wifi_set_hostname(name);
      ethernet_set_hostname(name);
      cJSON_AddBoolToObject(response, "success", true);
    } else {
      cJSON_AddBoolToObject(response, "success", false);
      cJSON_AddStringToObject(response, "error", esp_err_to_name(err));
    }
  } else {
    cJSON_AddBoolToObject(response, "success", false);
    cJSON_AddStringToObject(response, "error", "Invalid name");
  }

  char *json_str = cJSON_Print(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  cJSON_Delete(response);

  return ESP_OK;
}

static esp_err_t led_brightness_get_handler(httpd_req_t *req) {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddNumberToObject(json, "brightness", led_get_brightness());
  cJSON_AddBoolToObject(json, "success", true);
  char *json_str = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  return ESP_OK;
}

static esp_err_t led_brightness_post_handler(httpd_req_t *req) {
  char content[64];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  content[ret] = '\0';

  cJSON *json = cJSON_Parse(content);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *response = cJSON_CreateObject();
  cJSON *val = cJSON_GetObjectItem(json, "brightness");
  if (val && cJSON_IsNumber(val)) {
    int b = (int)val->valuedouble;
    if (b < 0) {
      b = 0;
    }
    if (b > 255) {
      b = 255;
    }
    esp_err_t err = led_set_brightness((uint8_t)b);
    if (err == ESP_OK) {
      cJSON_AddBoolToObject(response, "success", true);
    } else {
      cJSON_AddBoolToObject(response, "success", false);
      cJSON_AddStringToObject(response, "error", esp_err_to_name(err));
    }
  } else {
    cJSON_AddBoolToObject(response, "success", false);
    cJSON_AddStringToObject(response, "error",
                            "Expected {\"brightness\": 0-255}");
  }

  char *json_str = cJSON_Print(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  cJSON_Delete(response);
  return ESP_OK;
}

// Local assets-pak upload — the canary review path. The fleet path stays
// approved.json (W-044); this endpoint exists so a freshly built pak can be
// reviewed on ONE unit before any manifest publish. POST the raw BUNP pak.
//
// NVS "assetsver" is deliberately NOT touched unless ?v= is passed: the
// assets task re-fetches whenever manifest != NVS, so stamping a new local
// version here would make the unit immediately pull the OLD fleet pak back
// over the upload. Leaving NVS alone keeps manifest == NVS and the local
// pak sticks until a real art publish supersedes it.
// The webpage-port endpoints answer cross-origin: the Scene Assembler runs on another
// origin (localhost tools) and drives the port with fetch(). Anything not listed here
// keeps the same-origin default.
static void cors_allow(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}
static esp_err_t cors_preflight_handler(httpd_req_t *req) {
  cors_allow(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
  httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
  httpd_resp_sendstr(req, "");
  return ESP_OK;
}

// GET the assets partition back as the pak it holds - the other half of the webpage port.
// The browser fetches the unit's CURRENT pak, splices the child's new frames into it, and
// POSTs the merged pak to the same URI; the device never needs a PC in the loop. Length
// comes from the index itself (max offset+size), so only real bytes travel.
static esp_err_t ota_assets_get_handler(httpd_req_t *req) {
  cors_allow(req);
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "assets");
  if (!part) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no assets partition");
    return ESP_FAIL;
  }
  uint8_t hdr[8];
  if (esp_partition_read(part, 0, hdr, 8) != ESP_OK || memcmp(hdr, "BUNP", 4) != 0) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no pak on this unit");
    return ESP_FAIL;
  }
  uint32_t count = hdr[6] | (hdr[7] << 8);
  // Internal bounce, transient (the static-buffer regression of 8/13) - the index is read
  // one entry at a time and the body streamed in 4KB slices through the same buffer.
  uint8_t *buf = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    return ESP_FAIL;
  }
  uint32_t total = 8 + count * 56;
  for (uint32_t i = 0; i < count; i++) {
    if (esp_partition_read(part, 8 + i * 56 + 32, buf, 8) != ESP_OK)
      break;
    uint32_t off = buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24);
    uint32_t sz = buf[4] | (buf[5] << 8) | (buf[6] << 16) | ((uint32_t)buf[7] << 24);
    if (off + sz > total && off + sz <= part->size)
      total = off + sz;
  }
  httpd_resp_set_type(req, "application/octet-stream");
  char cl[16];
  snprintf(cl, sizeof(cl), "%u", (unsigned)total);
  httpd_resp_set_hdr(req, "X-Pak-Bytes", cl);
  for (uint32_t off = 0; off < total;) {
    uint32_t want = total - off;
    if (want > 4096)
      want = 4096;
    if (esp_partition_read(part, off, buf, want) != ESP_OK)
      break;
    if (httpd_resp_send_chunk(req, (const char *)buf, want) != ESP_OK)
      break;
    off += want;
  }
  free(buf);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t ota_assets_handler(httpd_req_t *req) {
  cors_allow(req);
  if (req->content_len == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No pak uploaded");
    return ESP_FAIL;
  }
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "assets");
  if (!part) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "no assets partition");
    return ESP_FAIL;
  }
  if (req->content_len > part->size) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "pak larger than partition");
    return ESP_FAIL;
  }

  char ver[32] = {0};
  {
    char q[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
      httpd_query_key_value(q, "v", ver, sizeof(ver));
    }
  }

  // Flash writes + streaming don't mix well; same call the app OTA makes.
  ESP_LOGI(TAG, "Stopping AirPlay for assets update (%d bytes)",
           (int)req->content_len);
  rtsp_server_stop();

  size_t erase_len = (req->content_len + 4095) & ~(size_t)4095;
  if (esp_partition_erase_range(part, 0, erase_len) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "erase failed");
    return ESP_FAIL;
  }

  // Transient, not static (regression 8/13: a static 4KB .bss buffer cost
  // every unit 4KB of internal RAM forever for a path that runs once a
  // month). Internal alloc because esp_partition_write sources from it.
  char *buf = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    return ESP_FAIL;
  }
  size_t off = 0;
  while (off < req->content_len) {
    size_t want = req->content_len - off;
    if (want > 4096) {
      want = 4096;
    }
    int n = httpd_req_recv(req, buf, want);
    if (n <= 0) {
      free(buf);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "receive failed");
      return ESP_FAIL;
    }
    if (off == 0 && (n < 4 || memcmp(buf, "BUNP", 4) != 0)) {
      // Partition is already part-erased at this point, but pakBegin()
      // treats a missing magic as "no assets" — loud and harmless, and
      // fw_assets_report_missing() will re-pull the fleet pak.
      free(buf);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "not a BUNP pak");
      return ESP_FAIL;
    }
    if (esp_partition_write(part, off, buf, (size_t)n) != ESP_OK) {
      free(buf);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "write failed");
      return ESP_FAIL;
    }
    off += (size_t)n;
  }
  free(buf);

  if (ver[0]) {
    nvs_handle_t h;
    if (nvs_open("bunbun", NVS_READWRITE, &h) == ESP_OK) {
      nvs_set_str(h, "assetsver", ver);
      nvs_commit(h);
      nvs_close(h);
    }
  }

  ESP_LOGI(TAG, "assets pak written: %u bytes%s%s", (unsigned)off,
           ver[0] ? " ver=" : "", ver);
  httpd_resp_sendstr(req, "Assets update complete, rebooting now!\n");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK;
}

// Age jump for design review (Jon 8/14: "can we test the different ages?"). GET
// /api/debug/age?min=N, or the friendlier ?phase=baby|teen|adult which picks a
// representative minute inside each band (baby <1440, teen <2880, adult beyond).
// A TIME MACHINE, not a reset: name, stats and wishes are all untouched — see
// bunbun_set_age_min(). Debug-only, and it goes behind the same gate as
// /api/wish/record before any non-family unit ships (Hush's ruling, 8/12).
// GET /api/screenshot -> a BMP of the room and everything drawn on it. The answer to
// "where does that object actually sit" without a phone camera (Jon 8/14).
extern uint8_t *bunbun_scene_bmp(size_t *out_len);
static esp_err_t screenshot_handler(httpd_req_t *req) {
  size_t len = 0;
  uint8_t *bmp = bunbun_scene_bmp(&len);
  if (!bmp) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/bmp");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=bunbun.bmp");
  esp_err_t r = httpd_resp_send(req, (const char *)bmp, len);
  free(bmp);
  return r;
}

// GET /api/debug/away -> run the run-away beat on demand, so the treat cycle can
// actually be watched (Jon 8/14). Same code path the neglect chain runs.
// GET /api/debug/cat -> the visiting cat, on demand rather than on her own 15-40min clock.
extern int bunbun_call_cat(void);
static esp_err_t debug_cat_handler(httpd_req_t *req) {
  int ok = bunbun_call_cat();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, ok ? "{\"ok\":true,\"note\":\"she is on her way in\"}\n"
                             : "{\"ok\":false,\"note\":\"room is busy, asleep or he is away\"}\n");
  return ESP_OK;
}

extern void bunbun_send_away(void);
static esp_err_t debug_away_handler(httpd_req_t *req) {
  bunbun_send_away();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req,
                     "{\"ok\":true,\"note\":\"put TREATS out on the care sheet;"
                     " he is home 10 minutes later\"}\n");
  return ESP_OK;
}

extern void bunbun_set_age_min(int m);
static esp_err_t debug_age_handler(httpd_req_t *req) {
  char q[64], v[24];
  int m = -1;
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    if (httpd_query_key_value(q, "min", v, sizeof(v)) == ESP_OK) {
      m = atoi(v);
    } else if (httpd_query_key_value(q, "phase", v, sizeof(v)) == ESP_OK) {
      if (!strcasecmp(v, "baby"))       m = 600;    // past toddler, still a baby
      else if (!strcasecmp(v, "teen"))  m = 1500;   // just over BABY_END
      else if (!strcasecmp(v, "adult")) m = 3000;   // just over TEEN_END
    }
  }
  if (m < 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "use ?min=N or ?phase=baby|teen|adult");
    return ESP_FAIL;
  }
  bunbun_set_age_min(m);
  char out[96];
  snprintf(out, sizeof(out), "{\"ok\":true,\"age_min\":%d}\n", m);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, out);
  return ESP_OK;
}

// which creature this pet is - the art changes, nothing about the pet does
static esp_err_t pet_species_handler(httpd_req_t *req) {
  cors_allow(req);
  extern const char *bunbun_species_id(void);
  extern int bunbun_set_species(const char *id);
  if (req->method == HTTP_POST) {
    char q[64], v[16] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "id", v, sizeof(v)) != ESP_OK || !v[0]) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "use ?id=bunny|capybara");
      return ESP_FAIL;
    }
    if (bunbun_set_species(v) < 0) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown species");
      return ESP_FAIL;
    }
  }
  char out[80];
  snprintf(out, sizeof(out), "{\"species\":\"%s\"}\n", bunbun_species_id());
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, out);
  return ESP_OK;
}

static esp_err_t ota_update_handler(httpd_req_t *req) {
  if (req->content_len == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No firmware uploaded");
    return ESP_FAIL;
  }

  // Stop AirPlay to free resources during OTA
  ESP_LOGI(TAG, "Stopping AirPlay for OTA update");
  rtsp_server_stop();

  esp_err_t err = ota_start_from_http(req);

  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        esp_err_to_name(err));
    return ESP_FAIL;
  }

  // Send response before restarting
  httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();

  return ESP_OK;
}

static esp_err_t system_info_handler(httpd_req_t *req) {
  cors_allow(req);
  cJSON *json = cJSON_CreateObject();
  cJSON *info = cJSON_CreateObject();

  char ip_str[16] = {0};
  char mac_str[18] = {0};
  char device_name[65] = {0};
  bool wifi_connected = wifi_is_connected();
  bool eth_connected = ethernet_is_connected();

  // Show IP and MAC for the active interface
  if (eth_connected) {
    ethernet_get_ip_str(ip_str, sizeof(ip_str));
    ethernet_get_mac_str(mac_str, sizeof(mac_str));
  } else {
    wifi_get_ip_str(ip_str, sizeof(ip_str));
    wifi_get_mac_str(mac_str, sizeof(mac_str));
  }
  settings_get_device_name(device_name, sizeof(device_name));

  cJSON_AddStringToObject(info, "ip", ip_str);
  cJSON_AddStringToObject(info, "mac", mac_str);
  cJSON_AddStringToObject(info, "device_name", device_name);
  cJSON_AddBoolToObject(info, "wifi_connected", wifi_connected);
  cJSON_AddBoolToObject(info, "eth_connected", eth_connected);
  cJSON_AddNumberToObject(info, "free_heap", esp_get_free_heap_size());
  // Internal RAM specifically: free_heap lumps PSRAM in, which hid a fatal
  // internal-RAM squeeze ("Failed to create receiver task" @ 12KB internal
  // free while free_heap showed 6.6MB). The regression gate reads these.
  cJSON_AddNumberToObject(info, "free_internal",
                          heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  cJSON_AddNumberToObject(
      info, "largest_internal_block",
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  // Seconds since boot. The regression gate's crash-loop watch needs this: a
  // rebooting unit resets it, a healthy one counts up, and nothing else in
  // the API can tell those apart from outside.
  cJSON_AddNumberToObject(info, "uptime_s",
                          (double)(esp_timer_get_time() / 1000000LL));
  // worst-ever stack headroom (words) on the wish/beacon task — the 8/14
  // panic hunt's decisive number; see wish_uploader.h
  cJSON_AddNumberToObject(info, "wish_stack_low", (double)wish_up_stack_low());
  // where the PREVIOUS life died, if it died inside the update/selfcheck
  // paths (crash breadcrumbs, 8/13 — see fw_update.h for the marker map)
  cJSON_AddNumberToObject(info, "crumb", (double)fw_crumb_prev());
  // Filesystem pressure (W-011): SPIFFS GC debt starved wish saves in the
  // field with no way to see it from a phone. Note used/total can look
  // roomy while GC debt still stalls writes — but a climbing `fs_used` with
  // an empty shelf is the tell.
  {
    size_t fs_total = 0, fs_used = 0;
    spiffs_storage_usage(&fs_total, &fs_used);
    cJSON_AddNumberToObject(info, "fs_total", (double)fs_total);
    cJSON_AddNumberToObject(info, "fs_used", (double)fs_used);
  }
  // W-019: why the chip last came up. Tonight's unexplained reboots cost an
  // hour because nothing answered this from a phone.
  cJSON_AddNumberToObject(info, "reset_reason", esp_reset_reason());
  // W-023: panel hygiene. touch_ghost climbing on an idle unit = noisy panel;
  // that number decides which boards may ship as gifts.
  {
    extern void bunbun_touch_stats(unsigned *ok, unsigned *fails,
                                   unsigned *ghosts);
    unsigned t_ok = 0, t_fail = 0, t_ghost = 0;
    bunbun_touch_stats(&t_ok, &t_fail, &t_ghost);
    cJSON_AddNumberToObject(info, "touch_ok", t_ok);
    cJSON_AddNumberToObject(info, "touch_fail", t_fail);
    cJSON_AddNumberToObject(info, "touch_ghost", t_ghost);
  }
  // Pet snapshot — lets a fleet OTA be verified to have preserved the pet
  // (stage/phase/age) rather than resetting it to a fresh egg.
  {
    extern void bunbun_pet_snapshot(int *stage, int *phase, int *age_min);
    int pstage = 0, pphase = 0, page = 0;
    bunbun_pet_snapshot(&pstage, &pphase, &page);
    cJSON_AddNumberToObject(info, "pet_stage", pstage);
    cJSON_AddNumberToObject(info, "pet_phase", pphase);
    cJSON_AddNumberToObject(info, "pet_age_min", page);
    // (species idx already exposed below by the Stage-1 field; the ID string lives at
    // GET /api/pet/species — a duplicate "species" key here confused every JSON parser)
  }
  // Which character pack the pet is wearing. 0 / "" is the base bunny.
  {
    extern void bunbun_species_snapshot(int *idx, const char **id);
    int sidx = 0;
    const char *sid = "";
    bunbun_species_snapshot(&sidx, &sid);
    cJSON_AddNumberToObject(info, "species", sidx);
    cJSON_AddStringToObject(info, "species_id", sid);
  }
  // Where everyone is standing, in the room's own 320x240 space. This exists to make "does he
  // honour the floor the child drew" a question with a NUMERIC answer: sample this, test each
  // sample against the scene's floor polygon, and the claim is either proven or it is not.
  // Eyeballing screenshots for a pet who is 30px tall cannot settle it.
  {
    extern void bunbun_actor_snapshot(int *bx, int *by, int *cx, int *cy, int *cphase);
    int bx = 0, by = 0, cx = 0, cy = 0, cphase = 0;
    bunbun_actor_snapshot(&bx, &by, &cx, &cy, &cphase);
    cJSON_AddNumberToObject(info, "bun_x", bx);
    cJSON_AddNumberToObject(info, "bun_y", by);
    cJSON_AddNumberToObject(info, "cat_x", cx);
    cJSON_AddNumberToObject(info, "cat_y", cy);
    cJSON_AddNumberToObject(info, "cat_phase", cphase);
  }
  {
    extern void bunbun_perf_snapshot(float *fps, int *draw_max_ms, int *iter_max_ms);
    float fps = 0.0f;
    int draw_ms = 0, iter_ms = 0;
    bunbun_perf_snapshot(&fps, &draw_ms, &iter_ms);
    cJSON_AddNumberToObject(info, "fps", fps);
    cJSON_AddNumberToObject(info, "draw_max_ms", draw_ms);
    cJSON_AddNumberToObject(info, "iter_max_ms", iter_ms);
    extern void bunbun_perf_stages(int *pak_us, int *pix_us, int *push_us);
    int pak_us = 0, pix_us = 0, push_us = 0;
    bunbun_perf_stages(&pak_us, &pix_us, &push_us);
    cJSON_AddNumberToObject(info, "t_pak_us", pak_us);
    cJSON_AddNumberToObject(info, "t_pix_us", pix_us);
    cJSON_AddNumberToObject(info, "t_push_us", push_us);
  }
  // WHERE IT DIED, if it died. Unlike the older "crumb" this one carries a magic word, so
  // bc_valid distinguishes a real stamp from uninitialised RTC memory — which is exactly what
  // made the old one useless when it read back values nothing ever writes.
  {
    extern void bunbun_bc_snapshot(int *valid, int *where, unsigned *seq);
    int bc_valid = 0, bc_where = 0;
    unsigned bc_seq = 0;
    bunbun_bc_snapshot(&bc_valid, &bc_where, &bc_seq);
    cJSON_AddBoolToObject(info, "bc_valid", bc_valid);
    cJSON_AddNumberToObject(info, "bc_where", bc_where);
    cJSON_AddNumberToObject(info, "bc_seq", (double)bc_seq);
  }
  // Last wish-recording outcome (0 none/ok, 1 mic, 2 memory, 3 too-short,
  // 4 storage) — so the mic can be debugged remotely on the canary.
  cJSON_AddNumberToObject(info, "wish_last_fail", (double)wish_recorder_fail_reason());
  cJSON_AddBoolToObject(info, "wish_recording", wish_recorder_active());

  // WiFi link diagnostics (only meaningful when associated as STA)
  if (wifi_connected) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
      char ssid_buf[33];
      size_t slen = strnlen((const char *)ap.ssid, sizeof(ap.ssid));
      if (slen > sizeof(ssid_buf) - 1) {
        slen = sizeof(ssid_buf) - 1;
      }
      memcpy(ssid_buf, ap.ssid, slen);
      ssid_buf[slen] = '\0';
      char bssid_buf[18];
      snprintf(bssid_buf, sizeof(bssid_buf), "%02x:%02x:%02x:%02x:%02x:%02x",
               ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4],
               ap.bssid[5]);
      const char *phy = "?";
      if (ap.phy_11n) {
        phy = "11n";
      } else if (ap.phy_11g) {
        phy = "11g";
      } else if (ap.phy_11b) {
        phy = "11b";
      } else if (ap.phy_lr) {
        phy = "LR";
      }
      cJSON_AddStringToObject(info, "wifi_ssid", ssid_buf);
      cJSON_AddStringToObject(info, "wifi_bssid", bssid_buf);
      cJSON_AddNumberToObject(info, "wifi_rssi", ap.rssi);
      cJSON_AddNumberToObject(info, "wifi_channel", ap.primary);
      cJSON_AddStringToObject(info, "wifi_phy", phy);
    }
  }
  const esp_app_desc_t *app_desc = esp_app_get_description();
  cJSON_AddStringToObject(info, "firmware_version", app_desc->version);
#ifdef CONFIG_DAC_TAS58XX
  cJSON_AddBoolToObject(info, "eq_supported", true);
#else
  cJSON_AddBoolToObject(info, "eq_supported", false);
#endif

  cJSON_AddItemToObject(json, "info", info);
  cJSON_AddBoolToObject(json, "success", true);

  char *json_str = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);

  return ESP_OK;
}

static esp_err_t system_restart_handler(httpd_req_t *req) {
  cors_allow(req);
  cJSON *json = cJSON_CreateObject();
  cJSON_AddBoolToObject(json, "success", true);

  char *json_str = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);

  ESP_LOGI(TAG, "Restart requested via web interface");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();

  return ESP_OK;
}

/* ================================================================== */
/*  SPIFFS File Management API                                         */
/* ================================================================== */

// Allowed path prefixes for file upload (prevent writes outside SPIFFS)
static const char *ALLOWED_PREFIXES[] = {"/spiffs/"};

static bool is_path_allowed(const char *path) {
  for (int i = 0; i < sizeof(ALLOWED_PREFIXES) / sizeof(ALLOWED_PREFIXES[0]);
       i++) {
    if (strncmp(path, ALLOWED_PREFIXES[i], strlen(ALLOWED_PREFIXES[i])) == 0) {
      // Reject path traversal
      if (strstr(path, "..") != NULL) {
        return false;
      }
      return true;
    }
  }
  return false;
}

// Flight recorder for field silence: everything that decides whether sound
// exists, in one URL, readable from a phone while the problem is happening.
static esp_err_t debug_audio_handler(httpd_req_t *req) {
  char es[256] = "n/a";
#ifdef CONFIG_DAC_ES8311
  dac_es8311_reg_dump(es, sizeof(es));
#endif
  int mute_lvl = -1;
#ifdef CONFIG_MUTE_GPIO
  if (CONFIG_MUTE_GPIO >= 0) {
    mute_lvl = gpio_get_level(CONFIG_MUTE_GPIO);
  }
#endif
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  uint32_t ls = audio_output_last_stream_ms();
  char probe[256] = "";
  wish_recorder_probe_stats(probe, sizeof(probe));
  char out[768];
  snprintf(out, sizeof(out),
           "{\"uptime_s\":%u,\"stream_ms_ago\":%d,\"receiver_is_playing\":%s,"
           "\"free_internal\":%u,\"largest_internal\":%u,\"mute_gpio\":%d,"
           "\"es8311\":\"%s\",\"wish_probe\":\"%s\"}",
           (unsigned)(now_ms / 1000), ls ? (int)(now_ms - ls) : -1,
           audio_receiver_is_playing() ? "true" : "false",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           mute_lvl, es, probe);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// ---- W-018: music over WiFi, so gift cases can seal the SD slot ----
// The bunbun side owns the card (single-owner invariant); these handlers
// only collect bytes into PSRAM and talk to its mailbox. Hush's guardrails:
// .mp3 only, 12MB cap, one flat directory, sanitized names, LAN only (which
// is the family's front door — same trust AirPlay already grants).
extern int bunbun_music_card_present(void);
extern int bunbun_music_upload_begin(const char *name, size_t len);
extern unsigned char *bunbun_music_upload_buf(void);
extern void bunbun_music_upload_commit(void);
extern void bunbun_music_upload_abort(void);
extern int bunbun_music_upload_state(void);
extern void bunbun_music_upload_ack(void);
extern const char *bunbun_music_upload_err(void);
extern int bunbun_music_delete(const char *name);
extern int bunbun_music_list_json(char *out, size_t cap);
extern int bunbun_music_playing_now(void);

// Served from firmware (not SPIFFS) so it rides every OTA — the W-016
// lesson: pages on the storage partition strand old copies on the fleet.
static const char MUSIC_PAGE[] =
"<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>bunbun music</title><style>"
"body{background:#f4eede;color:#2b2620;font:16px Georgia,serif;margin:0;padding:24px 12px}"
".w{max-width:520px;margin:0 auto}h1{font-family:Consolas,monospace;font-size:22px;text-align:center}"
"h1 b{color:#d97a1f}.c{background:#fdfaf1;border:1px solid #e3dac4;border-radius:12px;padding:16px 18px;margin:12px 0}"
".t{display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:1px solid #e3dac4;font-size:14px}"
"button{background:#d97a1f;color:#fdfaf1;border:0;border-radius:6px;padding:6px 12px;font:bold 12px Consolas,monospace;cursor:pointer}"
"button.x{background:#a29a8b}#s{font:12px Consolas,monospace;color:#837a68;text-align:center;min-height:18px}"
"input[type=file]{width:100%;font:13px Consolas,monospace}"
"</style></head><body><div class=w>"
"<h1><b>((</b> bunbun music <b>))</b></h1>"
"<div class=c><input type=file id=f accept=.mp3><br><br>"
"<button onclick='up()'>add song to bunbun</button> <span id=s></span></div>"
"<div class=c id=list>loading...</div>"
"<div class=c><button class=x onclick='usb()'>USB mode: plug into a computer</button>"
"<div style='font:12px Consolas,monospace;color:#837a68;margin-top:8px'>"
"bunbun reboots as a USB drive - drag songs on and off, eject, then tap its screen to wake it</div></div>"
"<script>"
"function esc(s){return s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}"
"async function load(){let r=await fetch('/api/music/list');let j=await r.json();"
"let h=j.card?(j.tracks.length?'':'<i>no songs yet</i>'):'<i>no card in this bunbun</i>';"
"for(let t of j.tracks){h+=`<div class=t><span>${esc(t)}</span><button class=x onclick=\"del('${esc(t)}')\">remove</button></div>`}"
"document.getElementById('list').innerHTML=h}"
"async function del(n){if(!confirm('remove '+n+'?'))return;"
"await fetch('/api/music/delete?name='+encodeURIComponent(n),{method:'POST'});setTimeout(load,800)}"
"async function usb(){if(!confirm('bunbun will stop and become a USB drive until its screen is tapped. go?'))return;"
"let r=await fetch('/api/usbmode',{method:'POST'});let j=await r.json();"
"if(!j.ok){alert(j.error);return}"
"document.body.innerHTML='<div style=\\'text-align:center;padding-top:40vh;font:16px Georgia\\'>bunbun is a USB drive now - tap its screen when done</div>'}"
"async function up(){let f=document.getElementById('f').files[0];let s=document.getElementById('s');"
"if(!f){s.textContent='pick an mp3 first';return}"
"if(!/\\.mp3$/i.test(f.name)){s.textContent='mp3 only';return}"
"s.textContent='sending to bunbun...';"
"let r=await fetch('/api/music/upload?name='+encodeURIComponent(f.name),{method:'POST',body:f});"
"let j=await r.json();s.textContent=j.ok?'saved! bunbun is rescanning':'failed: '+j.error;setTimeout(load,1200)}"
"load();"
"</script></div></body></html>";

static esp_err_t music_page_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, MUSIC_PAGE, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t music_list_handler(httpd_req_t *req) {
  char out[2048];
  bunbun_music_list_json(out, sizeof(out));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, out);
  return ESP_OK;
}

// name= from query, url-decoded IS NOT done by httpd; accept %20 by decoding
// just enough, then whitelist. A name that survives: letters, digits,
// space, dot, dash, underscore, ends .mp3, no leading dot.
static bool music_name_ok(char *name) {
  size_t len = strlen(name);
  if (len < 5 || len > 70 || name[0] == '.') {
    return false;
  }
  if (strcasecmp(name + len - 4, ".mp3") != 0) {
    return false;
  }
  for (char *c = name; *c; c++) {
    if (!isalnum((unsigned char)*c) && !strchr(" ._-()[]&'", *c)) {
      return false;
    }
  }
  return true;
}

static void music_url_decode(char *s) {
  char *o = s;
  while (*s) {
    if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
      char h[3] = {s[1], s[2], 0};
      *o++ = (char)strtol(h, NULL, 16);
      s += 3;
    } else if (*s == '+') {
      *o++ = ' ';
      s++;
    } else {
      *o++ = *s++;
    }
  }
  *o = 0;
}

static esp_err_t music_upload_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  // AirPlay outranks the mailman, and so does the SD player: an upload
  // while anything is audible would fight the very stream it feeds.
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  uint32_t ls = audio_output_last_stream_ms();
  if ((ls && now_ms - ls < 5000) || bunbun_music_playing_now()) {
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"turn the music off first\"}");
    return ESP_OK;
  }
  char q[160] = {0}, name[96] = {0};
  httpd_req_get_url_query_str(req, q, sizeof(q));
  if (httpd_query_key_value(q, "name", name, sizeof(name)) != ESP_OK) {
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no name\"}");
    return ESP_OK;
  }
  music_url_decode(name);
  if (!music_name_ok(name)) {
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"that name won't fit on the shelf\"}");
    return ESP_OK;
  }
  size_t len = req->content_len;
  int rc = bunbun_music_upload_begin(name, len);
  if (rc != 0) {
    const char *why = (rc == -1) ? "another song is still arriving"
                      : (rc == -2) ? "no card in this bunbun"
                      : (rc == -3) ? "that file is too big (12MB max)"
                                   : "bunbun's memory is full right now";
    char out[96];
    snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"%s\"}", why);
    httpd_resp_sendstr(req, out);
    return ESP_OK;
  }
  unsigned char *buf = bunbun_music_upload_buf();
  size_t got = 0;
  while (got < len) {
    int n = httpd_req_recv(req, (char *)buf + got, len - got);
    if (n <= 0) {
      bunbun_music_upload_abort();
      httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"the song got lost on the way\"}");
      return ESP_OK;
    }
    got += (size_t)n;
  }
  bunbun_music_upload_commit();
  // The card write is a few seconds; wait it out so the page gets a verdict.
  for (int i = 0; i < 300; i++) {
    int st = bunbun_music_upload_state();
    if (st == 3 || st == 4) {
      char out[128];
      snprintf(out, sizeof(out), "{\"ok\":%s,\"error\":\"%s\"}",
               st == 3 ? "true" : "false",
               st == 3 ? "" : bunbun_music_upload_err());
      bunbun_music_upload_ack();
      httpd_resp_sendstr(req, out);
      return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"the card is taking too long\"}");
  return ESP_OK;
}

static esp_err_t music_delete_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  char q[160] = {0}, name[96] = {0};
  httpd_req_get_url_query_str(req, q, sizeof(q));
  if (httpd_query_key_value(q, "name", name, sizeof(name)) != ESP_OK) {
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no name\"}");
    return ESP_OK;
  }
  music_url_decode(name);
  if (!music_name_ok(name)) {
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad name\"}");
    return ESP_OK;
  }
  bunbun_music_delete(name);
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// W-020: arm USB card-reader mode and reboot into it. POST-only; the page
// confirms first. The reply races the restart on purpose — the browser gets
// its answer, then the device becomes a USB drive until tapped awake.
extern void usb_msc_mode_set_flag(bool on);
extern bool usb_msc_mode_available(void);

static void msc_restart_cb(void *arg) { esp_restart(); }

static esp_err_t usbmode_handler(httpd_req_t *req) {
  if (!usb_msc_mode_available()) {
    // The scaffolding shipped ahead of the USB stack (three-flavor TinyUSB
    // collision, W-020 session 2). A button that reboots the pet and
    // delivers nothing taught us this check the hard way, same night.
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"ok\":false,\"error\":\"USB mode isn't in this build yet - "
        "it arrives with the next update\"}");
    return ESP_OK;
  }
  usb_msc_mode_set_flag(true);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req,
      "{\"ok\":true,\"note\":\"rebooting as a USB drive - tap the screen to wake bunbun\"}");
  const esp_timer_create_args_t targs = {.callback = msc_restart_cb,
                                         .name = "msc_reboot"};
  esp_timer_handle_t t;
  if (esp_timer_create(&targs, &t) == ESP_OK) {
    esp_timer_start_once(t, 800 * 1000);  // let the response flush
  }
  return ESP_OK;
}

// Clock provenance + timezone provisioning (W-019). GET reports the taught
// timezone offset and the last reset reason — the two facts that turned
// tonight's "it keeps resetting to 10:12" from an hour of theories into a
// one-line answer. POST {"tz_off_min": -300} teaches the offset the same
// way the family token rides provisioning; {"tz_off_min": null}/out-of-range
// clears it. The bunbun side reads the same NVS key the touch/serial
// learning paths write ("bunbun"/"tzOffMin").
static esp_err_t clock_handler(httpd_req_t *req) {
  if (req->method == HTTP_POST) {
    char buf[96];
    int n = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (n <= 0) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
      return ESP_FAIL;
    }
    buf[n] = '\0';
    cJSON *j = cJSON_Parse(buf);
    if (!j) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
      return ESP_FAIL;
    }
    cJSON *v = cJSON_GetObjectItem(j, "tz_off_min");
    nvs_handle_t h;
    esp_err_t err = nvs_open("bunbun", NVS_READWRITE, &h);
    if (err == ESP_OK) {
      if (cJSON_IsNumber(v) && v->valueint >= -720 && v->valueint <= 840 &&
          v->valueint % 15 == 0) {
        nvs_set_i32(h, "tzOffMin", v->valueint);
      } else {
        nvs_erase_key(h, "tzOffMin"); // anything else = forget the lesson
      }
      nvs_commit(h);
      nvs_close(h);
    }
    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
  }
  int32_t off = 0;
  bool have = false;
  nvs_handle_t h;
  if (nvs_open("bunbun", NVS_READONLY, &h) == ESP_OK) {
    have = (nvs_get_i32(h, "tzOffMin", &off) == ESP_OK);
    nvs_close(h);
  }
  char out[128];
  if (have) {
    snprintf(out, sizeof(out),
             "{\"tz_off_min\":%d,\"reset_reason\":%d}", (int)off,
             (int)esp_reset_reason());
  } else {
    snprintf(out, sizeof(out),
             "{\"tz_off_min\":null,\"reset_reason\":%d}",
             (int)esp_reset_reason());
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, out);
  return ESP_OK;
}

// Debug/canary: POST /api/wish/record triggers a ~15s mic capture, same as
// holding the WISH button — so the mic can be exercised remotely on the canary
// without a person present (Jon 8/12, chasing the good/quiet/static cycle).
static esp_err_t wish_record_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  if (wish_recorder_active()) {
    httpd_resp_sendstr(req, "{\"started\":false,\"busy\":true}");
    return ESP_OK;
  }
  esp_err_t r = wish_recorder_start(NULL);
  httpd_resp_sendstr(req, r == ESP_OK ? "{\"started\":true}"
                                      : "{\"started\":false}");
  return ESP_OK;
}

// Configure the wish uploader's GitHub token: POST {"token":"..."} sets it,
// {"token":""} clears it. GET reports {"configured":bool} and NEVER the token.
static esp_err_t wish_uploader_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    // ?set=<token> provisions from a plain browser URL — the only tool
    // guaranteed to exist next to a bunbun on a hotspot in a moving car.
    // Local HTTP on the device's own subnet; the convenience is worth it.
    char query[256] = {0};
    char tokv[200] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "set", tokv, sizeof(tokv)) == ESP_OK &&
        tokv[0]) {
      esp_err_t err = wish_uploader_set_token(tokv);
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req,
                      err == ESP_OK ? "{\"ok\":true,\"configured\":true}"
                                    : "{\"ok\":false}",
                      HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }
    // Count clips still on the shelf so "is my wish sent?" is one URL.
    int pending = 0;
    DIR *d = opendir("/spiffs/wishes");
    if (d) {
      struct dirent *e;
      while ((e = readdir(d)) != NULL) {
        if (strstr(e->d_name, ".wav")) {
          pending++;
        }
      }
      closedir(d);
    }
    char out[192];
    snprintf(out, sizeof(out),
             "{\"configured\":%s,\"pending\":%d,\"status\":\"%s\"}",
             wish_uploader_configured() ? "true" : "false", pending,
             wish_uploader_status());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  char body[256];
  int n = httpd_req_recv(req, body, sizeof(body) - 1);
  if (n <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  body[n] = '\0';
  cJSON *json = cJSON_Parse(body);
  const cJSON *tok = json ? cJSON_GetObjectItem(json, "token") : NULL;
  if (!tok || !cJSON_IsString(tok)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing token");
    return ESP_FAIL;
  }
  esp_err_t err = wish_uploader_set_token(tok->valuestring);
  cJSON_Delete(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, err == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false}",
                  HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Stream any allowed SPIFFS file back to the caller. Added for the nightly
// wish collector (pulls /spiffs/wishes/*.wav for transcription), but generic:
// GET /api/fs/download?path=/spiffs/...
static esp_err_t fs_download_handler(httpd_req_t *req) {
  cors_allow(req);
  char query[160] = {0};
  char path[128] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path");
    return ESP_FAIL;
  }
  if (!is_path_allowed(path)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Path not allowed");
    return ESP_FAIL;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No such file");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/octet-stream");
  char buf[1024]; // httpd task stack, not a permanent static in internal RAM
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
      fclose(f);
      httpd_resp_send_chunk(req, NULL, 0);
      return ESP_FAIL;
    }
  }
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// Ceiling on a single upload body. Was 64KB, raised 8/15 for the scene
// builder page (/build), which lands somewhere in the 150-400KB range —
// under the old cap the child's own tool was the one thing this endpoint
// could not carry. RAM does NOT scale with this number: the loop below
// streams through a 1KB stack buffer, so a 1MB upload costs exactly what a
// 1KB one does. The cap is there to stop a runaway request, not to protect
// memory, and free space is now checked separately below.
#define FS_UPLOAD_MAX_BYTES ((size_t)1024 * 1024)

// Headroom demanded on top of the payload before we agree to write, and the
// amount asked of the garbage collector. SPIFFS needs slack for metadata and
// for pages it has not erased yet; writing right up to the reported free
// count is how a write comes back short.
#define FS_UPLOAD_SLACK ((size_t)32 * 1024)

static esp_err_t fs_upload_handler(httpd_req_t *req) {
  cors_allow(req);
  // Get target path from query string
  char query[128] = {0};
  char path[64] = {0};

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Missing 'path' query parameter");
    return ESP_FAIL;
  }

  if (!is_path_allowed(path)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Path not allowed");
    return ESP_FAIL;
  }

  if (req->content_len == 0 || req->content_len > FS_UPLOAD_MAX_BYTES) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body required (max 1MB)");
    return ESP_FAIL;
  }

  // Nothing checked free space before 8/15: the handler opened the file,
  // wrote until the filesystem gave up, and still answered success. Refuse
  // up front instead — BEFORE the fopen, because fopen("wb") truncates and a
  // doomed upload must not destroy the page that is already there.
  //
  // Credit whatever the existing file will give back on truncation, then, if
  // it still looks tight, pay down SPIFFS's lazy GC debt and re-measure.
  // Deleted pages are reclaimed lazily — lazily enough that a partition with
  // plenty of logical room short-writes anyway (field, 2026-08-05: full wish
  // clips landed as 128-byte husks and the debt survived reboots). GC only
  // when tight: on a roomy partition esp_spiffs_gc degrades into a
  // full-partition scan that has cost 25s+ per call (2026-08-06).
  {
    struct stat st;
    size_t reclaim =
        (stat(path, &st) == 0 && st.st_size > 0) ? (size_t)st.st_size : 0;
    size_t need = req->content_len + FS_UPLOAD_SLACK;
    size_t fs_total = 0, fs_used = 0;
    spiffs_storage_usage(&fs_total, &fs_used);
    size_t fs_free = (fs_total > fs_used) ? fs_total - fs_used : 0;
    if (fs_free + reclaim < need) {
      spiffs_storage_gc(need);
      spiffs_storage_usage(&fs_total, &fs_used);
      fs_free = (fs_total > fs_used) ? fs_total - fs_used : 0;
    }
    if (fs_free + reclaim < need) {
      ESP_LOGE(TAG, "Refusing %u bytes to %s: %u free (+%u reclaimable)",
               (unsigned)req->content_len, path, (unsigned)fs_free,
               (unsigned)reclaim);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Not enough space left on the device");
      return ESP_FAIL;
    }
  }

  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to create %s", path);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to create file");
    return ESP_FAIL;
  }

  char buf[SPIFFS_CHUNK_SIZE];
  size_t remaining = req->content_len;
  bool rescued = false;
  while (remaining > 0) {
    size_t to_read = remaining < sizeof(buf) ? remaining : sizeof(buf);
    int received = httpd_req_recv(req, buf, to_read);
    if (received <= 0) {
      fclose(f);
      remove(path);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Receive failed");
      return ESP_FAIL;
    }
    // fwrite's return value was IGNORED here until 8/15. A short write on a
    // GC-starved SPIFFS left a truncated file on flash behind a cheerful
    // {"success":true} — the 2026-08-05 husk failure exactly, except the
    // uploader was told everything was fine. Give the filesystem one chance
    // to reclaim, then fail LOUDLY and take the husk with us: a half-written
    // page is worse than no page, because nothing downstream can tell.
    size_t wrote = fwrite(buf, 1, (size_t)received, f);
    if (wrote < (size_t)received && !rescued) {
      rescued = true;
      clearerr(f); // a short fwrite latches the stream error flag
      spiffs_storage_gc(remaining + FS_UPLOAD_SLACK);
      wrote += fwrite(buf + wrote, 1, (size_t)received - wrote, f);
    }
    if (wrote < (size_t)received) {
      ESP_LOGE(TAG, "Short write to %s (%u of %d bytes) - husk deleted", path,
               (unsigned)wrote, received);
      clearerr(f);
      fclose(f);
      remove(path);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Storage refused the write - nothing was saved");
      return ESP_FAIL;
    }
    remaining -= (size_t)received;
  }
  // fclose flushes; a failure here means the tail never reached flash.
  if (fclose(f) != 0) {
    ESP_LOGE(TAG, "Flush of %s failed - husk deleted", path);
    remove(path);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Storage refused the write - nothing was saved");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Uploaded %u bytes to %s", (unsigned)req->content_len, path);

  cJSON *json = cJSON_CreateObject();
  cJSON_AddBoolToObject(json, "success", true);
  cJSON_AddNumberToObject(json, "size", (double)req->content_len);
  cJSON_AddStringToObject(json, "path", path);
  char *json_str = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  return ESP_OK;
}

static esp_err_t fs_delete_handler(httpd_req_t *req) {
  char query[128] = {0};
  char path[64] = {0};

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Missing 'path' query parameter");
    return ESP_FAIL;
  }

  if (!is_path_allowed(path)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Path not allowed");
    return ESP_FAIL;
  }

  cJSON *json = cJSON_CreateObject();
  if (remove(path) == 0) {
    ESP_LOGI(TAG, "Deleted %s", path);
    cJSON_AddBoolToObject(json, "success", true);
  } else {
    cJSON_AddBoolToObject(json, "success", false);
    cJSON_AddStringToObject(json, "error", "File not found");
  }
  char *json_str = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  return ESP_OK;
}

static esp_err_t fs_list_handler(httpd_req_t *req) {
  char query[128] = {0};
  char dir_path[64] = "/spiffs";

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    httpd_query_key_value(query, "dir", dir_path, sizeof(dir_path));
  }

  if (!is_path_allowed(dir_path) && strcmp(dir_path, "/spiffs") != 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Path not allowed");
    return ESP_FAIL;
  }

  DIR *d = opendir(dir_path);
  cJSON *json = cJSON_CreateObject();
  cJSON *files = cJSON_CreateArray();

  if (d) {
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "name", entry->d_name);

      char full_path[320];
      snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
      struct stat st;
      if (stat(full_path, &st) == 0) {
        cJSON_AddNumberToObject(item, "size", (double)st.st_size);
      }
      cJSON_AddItemToArray(files, item);
    }
    closedir(d);
    cJSON_AddBoolToObject(json, "success", true);
  } else {
    cJSON_AddBoolToObject(json, "success", false);
    cJSON_AddStringToObject(json, "error", "Cannot open directory");
  }

  cJSON_AddItemToObject(json, "files", files);
  char *json_str = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  return ESP_OK;
}

/* ================================================================== */
/*  EQ Page + API  (only when TAS58xx DAC is configured)               */
/* ================================================================== */

#ifdef CONFIG_DAC_TAS58XX

static esp_err_t eq_page_handler(httpd_req_t *req) {
  return serve_spiffs_file(req, "/spiffs/www/eq.html", "text/html");
}

static esp_err_t eq_get_handler(httpd_req_t *req) {
  cJSON *json = cJSON_CreateObject();
  cJSON *arr = cJSON_CreateArray();

  float gains[SETTINGS_EQ_BANDS];
  if (settings_get_eq_gains(gains) == ESP_OK) {
    for (int i = 0; i < SETTINGS_EQ_BANDS; i++) {
      cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)gains[i]));
    }
  } else {
    /* No saved EQ — return all zeros (flat) */
    for (int i = 0; i < SETTINGS_EQ_BANDS; i++) {
      cJSON_AddItemToArray(arr, cJSON_CreateNumber(0.0));
    }
  }

  cJSON_AddItemToObject(json, "gains", arr);
  cJSON_AddNumberToObject(json, "bands", SETTINGS_EQ_BANDS);
  cJSON_AddBoolToObject(json, "success", true);

  char *json_str = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  return ESP_OK;
}

static esp_err_t eq_post_handler(httpd_req_t *req) {
  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  content[ret] = '\0';

  cJSON *json = cJSON_Parse(content);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *response = cJSON_CreateObject();
  cJSON *gains_arr = cJSON_GetObjectItem(json, "gains");

  if (gains_arr && cJSON_IsArray(gains_arr) &&
      cJSON_GetArraySize(gains_arr) == SETTINGS_EQ_BANDS) {

    float gains[SETTINGS_EQ_BANDS];
    for (int i = 0; i < SETTINGS_EQ_BANDS; i++) {
      cJSON *item = cJSON_GetArrayItem(gains_arr, i);
      gains[i] = cJSON_IsNumber(item) ? (float)item->valuedouble : 0.0f;
      /* Clamp */
      if (gains[i] > 15.0f) {
        gains[i] = 15.0f;
      }
      if (gains[i] < -15.0f) {
        gains[i] = -15.0f;
      }
    }

    /* Emit event — listeners (settings + DAC) will handle it */
    eq_event_data_t ev_data;
    memcpy(ev_data.all_bands.gains_db, gains, sizeof(gains));
    eq_events_emit(EQ_EVENT_ALL_BANDS_SET, &ev_data);

    cJSON_AddBoolToObject(response, "success", true);
  } else {
    cJSON_AddBoolToObject(response, "success", false);
    cJSON_AddStringToObject(response, "error",
                            "Expected 'gains' array with 15 values");
  }

  char *json_str = cJSON_Print(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  cJSON_Delete(json);
  cJSON_Delete(response);
  return ESP_OK;
}

#endif /* CONFIG_DAC_TAS58XX */

esp_err_t web_server_start(uint16_t port) {
  if (s_server) {
    ESP_LOGW(TAG, "Web server already running");
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
#ifdef CONFIG_BT_ENABLED
  config.max_open_sockets = 2;   // BT: tighter socket budget (LWIP 12)
  config.send_wait_timeout = 10; // BT/WiFi coexistence slows TCP drain
#else
  config.max_open_sockets = 3; // Limit to save lwIP socket slots for AirPlay
#endif
  config.lru_purge_enable = true; // Reclaim stale sockets when all are in use
  config.max_uri_handlers =
      56; // portal + EQ + speedtest + brightness + fs + wish + debug + clock + music
          // + age jump and screenshot (8/14 design review) + /build (8/15).
          //
          // A registration past this number fails SILENTLY — its route just
          // 404s — so the count and the handler list have to move together.
          // Was 42 with 40 in use (39 registered here on a non-TAS58XX board,
          // plus /ws/logs from log_stream_register): two spare slots, which is
          // how many afternoons the next person adding a page would have lost.
          // Raised to 56 for real headroom; each slot is one pointer in
          // httpd's handler array, so the ceiling costs ~4 bytes each.
  config.max_resp_headers = 8;
  // 7680, was 8192: W-019's two clock handlers tipped the 40KB internal-RAM
  // floor by ~190 bytes and the gate refused the build. The deepest httpd
  // paths (OTA upload, fs streaming, system/info JSON) are all exercised by
  // the gate every ship, so a stack squeeze here cannot reach the fleet
  // silently. The real ~2KB reclamation project stays on the council books.
  config.stack_size = 7680;

  esp_err_t err = httpd_start(&s_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(err));
    return err;
  }

  // Register handlers
  httpd_uri_t root_uri = {
      .uri = "/", .method = HTTP_GET, .handler = root_handler};
  httpd_register_uri_handler(s_server, &root_uri);

  httpd_uri_t favicon_uri = {
      .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler};
  httpd_register_uri_handler(s_server, &favicon_uri);

  httpd_uri_t logs_uri = {
      .uri = "/logs", .method = HTTP_GET, .handler = logs_page_handler};
  httpd_register_uri_handler(s_server, &logs_uri);

  httpd_uri_t speedtest_page_uri = {.uri = "/speedtest",
                                    .method = HTTP_GET,
                                    .handler = speedtest_page_handler};
  httpd_register_uri_handler(s_server, &speedtest_page_uri);

  httpd_uri_t builder_page_uri = {
      .uri = "/build", .method = HTTP_GET, .handler = builder_page_handler};
  httpd_register_uri_handler(s_server, &builder_page_uri);

  httpd_uri_t speedtest_ping_uri = {.uri = "/api/speedtest/ping",
                                    .method = HTTP_GET,
                                    .handler = speedtest_ping_handler};
  httpd_register_uri_handler(s_server, &speedtest_ping_uri);

  httpd_uri_t speedtest_dl_uri = {.uri = "/api/speedtest/download",
                                  .method = HTTP_GET,
                                  .handler = speedtest_download_handler};
  httpd_register_uri_handler(s_server, &speedtest_dl_uri);

  httpd_uri_t speedtest_ul_uri = {.uri = "/api/speedtest/upload",
                                  .method = HTTP_POST,
                                  .handler = speedtest_upload_handler};
  httpd_register_uri_handler(s_server, &speedtest_ul_uri);

  httpd_uri_t wifi_scan_uri = {.uri = "/api/wifi/scan",
                               .method = HTTP_GET,
                               .handler = wifi_scan_handler};
  httpd_register_uri_handler(s_server, &wifi_scan_uri);

  httpd_uri_t wifi_config_uri = {.uri = "/api/wifi/config",
                                 .method = HTTP_POST,
                                 .handler = wifi_config_handler};
  httpd_register_uri_handler(s_server, &wifi_config_uri);

  httpd_uri_t device_name_uri = {.uri = "/api/device/name",
                                 .method = HTTP_POST,
                                 .handler = device_name_handler};
  httpd_register_uri_handler(s_server, &device_name_uri);

  httpd_uri_t led_brightness_get_uri = {.uri = "/api/led/brightness",
                                        .method = HTTP_GET,
                                        .handler = led_brightness_get_handler};
  httpd_register_uri_handler(s_server, &led_brightness_get_uri);

  httpd_uri_t led_brightness_post_uri = {.uri = "/api/led/brightness",
                                         .method = HTTP_POST,
                                         .handler =
                                             led_brightness_post_handler};
  httpd_register_uri_handler(s_server, &led_brightness_post_uri);

  httpd_uri_t ota_uri = {.uri = "/api/ota/update",
                         .method = HTTP_POST,
                         .handler = ota_update_handler};
  httpd_register_uri_handler(s_server, &ota_uri);

  httpd_uri_t ota_assets_uri = {.uri = "/api/ota/assets",
                                .method = HTTP_POST,
                                .handler = ota_assets_handler};
  httpd_register_uri_handler(s_server, &ota_assets_uri);

  // the webpage port: read the pak back, and the CORS preflights fetch() insists on
  httpd_uri_t ota_assets_get_uri = {.uri = "/api/ota/assets",
                                    .method = HTTP_GET,
                                    .handler = ota_assets_get_handler};
  httpd_register_uri_handler(s_server, &ota_assets_get_uri);
  static const char *k_cors_uris[] = {"/api/ota/assets", "/api/fs/upload",
                                      "/api/system/restart"};
  for (size_t ci = 0; ci < sizeof(k_cors_uris) / sizeof(k_cors_uris[0]); ci++) {
    httpd_uri_t pre = {.uri = k_cors_uris[ci],
                       .method = HTTP_OPTIONS,
                       .handler = cors_preflight_handler};
    httpd_register_uri_handler(s_server, &pre);
  }

  httpd_uri_t species_get_uri = {.uri = "/api/pet/species",
                                 .method = HTTP_GET,
                                 .handler = pet_species_handler};
  httpd_register_uri_handler(s_server, &species_get_uri);
  httpd_uri_t species_set_uri = {.uri = "/api/pet/species",
                                 .method = HTTP_POST,
                                 .handler = pet_species_handler};
  httpd_register_uri_handler(s_server, &species_set_uri);

  httpd_uri_t debug_age_uri = {.uri = "/api/debug/age",
                               .method = HTTP_GET,
                               .handler = debug_age_handler};
  httpd_register_uri_handler(s_server, &debug_age_uri);

  httpd_uri_t screenshot_uri = {.uri = "/api/screenshot",
                                .method = HTTP_GET,
                                .handler = screenshot_handler};
  httpd_register_uri_handler(s_server, &screenshot_uri);

  httpd_uri_t debug_away_uri = {.uri = "/api/debug/away",
                                .method = HTTP_GET,
                                .handler = debug_away_handler};
  httpd_register_uri_handler(s_server, &debug_away_uri);

  httpd_uri_t debug_cat_uri = {.uri = "/api/debug/cat",
                               .method = HTTP_GET,
                               .handler = debug_cat_handler};
  httpd_register_uri_handler(s_server, &debug_cat_uri);

  httpd_uri_t system_info_uri = {.uri = "/api/system/info",
                                 .method = HTTP_GET,
                                 .handler = system_info_handler};
  httpd_register_uri_handler(s_server, &system_info_uri);

  httpd_uri_t system_restart_uri = {.uri = "/api/system/restart",
                                    .method = HTTP_POST,
                                    .handler = system_restart_handler};
  httpd_register_uri_handler(s_server, &system_restart_uri);

  // File management API
  httpd_uri_t fs_upload_uri = {.uri = "/api/fs/upload",
                               .method = HTTP_POST,
                               .handler = fs_upload_handler};
  httpd_register_uri_handler(s_server, &fs_upload_uri);

  httpd_uri_t fs_download_uri = {.uri = "/api/fs/download",
                                 .method = HTTP_GET,
                                 .handler = fs_download_handler};
  httpd_register_uri_handler(s_server, &fs_download_uri);

  httpd_uri_t debug_audio_uri = {.uri = "/api/debug/audio",
                                 .method = HTTP_GET,
                                 .handler = debug_audio_handler};
  httpd_register_uri_handler(s_server, &debug_audio_uri);

  httpd_uri_t wish_up_get = {.uri = "/api/wish/uploader",
                             .method = HTTP_GET,
                             .handler = wish_uploader_handler};
  httpd_register_uri_handler(s_server, &wish_up_get);
  httpd_uri_t wish_record_post = {.uri = "/api/wish/record",
                                  .method = HTTP_POST,
                                  .handler = wish_record_handler};
  httpd_register_uri_handler(s_server, &wish_record_post);
  httpd_uri_t wish_up_post = {.uri = "/api/wish/uploader",
                              .method = HTTP_POST,
                              .handler = wish_uploader_handler};
  httpd_register_uri_handler(s_server, &wish_up_post);

  httpd_uri_t clock_get = {
      .uri = "/api/clock", .method = HTTP_GET, .handler = clock_handler};
  httpd_register_uri_handler(s_server, &clock_get);
  httpd_uri_t clock_post = {
      .uri = "/api/clock", .method = HTTP_POST, .handler = clock_handler};
  httpd_register_uri_handler(s_server, &clock_post);

  httpd_uri_t music_page = {
      .uri = "/music", .method = HTTP_GET, .handler = music_page_handler};
  httpd_register_uri_handler(s_server, &music_page);
  httpd_uri_t music_list = {.uri = "/api/music/list",
                            .method = HTTP_GET,
                            .handler = music_list_handler};
  httpd_register_uri_handler(s_server, &music_list);
  httpd_uri_t music_up = {.uri = "/api/music/upload",
                          .method = HTTP_POST,
                          .handler = music_upload_handler};
  httpd_register_uri_handler(s_server, &music_up);
  httpd_uri_t music_del = {.uri = "/api/music/delete",
                           .method = HTTP_POST,
                           .handler = music_delete_handler};
  httpd_register_uri_handler(s_server, &music_del);
  httpd_uri_t usbmode = {
      .uri = "/api/usbmode", .method = HTTP_POST, .handler = usbmode_handler};
  httpd_register_uri_handler(s_server, &usbmode);

  httpd_uri_t fs_delete_uri = {.uri = "/api/fs/delete",
                               .method = HTTP_POST,
                               .handler = fs_delete_handler};
  httpd_register_uri_handler(s_server, &fs_delete_uri);

  httpd_uri_t fs_list_uri = {
      .uri = "/api/fs/list", .method = HTTP_GET, .handler = fs_list_handler};
  httpd_register_uri_handler(s_server, &fs_list_uri);

  // Captive portal detection endpoints
  // Apple iOS/macOS
  httpd_uri_t apple_captive1 = {.uri = "/hotspot-detect.html",
                                .method = HTTP_GET,
                                .handler = captive_apple_handler};
  httpd_register_uri_handler(s_server, &apple_captive1);

  httpd_uri_t apple_captive2 = {.uri = "/library/test/success.html",
                                .method = HTTP_GET,
                                .handler = captive_apple_handler};
  httpd_register_uri_handler(s_server, &apple_captive2);

  // Android
  httpd_uri_t android_captive = {.uri = "/generate_204",
                                 .method = HTTP_GET,
                                 .handler = captive_android_handler};
  httpd_register_uri_handler(s_server, &android_captive);

  // Windows
  httpd_uri_t windows_captive = {.uri = "/connecttest.txt",
                                 .method = HTTP_GET,
                                 .handler = captive_windows_handler};
  httpd_register_uri_handler(s_server, &windows_captive);

#ifdef CONFIG_DAC_TAS58XX
  httpd_uri_t eq_page_uri = {
      .uri = "/eq", .method = HTTP_GET, .handler = eq_page_handler};
  httpd_register_uri_handler(s_server, &eq_page_uri);

  httpd_uri_t eq_get_uri = {
      .uri = "/api/eq", .method = HTTP_GET, .handler = eq_get_handler};
  httpd_register_uri_handler(s_server, &eq_get_uri);

  httpd_uri_t eq_post_uri = {
      .uri = "/api/eq", .method = HTTP_POST, .handler = eq_post_handler};
  httpd_register_uri_handler(s_server, &eq_post_uri);
#endif

  log_stream_register(s_server);

  ESP_LOGI(TAG, "Web server started on port %d with captive portal support",
           port);
  return ESP_OK;
}

void web_server_stop(void) {
  if (s_server) {
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "Web server stopped");
  }
}
