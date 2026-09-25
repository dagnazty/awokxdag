#!/usr/bin/env python3
"""Compile and package a board-specific image; never opens or flashes a device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
C5_FQBN = "esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled"

CLASSIC_FQBN = "esp32:esp32:esp32:FlashSize=4M,PartitionScheme=huge_app,PSRAM=disabled"

# AWOKxDAG is BLE Observer-only (scan; no client/server/adv anywhere in the
# sketch), so NimBLE-Arduino's default Central/Peripheral/Broadcaster roles are
# pure waste. Trimming them shrinks the BLE footprint; the actual fix for BLE
# scan failing with HCI "Memory Capacity Exceeded" (NimBLE rc=519) on Dual C5
# is the time-multiplex RadioScheduler (only one radio DMA-resident at a time).
#
# NOTE: the esp32 Arduino core's own generated sdkconfig.h unconditionally
# #defines the NimBLE buffer-count and MEM_ALLOC_MODE macros (no #ifndef
# guard), so command-line -D overrides for CONFIG_BT_NIMBLE_MAX_CONNECTIONS /
# _MAX_BONDS / _MAX_CCCDS / _WHITELIST_SIZE / _TRANSPORT_ACL_FROM_LL_COUNT /
# _MSYS1_BLOCK_COUNT / _MEM_ALLOC_MODE_EXTERNAL are silently ignored. Don't add
# flags for those -- they compile without warnings but have no effect.
NIMBLE_OBSERVER_ONLY_FLAGS = (
    "-DCONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED "
    "-DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED "
    "-DCONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED"
)
# The merged bridge runs the full firmware headless with a GATT server: it needs
# the PERIPHERAL role (server), BROADCASTER role (advertising), and OBSERVER
# role (BLE-scan tools), so only Central is trimmed.
NIMBLE_BRIDGE_FLAGS = "-DCONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED"
LINKER_WRAP_FLAGS = (
    "-Wl,-z,muldefs "
    "-Wl,--wrap=esp_wifi_init -Wl,--wrap=esp_bt_controller_init"
)

PROFILES = {
    "dual-c5-touch": (C5_FQBN, "AWOK_DUAL_C5_TOUCH"),
    "pancake-c5": (C5_FQBN, "PANCAKE_C5"),
    "dual-c5-mini": (C5_FQBN, "AWOK_DUAL_C5_MINI"),
    "dual-esp32-touch-v1": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_V1"),
    "dual-esp32-touch-v2": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_V2"),
    "dual-esp32-touch-v3": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_V3"),
    "dual-esp32-mini-v1": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_V1"),
    "dual-esp32-mini-v2": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_V2"),
    "dual-esp32-mini-v3": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_V3"),
}

# Bridge profiles run the SAME firmware as the screen chip, just headless (no
# display/touch) with the BLE GATT server folded in. Touch/Mini and revision stay
# explicit for classic boards because their GPS and SD wiring differs.
BRIDGES = {
    "dual-c5-bridge": (C5_FQBN, "AWOK_DUAL_C5_BRIDGE"),
    "dual-esp32-touch-bridge-v1":
        (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_BRIDGE_V1"),
    "dual-esp32-touch-bridge-v2":
        (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_BRIDGE_V2"),
    "dual-esp32-touch-bridge-v3":
        (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_BRIDGE_V3"),
    "dual-esp32-mini-bridge-v1":
        (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_BRIDGE_V1"),
    "dual-esp32-mini-bridge-v2":
        (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_BRIDGE_V2"),
    "dual-esp32-mini-bridge-v3":
        (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_BRIDGE_V3"),
}
ALL_TARGETS = tuple(PROFILES) + tuple(BRIDGES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", choices=ALL_TARGETS)
    parser.add_argument("--build-path", type=Path)
    args = parser.parse_args()
    source = (ROOT / "AWOKxDAG/awok_common.h").read_text()
    version = re.search(r'kVersion\[\]\s*=\s*"([^"]+)"', source)
    if not version:
        raise SystemExit("Cannot locate kVersion in awok_common.h")
    version = version.group(1)
    work = (args.build_path or ROOT / "build" / (args.board + "-objects")).resolve()
    output = ROOT / "build" / f"{args.board}-{version}"

    if args.board in BRIDGES:
        fqbn, define = BRIDGES[args.board]
        sketch = ROOT / "AWOKxDAG/AWOKxDAG.ino"
        celf_flags = LINKER_WRAP_FLAGS
        cpp_flags = f"-D{define} {NIMBLE_BRIDGE_FLAGS}"
    else:
        fqbn, define = PROFILES[args.board]
        sketch = ROOT / "AWOKxDAG/AWOKxDAG.ino"
        celf_flags = LINKER_WRAP_FLAGS
        cpp_flags = f"-D{define} {NIMBLE_OBSERVER_ONLY_FLAGS}"
    stem = sketch.name  # e.g. AWOKxDAG.ino or AxDBridge.ino

    command = ["arduino-cli", "compile", "--fqbn", fqbn, "--warnings", "all",
               "--build-path", str(work)]
    # The raw-frame override and radio-memory wrappers are shared by screen and
    # bridge profiles because every target runs the same full firmware.
    command += ["--build-property", f"compiler.c.elf.extra_flags={celf_flags}"]
    command += ["--build-property", f"compiler.cpp.extra_flags={cpp_flags}"]
    # Error-level CORE_DEBUG_LEVEL: silent unless something is actually
    # wrong, but it's what surfaces NimBLE's own "Error starting scan"
    # rc codes -- those are compiled out entirely at the default level 0.
    command += ["--build-property", "build.code_debug=1"]
    command += [str(sketch)]
    subprocess.run(command, check=True)
    output.mkdir(parents=True, exist_ok=True)
    prefix = f"awokxdag-{version}-{args.board}"
    files = []
    for source_name, suffix in (
        (f"{stem}.merged.bin", "merged.bin"),
        (f"{stem}.bin", "app.bin"),
        (f"{stem}.bootloader.bin", "bootloader.bin"),
        (f"{stem}.partitions.bin", "partitions.bin"),
        ("boot_app0.bin", "boot_app0.bin"),
        (f"{stem}.elf", "elf"),
    ):
        target = output / f"{prefix}-{suffix}"
        shutil.copy2(work / source_name, target)
        files.append(target)
    metadata = output / "build-info.json"
    metadata.write_text(json.dumps({
        "version": version, "board": args.board, "fqbn": fqbn,
        "experimental": args.board.startswith("dual-esp32"),
        "hardware_tested": args.board in ("dual-c5-touch", "dual-c5-mini"),
        "compile_command": command,
        "core": subprocess.check_output(["arduino-cli", "core", "list"], text=True),
        "libraries": subprocess.check_output(["arduino-cli", "lib", "list"], text=True),
    }, indent=2) + "\n")
    files.append(metadata)
    (output / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files))
    print(f"Packaged firmware: {output}")


if __name__ == "__main__":
    main()
