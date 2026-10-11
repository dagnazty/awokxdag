#!/usr/bin/env python3
"""Fit the existing AxD boot artwork into the T-Dongle-C5's 60x80 logo pane.

The generated XBM uses set bits for white pixels so it can be drawn directly
with Adafruit_GFX::drawXBitmap(). The remaining 100x80 LCD area shows bridge
status. Run with --preview to inspect the actual 1-bit result before flashing.
"""
import argparse
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/boot_screen_source.png"
OUTPUT = ROOT / "AWOKxDAG/t_dongle_logo_data.h"
WIDTH, HEIGHT = 60, 80


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--threshold", type=int, default=96)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    source = Image.open(SOURCE).convert("L")
    small = source.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    bitmap = small.point(lambda value: 255 if value >= args.threshold else 0,
                         mode="1")
    pixels = bitmap.load()
    data = bytearray()
    for y in range(HEIGHT):
        for x0 in range(0, WIDTH, 8):
            byte = 0
            for bit in range(8):
                if x0 + bit < WIDTH and pixels[x0 + bit, y]:
                    byte |= 1 << bit
            data.append(byte)

    lines = ["#pragma once", "", "#include <Arduino.h>", "",
             f"constexpr int kDongleLogoWidth = {WIDTH};",
             f"constexpr int kDongleLogoHeight = {HEIGHT};",
             "const uint8_t kDongleLogoBitmap[] PROGMEM = {"]
    for offset in range(0, len(data), 12):
        lines.append("  " + ", ".join(f"0x{byte:02X}" for byte in data[offset:offset + 12]) + ",")
    lines.append("};")
    OUTPUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUTPUT} ({len(data)} bytes)")

    if args.preview:
        # The firmware enables the ST7735's hardware color inversion.
        panel = Image.new("L", (160, 80), 255)
        panel.paste(Image.eval(bitmap.convert("L"), lambda value: 255 - value),
                    (0, 0))
        panel.resize((640, 320), Image.Resampling.NEAREST).save(args.preview)
        print(f"wrote {args.preview}")


if __name__ == "__main__":
    main()
