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

// Returns true once on the leading edge of a finger-down touch. Call
// lastTouchPoint() right after a true return to get where it landed.
bool consumeTouch();

// Coordinates of the touch that consumeTouch() last reported, in panel
// space (0..960 x, 0..540 y — matches the landscape canvas). Undefined
// before the first true return from consumeTouch().
void lastTouchPoint(int* x, int* y);

// Hit-test a point against the card grid as last drawn by showNodeList()
// for this same `list` (column/row math must match what's on screen).
// Returns the tapped node's index, or -1 if the point missed every card.
int hitTestCard(const carl::hub::NodeList& list, int touch_x, int touch_y);

// True if a point landed in the detail page's "< Back" hot zone.
bool isBackTouch(int touch_x, int touch_y);

// Best-effort: battery voltage above ~4.15 V means external USB power is
// driving the charger. Used to switch the header indicator and to speed
// the poll cadence while plugged in.
bool isCharging();

// Render the full list of plants. Caller decides when to push (not every
// poll — only when content actually changed, to spare e-paper cycles).
void showNodeList(const carl::hub::NodeList& list);

// Render a full-screen detail page for one node: name + "< Back", current
// reading in full, and a line graph of `history`'s primary trend metric
// (soil % for a plant node, else temperature — whichever the node reports).
// Caller supplies already-fetched history; this only draws.
void showNodeDetail(const carl::hub::Node& node, const carl::hub::History& history);

// Transient status line at the bottom — "fetching…", "wifi down",
// "no nodes yet". Renders with a fast A2 partial refresh.
void showStatus(const char* msg);

}  // namespace carl::dashboard
