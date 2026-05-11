// mDNS / Bonjour for the Carl hub.
//
// Advertises:
//   - hostname: <CARL_MDNS_HOSTNAME>.local  (default carl-hub.local)
//   - service:  _carl-hub._tcp on port 80   (so iOS Bonjour discovery finds us)
//   - service:  _http._tcp on port 80       (so generic HTTP browsers find us)

#pragma once

void carl_mdns_start(void);
