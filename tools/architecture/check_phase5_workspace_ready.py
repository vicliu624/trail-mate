#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def exists(path: str) -> bool:
    return (ROOT / path).exists()


def fail(message: str) -> int:
    print(f"[phase5-workspace-ready] FAIL: {message}")
    return 1


def iter_code_files(*roots: Path):
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
                yield path


def check_required_files() -> int:
    required = [
        "docs/specification/PRESENTATION_WORKSPACE_SPEC.md",
        "docs/audits/PHASE5_PRESENTATION_BOUNDARY_SUMMARY.md",
        "modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace.h",
        "modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace_snapshot.h",
        "modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace_probe.h",
        "modules/ui_presentation/src/workspace/presentation_workspace_probe.cpp",
        "modules/ui_presentation/tests/test_presentation_workspace.cpp",
        "modules/ui_presentation/tests/test_presentation_workspace_probe.cpp",
        "legacy/app_implementations/linux_sim/tests/presentation_workspace_ascii_probe.cpp",
        "legacy/app_implementations/linux_uconsole/tests/uconsole_presentation_workspace_smoke.cpp",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
    return failures


def check_workspace_portability() -> int:
    forbidden_tokens = [
        "platform/",
        "platform\\",
        "lvgl.h",
        "gtk",
        "ChatService",
        "ContactService",
        "GpsService",
        "MeshSession",
        "MapTileCache",
        "gps_runtime",
        "TeamUiStore",
        "sqlite3",
        "Preferences",
        "RadioLib",
        "Arduino.h",
    ]

    failures = 0
    roots = [
        ROOT / "modules/ui_presentation/include/ui_presentation/workspace",
        ROOT / "modules/ui_presentation/src/workspace",
    ]
    for path in iter_code_files(*roots):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden token {token}"
                )
    return failures


def check_workspace_graph_shape() -> int:
    path = "modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace.h"
    if not exists(path):
        return 0

    text = read_text(path)
    required_tokens = [
        "DeviceStatusModel",
        "GpsStatusModel",
        "MeshStatusModel",
        "SettingsModel",
        "ChatWorkspaceModel",
        "MapWorkspaceModel",
        "team_chat",
        "hasCoreStatusModels",
        "hasInteractiveWorkspaceModels",
    ]

    forbidden_tokens = [
        "new ",
        "delete ",
        "Legacy",
        "Service&",
        "Service*",
        "Source ",
        "Sink ",
        "get(",
        "resolve",
    ]

    failures = 0
    for token in required_tokens:
        if token not in text:
            failures += fail(f"PresentationWorkspace missing token: {token}")
    for token in forbidden_tokens:
        if token in text:
            failures += fail(f"PresentationWorkspace contains factory/locator token: {token}")
    return failures


def check_workspace_snapshot_is_summary() -> int:
    path = "modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace_snapshot.h"
    if not exists(path):
        return 0

    text = read_text(path)
    required_tokens = [
        "DeviceStatusSnapshot",
        "GpsStatusSnapshot",
        "MeshStatusSnapshot",
        "MapWorkspaceSnapshot",
        "has_settings",
        "has_chat",
        "has_team_chat",
    ]
    forbidden_tokens = [
        "ChatWorkspaceSnapshot",
        "SettingsSnapshot",
        "TeamUiStore",
        "MapTileCache",
        "std::vector",
        "std::string",
    ]

    failures = 0
    for token in required_tokens:
        if token not in text:
            failures += fail(f"PresentationWorkspaceSnapshot missing token: {token}")
    for token in forbidden_tokens:
        if token in text:
            failures += fail(
                f"PresentationWorkspaceSnapshot contains non-summary token: {token}"
            )
    return failures


def check_workspace_probe_is_read_only_summary() -> int:
    path = "modules/ui_presentation/src/workspace/presentation_workspace_probe.cpp"
    if not exists(path):
        return 0

    text = read_text(path)
    required_tokens = [
        "PresentationWorkspaceProbe",
        "has_device",
        "has_gps",
        "has_mesh",
        "has_map",
        "has_chat",
        "has_team_chat",
        "device->snapshot()",
        "gps->snapshot()",
        "mesh->snapshot()",
        "map->snapshot()",
    ]
    forbidden_tokens = [
        "sendMessage",
        "selectConversation",
        "markRead",
        "centerOnSelf",
        "setLayer",
        "setViewport",
        "setActiveTool",
        "apply(",
        "chat->snapshot()",
        "team_chat->snapshot()",
        "settings->snapshot()",
    ]

    failures = 0
    for token in required_tokens:
        if token not in text:
            failures += fail(f"PresentationWorkspaceProbe missing token: {token}")
    for token in forbidden_tokens:
        if token in text:
            failures += fail(f"PresentationWorkspaceProbe contains action/full-read token: {token}")
    return failures


def check_ascii_probe() -> int:
    path = "legacy/app_implementations/linux_sim/tests/presentation_workspace_ascii_probe.cpp"
    if not exists(path):
        return 0

    text = read_text(path)
    required_tokens = [
        "PRESENTATION WORKSPACE",
        "DEVICE:",
        "GPS:",
        "MESH:",
        "SETTINGS:",
        "CHAT:",
        "TEAM_CHAT:",
        "MAP:",
        "PresentationWorkspaceProbe::snapshot",
    ]
    forbidden_tokens = [
        "lvgl.h",
        "gtk",
        "platform/ui/gps_runtime.h",
        "ChatService",
        "TeamUiStore",
        "MapTileCache",
        "RadioLib",
        "sqlite",
        "Preferences",
    ]

    failures = 0
    for token in required_tokens:
        if token not in text:
            failures += fail(f"workspace ASCII probe missing token: {token}")
    for token in forbidden_tokens:
        if token in text:
            failures += fail(f"workspace ASCII probe contains forbidden token: {token}")
    return failures


def check_uconsole_smoke() -> int:
    path = "legacy/app_implementations/linux_uconsole/tests/uconsole_presentation_workspace_smoke.cpp"
    if not exists(path):
        return 0

    text = read_text(path)
    required_tokens = [
        "PresentationWorkspace",
        "PresentationWorkspaceProbe::snapshot",
        "workspace.device",
        "workspace.gps",
        "workspace.mesh",
        "workspace.settings",
        "workspace.chat",
        "workspace.team_chat",
        "workspace.map",
    ]
    forbidden_tokens = [
        "lvgl.h",
        "gtk",
        "platform/ui/gps_runtime.h",
        "ChatService",
        "TeamUiStore",
        "MapTileCache",
        "RadioLib",
        "sqlite",
        "Preferences",
    ]

    failures = 0
    for token in required_tokens:
        if token not in text:
            failures += fail(f"uConsole workspace smoke missing token: {token}")
    for token in forbidden_tokens:
        if token in text:
            failures += fail(f"uConsole workspace smoke contains forbidden token: {token}")
    return failures


def check_docs() -> int:
    failures = 0

    spec = "docs/specification/PRESENTATION_WORKSPACE_SPEC.md"
    if exists(spec):
        text = read_text(spec)
        for token in [
            "Typed Presentation Graph",
            "Optional Capability Set",
            "service locator",
            "does not own",
            "All fields are nullable",
            "composite snapshot is for readiness and probe output only",
            "must not construct models",
        ]:
            if token not in text:
                failures += fail(f"Presentation workspace spec missing token: {token}")

    summary = "docs/audits/PHASE5_PRESENTATION_BOUNDARY_SUMMARY.md"
    if exists(summary):
        text = read_text(summary)
        for token in [
            "DeviceStatusModel",
            "GpsStatusModel",
            "MeshStatusModel",
            "SettingsModel",
            "ChatWorkspaceModel",
            "TeamChatPresentationSource",
            "MapWorkspaceModel",
            "PresentationWorkspace",
            "does not fully clean every legacy UI island",
        ]:
            if token not in text:
                failures += fail(f"Phase 5 boundary summary missing token: {token}")

    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_workspace_portability()
    failures += check_workspace_graph_shape()
    failures += check_workspace_snapshot_is_summary()
    failures += check_workspace_probe_is_read_only_summary()
    failures += check_ascii_probe()
    failures += check_uconsole_smoke()
    failures += check_docs()

    if failures == 0:
        print("[phase5-workspace-ready] OK")
        return 0

    print(f"[phase5-workspace-ready] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
