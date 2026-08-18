#include "spiffs_storage.h"

#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "spiffs";
static bool s_mounted = false;

esp_err_t spiffs_storage_init(void) {
  if (s_mounted) {
    return ESP_OK;
  }

  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage",
      .max_files = 5,
      .format_if_mount_failed = true,
  };

  esp_err_t err = esp_vfs_spiffs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
    return err;
  }

  size_t total = 0, used = 0;
  esp_spiffs_info("storage", &total, &used);
  ESP_LOGI(TAG, "SPIFFS mounted: %zu/%zu bytes used", used, total);

  s_mounted = true;

  // Pay down any garbage-collection debt from the previous boot's churn right
  // now, while nothing is time-sensitive. A unit that spent the day cycling
  // wish clips (write 330KB, upload, delete, repeat) accumulates deleted
  // pages that SPIFFS reclaims lazily — lazily enough that the NEXT clip's
  // write comes back short (field, 2026-08-05: full 15s wishes reduced to
  // 128-byte husks, persisting across reboots).
  err = esp_spiffs_gc("storage", 512 * 1024);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "boot gc: %s (partition genuinely tight)",
             esp_err_to_name(err));
  }

  return ESP_OK;
}

esp_err_t spiffs_storage_gc(size_t need) {
  if (!s_mounted) {
    return ESP_ERR_INVALID_STATE;
  }
  return esp_spiffs_gc("storage", need);
}

void spiffs_storage_usage(size_t *total, size_t *used) {
  size_t t = 0, u = 0;
  if (s_mounted) {
    esp_spiffs_info("storage", &t, &u);
  }
  if (total) {
    *total = t;
  }
  if (used) {
    *used = u;
  }
}
