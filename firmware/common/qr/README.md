# qr — vendored Nayuki qrcodegen + Carl integration

Carl uses [Nayuki's qrcodegen-c](https://github.com/nayuki/QR-Code-generator) to render the provisioning QR (`carl://node?mac=…&key=…`) on the display-profile sensor node's OLED. The library is **not vendored in-tree** — drop the two upstream files in here before building with `CONFIG_CARL_HAS_QR=y`.

## Vendor it (one-time)

From the repo root:

```bash
curl -L https://raw.githubusercontent.com/nayuki/QR-Code-generator/master/c/qrcodegen.h \
    -o firmware/common/qr/qrcodegen.h
curl -L https://raw.githubusercontent.com/nayuki/QR-Code-generator/master/c/qrcodegen.c \
    -o firmware/common/qr/qrcodegen.c
```

That's it. ~700 lines of MIT-licensed C, no build dependencies. The Carl firmware build picks it up automatically when `CONFIG_CARL_HAS_QR=y` is set on the build line.

## License

Nayuki's qrcodegen is MIT-licensed. Keep the upstream copyright header intact in the .c and .h files (the `curl` commands above preserve it). The Carl integration code in [`../../node-sensor/src/ui/oled.cpp`](../../node-sensor/src/ui/oled.cpp) is part of Project-Carl and stays under whatever license Carl ships with.

## What gets used

Carl's display-profile firmware ([`firmware/node-sensor/src/ui/oled.cpp`](../../node-sensor/src/ui/oled.cpp)) calls the standard `qrcodegen_encodeText()` entry point with byte-mode encoding at the lowest error-correction level (L) and renders each module to the SSD1306 framebuffer at 1 px per module via `display_write`. Resulting QR is 33×33 px (Version 4 byte mode L EC), comfortably under the 70-character payload of the canonical provisioning URL.

## When CONFIG_CARL_HAS_QR is off

The build skips the qrcodegen sources entirely and `oled::showProvisioningQR()` becomes a stub that prints "QR off" on the OLED. You can still flash + provision manually by transcribing the AES key from the welcome screen — the QR is a UX accelerator, not a requirement.
