// Wish uploader: the moment a wish exists and the device has ANY internet,
// the clip is pushed to the private GitHub repo (contents API) and the local
// copy is deleted. The repo's commit feed becomes the live wish log — "wish
// from bunbun-D0C4" lands within a minute of a kid speaking, from home WiFi,
// a phone hotspot, or grandma's house. Transcription still happens on the
// home PC's nightly pass; this moves the AUDIO and the log entry to realtime.
//
// The token is runtime configuration (NVS), set once per unit via
// POST /api/wish/uploader — never compiled into firmware, so fleet builds
// stay clean and a gifted unit can be tokened (or de-tokened) in the field.

#include "wish_uploader.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "nvs.h"

#include "esp_app_desc.h"
#include "esp_netif.h"

#include "settings.h"
#include "audio_output.h"
#include "fw_update.h"

static const char *TAG = "wish_up";

// ---- PUBLIC BUILD ----
// No wish uploads, no fleet beacon, no GitHub token storage. Wishes still
// record locally (wish_recorder.c is untouched); they simply stay on the
// shelf. Callers link against inert stubs.
void wish_uploader_init(void) {}
esp_err_t wish_uploader_set_token(const char *token) {
  (void)token;
  return ESP_ERR_NOT_SUPPORTED;
}
bool wish_uploader_configured(void) { return false; }
void wish_uploader_poke(void) {}
bool wish_uploader_busy(void) { return false; }
int wish_uploader_pct(void) { return 0; }
int wish_uploader_take_event(void) { return 0; }
const char *wish_uploader_status(void) { return "not in this build"; }
uint32_t wish_up_stack_low(void) { return 0; }
bool wish_uploader_online(void) { return false; }
int wish_uploader_pending(void) { return 0; }

