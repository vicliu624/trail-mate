#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

CHECKERS = [
    "tools/architecture/check_uconsole_real_ui_restored.py",
    "tools/architecture/check_pio_real_env_restored.py",
    "tools/architecture/check_esp_arduino_real_targets_restored.py",
    "tools/architecture/check_esp_idf_real_targets_restored.py",
    "tools/architecture/check_no_root_legacy_ready.py",
    "tools/architecture/check_all_target_architecture_ready.py",
]


def main() -> int:
    failures: list[str] = []
    for rel in CHECKERS:
        path = ROOT / rel
        if not path.is_file():
            failures.append(f"missing checker: {rel}")
            continue
        result = subprocess.run(
            [sys.executable, str(path)],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            output = (result.stdout + result.stderr).strip()
            failures.append(f"{rel} failed:\n{output}")

    if failures:
        for failure in failures:
            print(f"[real-target-regression-recovery] {failure}")
        return 1

    print("[real-target-regression-recovery] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
