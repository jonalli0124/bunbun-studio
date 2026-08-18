#pragma once

#include <stddef.h>

#include "esp_err.h"

/**
 * Mount the SPIFFS "storage" partition at /spiffs.
 * Safe to call multiple times — returns ESP_OK if already mounted.
 */
esp_err_t spiffs_storage_init(void);

/**
 * Force SPIFFS to reclaim at least `need` bytes of deleted-but-not-erased
 * pages. SPIFFS garbage-collects lazily; under heavy write/delete churn a
 * large write starts coming back short even though the logical free space is
 * fine. Call before any large write. Returns ESP_OK when the space is
 * available (including "it already was").
 */
esp_err_t spiffs_storage_gc(size_t need);

/** Filesystem pressure, for the flight recorder. Either arg may be NULL. */
void spiffs_storage_usage(size_t *total, size_t *used);
