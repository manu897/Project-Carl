#!/usr/bin/env python3
"""Carl node provisioning — turn a node's (MAC, AES key) into a stick-on QR label.

The budget/cheap plant probe has no screen, so it can't show its provisioning
QR the way the display profile does on its OLED. Instead this tool captures the
node's credentials and renders a printable label you stick on the node. The QR
encodes the EXACT same payload the OLED QR uses:

    CARL://AABBCCDDEEFF/<32-hex-key>

so the iOS Add-Plant scanner needs no special-casing — sticker or screen, same
parse, same POST /api/nodes enrolment.

Two ways to get the credentials:

  1. Live, straight off the node's serial console (right after flashing):
         ./provision.py --port /dev/tty.usbmodem101 --name "Bedroom Monstera"
     The node firmware prints one line on boot:
         CARL-PROV mac=D0:B2:A9:AD:39:36 key=ABCD...  (32 hex)
     We wait for it, parse it, and render the label.

  2. Manual, if you already have the values (e.g. copied from the console):
         ./provision.py --mac D0:B2:A9:AD:39:36 --key abcd...(32 hex) --name "Basil"

Output (per node, into --out, default ./labels/):
  - <MAC>.png   the printable label (QR + human-readable MAC + name)  [needs qrcode]
  - <MAC>.txt   the raw CARL:// payload + a ready-to-run enrolment curl

Dependencies: Pillow (label canvas) + qrcode (QR matrix). pyserial only for
--port mode. Install: pip install -r tools/requirements.txt
"""

import argparse
import os
import re
import sys
import time

MAC_RE = re.compile(r"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")
KEY_RE = re.compile(r"^[0-9A-Fa-f]{32}$")
# Matches the firmware's boot line: "CARL-PROV mac=AA:.. key=<32hex>"
PROV_LINE_RE = re.compile(
    r"CARL-PROV\s+mac=([0-9A-Fa-f:]{17})\s+key=([0-9A-Fa-f]{32})"
)


def die(msg: str, code: int = 1):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def payload_url(mac: str, key: str) -> str:
    """The QR payload — must match firmware ui/oled.cpp formatProvisioningUrl()."""
    mac12 = mac.replace(":", "").upper()
    return f"CARL://{mac12}/{key.upper()}"


def enroll_curl(mac: str, key: str, name: str, host: str) -> str:
    name_field = f',"name":"{name}"' if name else ""
    body = f'{{"mac":"{mac.upper()}","key_hex":"{key.lower()}"{name_field}}}'
    return f"curl -X POST http://{host}/api/nodes -H 'Content-Type: application/json' -d '{body}'"


def read_from_serial(port: str, baud: int, timeout_s: float) -> tuple[str, str]:
    try:
        import serial  # pyserial
    except ImportError:
        die("pyserial not installed — `pip install pyserial` (or use --mac/--key)")

    print(f"listening on {port} @ {baud} for a CARL-PROV line "
          f"(reset the node now)…", file=sys.stderr)
    deadline = time.monotonic() + timeout_s
    with serial.Serial(port, baud, timeout=1) as ser:
        while time.monotonic() < deadline:
            try:
                raw = ser.readline().decode("utf-8", errors="replace").strip()
            except Exception as e:  # noqa: BLE001 - surface and keep trying
                print(f"  (read error: {e})", file=sys.stderr)
                continue
            if not raw:
                continue
            m = PROV_LINE_RE.search(raw)
            if m:
                return m.group(1), m.group(2)
            # Echo other boot lines so the operator sees progress.
            print(f"  · {raw}", file=sys.stderr)
    die(f"no CARL-PROV line seen within {timeout_s:.0f}s — is the node booting?")


def render_label(mac: str, key: str, name: str, out_dir: str) -> None:
    url = payload_url(mac, key)
    safe_mac = mac.replace(":", "-").upper()
    os.makedirs(out_dir, exist_ok=True)

    # Always write the text artifact — useful even without the imaging libs.
    txt_path = os.path.join(out_dir, f"{safe_mac}.txt")
    with open(txt_path, "w") as f:
        f.write(url + "\n")
        f.write(enroll_curl(mac, key, name, "carl-hub.local") + "\n")
    print(f"wrote {txt_path}")

    try:
        import qrcode
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as e:
        print(f"note: {e.name} not installed — wrote payload .txt only.",
              file=sys.stderr)
        print(f"      `pip install -r tools/requirements.txt` to get the PNG label.",
              file=sys.stderr)
        print(f"      payload: {url}", file=sys.stderr)
        return

    # QR matrix. ECC_M tolerates a bit of label wear/print noise.
    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M,
                       box_size=8, border=2)
    qr.add_data(url)
    qr.make(fit=True)
    qr_img = qr.make_image(fill_color="black", back_color="white").convert("RGB")

    # Compose a label: QR on the left, text block on the right.
    pad = 16
    qr_w, qr_h = qr_img.size
    text_w = 360
    label = Image.new("RGB", (qr_w + text_w + pad * 3, max(qr_h, 150) + pad * 2),
                      "white")
    label.paste(qr_img, (pad, pad))

    draw = ImageDraw.Draw(label)

    def font(size: int):
        for path in (
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
        ):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
        return ImageFont.load_default()

    tx = qr_w + pad * 2
    ty = pad
    draw.text((tx, ty), "Carl plant sensor", fill="black", font=font(26)); ty += 38
    if name:
        draw.text((tx, ty), name, fill="black", font=font(22)); ty += 32
    draw.text((tx, ty), mac.upper(), fill="black", font=font(20)); ty += 30
    draw.text((tx, ty), "Scan in the Carl app to add", fill="#555", font=font(16))

    png_path = os.path.join(out_dir, f"{safe_mac}.png")
    label.save(png_path)
    print(f"wrote {png_path}")


def main():
    ap = argparse.ArgumentParser(
        description="Generate a stick-on provisioning QR label for a Carl node.")
    ap.add_argument("--port", help="serial port to read CARL-PROV from (e.g. /dev/tty.usbmodem101)")
    ap.add_argument("--baud", type=int, default=115200, help="serial baud (default 115200)")
    ap.add_argument("--timeout", type=float, default=30.0, help="serial wait timeout seconds")
    ap.add_argument("--mac", help="node MAC AA:BB:CC:DD:EE:FF (skips serial)")
    ap.add_argument("--key", help="32-char hex AES key (skips serial)")
    ap.add_argument("--name", default="", help="plant/room name to print on the label")
    ap.add_argument("--out", default="labels", help="output directory (default ./labels)")
    args = ap.parse_args()

    if args.mac and args.key:
        mac, key = args.mac, args.key
    elif args.port:
        mac, key = read_from_serial(args.port, args.baud, args.timeout)
    else:
        die("provide either --port, or both --mac and --key")

    if not MAC_RE.match(mac):
        die(f"bad MAC {mac!r} — expected AA:BB:CC:DD:EE:FF")
    if not KEY_RE.match(key):
        die(f"bad key {key!r} — expected 32 hex chars")

    print(f"node {mac.upper()}  key {key.lower()}")
    render_label(mac, key, args.name, args.out)
    print("\nenrol with:")
    print("  " + enroll_curl(mac, key, args.name, "carl-hub.local"))


if __name__ == "__main__":
    main()
