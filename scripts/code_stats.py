#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Iterator, Optional, Tuple


@dataclass
class Stats:
    files: int = 0
    total: int = 0
    code: int = 0
    comment: int = 0
    blank: int = 0

    def add(self, other: "Stats") -> None:
        self.files += other.files
        self.total += other.total
        self.code += other.code
        self.comment += other.comment
        self.blank += other.blank


C_LIKE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp", ".tpp", ".ino", ".proto"
}
HASH_COMMENT_SUFFIXES = {".py", ".sh", ".bash", ".zsh", ".toml", ".yml", ".yaml"}
SLASH_COMMENT_SUFFIXES = {".js", ".ts", ".tsx", ".jsx", ".java", ".cs", ".rs", ".go"}
POWERSHELL_SUFFIXES = {".ps1", ".psm1", ".psd1"}
CMAKE_SUFFIXES = {".cmake"}

DEFAULT_EXCLUDE_DIRS = {
    ".git",
    ".pio",
    ".tmp",
    ".vscode",
    ".idea",
    "__pycache__",
    "managed_components",
}
DEFAULT_EXCLUDE_PREFIXES = ("build",)
DEFAULT_EXCLUDE_FILES = {
    "sdkconfig",
    "sdkconfig.old",
}


def detect_language(path: Path) -> Optional[str]:
    name = path.name
    suffix = path.suffix.lower()

    if name == "CMakeLists.txt" or suffix in CMAKE_SUFFIXES:
        return "CMake"
    if suffix in C_LIKE_SUFFIXES:
        return "C/C++"
    if suffix in HASH_COMMENT_SUFFIXES:
        return "Script"
    if suffix in POWERSHELL_SUFFIXES:
        return "PowerShell"
    if suffix in SLASH_COMMENT_SUFFIXES:
        return "OtherCode"
    return None


def should_skip_dir(dirname: str, include_managed: bool, include_build: bool) -> bool:
    lower = dirname.lower()
    if lower in DEFAULT_EXCLUDE_DIRS:
        if lower == "managed_components" and include_managed:
            return False
        return True
    if not include_build and any(lower == prefix or lower.startswith(prefix + ".") for prefix in DEFAULT_EXCLUDE_PREFIXES):
        return True
    return False


def iter_source_files(root: Path, include_managed: bool, include_build: bool) -> Iterator[Tuple[Path, str]]:
    for current_root, dirnames, filenames in os.walk(root):
        dirnames[:] = [
            dirname for dirname in dirnames
            if not should_skip_dir(dirname, include_managed=include_managed, include_build=include_build)
        ]

        current_path = Path(current_root)
        for filename in filenames:
            if filename in DEFAULT_EXCLUDE_FILES:
                continue
            path = current_path / filename
            language = detect_language(path)
            if language:
                yield path, language


def count_hash_comment_lines(lines: Iterable[str], comment_prefix: str = "#") -> Stats:
    stats = Stats(files=1)
    for raw_line in lines:
        stats.total += 1
        stripped = raw_line.strip()
        if not stripped:
            stats.blank += 1
        elif stripped.startswith(comment_prefix):
            stats.comment += 1
        else:
            stats.code += 1
    return stats


def count_c_like_lines(lines: Iterable[str]) -> Stats:
    stats = Stats(files=1)
    in_block_comment = False

    for raw_line in lines:
        stats.total += 1
        line = raw_line.rstrip("\n\r")
        if not line.strip() and not in_block_comment:
            stats.blank += 1
            continue

        i = 0
        has_code = False
        has_comment = False
        length = len(line)

        while i < length:
            if in_block_comment:
                has_comment = True
                end = line.find("*/", i)
                if end == -1:
                    i = length
                    break
                in_block_comment = False
                i = end + 2
                continue

            if line.startswith("//", i):
                has_comment = True
                break

            if line.startswith("/*", i):
                has_comment = True
                in_block_comment = True
                i += 2
                continue

            ch = line[i]
            if ch in ('"', "'"):
                has_code = True
                quote = ch
                i += 1
                while i < length:
                    if line[i] == "\\":
                        i += 2
                    elif line[i] == quote:
                        i += 1
                        break
                    else:
                        i += 1
                continue

            if not ch.isspace():
                has_code = True
            i += 1

        if has_code:
            stats.code += 1
        elif has_comment or in_block_comment:
            stats.comment += 1
        else:
            stats.blank += 1

    return stats


def count_file(path: Path, language: str) -> Stats:
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        lines = handle.readlines()

    if language == "C/C++" or language == "OtherCode":
        return count_c_like_lines(lines)
    if language == "PowerShell":
        return count_hash_comment_lines(lines, comment_prefix="#")
    return count_hash_comment_lines(lines, comment_prefix="#")


def top_level_group(root: Path, path: Path) -> str:
    rel = path.relative_to(root)
    if len(rel.parts) <= 1:
        return "."
    return rel.parts[0]


def format_table(rows: list[tuple[str, Stats]], title: str) -> str:
    if not rows:
        return f"{title}\n  <empty>"

    header = ("Name", "Files", "Total", "Code", "Comment", "Blank")
    widths = [
        max(len(str(item[idx])) for item in ([header] + [(name, s.files, s.total, s.code, s.comment, s.blank) for name, s in rows]))
        for idx in range(len(header))
    ]

    def fmt_row(values: tuple[object, ...]) -> str:
        return "  " + "  ".join(
            str(value).ljust(widths[idx]) if idx == 0 else str(value).rjust(widths[idx])
            for idx, value in enumerate(values)
        )

    lines = [title, fmt_row(header), fmt_row(tuple("-" * width for width in widths))]
    for name, stat in rows:
        lines.append(fmt_row((name, stat.files, stat.total, stat.code, stat.comment, stat.blank)))
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="统计工程中的代码行数")
    parser.add_argument("root", nargs="?", default=".", help="工程根目录，默认当前目录")
    parser.add_argument("--include-managed", action="store_true", help="包含 managed_components")
    parser.add_argument("--include-build", action="store_true", help="包含 build/build.* 目录")
    parser.add_argument("--by-dir", action="store_true", help="额外输出按顶层目录分组的统计")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    all_stats = Stats()
    by_language: Dict[str, Stats] = {}
    by_dir: Dict[str, Stats] = {}

    for path, language in iter_source_files(root, include_managed=args.include_managed, include_build=args.include_build):
        stat = count_file(path, language)
        all_stats.add(stat)
        by_language.setdefault(language, Stats()).add(stat)
        if args.by_dir:
            by_dir.setdefault(top_level_group(root, path), Stats()).add(stat)

    print(f"Root: {root}")
    print("Exclude dirs: " + ", ".join(sorted(DEFAULT_EXCLUDE_DIRS if not args.include_managed else (DEFAULT_EXCLUDE_DIRS - {'managed_components'}))))
    print("Exclude build dirs: " + ("no" if args.include_build else "yes"))
    print()

    language_rows = sorted(by_language.items(), key=lambda item: (-item[1].code, item[0]))
    print(format_table(language_rows, "By Language"))
    print()
    print(format_table([("TOTAL", all_stats)], "Summary"))

    if args.by_dir:
        print()
        dir_rows = sorted(by_dir.items(), key=lambda item: (-item[1].code, item[0]))
        print(format_table(dir_rows, "By Top-Level Directory"))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
