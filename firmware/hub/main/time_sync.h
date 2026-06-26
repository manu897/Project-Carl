// Wall-clock time via SNTP.
//
// The hub has no RTC, so until it syncs with an NTP server it only knows
// "microseconds since boot" (esp_timer). Phase 3d starts SNTP after Wi-Fi
// comes up so /api/nodes and /api/nodes/{id}/history can emit real
// ISO-8601 timestamps instead of relative seconds-since-boot.

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Kick off SNTP polling. Call once, after Wi-Fi is connected. Non-blocking —
// the clock becomes valid a few seconds later when the first NTP reply lands.
void carl_time_sync_start(void);

// True once the system clock has been set from NTP (or any source that put
// it past the 2020 epoch sanity threshold).
bool carl_time_is_synced(void);

// Format the wall-clock instant corresponding to an esp_timer timestamp
// (microseconds since boot, as returned by esp_timer_get_time()) into `buf`.
//
// If the clock is synced, writes ISO-8601 UTC, e.g. "2026-06-26T14:32:05Z".
// If not yet synced, writes a relative fallback, e.g. "123s-ago", so callers
// always get a non-empty string for the (required) `ts` / `last_seen` fields.
void carl_time_format_event(int64_t event_us, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
