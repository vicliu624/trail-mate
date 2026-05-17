#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def iter_files(root: Path):
    if not root.exists():
        return
    for path in root.rglob("*"):
        if path.is_file():
            yield path


def main() -> int:
    failures: list[str] = []

    platformio_rel = "builds/pio_nrf52/platformio.ini"
    platformio = read(platformio_rel)
    for token in [
        "[env:gat562_mesh_evb_pro]",
        "board = gat562_mesh_evb_pro",
        "symlink://../../apps/nrf52_node",
        "symlink://../../platform/nrf52",
        "symlink://../../boards/gat562_mesh_evb_pro",
    ]:
        if token not in platformio:
            failures.append(f"{platformio_rel} missing token: {token}")

    for rel in [
        "apps/nrf52_node/src/nrf52_node_startup_runtime.cpp",
        "apps/nrf52_node/src/nrf52_node_loop_runtime.cpp",
        "apps/nrf52_node/src/nrf52_node_arduino_entry.cpp",
        "apps/nrf52_node/src/nrf52_node_runtime_config.cpp",
        "apps/nrf52_node/src/nrf52_node_app_runtime_access.cpp",
        "platform/nrf52/README.md",
        "platform/nrf52/debug/nrf52_debug_console.cpp",
        "platform/nrf52/protocol/nrf52_protocol_factory.cpp",
        "platform/nrf52/runtime/nrf52_runtime_apply_service.cpp",
        "builds/pio_nrf52/src/gat562_mesh_evb_pro.cpp",
    ]:
        if not (ROOT / rel).is_file():
            failures.append(f"missing restored PIO/nRF52 owner file: {rel}")

    for forbidden_dir in [
        "apps/esp_pio",
        "apps/gat562_mesh_evb_pro",
    ]:
        if (ROOT / forbidden_dir).exists():
            failures.append(f"{forbidden_dir}/ must not be restored")

    for root_name in ["apps", "builds", "platform", "boards"]:
        for path in iter_files(ROOT / root_name):
            text = path.read_text(encoding="utf-8", errors="ignore")
            if "legacy/app_implementations" in text:
                failures.append(
                    f"{path.relative_to(ROOT).as_posix()} contains legacy/app_implementations"
                )

    if failures:
        for failure in failures:
            print(f"[pio-real-env] {failure}")
        return 1

    print("[pio-real-env] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
