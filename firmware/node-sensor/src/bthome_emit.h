// Build + transmit one BTHome v2 advertisement per cycle.
//
// Legacy BLE non-connectable advertising can fit ~15 bytes of plaintext after
// the 11-byte BTHome overhead. To stay within budget while still reporting
// every channel, we alternate **packets** across two cycles:
//
//   even cycle: packet_id, battery, temp, humidity, pressure   (12 bytes pt)
//   odd cycle:  packet_id, battery, moisture, illuminance      (10 bytes pt)
//
// Both fit comfortably; the hub aggregates them by source MAC. Plant-monitor
// readings change on minute timescales so the alternation is invisible to the
// user.

#pragma once

#include "sensors.h"
#include "thresholds.h"

namespace carl::bthome_emit {

bool init();

// Encode the next advertisement and broadcast it for ~200 ms (one full
// scanning cycle on the hub side). Returns true on success.
bool broadcastOnce(const carl::sensors::Sample& s,
                   carl::thresholds::Severity severity);

}  // namespace carl::bthome_emit
