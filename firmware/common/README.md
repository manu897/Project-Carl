# common

Header-mostly shared library used by every node and any future Carl accessory.

- `bthome/` — BTHome v2 encoder + decoder, AES-CCM helpers, Carl custom object IDs.
- `sensors/` — sensor drivers (BME280, capacitive soil ADC, VEML7700) wrapped behind a uniform interface so node firmware doesn't reach into vendor libraries directly.
- `test/` — host-side unit tests (build with native PlatformIO env or plain g++).

This folder is not a buildable target on its own. Each firmware target's `platformio.ini` adds it to its `lib_extra_dirs` / include path.
