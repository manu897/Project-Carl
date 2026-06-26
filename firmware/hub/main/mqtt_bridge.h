// Optional Home Assistant MQTT bridge.
//
// When enabled (CONFIG_CARL_MQTT_HA_ENABLE), connects to a local MQTT broker
// and publishes each node's readings using Home Assistant MQTT Discovery, so
// plants appear automatically as HA sensor entities. Compiled out / no-op
// when disabled. Carl works standalone without it.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Connect to the broker and start the periodic publish task. No-op unless
// CONFIG_CARL_MQTT_HA_ENABLE. Call once in normal mode after Wi-Fi is up.
void carl_mqtt_bridge_start(void);

#ifdef __cplusplus
}
#endif
