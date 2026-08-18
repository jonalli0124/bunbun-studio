/**
 * @file board.c
 * @brief ESP32-S3 Generic board implementation
 *
 * Minimal implementation for generic ESP32-S3 dev boards with external I2S DAC.
 * No board-specific initialization required.
 */

#include "iot_board.h"

#include "driver/gpio.h"
#include "esp_log.h"

#if defined(CONFIG_DAC_ES8311)
#include "dac.h"
#include "dac_es8311.h"
#include "driver/i2c_master.h"
#include "settings.h"
#endif

static const char TAG[] = "ESP32S3-Generic";

static bool s_board_initialized = false;

#if defined(CONFIG_DAC_ES8311)
static i2c_master_bus_handle_t s_i2c_dac_bus_handle = NULL;

// Exposed because on this board the codec is NOT the only thing on I2C: the touch panel and the
// DS3231 share the same pins. Whoever creates the bus owns it — a second i2c_new_master_bus()
// on the same port fails with "bus id(0) has already been acquired" — so everyone else has to
// borrow this handle rather than bring up their own.
i2c_master_bus_handle_t board_get_i2c_bus(void) { return s_i2c_dac_bus_handle; }

// The generic board declares I2S pins and a mute GPIO through Kconfig but never registered a
// codec, so dac_register() was never called on it. That left dac_set_volume() a no-op while
// CONFIG_DAC_CONTROLS_VOLUME — which DAC_ES8311=y turns on automatically — had already disabled
// the software attenuator on the grounds that "the DAC handles volume". Volume was therefore
// handled by NOBODY: the phone's slider did nothing and everything played at full scale, which
// is also why it sounded harsh. Registering the codec here closes that gap.
static esp_err_t init_es8311(void) {
  i2c_master_bus_config_t i2c_cfg = {
      .i2c_port = 0,
      .sda_io_num = CONFIG_DAC_I2C_SDA,
      .scl_io_num = CONFIG_DAC_I2C_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  esp_err_t err = i2c_new_master_bus(&i2c_cfg, &s_i2c_dac_bus_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "DAC I2C bus init failed: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(TAG, "DAC I2C bus initialized: sda=%d scl=%d", CONFIG_DAC_I2C_SDA,
           CONFIG_DAC_I2C_SCL);

  dac_register(&dac_es8311_ops);

  err = dac_init(s_i2c_dac_bus_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ES8311 init failed: %s", esp_err_to_name(err));
    return err;
  }

  // The ES8311 powers up at 0 dB until told otherwise, so without this the first track plays at
  // full scale no matter what the phone's slider last said.
  float vol_db;
  if (settings_get_volume(&vol_db) == ESP_OK) {
    dac_set_volume(vol_db);
    ESP_LOGI(TAG, "Restored saved volume: %.1f dB", vol_db);
  }
  ESP_LOGI(TAG, "ES8311 registered and initialized");
  return ESP_OK;
}
#endif

#ifdef CONFIG_MUTE_GPIO
static esp_err_t init_mute_gpio(void) {
  if (CONFIG_MUTE_GPIO < 0) {
    return ESP_OK;
  }

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << CONFIG_MUTE_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure mute GPIO: %s", esp_err_to_name(err));
    return err;
  }

  // Initialize to unmuted state — set opposite of active level
  gpio_set_level(CONFIG_MUTE_GPIO, !CONFIG_MUTE_GPIO_LEVEL);

  ESP_LOGI(TAG, "Mute GPIO %d initialized (active %s, init %s)",
           CONFIG_MUTE_GPIO, CONFIG_MUTE_GPIO_LEVEL ? "high" : "low",
           CONFIG_MUTE_GPIO_LEVEL ? "low" : "high");
  return ESP_OK;
}
#endif

const char *iot_board_get_info(void) {
  return BOARD_NAME;
}

bool iot_board_is_init(void) {
  return s_board_initialized;
}

board_res_handle_t iot_board_get_handle(int id) {
  (void)id;
  return NULL;
}

esp_err_t iot_board_init(void) {
  if (s_board_initialized) {
    ESP_LOGW(TAG, "Board already initialized");
    return ESP_OK;
  }

#ifdef CONFIG_MUTE_GPIO
  esp_err_t err = init_mute_gpio();
  if (err != ESP_OK) {
    return err;
  }
#endif

#if defined(CONFIG_DAC_ES8311)
  err = init_es8311();
  if (err != ESP_OK) {
    return err;
  }
#endif

  s_board_initialized = true;
  ESP_LOGI(TAG, "Generic board initialized");
  return ESP_OK;
}

esp_err_t iot_board_deinit(void) {
  s_board_initialized = false;
  return ESP_OK;
}
