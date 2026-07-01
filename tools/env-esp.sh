# Activate the ESP-IDF build environment (idf.py) for the hub.
#
#   SOURCE this file — don't execute it:
#       source tools/env-esp.sh
#
# Then `idf.py build flash monitor` works in firmware/hub.

IDF_EXPORT="${IDF_PATH:-$HOME/esp/esp-idf}/export.sh"

if [ ! -f "$IDF_EXPORT" ]; then
    echo "env-esp: $IDF_EXPORT not found — set IDF_PATH or install ESP-IDF under ~/esp/esp-idf" >&2
    return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1090
source "$IDF_EXPORT"
