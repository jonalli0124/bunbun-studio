/* W-060: the post-OTA self-check — "the difference between it booted and
 * it lived." (Hoppington engagement 2026-081, council 10-0, spec amended
 * in the live exchange.)
 *
 * With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, a freshly-OTA'd image boots
 * PENDING_VERIFY. If this module never marks it valid, ANY reboot returns
 * the previous firmware at the bootloader — no cable, no hands. So the
 * whole design is: delay the blessing until the image has proven itself
 * across a ten-minute validation window, then bless it once.
 *
 * The health tests, per the joint minutes:
 *  - The window itself: a build that crashes at minute 9 reboots, and the
 *    bootloader reverts it. (The consultants' minute-20 crasher is caught
 *    by the window; their RTC boot counter is kept as telemetry.)
 *  - Heap: sampled every 15s, enforced on the WINDOW MAXIMUM — a healthy
 *    unit idles past the floor sometime in ten minutes, while a true leak
 *    build never does. Floors: max free >= 24000, max largest >= 16384
 *    (the bench gate's own number — one definition of health, two
 *    enforcement points).
 *  - Assets: the art pak's "BUNP" magic must answer — a build that boots
 *    clean into a petless screen is unhealthy by decree.
 *  - LOCAL HEALTH ONLY: network state is advisory. RTSP is probed over
 *    loopback only while WiFi is up, and a dead router can never roll
 *    back a good build. */

#include <string.h>
#include <sys/socket.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_update.h"
#include "lwip/inet.h"

#include "wifi.h"

static const char *TAG = "fw_selfcheck";

#define WINDOW_MS (10 * 60 * 1000)
#define SAMPLE_MS 15000
/* Floors recalibrated by the fleet's FIRST AUTONOMOUS ROLLBACK (8/10,
 * 6D1C, 0.1.119): the bench's 16384 largest-block number was measured on
 * an UNCONNECTED unit idling at 31K; a connected unit fragments to ~14K
 * and lives there healthy - the window max read 24855/13824 and a good
 * build walked itself back. The mechanism was perfect; the number was a
 * bench number. New floors sit below every healthy field observation and
 * far above any true leak. */
/* Re-based a second time for 0.1.123: the wish_up stack-overflow fix costs
 * ~5.4KB of permanent internal RAM (stack 5120->8192 + 2.3KB of beacon
 * buffers moved to BSS), which moves a healthy connected unit's window
 * maximum from ~21-25K free down to ~16-20K. The floor must sit below
 * every healthy observation minus that delta, or the fix rolls itself
 * back — the 0.1.119 lesson, second verse. */
/* Third calibration (0.1.124): 0.1.123's window closed 16503/8704 against
 * 14000/9000 — free passed, largest missed by 296 bytes, and a correct
 * stack-overflow fix walked itself back onto the panicking build. The
 * 8192 wish_up stack takes a contiguous internal chunk, so a healthy
 * connected unit's largest block tops out ~8.7K now. A true leak build
 * collapses largest to the low thousands; 7000 splits those worlds. */
// Recalibrated a THIRD time (8/12): two home units (5DC0, D424) got stuck in
// an inescapable rollback loop - they idled fragmented to 6656 / 4352 largest
// (worse than the bench because a live AirPlay session + the 0.1.142 wake-bug
// churn fragment the pool), never crossed the 7000 largest floor, so no image
// ever blessed, and with an empty second slot they couldn't roll back either.
// The floors now sit below every observed healthy-but-fragmented unit and
// still far above a true leak (which collapses largest into the low hundreds).
// A healthy unit's window MAXIMUM clears these with room to spare.
// Fourth cut (8/12): 9000 free was still above the STREAMING heap - a unit
// with AirPlay playing through its whole 10-min window never idled past 9000,
// so 0.1.147 rolled back to 145 on D424 mid-song. The largest-block floor is
// the real leak detector (a leak collapses largest into the low hundreds);
// free legitimately runs low while streaming. Keep a meaningful largest floor,
// drop free below the streaming level. A truly dead build still fails (art
// magic gone, or largest < 2500), and the crash-counter catches boot loops.
#define FLOOR_FREE 5000
#define FLOOR_LARGEST 2500

/* Survives soft resets; cleared on power-on. Telemetry for the September
 * re-score: how many boots each pending image needed. */
RTC_NOINIT_ATTR static uint32_t s_img_boots;
/* WHICH image the count belongs to (first 8 bytes of its ELF sha256). The
 * count is RTC-persisted, so without this a freshly-OTA'd image INHERITS the
 * previous image's boot count across the update reboot - and if that count
 * had reached the rollback threshold, the new image rolls itself back on its
 * very first boot, before its validation window can run. Two home units were
 * bricked exactly this way (8/12): every update died on arrival at "boot 6".
 * Reset the count whenever the running image's sha differs from the counted
 * one. */
RTC_NOINIT_ATTR static uint8_t s_counted_sha[8];

static bool assets_ok(void) {
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, NULL);
  if (!p) {
    return false;
  }
  char magic[4];
  if (esp_partition_read(p, 0, magic, 4) != ESP_OK) {
    return false;
  }
  return memcmp(magic, "BUNP", 4) == 0;
}

static bool rtsp_answers_loopback(void) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    return false;
  }
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_port = htons(7000);
  a.sin_addr.s_addr = inet_addr("127.0.0.1");
  struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  int rc = connect(s, (struct sockaddr *)&a, sizeof(a));
  close(s);
  return rc == 0;
}

static void selfcheck_task(void *arg) {
  uint32_t max_free = 0;
  uint32_t max_largest = 0;
  uint32_t elapsed = 0;

  ESP_LOGI(TAG,
           "image PENDING_VERIFY (boot %lu of this image) - validation "
           "window %d min",
           (unsigned long)s_img_boots, WINDOW_MS / 60000);

  while (elapsed < WINDOW_MS) {
    vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    elapsed += SAMPLE_MS;
    uint32_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t l = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (f > max_free) {
      max_free = f;
    }
    if (l > max_largest) {
      max_largest = l;
    }
  }

  bool heap_ok = (max_free >= FLOOR_FREE) && (max_largest >= FLOOR_LARGEST);
  extern uint32_t g_fw_crumb;
  g_fw_crumb = 21;   /* window closed, entering art-wait */
  /* A fleet-art rewrite in flight at window close is not a broken pak - it
   * is new art LANDING. 8/13: D424's window closed while the 13a pak was
   * mid-erase, assets_ok() read the blank partition, and a healthy image
   * was rolled back for it. Wait the writer out (bounded), then judge. */
  int art_guard_s = 180;
  while (fw_assets_writing() && art_guard_s-- > 0) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  g_fw_crumb = 22;   /* art judged, running rtsp loopback */
  bool art_ok = assets_ok();
  /* Local health only: RTSP counts only when the radio is up. */
  bool net_up = wifi_is_connected();
  bool rtsp_ok = !net_up || rtsp_answers_loopback() || rtsp_answers_loopback();

  ESP_LOGI(TAG,
           "window closed: heap max %lu/%lu (floors %d/%d) %s | art %s | "
           "rtsp %s (wifi %s)",
           (unsigned long)max_free, (unsigned long)max_largest, FLOOR_FREE,
           FLOOR_LARGEST, heap_ok ? "OK" : "FAIL", art_ok ? "OK" : "FAIL",
           rtsp_ok ? "OK" : "FAIL", net_up ? "up" : "down");

  if (heap_ok && art_ok && rtsp_ok) {
    g_fw_crumb = 23;   /* mark-valid flash write starting */
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      ESP_LOGI(TAG, "image marked VALID - it lived");
      g_fw_crumb = 24;   /* blessed */
      s_img_boots = 0;
    } else {
      ESP_LOGE(TAG, "mark-valid failed - bootloader may revert on reboot");
    }
  } else {
    ESP_LOGE(TAG, "self-check FAILED - rolling back to previous firmware");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  vTaskDelete(NULL);
}

void fw_selfcheck_begin(void) {
  fw_crumb_snapshot();   /* preserve the previous life's last breadcrumb */
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(run, &st) != ESP_OK) {
    return;
  }
  /* PENDING_VERIFY = a rollback-aware bootloader transitioned us; NEW = a
   * rollback-aware APP wrote the entry but the unit still runs its original
   * bootloader (bootloaders only ship over USB). Either way the image is
   * unproven - run the window. On old-bootloader units the explicit
   * rollback path (mark_invalid on FAIL) works; only the crash-before-
   * blessing AUTO-revert waits for the unit's next USB touch. */
  if (st != ESP_OTA_IMG_PENDING_VERIFY && st != ESP_OTA_IMG_NEW) {
    s_img_boots = 0;
    return; /* long-blessed image - nothing to prove */
  }
  /* Reset the count on a power-on OR whenever THIS image differs from the one
   * the count was tracking - a fresh OTA must start its own life at boot 1,
   * never inherit the outgoing image's tally. */
  const esp_app_desc_t *ad = esp_app_get_description();
  bool same_image = (memcmp(s_counted_sha, ad->app_elf_sha256, 8) == 0);
  esp_reset_reason_t rr = esp_reset_reason();
  /* Only a CRASH counts toward the rollback teeth. A clean software reset -
   * a user tapping restart, an OTA reboot, an AirPlay-for-OTA cycle - is not
   * an "unproven crash loop", and counting it rolled a perfectly good image
   * back mid-window (8/12: a diagnostic /api/restart pushed 5DC0's 145 over
   * the edge). The teeth exist for crash loops; let crashes alone arm them. */
  bool crash = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT ||
                rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT ||
                rr == ESP_RST_BROWNOUT);
  if (rr == ESP_RST_POWERON || !same_image) {
    s_img_boots = 1;
    memcpy(s_counted_sha, ad->app_elf_sha256, 8);
  } else if (crash) {
    s_img_boots++;
  }
  /* THE COUNTER GETS TEETH (8/10 night, after a window-close panic on
   * 6D1C): a crash during the window just restarts the window - without
   * this, a crash-looping unproven image retries forever. Three CRASHES
   * without a blessing = explicit rollback. Clean restarts never arm it. */
  if (crash && s_img_boots >= 3) {
    ESP_LOGE(TAG,
             "unproven image on boot %lu without a blessing - rolling back",
             (unsigned long)s_img_boots);
    vTaskDelay(pdMS_TO_TICKS(2000));   /* let the log leave the UART */
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  /* Internal stack (the mark-valid flash write forbids PSRAM - the 0.1.91
   * lesson), sized for its worst moment: window-close runs lwip socket
   * connects + esp_partition reads + the flash write. 3072 was tight
   * enough to be the prime suspect in the 8/10 window-close panic. */
  /* 6144 -> 8192 (8/13): window-close now also runs the art-wait loop, and
   * the 8/10 window-close panic already had this stack as prime suspect. */
  xTaskCreatePinnedToCore(selfcheck_task, "fw_selfchk", 8192, NULL, 3, NULL,
                          0);
}
