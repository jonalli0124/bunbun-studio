// bunbun card-reader tusb_config — MSC only, full speed, device stack.
// Injected into espressif__tinyusb by this component's CMake (the
// usb_device_uac pattern; uac's own injection stands down via
// CONFIG_USB_DEVICE_UAC_AS_PART).
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "sdkconfig.h"

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_FREERTOS
#endif

#ifndef ESP_PLATFORM
#define ESP_PLATFORM 1
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#if TU_CHECK_MCU(OPT_MCU_ESP32S2, OPT_MCU_ESP32S3, OPT_MCU_ESP32P4)
#define CFG_TUSB_OS_INC_PATH freertos/
#endif

#define CFG_TUD_ENABLED 1

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

// The one class this personality speaks. EP buffer is 512 (one sector), NOT
// the 4KB it briefly was: TinyUSB's buffers are compile-time STATICS, so
// they are resident in EVERY boot — the gate caught the 4KB version costing
// normal operation ~5KB of internal RAM (37.1KB vs the 40KB floor). The
// spike's "zero resident cost" held for execution, not for BSS. One-sector
// transactions are still far faster than any WiFi path.
#define CFG_TUD_MSC 1
#define CFG_TUD_MSC_EP_BUFSIZE 512

#ifdef __cplusplus
}
#endif

#endif
