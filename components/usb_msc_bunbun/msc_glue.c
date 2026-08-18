// W-020: the eight MSC callbacks, mapped straight onto raw SD sectors.
// PHY + device-task bring-up cribbed from usb_device_uac (the in-tree proof
// that raw tinyusb coexists with Arduino-as-a-component).
#include "sdkconfig.h"
#include "usb_msc_bunbun.h"

#if CONFIG_BUNBUN_USB_MSC

#include <string.h>

#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

static const char *TAG = "msc_glue";
static sdmmc_card_t *s_card = NULL;
static usb_phy_handle_t s_phy;

static void tusb_device_task(void *arg) {
  (void)arg;
  for (;;) {
    tud_task();
  }
}

esp_err_t usb_msc_bunbun_start(sdmmc_card_t *card) {
  s_card = card;
  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .target = USB_PHY_TARGET_INT,
  };
  esp_err_t err = usb_new_phy(&phy_conf, &s_phy);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "phy: %s", esp_err_to_name(err));
    return err;
  }
  if (!tusb_init()) {
    ESP_LOGE(TAG, "tusb_init failed");
    return ESP_FAIL;
  }
  xTaskCreatePinnedToCore(tusb_device_task, "tusb", 4096, NULL, 5, NULL, 0);
  ESP_LOGI(TAG, "MSC live: %u sectors x %u", (unsigned)s_card->csd.capacity,
           (unsigned)s_card->csd.sector_size);
  return ESP_OK;
}

// ---- the eight ----

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4]) {
  (void)lun;
  memcpy(vendor_id, "bunbun  ", 8);
  memcpy(product_id, "music card      ", 16);
  memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void)lun;
  return s_card != NULL;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                         uint16_t *block_size) {
  (void)lun;
  *block_count = (uint32_t)s_card->csd.capacity;
  *block_size = (uint16_t)s_card->csd.sector_size;
}

static void reboot_cb(void *arg) {
  (void)arg;
  esp_restart();
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start,
                           bool load_eject) {
  (void)lun;
  (void)power_condition;
  if (load_eject && !start) {
    // Host clicked EJECT: the polite exit road. Give the host a beat to
    // finish the SCSI transaction, then reboot into the normal pet.
    ESP_LOGI(TAG, "host ejected - waking bunbun");
    const esp_timer_create_args_t targs = {.callback = reboot_cb,
                                           .name = "msc_eject"};
    esp_timer_handle_t t;
    if (esp_timer_create(&targs, &t) == ESP_OK) {
      esp_timer_start_once(t, 700 * 1000);
    }
  }
  return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize) {
  (void)lun;
  if (offset != 0 || bufsize % s_card->csd.sector_size != 0) {
    return -1;  // we only serve whole sectors; TinyUSB sizes requests so
  }
  uint32_t n = bufsize / s_card->csd.sector_size;
  if (sdmmc_read_sectors(s_card, buffer, lba, n) != ESP_OK) {
    return -1;
  }
  return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
  (void)lun;
  return true;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize) {
  (void)lun;
  if (offset != 0 || bufsize % s_card->csd.sector_size != 0) {
    return -1;
  }
  uint32_t n = bufsize / s_card->csd.sector_size;
  if (sdmmc_write_sectors(s_card, buffer, lba, n) != ESP_OK) {
    return -1;
  }
  return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer,
                        uint16_t bufsize) {
  (void)lun;
  (void)scsi_cmd;
  (void)buffer;
  (void)bufsize;
  return -1;  // everything we don't know is politely refused
}

#else  // !CONFIG_BUNBUN_USB_MSC

esp_err_t usb_msc_bunbun_start(sdmmc_card_t *card) {
  (void)card;
  return ESP_ERR_NOT_SUPPORTED;
}

#endif
