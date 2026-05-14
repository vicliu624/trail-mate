#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def exists(path: str) -> bool:
    return (ROOT / path).exists()


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def fail(message: str) -> int:
    print(f"[phase5-final] FAIL: {message}")
    return 1


def run_checker(path: str) -> int:
    checker = ROOT / path
    if not checker.exists():
        print(f"[phase5-final] SKIP missing checker: {path}")
        return 1

    result = subprocess.run(
        [sys.executable, str(checker)],
        cwd=str(ROOT),
        text=True,
    )
    return result.returncode


def check_required_files() -> int:
    required = [
        "docs/specification/UI_PRESENTATION_ARCHITECTURE_SPEC.md",
        "docs/specification/PRESENTATION_WORKSPACE_SPEC.md",
        "docs/audits/PHASE5_PRESENTATION_BOUNDARY_SUMMARY.md",
        "docs/audits/PHASE5_PRESENTATION_CONFORMANCE_MATRIX.md",
        "docs/audits/PHASE5_LEGACY_ISLAND_REGISTER.md",
        "docs/audits/PHASE5_FINAL_READINESS_REPORT.md",
        "docs/audits/PHASE5_RENDERER_HARDENING_SUMMARY.md",
        "docs/audits/PHASE6_INPUT_PLAN.md",
        "tools/architecture/check_phase5_ready.py",
        "tools/architecture/check_phase5_workspace_ready.py",
        "tools/architecture/check_phase5_renderer_ready.py",
        "tools/architecture/check_phase5_final_ready.py",
        "tools/architecture/phase5_renderer_legacy_baseline.json",
        "modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace.h",
        "modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace_snapshot.h",
        "modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace_probe.h",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
    return failures


def check_known_domain_files() -> int:
    required = [
        "modules/ui_presentation/include/ui_presentation/device/device_status_model.h",
        "modules/ui_presentation/include/ui_presentation/gps/gps_status_model.h",
        "modules/ui_presentation/include/ui_presentation/mesh/mesh_status_model.h",
        "modules/ui_presentation/include/ui_presentation/settings/settings_model.h",
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_model.h",
        "modules/ui_shared/include/ui/presentation_sources/chat_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_action_sink.h",
        "modules/ui_shared/include/ui/presentation_sources/team_chat_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/team_chat_action_sink.h",
        "modules/ui_presentation/include/ui_presentation/map/map_workspace_model.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_action_sink.h",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing domain boundary file: {path}")
    return failures


def check_final_docs_have_decision_language() -> int:
    docs = {
        "docs/audits/PHASE5_PRESENTATION_CONFORMANCE_MATRIX.md": [
            "Phase 5-ready does not mean fully clean",
            "ready-with-legacy",
            "No domain may remain blocked",
        ],
        "docs/audits/PHASE5_LEGACY_ISLAND_REGISTER.md": [
            "not Phase 5 blockers",
            "bounded legacy",
            "Future cleanup",
        ],
        "docs/audits/PHASE5_FINAL_READINESS_REPORT.md": [
            "Phase 5 is ready to close",
            "This decision does not mean the UI is fully clean",
            "Phase 6 may start",
        ],
        "docs/audits/PHASE6_INPUT_PLAN.md": [
            "Phase 6 may assume",
            "Phase 6 must not",
            "check_phase5_final_ready.py",
        ],
    }

    failures = 0
    for path, tokens in docs.items():
        if not exists(path):
            continue
        text = read_text(path)
        for token in tokens:
            if token not in text:
                failures += fail(f"{path} missing final readiness token: {token}")
    return failures


def run_existing_fitness_functions() -> int:
    failures = 0
    for checker in [
        "tools/architecture/check_phase5_ready.py",
        "tools/architecture/check_phase5_workspace_ready.py",
        "tools/architecture/check_phase5_renderer_ready.py",
    ]:
        if not exists(checker):
            failures += fail(f"missing checker: {checker}")
            continue
        rc = run_checker(checker)
        if rc != 0:
            failures += fail(f"{checker} failed")
    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_known_domain_files()
    failures += check_final_docs_have_decision_language()
    failures += run_existing_fitness_functions()

    if failures == 0:
        print("[phase5-final] OK")
        return 0

    print(f"[phase5-final] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
