#include "settings.h"

#include "dac.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include <stdio.h>
#include "nvs.h"
#include <string.h>

static const char *TAG = "settings";

#define NVS_NAMESPACE  "airplay"
#define NVS_KEY_VOLUME "volume_db"
#ifdef CONFIG_BT_A2DP_ENABLE
#define NVS_KEY_BT_VOLUME "bt_vol"
#endif
#define NVS_KEY_WIFI_SSID      "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD  "wifi_pass"
#define NVS_KEY_DEVICE_NAME    "device_name"
#define NVS_KEY_EQ_GAINS       "eq_gains"
#define NVS_KEY_LED_BRIGHTNESS "led_bright"

#define MAX_WIFI_SSID_LEN     32
#define MAX_WIFI_PASSWORD_LEN 64
#define MAX_DEVICE_NAME_LEN   64

// Cached values  (defaults = 50 %)
static float g_volume_db = -15.0f;
static bool g_volume_loaded = false;

#ifdef CONFIG_BT_A2DP_ENABLE
static uint8_t g_bt_volume = 64; /* default: 50 % */
static bool g_bt_volume_loaded = false;
#endif

static float g_eq_gains[SETTINGS_EQ_BANDS];
static bool g_eq_loaded = false;

esp_err_t settings_init(void) {
  // Load volume on init
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
  if (err == ESP_OK) {
    int32_t vol_fixed;
    err = nvs_get_i32(nvs, NVS_KEY_VOLUME, &vol_fixed);
    if (err == ESP_OK) {
      g_volume_db = (float)vol_fixed / 100.0f;
      g_volume_loaded = true;
      ESP_LOGI(TAG, "Loaded volume: %.2f dB", g_volume_db);
    }

    /* Load EQ gains blob */
    size_t eq_size = sizeof(g_eq_gains);
    err = nvs_get_blob(nvs, NVS_KEY_EQ_GAINS, g_eq_gains, &eq_size);
    if (err == ESP_OK && eq_size == sizeof(g_eq_gains)) {
      g_eq_loaded = true;
      ESP_LOGI(TAG, "Loaded EQ gains (%d bands)", SETTINGS_EQ_BANDS);
    }

    nvs_close(nvs);
  }

  return ESP_OK;
}

esp_err_t settings_get_volume(float *volume_db) {
  if (!volume_db) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!g_volume_loaded) {
    return ESP_ERR_NOT_FOUND;
  }

  *volume_db = g_volume_db;
  return ESP_OK;
}

esp_err_t settings_set_volume(float volume_db) {
  // Skip if unchanged
  if (g_volume_loaded && volume_db == g_volume_db) {
    return ESP_OK;
  }

  dac_set_volume(volume_db);

  g_volume_db = volume_db;
  g_volume_loaded = true;
  return ESP_OK;
}

/* A TASK WHOSE STACK IS IN PSRAM MAY NOT TOUCH FLASH.
 *
 * Writing NVS disables the flash cache, and PSRAM is reached THROUGH that cache - so a task
 * running on a PSRAM stack cannot execute while the write is in progress, cannot even return.
 * IDF asserts on it rather than let it fault:
 *
 *   assert failed: spi_flash_disable_interrupts_caches_and_other_cpu
 *                  cache_utils.c:127 (esp_task_stack_is_sane_cache_disabled())
 *
 * That is W-096, open since 2026-08-16 and quarantining the whole 0.1.291 line. It fires on
 * EVERY AirPlay disconnect, because rtsp_conn_free() persists the volume and client_task's
 * stack is MALLOC_CAP_SPIRAM (rtsp_server.c:503). Jon: "connecting and disconnecting airplay
 * is almost a guaranty to cause a crash panic." It was not almost - it was every time this
 * path ran.
 *
 * Fixed here rather than at the one call site, because volume is unlikely to be the only
 * thing ever persisted from a networking task: the check is on the CALLER's stack, so any
 * future caller is covered too. A zero-delay esp_timer callback runs on the esp_timer task,
 * which has an ordinary internal stack and may touch flash freely.
 *
 * The other two PSRAM stacks - s_recv_stack_psram and s_ctrl_stack_psram in
 * audio_stream_realtime.c - are exposed to exactly the same hazard on any flash path they can
 * reach. This makes THIS path safe; it does not audit those. */
static void volume_flush_cb(void *arg);
static bool caller_is_on_external_stack(void);
static esp_err_t volume_write_now(void);
static esp_timer_handle_t s_volume_flush = NULL;

esp_err_t settings_persist_volume(void) {
  if (!g_volume_loaded) {
    return ESP_OK;
  }
  if (caller_is_on_external_stack()) {
    if (!s_volume_flush) {
      const esp_timer_create_args_t a = {.callback = volume_flush_cb,
                                         .name = "volflush",
                                         .dispatch_method = ESP_TIMER_TASK};
      if (esp_timer_create(&a, &s_volume_flush) != ESP_OK) {
        return ESP_ERR_NO_MEM;     /* better to lose a volume than to panic the device */
      }
    }
    esp_timer_stop(s_volume_flush);          /* coalesce a burst of calls into one write */
    return esp_timer_start_once(s_volume_flush, 0);
  }
  return volume_write_now();
}

static void volume_flush_cb(void *arg) {
  (void)arg;
  volume_write_now();
}

static bool caller_is_on_external_stack(void) {
  /* Only its ADDRESS is wanted - a local lives on the caller's stack, so where it sits tells
   * us which memory that stack is in. Initialised because -Werror=maybe-uninitialized cannot
   * see that the value is never read. */
  volatile int probe = 0;
  return esp_ptr_external_ram((void *)&probe);
}

static esp_err_t volume_write_now(void) {
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
  }

  int32_t vol_fixed = (int32_t)(g_volume_db * 100.0f);
  err = nvs_set_i32(nvs, NVS_KEY_VOLUME, vol_fixed);
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }

  nvs_close(nvs);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Persisted volume: %.2f dB", g_volume_db);
  } else {
    ESP_LOGE(TAG, "Failed to persist volume: %s", esp_err_to_name(err));
  }

  return err;
}

#ifdef CONFIG_BT_A2DP_ENABLE
esp_err_t settings_get_bt_volume(uint8_t *volume) {
  if (!volume) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!g_bt_volume_loaded) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
      return err;
    }
    err = nvs_get_u8(nvs, NVS_KEY_BT_VOLUME, &g_bt_volume);
    nvs_close(nvs);
    if (err != ESP_OK) {
      return err;
    }
    g_bt_volume_loaded = true;
  }
  *volume = g_bt_volume;
  return ESP_OK;
}

esp_err_t settings_set_bt_volume(uint8_t volume) {
  if (g_bt_volume_loaded && volume == g_bt_volume) {
    return ESP_OK;
  }

  g_bt_volume = volume;
  g_bt_volume_loaded = true;
  return ESP_OK;
}

esp_err_t settings_persist_bt_volume(void) {
  if (!g_bt_volume_loaded) {
    return ESP_OK;
  }

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_u8(nvs, NVS_KEY_BT_VOLUME, g_bt_volume);
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }
  nvs_close(nvs);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Persisted BT volume: %d/127", g_bt_volume);
  } else {
    ESP_LOGE(TAG, "Failed to persist BT volume: %s", esp_err_to_name(err));
  }
  return err;
}
#endif

esp_err_t settings_get_wifi_ssid(char *ssid, size_t len) {
  if (!ssid || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
  if (err != ESP_OK) {
    return ESP_ERR_NOT_FOUND;
  }

  size_t required_size = len;
  err = nvs_get_str(nvs, NVS_KEY_WIFI_SSID, ssid, &required_size);
  nvs_close(nvs);

  if (err == ESP_OK && required_size > len) {
    return ESP_ERR_NVS_INVALID_LENGTH;
  }

  return err;
}

esp_err_t settings_get_wifi_password(char *password, size_t len) {
  if (!password || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
  if (err != ESP_OK) {
    return ESP_ERR_NOT_FOUND;
  }

  size_t required_size = len;
  err = nvs_get_str(nvs, NVS_KEY_WIFI_PASSWORD, password, &required_size);
  nvs_close(nvs);

  if (err == ESP_OK && required_size > len) {
    return ESP_ERR_NVS_INVALID_LENGTH;
  }

  return err;
}

esp_err_t settings_set_wifi_credentials(const char *ssid,
                                        const char *password) {
  if (!ssid || strlen(ssid) == 0 || strlen(ssid) > MAX_WIFI_SSID_LEN) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!password || strlen(password) > MAX_WIFI_PASSWORD_LEN) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_str(nvs, NVS_KEY_WIFI_SSID, ssid);
  if (err == ESP_OK) {
    err = nvs_set_str(nvs, NVS_KEY_WIFI_PASSWORD, password);
  }
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }

  nvs_close(nvs);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved WiFi credentials: SSID=%s", ssid);
  } else {
    ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
  }

  return err;
}

bool settings_has_wifi_credentials(void) {
  char ssid[MAX_WIFI_SSID_LEN + 1];
  return settings_get_wifi_ssid(ssid, sizeof(ssid)) == ESP_OK;
}

// The device name is cached in RAM after the first read (8/12). The RTSP
// client task reads it during SETUP, and that task now runs on a PSRAM stack
// - from which an NVS/flash op is an instant assert. Caching means the flash
// read happens exactly once, on whatever internal-stack context asks first
// (mDNS advertise at boot), and every later reader (including the PSRAM-stack
// client task) gets it from RAM. settings_set_device_name refreshes the cache.
static char g_name_cache[64];
static bool g_name_cached = false;

esp_err_t settings_get_device_name(char *name, size_t len) {
  if (!name || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  if (g_name_cached) {
    strlcpy(name, g_name_cache, len);
    return ESP_OK;
  }

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
  if (err == ESP_OK) {
    size_t required_size = len;
    err = nvs_get_str(nvs, NVS_KEY_DEVICE_NAME, name, &required_size);
    nvs_close(nvs);

    if (err == ESP_OK && required_size <= len) {
      strlcpy(g_name_cache, name, sizeof(g_name_cache));
      g_name_cached = true;
      return ESP_OK;
    }
  }

  // No stored name: default to "bunbun-XXXX", the last two MAC bytes in hex. With several of
  // these on one network — the whole point of the stereo-bunbuns plan — identical default names
  // are indistinguishable in Control Centre, and renaming after the first advertisement does not
  // propagate reliably because senders cache mDNS names. Unique-by-silicon from the very first
  // advertisement; eFuse, so it works before the radio is up. A name set through the web UI
  // still wins — this is only the default.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(name, len, "bunbun-%02X%02X", mac[4], mac[5]);
  return ESP_OK;
}

esp_err_t settings_set_device_name(const char *name) {
  if (!name || strlen(name) == 0 || strlen(name) > MAX_DEVICE_NAME_LEN) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_str(nvs, NVS_KEY_DEVICE_NAME, name);
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }

  nvs_close(nvs);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved device name: %s", name);
    strlcpy(g_name_cache, name, sizeof(g_name_cache));   // keep the RAM cache honest
    g_name_cached = true;
  } else {
    ESP_LOGE(TAG, "Failed to save device name: %s", esp_err_to_name(err));
  }

  return err;
}

/* ================================================================== */
/*  LED Brightness                                                     */
/* ================================================================== */

esp_err_t settings_get_led_brightness(uint8_t *brightness) {
  if (!brightness) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
  if (err != ESP_OK) {
    return ESP_ERR_NOT_FOUND;
  }

  err = nvs_get_u8(nvs, NVS_KEY_LED_BRIGHTNESS, brightness);
  nvs_close(nvs);
  return err;
}

esp_err_t settings_set_led_brightness(uint8_t brightness) {
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_u8(nvs, NVS_KEY_LED_BRIGHTNESS, brightness);
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }
  nvs_close(nvs);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved LED brightness: %d", brightness);
  } else {
    ESP_LOGE(TAG, "Failed to save LED brightness: %s", esp_err_to_name(err));
  }
  return err;
}

/* ================================================================== */
/*  EQ Gains                                                           */
/* ================================================================== */

esp_err_t settings_get_eq_gains(float gains_db[SETTINGS_EQ_BANDS]) {
  if (!gains_db) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!g_eq_loaded) {
    return ESP_ERR_NOT_FOUND;
  }

  memcpy(gains_db, g_eq_gains, sizeof(g_eq_gains));
  return ESP_OK;
}

esp_err_t settings_set_eq_gains(const float gains_db[SETTINGS_EQ_BANDS]) {
  if (!gains_db) {
    return ESP_ERR_INVALID_ARG;
  }

  /* Skip write if unchanged (compare element-by-element to avoid
     memcmp on floats, which is flagged by
     bugprone-suspicious-memory-comparison) */
  if (g_eq_loaded) {
    bool unchanged = true;
    for (int i = 0; i < SETTINGS_EQ_BANDS; i++) {
      if (gains_db[i] != g_eq_gains[i]) {
        unchanged = false;
        break;
      }
    }
    if (unchanged) {
      return ESP_OK;
    }
  }

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_blob(nvs, NVS_KEY_EQ_GAINS, gains_db,
                     sizeof(float) * SETTINGS_EQ_BANDS);
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }

  nvs_close(nvs);

  if (err == ESP_OK) {
    memcpy(g_eq_gains, gains_db, sizeof(g_eq_gains));
    g_eq_loaded = true;
    ESP_LOGI(TAG, "Saved EQ gains (%d bands)", SETTINGS_EQ_BANDS);
  } else {
    ESP_LOGE(TAG, "Failed to save EQ gains: %s", esp_err_to_name(err));
  }

  return err;
}

esp_err_t settings_clear_eq(void) {
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_erase_key(nvs, NVS_KEY_EQ_GAINS);
  if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_commit(nvs);
    memset(g_eq_gains, 0, sizeof(g_eq_gains));
    g_eq_loaded = false;
    err = ESP_OK;
  }

  nvs_close(nvs);
  return err;
}

bool settings_has_eq(void) {
  return g_eq_loaded;
}
