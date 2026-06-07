// Build + transmit one BTHome v2 advertisement per sample cycle.
//
// Room node ad fits in a single legacy-AD cycle:
//   packet_id + battery + T + H + P + lux = 18 bytes plaintext
//   + 11 bytes encryption overhead = 29 bytes service data
//   + 2 bytes AD struct header (length + type) = 31 bytes total — at the
//   legacy non-connectable AD ceiling. Adding gas/VOC would require either
//   two-cycle rotation (like firmware/node-sensor/) or extended advertising.

#pragma once

#include "sensors.h"

namespace carl::room::bthome_emit {

bool init();

// Encode one BTHome v2 advertisement and broadcast it for ~200 ms.
// Returns true on success; on failure, prints why to the console.
bool broadcastOnce(const carl::room::sensors::Sample& s);

}  // namespace carl::room::bthome_emit
