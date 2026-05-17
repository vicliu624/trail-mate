#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

REQUIRED_GTK_SOURCES = [
    "apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_app.cpp",
    "apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_pages.cpp",
    "apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_chat_layout.cpp",
    "apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_chat_logic.cpp",
    "apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_layout.cpp",
    "apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_logic.cpp",
    "apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_overview_layout.cpp",
    "apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp",
]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def main() -> int:
    failures: list[str] = []

    for rel in REQUIRED_GTK_SOURCES:
        if not (ROOT / rel).is_file():
            failures.append(f"missing restored uConsole GTK source: {rel}")

    cmake_rel = "apps/linux_uconsole_gtk/CMakeLists.txt"
    cmake = read(cmake_rel) if (ROOT / cmake_rel).is_file() else ""
    for rel in REQUIRED_GTK_SOURCES:
        token = rel.removeprefix("apps/linux_uconsole_gtk/")
        if token not in cmake:
            failures.append(f"{cmake_rel} does not list {token}")

    for rel in [
        "apps/linux_uconsole_gtk/packaging/trailmate-uconsole.desktop",
        "apps/linux_uconsole_gtk/packaging/trailmate-uconsole.png",
        "apps/linux_uconsole_gtk/tools/sx1262_probe.cpp",
        "apps/linux_uconsole_gtk/src/targets/uconsole_main.cpp",
    ]:
        if not (ROOT / rel).is_file():
            failures.append(f"missing restored uConsole packaging/tool file: {rel}")

    if (ROOT / "apps/linux_uconsole").exists():
        failures.append("apps/linux_uconsole/ must not be restored")

    if failures:
        for failure in failures:
            print(f"[uconsole-real-ui] {failure}")
        return 1

    print("[uconsole-real-ui] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
