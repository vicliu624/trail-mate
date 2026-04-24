#!/usr/bin/env python3
"""Guard shared/platform UI boundaries from regressing."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class Rule:
    name: str
    roots: tuple[str, ...]
    includes: tuple[re.Pattern[str], ...]
    excludes: tuple[str, ...] = ()


@dataclass(frozen=True)
class Violation:
    path: Path
    line_number: int
    line: str
    rule_name: str


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh"}
EXCLUDED_DIR_NAMES = {
    ".git",
    ".pio",
    ".pytest_cache",
    ".tmp",
    ".venv",
    "__pycache__",
    "build",
    "dist",
}


def compile_patterns(patterns: tuple[str, ...]) -> tuple[re.Pattern[str], ...]:
    return tuple(re.compile(pattern) for pattern in patterns)


RULES = (
    Rule(
        name="legacy-platform-ui-shim-include",
        roots=(".",),
        includes=compile_patterns(
            (
                r'#include\s*[<"]ui/runtime/pack_repository\.h[>"]',
                r'#include\s*[<"]ui/screens/team/team_ui_store\.h[>"]',
            )
        ),
        excludes=(
            "modules/ui_shared/src/ui/runtime/pack_repository.cpp",
            "modules/ui_shared/src/ui/screens/team/team_ui_store.cpp",
        ),
    ),
    Rule(
        name="ui_shared-esp-only-include",
        roots=("modules/ui_shared",),
        includes=compile_patterns(
            (
                r'#include\s*[<"]Arduino\.h[>"]',
                r'#include\s*[<"]Preferences[>"]',
                r'#include\s*[<"]SD\.h[>"]',
                r'#include\s*[<"]FS\.h[>"]',
                r'#include\s*[<"]platform/esp/[^>"]+[>"]',
            )
        ),
    ),
    Rule(
        name="core-modules-platform-tail",
        roots=(
            "modules/core_chat",
            "modules/core_gps",
            "modules/core_hostlink",
            "modules/core_sys",
            "modules/core_team",
        ),
        includes=compile_patterns(
            (
                r'#include\s*[<"]Arduino\.h[>"]',
                r'#include\s*[<"]Preferences[>"]',
                r'#include\s*[<"]SD\.h[>"]',
                r'#include\s*[<"]FS\.h[>"]',
                r'#include\s*[<"]freertos/[^>"]+[>"]',
                r'#include\s*[<"]rtos\.h[>"]',
                r'#include\s*[<"]platform/esp/[^>"]+[>"]',
            )
        ),
        excludes=("modules/core_chat/generated",),
    ),
)


def iter_source_files(root: Path):
    for current_root, dir_names, file_names in os.walk(root):
        dir_names[:] = [name for name in dir_names if name not in EXCLUDED_DIR_NAMES]
        current_root_path = Path(current_root)
        for file_name in file_names:
            path = current_root_path / file_name
            if path.suffix.lower() in SOURCE_SUFFIXES:
                yield path


def is_excluded(path: Path, excludes: tuple[str, ...]) -> bool:
    relative = path.relative_to(REPO_ROOT).as_posix()
    return any(relative == prefix or relative.startswith(prefix.rstrip("/") + "/") for prefix in excludes)


def collect_violations() -> list[Violation]:
    violations: list[Violation] = []
    seen_files: set[Path] = set()

    for rule in RULES:
        for root_name in rule.roots:
            root_path = REPO_ROOT / root_name
            if not root_path.exists():
                continue
            for path in iter_source_files(root_path):
                if is_excluded(path, rule.excludes):
                    continue
                # Avoid rescanning repo root duplicates for the same rule set.
                key = (rule.name, path)
                if key in seen_files:
                    continue
                seen_files.add(key)
                for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), start=1):
                    if any(pattern.search(line) for pattern in rule.includes):
                        violations.append(
                            Violation(
                                path=path,
                                line_number=line_number,
                                line=line.strip(),
                                rule_name=rule.name,
                            )
                        )
    return violations


def main() -> int:
    violations = collect_violations()
    if not violations:
        print("Boundary check passed.")
        return 0

    print("Boundary check failed:")
    for violation in violations:
        relative = violation.path.relative_to(REPO_ROOT).as_posix()
        print(f"- [{violation.rule_name}] {relative}:{violation.line_number}")
        print(f"  {violation.line}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
