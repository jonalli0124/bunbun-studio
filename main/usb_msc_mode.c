// W-020: bunbun as a USB-C card reader — a dedicated BOOT MODE.
//
// Entered by flag (NVS "bunbun"/"mscMode"), exited by reboot. Nothing else
// starts in this mode: no WiFi, no AirPlay, no game, no sdplayer — which is
// what makes it affordable (TinyUSB resident would blow the 40KB internal-RAM
// floor; here the RAM consumers never start) and what makes it SAFE (the
// firmware and the host PC never own the FAT volume in the same boot, so the
// corruption problem is designed out rather than guarded against).
//
// The screen and tap-exit live in the bunbun component (bunbun_msc_screen_run
// — Arduino owns the TFT and touch). This file owns the card and the USB.
//
// While this mode is active the USB serial console DOES NOT EXIST (the S3's
// one PHY is routed to USB-OTG). Recovery from a wedged build is always
// possible via the BOOT-button ROM download mode — a property of the chip,
// not of this code.

#include "usb_msc_mode.h"

#include "sdkconfig.h"

// Self-guarding: only real when the bunbun MSC personality is in the build
// (CONFIG_BUNBUN_USB_MSC, S3 only). Everyone else gets honest stubs, and
// every entry point checks usb_msc_mode_available() before promising.
#if CONFIG_BUNBUN_USB_MSC

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "sdmmc_cmd.h"
#include "usb_msc_bunbun.h"

static const char *TAG = "usb_msc";

bool usb_msc_mode_available(void) { return true; }

// bunbun component: init Arduino+TFT+touch, draw the mode screen, poll for
// the exit tap, and reboot when asked. Never returns.
extern void bunbun_msc_screen_run(void);

bool usb_msc_mode_flagged(void) {
  nvs_handle_t h;
  if (nvs_open("bunbun", NVS_READONLY, &h) != ESP_OK) {
    return false;
  }
  uint8_t v = 0;
  nvs_get_u8(h, "mscMode", &v);
  nvs_close(h);
  return v == 1;
}

void usb_msc_mode_set_flag(bool on) {
  nvs_handle_t h;
  if (nvs_open("bunbun", NVS_READWRITE, &h) != ESP_OK) {
    return;
  }
  if (on) {
    nvs_set_u8(h, "mscMode", 1);
  } else {
    nvs_erase_key(h, "mscMode");
  }
  nvs_commit(h);
  nvs_close(h);
}

void usb_msc_mode_run(void) {
  // The flag is one-shot: clear it FIRST, so a crash inside this mode boots
  // back to the normal pet instead of wedging in a card-reader loop.
  usb_msc_mode_set_flag(false);
  ESP_LOGI(TAG, "USB card-reader mode: this boot is a USB drive");

  // Raw SDMMC, 1-bit, same pins the sdplayer uses (clk 38, cmd 40, d0 39).
  // NO FAT mount on our side — the host PC owns the filesystem this boot.
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = GPIO_NUM_38;
  slot.cmd = GPIO_NUM_40;
  slot.d0 = GPIO_NUM_39;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  static sdmmc_card_t card;
  esp_err_t err = sdmmc_host_init();
  if (err == ESP_OK) {
    err = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
  }
  if (err == ESP_OK) {
    err = sdmmc_card_init(&host, &card);
  }
  if (err != ESP_OK) {
    // No card / dead card: nothing to expose. Show the screen anyway — the
    // person who pressed the button deserves words, not a black rectangle —
    // and let the tap take them home.
    ESP_LOGE(TAG, "SD init failed: %s - card missing?", esp_err_to_name(err));
    bunbun_msc_screen_run();  // never returns
  }

  ESP_ERROR_CHECK(usb_msc_bunbun_start(&card));
  ESP_LOGI(TAG, "USB MSC live - the PC owns the card until reboot");

  bunbun_msc_screen_run();  // screen + tap-exit; never returns
}

#else  // no TinyUSB on this target/build

bool usb_msc_mode_available(void) { return false; }
bool usb_msc_mode_flagged(void) { return false; }
void usb_msc_mode_set_flag(bool on) { (void)on; }
void usb_msc_mode_run(void) {}

#endif
