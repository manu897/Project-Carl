# Carl tools

## `env-ncs.sh` / `env-esp.sh` — toolchain activation

Each firmware target needs its vendor toolchain on `PATH`. Source the right one **once per terminal** before building — `west` for the two Zephyr targets (node-sensor, node-room), `idf.py` for the hub. Both auto-detect the installed SDK path, so they keep working across SDK updates.

```bash
source tools/env-ncs.sh     # → west (node-sensor, node-room)
source tools/env-esp.sh     # → idf.py (hub)
```

M5Paper needs neither — PlatformIO manages its own toolchain per-project (`pio run` just works from `firmware/reader-m5paper/`).

Must be `source`d, not executed (`./tools/env-ncs.sh` won't work — it needs to modify your current shell's `PATH`).

## `provision.py` — stick-on QR label generator

The budget/cheap plant probe has no screen, so it can't show its provisioning
QR the way the display profile does on its OLED. This tool captures a node's
`(MAC, AES key)` and renders a **printable label** you stick on the node. The QR
encodes the same payload the OLED QR uses — `CARL://AABBCCDDEEFF/<32-hex-key>` —
so the iOS Add-Plant scanner treats a sticker and a screen identically.

### Setup

```bash
pip install -r tools/requirements.txt
```

### Capture straight off the node (right after flashing)

The node firmware prints one line on boot:

```
CARL-PROV mac=D0:B2:A9:AD:39:36 key=ABCD…(32 hex)
```

Point the tool at the node's serial port and reset it:

```bash
./tools/provision.py --port /dev/tty.usbmodem101 --name "Bedroom Monstera"
```

### Or provide the values manually

```bash
./tools/provision.py --mac D0:B2:A9:AD:39:36 --key abcd…(32 hex) --name "Basil"
```

### Output (into `./labels/`, override with `--out`)

- `<MAC>.png` — the printable label: QR + human-readable MAC + name
- `<MAC>.txt` — the raw `CARL://` payload + a ready-to-run enrolment `curl`

After printing + sticking the label, enrol the node either by scanning it in
the iOS app, or with the `curl` line the tool prints (hits `POST /api/nodes`).

> Keys are still generated **on the device** at first boot — this tool only
> *reads and renders* them, it doesn't create them. The printed label is a
> physical secret, same trust model as the Wi-Fi password on a router.
