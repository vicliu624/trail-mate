#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

FORBIDDEN_TOKENS = [
    "ux_pack",
    "page_manifest",
    "layout_profile",
    "app_shell",
    "renderer",
    "TargetProfile",
    "product_composition",
]


def main() -> int:
    failures: list[str] = []

    boards_root = ROOT / "boards"
    for path in boards_root.rglob("*"):
        if not path.is_file() or path.suffix not in {".h", ".hpp", ".cpp", ".md"}:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in FORBIDDEN_TOKENS:
            if token in text:
                rel = path.relative_to(ROOT).as_posix()
                failures.append(f"{rel} contains board boundary violation token: {token}")

    if failures:
        for failure in failures:
            print(f"[board-facts-boundary] {failure}")
        return 1

    print("[board-facts-boundary] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
