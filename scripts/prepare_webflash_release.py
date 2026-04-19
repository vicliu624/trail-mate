from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from webflash_targets import TARGETS_BY_ENV


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create merged ESP Web Tools firmware binaries for a built PlatformIO env."
    )
    parser.add_argument("--env", required=True, help="PlatformIO environment name")
    parser.add_argument(
        "--build-root",
        default=".pio/build",
        help="Root directory that contains PlatformIO build outputs",
    )
    parser.add_argument(
        "--dist",
        default="dist",
        help="Directory where merged web flasher binaries should be written",
    )
    parser.add_argument(
        "--boot-app0",
        required=True,
        help="Path to boot_app0.bin from the Arduino ESP32 framework package",
    )
    parser.add_argument(
        "--esptool",
        required=True,
        help="Path to esptool.py used to merge the firmware image",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    target = TARGETS_BY_ENV.get(args.env)
    if target is None:
        print(f"Skipping web flasher packaging for unsupported env: {args.env}")
        return 0

    build_dir = Path(args.build_root) / args.env
    dist_dir = Path(args.dist)
    dist_dir.mkdir(parents=True, exist_ok=True)

    bootloader = build_dir / "bootloader.bin"
    partitions = build_dir / "partitions.bin"
    firmware = build_dir / "firmware.bin"
    boot_app0 = Path(args.boot_app0)
    esptool = Path(args.esptool)
    output = dist_dir / target["merged_asset_name"]

    required_paths = (bootloader, partitions, firmware, boot_app0, esptool)
    missing_paths = [str(path) for path in required_paths if not path.exists()]
    if missing_paths:
        raise FileNotFoundError(
            "Missing required files for web flasher packaging:\n  "
            + "\n  ".join(missing_paths)
        )

    command = [
        sys.executable,
        str(esptool),
        "--chip",
        "esp32s3",
        "merge_bin",
        "-o",
        str(output),
        "--flash_mode",
        target["flash_mode"],
        "--flash_freq",
        target["flash_freq"],
        "--flash_size",
        target["flash_size"],
        "0x0",
        str(bootloader),
        "0x8000",
        str(partitions),
        "0xe000",
        str(boot_app0),
        "0x10000",
        str(firmware),
    ]

    print(f"Creating merged web flasher image for {args.env} -> {output}")
    subprocess.run(command, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
