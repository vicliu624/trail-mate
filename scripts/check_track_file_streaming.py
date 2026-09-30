#!/usr/bin/env python3
"""Reject non-streaming GPX/KML route and track file reads."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os
import re
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh"}

TRACK_CONTEXT_MARKERS = (
    ".gpx",
    ".kml",
    "gpx",
    "kml",
    "route_storage",
    "route_path",
    "load_map_track_file",
    "track_file",
    "trackers",
    "routes",
)

TRACK_PATH_HINTS = (
    "modules/ui_shared/src/ui/screens/gps/",
    "modules/ui_shared/src/ui/screens/tracker/",
    "modules/ui_shared/include/ui/screens/gps/",
    "modules/ui_shared/include/ui/screens/tracker/",
    "platform/esp/arduino_common/src/platform_ui_route_storage.cpp",
    "platform/linux/common/src/platform/ui/route_storage.cpp",
)

FORBIDDEN_LINE_PATTERNS = (
    ("arduino_read_string", re.compile(r"\.readString\s*\(")),
    ("istreambuf_slurp", re.compile(r"istreambuf_iterator\s*<")),
    ("rdbuf_slurp", re.compile(r"<<\s*[^;]+\.rdbuf\s*\(")),
)

SIZE_READ_PATTERN = re.compile(r"\b(?:(?P<receiver>\w+)\s*\.\s*)?(?:size|available)\s*\(\s*\)")
FIXED_ARRAY_PATTERN = re.compile(r"\bstd::array\s*<[^;{}]+>\s+(\w+)\s*(?:\{|;|=)")
READ_CALL_PATTERN = re.compile(r"\.read\s*\(")


@dataclass(frozen=True)
class Violation:
    path: Path
    line_number: int
    rule: str
    line: str
    detail: str


def normalize(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def git_tracked_files() -> list[Path]:
    try:
        result = subprocess.run(
            ["git", "ls-files"],
            cwd=REPO_ROOT,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError):
        return []
    return [REPO_ROOT / line for line in result.stdout.splitlines() if line]


def walk_source_files() -> list[Path]:
    tracked = git_tracked_files()
    if tracked:
        return [path for path in tracked if path.suffix.lower() in SOURCE_SUFFIXES]

    source_files: list[Path] = []
    excluded_dirs = {".git", ".pio", ".tmp", "build", "dist", "third_party"}
    for root, dir_names, file_names in os.walk(REPO_ROOT):
        dir_names[:] = [name for name in dir_names if name not in excluded_dirs]
        root_path = Path(root)
        for file_name in file_names:
            path = root_path / file_name
            if path.suffix.lower() in SOURCE_SUFFIXES:
                source_files.append(path)
    return source_files


def is_track_related(relative: str, text: str) -> bool:
    if any(relative.startswith(prefix) for prefix in TRACK_PATH_HINTS):
        return True
    lowered = text.lower()
    return any(marker in lowered for marker in TRACK_CONTEXT_MARKERS)


def local_track_context(lines: list[str], index: int, radius: int = 8) -> bool:
    start = max(0, index - radius)
    end = min(len(lines), index + radius + 1)
    local = "\n".join(lines[start:end]).lower()
    return any(marker in local for marker in TRACK_CONTEXT_MARKERS)


def collect_violations() -> list[Violation]:
    violations: list[Violation] = []
    for path in walk_source_files():
        relative = normalize(path)
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        if not is_track_related(relative, text):
            continue

        lines = text.splitlines()
        fixed_arrays = set(FIXED_ARRAY_PATTERN.findall(text))
        for line_number, line in enumerate(lines, start=1):
            for rule, pattern in FORBIDDEN_LINE_PATTERNS:
                if pattern.search(line) and local_track_context(lines, line_number - 1):
                    violations.append(
                        Violation(
                            path=path,
                            line_number=line_number,
                            rule=rule,
                            line=line.strip(),
                            detail="GPX/KML route and track files must be consumed as bounded streams.",
                        )
                    )

        for index, line in enumerate(lines):
            # Fixed array capacity is a streaming bound, not a file length.
            # Keep all other size/available calls subject to the original rule.
            if not any(match.group("receiver") not in fixed_arrays
                       for match in SIZE_READ_PATTERN.finditer(line)):
                continue
            window = lines[index : min(index + 6, len(lines))]
            if any(READ_CALL_PATTERN.search(candidate) for candidate in window) and local_track_context(
                lines, index
            ):
                violations.append(
                    Violation(
                        path=path,
                        line_number=index + 1,
                        rule="size_then_read",
                        line=line.strip(),
                        detail="Do not allocate/read a whole GPX/KML file from its size; use chunked streaming.",
                    )
                )
    return violations


def main() -> int:
    violations = collect_violations()
    if not violations:
        print("Track file streaming check passed.")
        return 0

    print("Track file streaming check failed:")
    for violation in violations:
        print(
            f"{normalize(violation.path)}:{violation.line_number}: "
            f"{violation.rule}: {violation.detail}"
        )
        print(f"  {violation.line}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
