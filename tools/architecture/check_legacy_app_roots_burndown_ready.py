#!/usr/bin/env python3
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

AUDIT = "docs/audits/LEGACY_APP_IMPLEMENTATION_ROOT_BURN_DOWN_AUDIT.md"
INDEX = "legacy/app_implementations/LEGACY_IMPLEMENTATION_INDEX.md"
LINUX_COLLAPSE_AUDIT = "docs/audits/LEGACY_LINUX_LOCAL_ROOT_COLLAPSE_AUDIT.md"

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
    "nrf52_pio_legacy_implementation_adapter.h",
}

LINUX_LEGACY_ADAPTER_TOKENS = [
    "linux_sim_legacy_implementation_adapter",
    "uconsole_legacy_implementation_adapter",
    "trailmate_linux_sim_legacy_adapter",
    "trailmate_linux_uconsole_legacy_adapter",
    "legacy_adapter_target",
]


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


def check_final_linux_apps_do_not_reference_legacy_adapters(
    failures: list[str],
) -> None:
    for root_name in ["apps/linux_sim_shell", "apps/linux_uconsole_gtk"]:
        for path in iter_files(ROOT / root_name):
            if path.suffix not in CODE_SUFFIXES and path.name != "CMakeLists.txt":
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in LINUX_LEGACY_ADAPTER_TOKENS:
                if token in text:
                    failures.append(
                        f"{path.relative_to(ROOT).as_posix()} references burned-down Linux legacy adapter token: {token}"
                    )


def check_final_shell_owned_descriptors(failures: list[str]) -> None:
    required_files = [
        "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.h",
        "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.cpp",
        "apps/linux_sim_shell/tests/linux_sim_historical_source_descriptor_smoke.cpp",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.cpp",
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_historical_source_descriptor_smoke.cpp",
        "docs/audits/LEGACY_LINUX_APP_ADAPTER_BURN_DOWN_AUDIT.md",
    ]
    for rel in required_files:
        require_file(rel, failures)

    require_tokens(
        "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.h",
        [
            "LinuxSimHistoricalSourceDescriptor",
            "historical_root_name = \"legacy/app_implementations/linux_sim\"",
            "historical_role = \"pre-refactor Linux simulator implementation root\"",
            "replacement_owner = \"apps/linux_sim_shell + modules/ui_ascii_runtime\"",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.h",
        [
            "LinuxUConsoleGtkHistoricalSourceDescriptor",
            "historical_root_name = \"legacy/app_implementations/linux_uconsole\"",
            "historical_role = \"pre-refactor uConsole GTK implementation root\"",
            "replacement_owner = \"apps/linux_uconsole_gtk + modules/ui_gtk_runtime\"",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/LEGACY_LINUX_APP_ADAPTER_BURN_DOWN_AUDIT.md",
        [
            "linux_sim_legacy_implementation_adapter",
            "uconsole_legacy_implementation_adapter",
            "replacement owner",
            "migration decision",
            "legacy-local compatibility only",
        ],
        failures,
    )


def check_linux_local_root_collapse(failures: list[str]) -> None:
    require_file(LINUX_COLLAPSE_AUDIT, failures)
    require_tokens(
        LINUX_COLLAPSE_AUDIT,
        [
            "legacy/app_implementations/linux_sim",
            "legacy/app_implementations/linux_uconsole",
            "Current local CMake targets",
            "Current smoke targets",
            "Current composition root files",
            "Current old runtime/widget files",
            "Which targets are still referenced by final app shells",
            "Which targets can be removed from normal build",
            "Which files must remain as archive",
            "Which files can move to tests/compatibility",
            "Collapse decision",
            "local CMake root should become archive-only",
            "validation lives in `apps/linux_sim_shell`",
            "validation lives in `apps/linux_uconsole_gtk`",
        ],
        failures,
    )

    marker_files = [
        "legacy/app_implementations/linux_sim/ARCHIVE.md",
        "legacy/app_implementations/linux_uconsole/ARCHIVE.md",
    ]
    for rel in marker_files:
        require_tokens(
            rel,
            [
                "archive-only",
                "Replacement owner",
                "Do not use from final app shells",
            ],
            failures,
        )

    cmake_files = [
        "legacy/app_implementations/linux_sim/CMakeLists.txt",
        "legacy/app_implementations/linux_uconsole/CMakeLists.txt",
    ]
    forbidden_cmake_tokens = ["add_library(", "add_executable(", "add_test("]
    for rel in cmake_files:
        require_file(rel, failures)
        text = read(rel) if (ROOT / rel).is_file() else ""
        for token in forbidden_cmake_tokens:
            if token in text:
                failures.append(f"{rel} contains active CMake target token: {token}")
        require_tokens(
            rel,
            [
                "archive-only",
                "Use apps/",
            ],
            failures,
        )

    forbidden_active_paths = [
        "legacy/app_implementations/linux_sim/src/linux_sim_composition_root.cpp",
        "legacy/app_implementations/linux_sim/src/linux_sim_composition_root.h",
        "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.cpp",
        "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.h",
        "legacy/app_implementations/linux_sim/src/linux_sim_legacy_implementation_adapter.cpp",
        "legacy/app_implementations/linux_uconsole/src/uconsole_legacy_implementation_adapter.cpp",
    ]
    for rel in forbidden_active_paths:
        if (ROOT / rel).exists():
            failures.append(f"active legacy Linux source should be archived, not present: {rel}")

    required_archive_paths = [
        "legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.cpp",
        "legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.h",
        "legacy/app_implementations/linux_sim/archive/simulator/sdl_simulator.cpp",
        "legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.cpp",
        "legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.h",
        "legacy/app_implementations/linux_uconsole/archive/gtk/gtk/gtk_uconsole_pages.cpp",
    ]
    for rel in required_archive_paths:
        require_file(rel, failures)

    deleted_smokes = [
        "legacy/app_implementations/linux_sim/tests/linux_sim_legacy_implementation_adapter_smoke.cpp",
        "legacy/app_implementations/linux_uconsole/tests/uconsole_legacy_implementation_adapter_smoke.cpp",
        "legacy/app_implementations/linux_sim/archive/tests/linux_sim_composition_root_smoke.cpp",
        "legacy/app_implementations/linux_uconsole/archive/tests/uconsole_composition_root_smoke.cpp",
    ]
    for rel in deleted_smokes:
        if (ROOT / rel).exists():
            failures.append(f"old legacy adapter smoke should not remain active: {rel}")

    deleted_archive_adapters = [
        "legacy/app_implementations/linux_sim/archive/adapters/linux_sim_legacy_implementation_adapter.cpp",
        "legacy/app_implementations/linux_sim/archive/adapters/linux_sim_legacy_implementation_adapter.h",
        "legacy/app_implementations/linux_uconsole/archive/adapters/uconsole_legacy_implementation_adapter.cpp",
        "legacy/app_implementations/linux_uconsole/archive/adapters/uconsole_legacy_implementation_adapter.h",
    ]
    for rel in deleted_archive_adapters:
        if (ROOT / rel).exists():
            failures.append(f"deleted archive adapter should not remain: {rel}")

    final_shell_archive_tokens = [
        "legacy/app_implementations/linux_sim/archive",
        "legacy/app_implementations/linux_uconsole/archive",
    ]
    for root_name in ["apps/linux_sim_shell", "apps/linux_uconsole_gtk"]:
        for path in iter_files(ROOT / root_name):
            if path.suffix not in CODE_SUFFIXES and path.name != "CMakeLists.txt":
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in final_shell_archive_tokens:
                if token in text:
                    failures.append(
                        f"{path.relative_to(ROOT).as_posix()} references archived legacy root: {token}"
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
    if not (ROOT / "legacy").exists():
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/architecture/check_no_root_legacy_ready.py")],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            output = (result.stdout + result.stderr).strip()
            print(f"[legacy-app-roots-burndown] {output}")
            return 1
        print("[legacy-app-roots-burndown] historical helper: root legacy removed")
        return 0

    failures: list[str] = []

    check_audit_completeness(failures)
    check_index_burndown_fields(failures)
    check_apps_do_not_directly_include_legacy_roots(failures)
    check_final_linux_apps_do_not_reference_legacy_adapters(failures)
    check_final_shell_owned_descriptors(failures)
    check_linux_local_root_collapse(failures)
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
