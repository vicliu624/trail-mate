#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

AUDIT = "docs/audits/LEGACY_APP_IMPLEMENTATION_ROOT_BURN_DOWN_AUDIT.md"
INDEX = "legacy/app_implementations/LEGACY_IMPLEMENTATION_INDEX.md"

LEGACY_ROOTS = [
    "legacy/app_implementations/esp_idf",
    "legacy/app_implementations/esp_pio",
    "legacy/app_implementations/gat562_mesh_evb_pro",
    "legacy/app_implementations/linux_rpi",
    "legacy/app_implementations/linux_sim",
    "legacy/app_implementations/linux_uconsole",
    "legacy/app_implementations/linux_unoq",
]

REQUIRED_AUDIT_FIELDS = [
    "Current purpose:",
    "Current build callers:",
    "Current app shell callers:",
    "Current CMake / PlatformIO callers:",
    "Current include callers:",
    "Runtime ownership status:",
    "Safe to delete now?",
    "First deletion blocker:",
    "Target replacement owner:",
    "Burn-down step:",
    "Deletion blocker:",
    "Replacement owner:",
]

REQUIRED_INDEX_FIELDS = [
    "Burn-down status:",
    "Deletion blocker:",
    "Replacement owner:",
    "Next deletion task:",
]

POST_REFACTOR_TOKENS = [
    "RuntimeEntryAdoption",
    "DescriptorRenderer",
    "ScreenGraphPresenter",
]

CODE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
CMAKE_SOURCE_NAMES = {"CMakeLists.txt", "platformio.ini"}
CMAKE_SOURCE_SUFFIXES = {".cmake"}

SKIP_DIRS = {
    ".git",
    ".vs",
    ".vscode",
    "build",
    "builds-out",
    "cmake-build-debug",
    "cmake-build-release",
    "_deps",
}

DECLARED_TRANSITIONAL_APP_HEADERS = {
    "esp_idf_legacy_implementation_adapter.h",
    "linux_sim_legacy_implementation_adapter.h",
    "nrf52_pio_legacy_implementation_adapter.h",
    "uconsole_legacy_implementation_adapter.h",
}


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required file: {rel}")


def require_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        failures.append(f"missing file for token check: {rel}")
        return
    text = read(rel)
    for token in tokens:
        if token not in text:
            failures.append(f"{rel} missing token: {token}")


def iter_files(base: Path):
    if not base.exists():
        return
    for path in base.rglob("*"):
        if not path.is_file():
            continue
        parts = set(path.relative_to(ROOT).parts)
        if parts.intersection(SKIP_DIRS):
            continue
        yield path


def iter_code_files(base: Path):
    for path in iter_files(base):
        if path.suffix in CODE_SUFFIXES:
            yield path


def check_audit_completeness(failures: list[str]) -> None:
    require_file(AUDIT, failures)
    require_file(INDEX, failures)
    if not (ROOT / AUDIT).is_file():
        return

    audit_text = read(AUDIT)
    for root in LEGACY_ROOTS:
        if root not in audit_text:
            failures.append(f"{AUDIT} missing legacy root: {root}")

    for field in REQUIRED_AUDIT_FIELDS:
        count = audit_text.count(field)
        if count < len(LEGACY_ROOTS):
            failures.append(
                f"{AUDIT} has {count} occurrences of '{field}', expected at least {len(LEGACY_ROOTS)}"
            )

    require_tokens(
        AUDIT,
        [
            "CMake And PlatformIO References Listed For Checker",
            "Linux Root Burn-Down Baseline",
            "`legacy/app_implementations/linux_sim`",
            "`legacy/app_implementations/linux_uconsole`",
            "no `RuntimeEntryAdoption`, `DescriptorRenderer`, or",
            "`ScreenGraphPresenter` ownership remains under the root",
        ],
        failures,
    )


def check_index_burndown_fields(failures: list[str]) -> None:
    if not (ROOT / INDEX).is_file():
        return
    index_text = read(INDEX)
    for root in LEGACY_ROOTS:
        if root not in index_text:
            failures.append(f"{INDEX} missing legacy root: {root}")
    for field in REQUIRED_INDEX_FIELDS:
        count = index_text.count(field)
        if count < len(LEGACY_ROOTS):
            failures.append(
                f"{INDEX} has {count} occurrences of '{field}', expected at least {len(LEGACY_ROOTS)}"
            )


def check_apps_do_not_directly_include_legacy_roots(failures: list[str]) -> None:
    include_re = re.compile(r"#\s*include\s*[<\"]([^>\"]+)[>\"]")
    for path in iter_code_files(ROOT / "apps"):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in include_re.finditer(text):
            include_path = match.group(1)
            if "legacy/app_implementations" not in include_path:
                continue
            if Path(include_path).name in DECLARED_TRANSITIONAL_APP_HEADERS:
                continue
            failures.append(
                f"{path.relative_to(ROOT).as_posix()} directly includes legacy app implementation header: {include_path}"
            )


def check_modules_do_not_reference_legacy_roots(failures: list[str]) -> None:
    for path in iter_code_files(ROOT / "modules"):
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "legacy/app_implementations" in text:
            failures.append(
                f"{path.relative_to(ROOT).as_posix()} references legacy/app_implementations"
            )


def check_legacy_roots_do_not_own_post_refactor_runtime(failures: list[str]) -> None:
    legacy_root = ROOT / "legacy" / "app_implementations"
    for path in iter_code_files(legacy_root):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in POST_REFACTOR_TOKENS:
            if token in text:
                failures.append(
                    f"{path.relative_to(ROOT).as_posix()} contains post-refactor ownership token: {token}"
                )


def is_cmake_or_platformio_file(path: Path) -> bool:
    return path.name in CMAKE_SOURCE_NAMES or path.suffix in CMAKE_SOURCE_SUFFIXES


def check_build_references_are_listed(failures: list[str]) -> None:
    if not (ROOT / AUDIT).is_file():
        return
    audit_text = read(AUDIT)
    scan_roots = [
        ROOT / "CMakeLists.txt",
        ROOT / "apps",
        ROOT / "builds",
        ROOT / "cmake",
        ROOT / "legacy" / "app_implementations",
    ]
    checked: set[Path] = set()
    for scan_root in scan_roots:
        if scan_root.is_file():
            candidate_files = [scan_root]
        else:
            candidate_files = list(iter_files(scan_root))
        for path in candidate_files:
            if path in checked or not is_cmake_or_platformio_file(path):
                continue
            checked.add(path)
            text = path.read_text(encoding="utf-8", errors="ignore")
            if "legacy/app_implementations" not in text:
                continue
            rel = path.relative_to(ROOT).as_posix()
            if rel not in audit_text:
                failures.append(f"{AUDIT} does not list CMake/PIO reference file: {rel}")
            for root in LEGACY_ROOTS:
                if root in text and root not in audit_text:
                    failures.append(
                        f"{AUDIT} does not list legacy root {root} referenced by {rel}"
                    )


def main() -> int:
    failures: list[str] = []

    check_audit_completeness(failures)
    check_index_burndown_fields(failures)
    check_apps_do_not_directly_include_legacy_roots(failures)
    check_modules_do_not_reference_legacy_roots(failures)
    check_legacy_roots_do_not_own_post_refactor_runtime(failures)
    check_build_references_are_listed(failures)

    if failures:
        for failure in failures:
            print(f"[legacy-app-roots-burndown] {failure}")
        return 1

    print("[legacy-app-roots-burndown] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
