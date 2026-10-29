#!/usr/bin/python3
"""Register an explicitly configured I2C peripheral with an existing kernel driver."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import time


def find_adapter(i2c_root, tree_root, node):
    relative = Path(node.lstrip("/"))
    if not node.startswith("/") or ".." in relative.parts:
        raise ValueError("adapter device-tree path must be absolute without parent traversal")
    target = (tree_root / relative).resolve(strict=True)
    matches = [entry for entry in i2c_root.glob("i2c-*")
               if re.fullmatch(r"i2c-[0-9]+", entry.name) and
               (entry / "of_node").exists() and (entry / "of_node").resolve() == target]
    if len(matches) != 1:
        raise RuntimeError("no unique I2C adapter matches the configured device-tree node")
    return matches[0]


def create_client(adapter, name, address):
    (adapter / "new_device").write_text(f"{name} 0x{address:02x}\n")


def register_peripheral(adapter, name, address, create=create_client, timeout=5):
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,31}", name) or not 0x03 <= address <= 0x77:
        raise ValueError("invalid I2C device name or address")
    number = adapter.name.removeprefix("i2c-")
    if not number.isdigit():
        raise ValueError("invalid I2C adapter")
    client = adapter.parent / f"{number}-{address:04x}"
    created = not client.exists()
    if created:
        create(adapter, name, address)
    deadline = time.monotonic() + timeout
    while True:
        if client.exists():
            actual = (client / "name").read_text().strip()
            if actual != name:
                raise RuntimeError("configured I2C address belongs to a different device")
            if (client / "driver").exists():
                for rtc in client.glob("rtc/rtc*"):
                    try:
                        epoch = int((rtc / "since_epoch").read_text().strip())
                    except (OSError, ValueError):
                        continue
                    if epoch >= 0:
                        return {"adapter": adapter.name, "device": name, "address": hex(address),
                                "rtc": rtc.name, "created": created}
        if time.monotonic() >= deadline:
            raise RuntimeError("RTC driver did not expose a valid hardware clock; no time reset was attempted")
        time.sleep(0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adapter-of-node", required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--address", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--module", required=True)
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"[A-Za-z0-9_-]+", args.module):
            raise ValueError("invalid kernel module name")
        adapter = find_adapter(Path("/sys/bus/i2c/devices"), Path("/sys/firmware/devicetree/base"), args.adapter_of_node)
        modprobe = shutil.which("modprobe")
        if not modprobe:
            raise RuntimeError("kernel module loader is unavailable")
        subprocess.run([modprobe, args.module], check=True)
        print(json.dumps(register_peripheral(adapter, args.device, args.address)))
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"I2C peripheral setup failed: {error}\n")


if __name__ == "__main__":
    main()
