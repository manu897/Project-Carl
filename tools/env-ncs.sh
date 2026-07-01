# Activate the nRF Connect SDK / Zephyr build environment (west, cmake, ninja).
#
#   SOURCE this file — don't execute it:
#       source tools/env-ncs.sh
#
# Then `west build ...` works for the Zephyr targets (node-sensor, node-room).
# Auto-detects the installed toolchain so it keeps working across SDK updates.

NCS_VERSION="${NCS_VERSION:-v2.6.1}"
ZEPHYR_ENV="/opt/nordic/ncs/${NCS_VERSION}/zephyr/zephyr-env.sh"

if [ ! -f "$ZEPHYR_ENV" ]; then
    echo "env-ncs: $ZEPHYR_ENV not found — is nRF Connect SDK $NCS_VERSION installed?" >&2
    return 1 2>/dev/null || exit 1
fi

# Find the toolchain bundle that ships west.
_NCS_TC_BIN=""
for _d in /opt/nordic/ncs/toolchains/*/bin; do
    if [ -x "$_d/west" ]; then _NCS_TC_BIN="$_d"; break; fi
done
if [ -z "$_NCS_TC_BIN" ]; then
    echo "env-ncs: no toolchain with 'west' found under /opt/nordic/ncs/toolchains/" >&2
    return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1090
source "$ZEPHYR_ENV"
export PATH="$_NCS_TC_BIN:$PATH"

echo "env-ncs: ready — $(west --version 2>/dev/null | head -1)  [toolchain: $_NCS_TC_BIN]"
unset _NCS_TC_BIN _d
