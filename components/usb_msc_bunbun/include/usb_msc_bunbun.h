#pragma once

#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring up the USB MSC device over an initialized raw SD card (W-020).
 * PHY -> OTG (the serial console on this port ceases for this boot),
 * TinyUSB device task started, the 8 MSC callbacks mapped onto
 * sdmmc_read/write_sectors. Host EJECT reboots the unit back to normal.
 * Returns ESP_OK when the stack is live. Only exists when
 * CONFIG_BUNBUN_USB_MSC=y; otherwise returns ESP_ERR_NOT_SUPPORTED.
 */
esp_err_t usb_msc_bunbun_start(sdmmc_card_t *card);

#ifdef __cplusplus
}
#endif
