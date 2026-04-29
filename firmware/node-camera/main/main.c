/* Project-Carl — XIAO ESP32-S3 Sense camera node.
 *
 * Skeleton only; real implementation lands in Phase 5 of the plan
 * (live at /Users/fwdev/.claude/plans/work-on-carl-for-silly-lark.md).
 *
 * Responsibilities (when fleshed out):
 *   - Sample BME280 / soil ADC / VEML7700 like the cheap sensor node.
 *   - Broadcast encrypted BTHome v2 advertisements via NimBLE.
 *   - Expose a connectable GATT service (Plant Camera Service) that the hub
 *     uses to pull a daily plant-analysis JPEG and to sync the soft RTC.
 *   - Capture once per day from the onboard OV2640, save to onboard microSD.
 */

#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "carl-camera";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "carl-node-camera boot — skeleton (no behavior yet)");
}
