#include "time_sync.h"

#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

static const char *TAG = "carl-time";

// Anything past 2020-01-01 means the clock has been set for real (the chip
// boots at epoch 0 = 1970).
#define SYNCED_THRESHOLD_EPOCH 1577836800  // 2020-01-01T00:00:00Z

static void on_time_synced(struct timeval *tv) {
    (void)tv;
    ESP_LOGI(TAG, "system clock set from NTP");
}

void carl_time_sync_start(void) {
    if (esp_sntp_enabled()) return;  // idempotent

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    sntp_set_time_sync_notification_cb(on_time_synced);
    esp_sntp_init();

    // Hub renders all timestamps in UTC (the reader/app localise for display).
    setenv("TZ", "UTC0", 1);
    tzset();

    ESP_LOGI(TAG, "SNTP started (pool.ntp.org)");
}

bool carl_time_is_synced(void) {
    return (time_t)time(NULL) > (time_t)SYNCED_THRESHOLD_EPOCH;
}

void carl_time_format_event(int64_t event_us, char *buf, size_t cap) {
    if (cap == 0) return;

    const int64_t now_us = esp_timer_get_time();
    const int64_t age_s  = (now_us - event_us) / 1000000;

    if (carl_time_is_synced()) {
        // Reconstruct the wall-clock instant of the event from "now minus age".
        const time_t event_epoch = (time_t)(time(NULL) - age_s);
        struct tm tm_utc;
        gmtime_r(&event_epoch, &tm_utc);
        strftime(buf, cap, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    } else {
        // Pre-sync fallback so the required field is never empty.
        snprintf(buf, cap, "%llds-ago", (long long)age_s);
    }
}
