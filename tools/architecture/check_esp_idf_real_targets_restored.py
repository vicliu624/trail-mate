#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

TARGETS = [
    "tab5",
    "tdisplayp4_tft",
    "tdisplayp4_amoled",
    "tdeck",
    "tlora_pager",
    "twatch",
]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def main() -> int:
    failures: list[str] = []

    for target in TARGETS:
        rel = f"builds/esp_idf/targets/{target}/sdkconfig.defaults"
        if not (ROOT / rel).is_file():
            failures.append(f"missing ESP-IDF target defaults: {rel}")

    readme = read("builds/esp_idf/targets/README.md")
    if "Deferred targets such as `tdeck`, `tlora_pager`, and `twatch`" in readme:
        failures.append("ESP-IDF targets README still marks tdeck/pager/twatch deferred")

    profiles = read("modules/product_composition/src/target_profile.cpp")
    for target in TARGETS:
        if f'"{target}"' not in profiles:
            failures.append(f"TargetProfile missing ESP-IDF target: {target}")

    target_profiles = read("builds/esp_idf/target_profiles.cmake")
    for target in TARGETS:
        if target not in target_profiles:
            failures.append(f"builds/esp_idf/target_profiles.cmake missing {target}")

    if failures:
        for failure in failures:
            print(f"[esp-idf-real-targets] {failure}")
        return 1

    print("[esp-idf-real-targets] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
