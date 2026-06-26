// Optional Project-Norman cloud uplink.
//
// When enabled (CONFIG_CARL_NORMAN_ENABLE), continuously publishes node
// readings to Norman's MQTT broker (cloud ingestion / ML / long-term history).
// This is the Carl↔Norman boundary — continuous MQTT, not a daily HTTPS POST.
// Uses TLS when the broker URI is mqtts://. Compiled out / no-op when disabled.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Connect to Norman's broker and start the periodic publish task. No-op unless
// CONFIG_CARL_NORMAN_ENABLE. Call once in normal mode after Wi-Fi is up.
void carl_norman_uplink_start(void);

#ifdef __cplusplus
}
#endif
