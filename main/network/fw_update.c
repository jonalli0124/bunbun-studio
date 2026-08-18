// Firmware self-update from the private GitHub repo.
//
// The nightly pipeline (after the regression gate passes and Jon approves)
// publishes an approved build to the repo as firmware/approved.json + the
// .bin it names. Any device with the wish-uploader token can then update
// itself from ANY internet connection — home, hotspot, another house — via
// the UPDATE button on the SND screen or the hourly quiet check.
//
// Status is a small state machine the UI polls; every outcome ends in an
// unambiguous state (UP_TO_DATE / SUCCESS-then-reboot / FAILED with reason),
// because the button's whole job is that nobody wonders what happened.

#include "fw_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

static const char *TAG = "fw_update";

// ---- PUBLIC BUILD ----
// No fleet: no manifest polling, no auto asset fetch, no repo URLs or token
// reads in the image. Every caller keeps linking against these inert stubs;
// updates arrive over USB or the web interface's manual /api/ota/update.
// The crash-breadcrumb RTC word stays real - fw_selfcheck snapshots it.
RTC_NOINIT_ATTR uint32_t g_fw_crumb;
static uint32_t s_crumb_prev;
void fw_crumb_snapshot(void) { s_crumb_prev = g_fw_crumb; g_fw_crumb = 0; }
uint32_t fw_crumb_prev(void) { return s_crumb_prev; }
esp_err_t fw_update_start(void) { return ESP_ERR_NOT_SUPPORTED; }
fw_update_state_t fw_update_state(void) { return FW_IDLE; }
int fw_update_pct(void) { return 0; }
const char *fw_update_reason(void) { return "updates: USB or /api/ota/update"; }
void fw_update_ack(void) {}
void fw_assets_report_missing(void) {}
bool fw_assets_repair_wanted(void) { return false; }
bool fw_assets_writing(void) { return false; }

