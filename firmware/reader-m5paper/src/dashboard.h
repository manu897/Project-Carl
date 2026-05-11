// E-paper rendering for the Carl reader.
//
// Card grid: one tile per plant, severity-colored frame, big moisture %,
// supporting fields below. Layout fills the 540×960 portrait screen with
// up to 6 cards on a 1×6 list (v1) — multi-column grid + a detail page
// behind tap come later. E-paper refresh is GC16 (full grayscale, 5–7 s)
// so we only push when readings change, not on every poll cycle.

#pragma once

#include <stdint.h>

#include "hub_client.h"

namespace carl::dashboard {

void init();

// Boot splash — Carl wordmark + "Connecting…" while Wi-Fi negotiates.
void showBootSplash();

// Animated welcome splash — big "Carl" + "Plant monitor" with two potted
// plants growing in 6 frames using A2 partial refreshes for speed. Final
// frame is committed via GC16 to clean up any partial-refresh ghosting.
// Blocks for ~3 seconds total.
void showWelcomeSplash();

// Returns true once on the leading edge of a finger-down touch. Used by
// future tap interactions on the dashboard (open detail page, snooze).
bool consumeTouch();

// Best-effort: battery voltage above ~4.15 V means external USB power is
// driving the charger. Used to switch the header indicator and to speed
// the poll cadence while plugged in.
bool isCharging();

// Render the full list of plants. Caller decides when to push (not every
// poll — only when content actually changed, to spare e-paper cycles).
void showNodeList(const carl::hub::NodeList& list);

// Transient status line at the bottom — "fetching…", "wifi down",
// "no nodes yet". Renders with a fast A2 partial refresh.
void showStatus(const char* msg);

}  // namespace carl::dashboard
