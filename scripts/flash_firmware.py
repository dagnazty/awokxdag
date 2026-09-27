#!/usr/bin/env python3
"""Flash a packaged firmware image to a board over USB, from the terminal.

Companion to build_firmware.py (which only compiles/packages, never flashes).
This writes the packaged *merged* image at 0x0 with esptool -- the same layout
the Recovery section documents -- so no Arduino IDE is needed to program a board.

Ports on the AWOK Dual C5:
  * White USB port  -> CP2102 UART bridge, enumerates as /dev/ttyUSB* (Linux),
    /dev/cu.usbserial-* (macOS), COM* (Windows). Hold SCREEN BOOT while applying
    power / resetting so the chip enters download mode.
  * Orange USB port -> the C5's native USB-Serial-JTAG, enumerates as
    /dev/ttyACM* (Linux), /dev/cu.usbmodem* (macOS), COM* (Windows), and only
    while the chip is in download mode. Put it in download mode first (hold its
    BOOT button, tap RESET or replug) or esptool will not find it.

Examples:
  python3 scripts/flash_firmware.py dual-c5-touch                 # autodetect port
  python3 scripts/flash_firmware.py dual-c5-touch --port /dev/ttyACM0   # orange
  python3 scripts/flash_firmware.py dual-c5-touch --port /dev/ttyUSB0   # white
  python3 scripts/flash_firmware.py dual-c5-touch --build         # build then flash
  python3 scripts/flash_firmware.py lilygo-t-dongle-c5-bridge --port /dev/ttyACM0
"""
import argparse
import glob
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Chip family per board profile (mirrors build_firmware.py's FQBNs).
C5 = ("dual-c5-touch", "dual-c5-mini", "dual-c5-bridge",
      "lilygo-t-dongle-c5-bridge")
CHIP = {b: ("esp32c5" if b in C5 else "esp32s3" if b == "cardputer-v11"
            else "esp32") for b in (
    "cardputer-v11", "lilygo-t-dongle-c5-bridge",
    "dual-c5-touch", "dual-c5-mini", "dual-c5-bridge",
    "dual-esp32-touch-v1", "dual-esp32-touch-v2", "dual-esp32-touch-v3",
    "dual-esp32-mini-v1", "dual-esp32-mini-v2", "dual-esp32-mini-v3",
    "dual-esp32-touch-bridge-v1", "dual-esp32-touch-bridge-v2",
    "dual-esp32-touch-bridge-v3", "dual-esp32-mini-bridge-v1",
    "dual-esp32-mini-bridge-v2", "dual-esp32-mini-bridge-v3",
)}


def firmware_version() -> str:
    source = (ROOT / "AWOKxDAG/awok_common.h").read_text()
    match = re.search(r'kVersion\[\]\s*=\s*"([^"]+)"', source)
    if not match:
        raise SystemExit("Cannot locate kVersion in awok_common.h")
    return match.group(1)


def find_esptool() -> str:
    candidates = sorted(
        glob.glob(str(Path.home() /
                      ".arduino15/packages/esp32/tools/esptool_py/*/esptool")) +
        glob.glob(str(Path.home() /
                      "Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool")))
    if not candidates:
        raise SystemExit(
            "esptool not found under the ESP32 Arduino core. Install the core "
            "(or `pip install esptool` and pass it on PATH).")
    return candidates[-1]  # newest installed


def autodetect_port() -> str:
    # Native USB (orange, download mode) first, then UART bridges (white).
    acm = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/cu.usbmodem*"))
    uart = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/cu.usbserial*"))
    found = acm + uart
    if not found:
        raise SystemExit(
            "No serial port found. Plug the board in (and for the orange/native "
            "USB port, put the chip in download mode: hold BOOT, tap RESET), "
            "then pass --port explicitly.")
    if len(found) > 1:
        raise SystemExit(
            "Multiple serial ports found: " + ", ".join(found) +
            ".\nPass --port to pick one (ttyACM* = orange/native USB, "
            "ttyUSB* = white/UART).")
    return found[0]


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("board", choices=tuple(CHIP))
    parser.add_argument("--port", help="serial port (autodetected if omitted)")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--build", action="store_true",
                        help="run build_firmware.py first if the image is stale")
    args = parser.parse_args()

    version = firmware_version()
    merged = (ROOT / "build" / f"{args.board}-{version}" /
              f"awokxdag-{version}-{args.board}-merged.bin")
    if args.build or not merged.is_file():
        subprocess.run([sys.executable, str(ROOT / "scripts/build_firmware.py"),
                        args.board], check=True)
    if not merged.is_file():
        raise SystemExit(f"Merged image not found: {merged}\n"
                         f"Build it first: build_firmware.py {args.board}")

    port = args.port or autodetect_port()
    command = [find_esptool(), "--chip", CHIP[args.board], "--port", port,
               "--baud", str(args.baud), "--before", "default_reset",
               "--after", "hard_reset", "write_flash", "0x0", str(merged)]
    print(f"Flashing {merged.name} -> {port} ({CHIP[args.board]})")
    print(" ".join(command))
    subprocess.run(command, check=True)
    print("Done. Reset the board to run the new firmware.")


if __name__ == "__main__":
    main()
