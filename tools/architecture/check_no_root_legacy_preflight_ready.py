#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = "docs/audits/LEGACY_COMPAT_TEMP_SURFACE_INVENTORY.md"


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def iter_files(base: Path):
    if not base.exists():
        return
    for path in base.rglob("*"):
        if path.is_file():
            yield path


def main() -> int:
    failures: list[str] = []

    if not (ROOT / "legacy").is_dir():
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/architecture/check_no_root_legacy_ready.py")],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            output = (result.stdout + result.stderr).strip()
            print(f"[no-root-legacy-preflight] historical helper failed final checker:\n{output}")
            return 1
        print("[no-root-legacy-preflight] historical helper: root legacy already removed")
        return 0

    inventory_path = ROOT / INVENTORY
    if not inventory_path.is_file():
        failures.append(f"missing inventory: {INVENTORY}")
    else:
        inventory = read(INVENTORY)
        for token in [
            "## Surface: root legacy/",
            "Disposition:\n- Must Delete",
            "Delete condition:",
            "docs/archive",
        ]:
            if token not in inventory:
                failures.append(f"{INVENTORY} missing preflight token: {token}")

    for root_name in ["apps", "builds", "modules", "platform", "boards", "cmake"]:
        for path in iter_files(ROOT / root_name):
            rel = path.relative_to(ROOT).as_posix()
            if "legacy_source_descriptor" in rel:
                failures.append(f"{rel} still has legacy_source_descriptor filename")

    pio_text = read("builds/pio_nrf52/platformio.ini")
    for token in [
        "legacy/app_implementations/esp_pio/include",
        "legacy/app_implementations/esp_pio/src",
        "legacy/app_implementations/gat562_mesh_evb_pro/include",
        "legacy/app_implementations/gat562_mesh_evb_pro/src",
    ]:
        if token in pio_text:
            failures.append(f"builds/pio_nrf52/platformio.ini still has active legacy include/src token: {token}")

    if failures:
        for failure in failures:
            print(f"[no-root-legacy-preflight] {failure}")
        return 1

    print("[no-root-legacy-preflight] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
