// HTTP server implementing the OpenAPI defined at
// Project-Carl-IOS/api/openapi.yaml — the contract the iOS app codegens
// against and the M5Paper reader polls. This file owns route registration;
// per-endpoint handlers may live alongside (or get split out as the API
// surface grows in Phase 3b/3c/3d).

#pragma once

#include <stdbool.h>

void carl_http_api_start(void);

// In SoftAP onboarding ("setup mode"), the static handler serves setup.html
// for every page request so captive-portal probes land on the Wi-Fi form.
// Call before carl_http_api_start() when no Wi-Fi credentials are configured.
void carl_http_set_setup_mode(bool on);
