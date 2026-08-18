// bunbun card-reader USB descriptors — one MSC interface, nothing else.
// Compiled INTO the tinyusb component (see this component's CMakeLists).
#include "sdkconfig.h"
#if CONFIG_BUNBUN_USB_MSC

#include "tusb.h"

#define USB_VID 0x303A  // Espressif's VID, test/product PID space
#define USB_PID 0x82B7
#define USB_BCD 0x0200

enum { ITF_NUM_MSC = 0, ITF_NUM_TOTAL };
enum { EPNUM_MSC_OUT = 0x01, EPNUM_MSC_IN = 0x81 };

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 250),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 4, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: English (US)
    "bunbun",                    // 1: manufacturer
    "bunbun music card",         // 2: product
    "080526",                    // 3: serial
    "bunbun MSC",                // 4: MSC interface
};

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

static uint16_t _desc_str[32];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint8_t chr_count;
  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
      return NULL;
    }
    const char *str = string_desc_arr[index];
    chr_count = (uint8_t)strlen(str);
    if (chr_count > 31) {
      chr_count = 31;
    }
    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = str[i];
    }
  }
  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}

#endif  // CONFIG_BUNBUN_USB_MSC
