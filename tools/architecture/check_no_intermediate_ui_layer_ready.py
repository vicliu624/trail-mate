#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

FORBIDDEN_PATH_PARTS = [
    "intermediate_ui",
    "transitional_ui",
    "legacy_ui_extraction",
    "ui_migration_adapter",
    "temporary_ui_profile",
    "modules/ui_transition_",
    "modules/ui_intermediate_",
]

FORBIDDEN_CONTENT = [
    "intermediate_ui",
    "transitional_ui",
    "legacy_ui_extraction",
    "ui_migration_adapter",
    "temporary_ui_profile",
]

CONTENT_ROOTS = ["apps", "builds", "modules", "platform", "boards", "cmake"]
CODE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".c", ".cmake", ".txt", ".json"}


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def is_allowed_existing_deprecated_alias(path: Path) -> bool:
    return rel(path).startswith("modules/ui_legacy_adapters/")


def main() -> int:
    failures: list[str] = []

    for path in ROOT.rglob("*"):
        path_rel = rel(path)
        normalized = path_rel.lower()
        if is_allowed_existing_deprecated_alias(path):
            continue
        if normalized.startswith("docs/") or normalized.startswith("tools/"):
            continue
        for token in FORBIDDEN_PATH_PARTS:
            if token in normalized:
                failures.append(f"forbidden intermediate UI path token `{token}` in {path_rel}")

    for root_name in CONTENT_ROOTS:
        root = ROOT / root_name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in CODE_SUFFIXES:
                continue
            if is_allowed_existing_deprecated_alias(path):
                continue
            text = path.read_text(encoding="utf-8", errors="ignore").lower()
            for token in FORBIDDEN_CONTENT:
                if token in text:
                    failures.append(f"{rel(path)} contains forbidden token: {token}")

    if failures:
        for failure in failures:
            print(f"[no-intermediate-ui-layer] {failure}")
        return 1

    print("[no-intermediate-ui-layer] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
