#!/usr/bin/env python3
"""Build a full 4 MB-style flash image for Wokwi: 0xFF padding + app at 0x1000.

Zephyr's ESP32 "simple boot" image is loaded by the ROM from flash offset
0x1000, exactly like on the real chip.
"""
import sys

APP_OFFSET = 0x1000

app_bin, out_bin = sys.argv[1], sys.argv[2]
with open(app_bin, "rb") as f:
    app = f.read()
with open(out_bin, "wb") as f:
    f.write(b"\xff" * APP_OFFSET)
    f.write(app)
