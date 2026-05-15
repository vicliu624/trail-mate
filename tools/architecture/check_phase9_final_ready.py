#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required file: {rel}")


def require_absent(rel: str, failures: list[str]) -> None:
    if (ROOT / rel).exists():
        failures.append(f"old implementation should remain absent: {rel}")


def require_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        failures.append(f"missing file for token check: {rel}")
        return
    text = read(rel)
    for token in tokens:
        if token not in text:
            failures.append(f"{rel} missing token: {token}")


def require_any_token(rel: str, tokens: list[str], failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        failures.append(f"missing file for token check: {rel}")
        return
    text = read(rel)
    if not any(token in text for token in tokens):
        failures.append(f"{rel} missing one of: {', '.join(tokens)}")


def iter_code_files(root: Path):
    if not root.exists():
        return
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
            yield path


def is_allowed_alias_path(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    name = path.name
    allowed_prefixes = [
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/",
        "modules/ui_shared/include/ui/presentation_sources/",
    ]
    if any(rel.startswith(prefix) for prefix in allowed_prefixes):
        return True
    return (
        name.endswith("_legacy_alias_test.cpp")
        or name.endswith("_legacy_alias.cpp")
        or name.endswith("_compatibility_test.cpp")
    )


def check_no_main_code_includes_burned_down_legacy_headers(failures: list[str]) -> None:
    forbidden_includes = [
        '#include "ui_legacy_adapters/legacy_chat_delivery_action_bridge.h"',
        '#include "ui_legacy_adapters/legacy_chat_delivery_event_bridge.h"',
        '#include "ui_legacy_adapters/legacy_key_verification_source.h"',
        '#include "ui_legacy_adapters/legacy_key_verification_action_sink.h"',
        '#include "ui_legacy_adapters/legacy_key_verification_session.h"',
        '#include "ui_legacy_adapters/legacy_map_overlay_source.h"',
    ]
    for root_name in ["apps", "legacy/app_implementations", "modules", "platform", "boards"]:
        for path in iter_code_files(ROOT / root_name):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in forbidden_includes:
                if token in text and not is_allowed_alias_path(path):
                    failures.append(
                        f"{path.relative_to(ROOT).as_posix()} includes burned-down legacy header: {token}"
                    )


def run_checker(rel: str, failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        failures.append(f"missing checker: {rel}")
        return

    result = subprocess.run(
        [sys.executable, str(path)],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr).strip()
        failures.append(f"{rel} failed:\n{output}")


def check_final_report(failures: list[str]) -> None:
    require_tokens(
        "docs/audits/PHASE9_FINAL_READINESS_REPORT.md",
        [
            "Runtime Adoption",
            "ASCII / LinuxSim",
            "ASCII presenter",
            "GTK / uConsole",
            "GTK presenter",
            "LVGL presenter",
            "Legacy Burn-down",
            "ChatDelivery",
            "KeyVerification",
            "MapOverlay",
            "Remaining Fallback",
            "LinuxSim hardcoded runtime routing",
            "GTK hardcoded page registry",
            "LVGL hardcoded menu/page creation",
            "Phase 10 Entry Recommendation",
            "LinuxSim / ASCII primary path",
            "AsciiRuntimeEntryAdoption as primary source",
            "Phase 10 makes one adopted path the primary runtime path",
        ],
        failures,
    )


def check_cross_doc_consistency(failures: list[str]) -> None:
    docs = [
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
        "docs/audits/PHASE9_FINAL_READINESS_REPORT.md",
    ]
    stable_owner_tokens = [
        "ChatDeliveryActionPortAdapter",
        "ChatDeliveryEventProjectionAdapter",
        "KeyVerificationPresentationSource",
        "KeyVerificationActionSink",
        "KeyVerificationSessionAdapter",
        "MapOverlaySnapshotSource",
        "MapOverlayProjectionAdapter",
    ]
    for rel in docs:
        require_tokens(rel, stable_owner_tokens, failures)
        require_any_token(
            rel,
            ["main runtime callers removed", "no main runtime callers"],
            failures,
        )
        require_any_token(
            rel,
            ["deprecated aliases", "deprecated alias", "deprecated aliases only"],
            failures,
        )

    require_tokens(
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
        [
            "LinuxSim hardcoded runtime routing",
            "GTK hardcoded page registry",
            "LVGL hardcoded menu/page creation",
            "contained fallback",
            "Exit condition",
            "Phase 10 first cut makes `AsciiRuntimeEntryAdoption as primary source`",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
        [
            "Phase 9.6 Final Readiness Alignment",
            "The remaining Phase 10-facing UI fallbacks are not legacy adapter burn-down items",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        [
            "Phase 9.6 Final Readiness Alignment",
            "The remaining LinuxSim, GTK, and LVGL hardcoded runtime paths are not reported as burned down",
        ],
        failures,
    )


def check_required_files(failures: list[str]) -> None:
    for rel in [
        "tools/architecture/check_phase9_runtime_adoption_ready.py",
        "tools/architecture/check_phase9_legacy_burndown_ready.py",
        "docs/audits/PHASE9_FINAL_READINESS_REPORT.md",
        "docs/audits/PHASE9_RUNTIME_ADOPTION_REPORT.md",
        "docs/audits/PHASE9_RUNTIME_ENTRY_ADOPTION_REPORT.md",
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
    ]:
        require_file(rel, failures)

    for rel in [
        "modules/ui_legacy_adapters/src/legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_legacy_adapters/src/legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_legacy_adapters/src/legacy_key_verification_source.cpp",
        "modules/ui_legacy_adapters/src/legacy_key_verification_action_sink.cpp",
        "modules/ui_legacy_adapters/src/legacy_key_verification_session.cpp",
        "modules/ui_legacy_adapters/src/legacy_map_overlay_source.cpp",
    ]:
        require_absent(rel, failures)


def main() -> int:
    failures: list[str] = []

    check_required_files(failures)
    run_checker("tools/architecture/check_phase9_runtime_adoption_ready.py", failures)
    run_checker("tools/architecture/check_phase9_legacy_burndown_ready.py", failures)
    check_final_report(failures)
    check_cross_doc_consistency(failures)
    check_no_main_code_includes_burned_down_legacy_headers(failures)

    if failures:
        for failure in failures:
            print(f"[phase9-final-ready] {failure}")
        return 1

    print("[phase9-final-ready] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
