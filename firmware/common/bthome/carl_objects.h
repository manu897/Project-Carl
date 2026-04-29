// Carl-specific advertisement extensions — placeholder.
//
// BTHome v2 doesn't define a formal "user range" of object IDs, so we don't
// reuse its object table for project-specific semantics. If we later need to
// transmit data that doesn't fit standard BTHome objects (e.g. a structured
// "thirsty severity" enum or a node role bitfield), the right home for it is
// a separate manufacturer-specific data field included in the same BLE
// advertisement alongside the BTHome service data — the hub decodes both.
//
// Until then, "thirsty" status is derived from the moisture % crossing
// thresholds, in the hub or in HA. This header exists so the placement is
// obvious when we do add extensions.

#pragma once

#include <stdint.h>

namespace carl::ext {

// 16-bit Bluetooth SIG manufacturer ID we'd use for Carl-namespace data.
// 0xFFFF is reserved for testing per the SIG; fine for prototype use.
// Replace with a real allocation if/when we ship publicly.
static constexpr uint16_t kManufacturerId = 0xFFFF;

}  // namespace carl::ext
