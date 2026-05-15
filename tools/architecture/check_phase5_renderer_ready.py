#!/usr/bin/env python3
from pathlib import Path
import argparse
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
BASELINE_PATH = ROOT / "tools/architecture/phase5_renderer_legacy_baseline.json"


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def exists(path: str) -> bool:
    return (ROOT / path).exists()


def fail(message: str) -> int:
    print(f"[phase5-renderer-ready] FAIL: {message}")
    return 1


def warn(message: str) -> None:
    print(f"[phase5-renderer-ready] baseline: {message}")


def iter_code_files(*roots: Path):
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
                yield path


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def check_required_files() -> int:
    required = [
        "docs/audits/RENDERER_BOUNDARY_AUDIT.md",
        "docs/audits/RENDERER_HARDENING_BASELINE.md",
        "docs/audits/PHASE5_RENDERER_HARDENING_SUMMARY.md",
        "tools/architecture/check_phase5_renderer_ready.py",
        "tools/architecture/phase5_renderer_legacy_baseline.json",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
    return failures


def check_ui_presentation_is_renderer_portable() -> int:
    forbidden_tokens = [
        "lvgl.h",
        "gtk",
        "platform/",
        "platform\\",
        "gps_runtime",
        "ChatService",
        "ContactService",
        "TeamUiStore",
        "MapTileCache",
        "RadioLib",
        "sqlite3",
        "Preferences",
        "Arduino.h",
    ]

    failures = 0
    roots = [
        ROOT / "modules/ui_presentation/include",
        ROOT / "modules/ui_presentation/src",
    ]
    for path in iter_code_files(*roots):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden renderer token {token}"
                )
    return failures


def check_ascii_probes_are_strict() -> int:
    forbidden_tokens = [
        "lvgl.h",
        "gtk",
        "platform/ui/gps_runtime.h",
        "ChatService",
        "TeamUiStore",
        "MapTileCache",
        "RadioLib",
        "sqlite3",
        "Preferences",
    ]

    failures = 0
    root = ROOT / "legacy/app_implementations/linux_sim/tests"
    for path in root.glob("*ascii_probe*.cpp"):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden probe token {token}"
                )
        if "ui_presentation/" not in text:
            failures += fail(
                f"{path.relative_to(ROOT)} does not consume ui_presentation"
            )

    required_probe_tokens = {
        "legacy/app_implementations/linux_sim/tests/map_workspace_ascii_probe.cpp": [
            "MapWorkspaceModel",
            "MAP CENTER:",
            "SELF:",
            "LAYERS:",
            "TEAM:",
        ],
        "legacy/app_implementations/linux_sim/tests/presentation_workspace_ascii_probe.cpp": [
            "PresentationWorkspaceProbe::snapshot",
            "PRESENTATION WORKSPACE",
            "DEVICE:",
            "GPS:",
            "MESH:",
            "SETTINGS:",
            "CHAT:",
            "TEAM_CHAT:",
            "MAP:",
        ],
    }
    for probe, tokens in required_probe_tokens.items():
        if not exists(probe):
            failures += fail(f"missing ASCII/headless probe: {probe}")
            continue
        text = read_text(probe)
        for token in tokens:
            if token not in text:
                failures += fail(f"{probe} missing probe output token: {token}")
    return failures


def check_chat_presentation_adapters_are_renderer_free() -> int:
    forbidden_tokens = [
        "lvgl.h",
        "gtk",
        "sendDirect",
        "MeshSession",
        "IMeshAdapter",
        "RadioLib",
        "Arduino.h",
    ]

    paths = [
        "modules/ui_shared/include/ui/presentation_sources/chat_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/chat_presentation_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_chat_action_sink.cpp",
    ]

    failures = 0
    for path in paths:
        if not exists(path):
            failures += fail(f"missing chat presentation adapter: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(f"{path} contains forbidden chat adapter token {token}")
    return failures


def check_map_presentation_adapters_are_renderer_free() -> int:
    forbidden_tokens = [
        "lvgl.h",
        "gtk",
        "MapTileCache",
        "map_tiles",
        "route_storage",
        "sendDirect",
        "RadioLib",
        "sqlite3",
    ]

    paths = [
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_map_presentation_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_map_action_sink.cpp",
    ]

    failures = 0
    for path in paths:
        if not exists(path):
            failures += fail(f"missing map presentation adapter: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(f"{path} contains forbidden map adapter token {token}")
    return failures


def check_team_chat_adapters_are_bounded() -> int:
    forbidden_tokens = [
        "toCoreConversationId",
        "LegacyChatActionSink",
        "chat/domain/chat_types.h",
        "MeshSession",
        "IMeshAdapter",
        "lvgl.h",
        "gtk",
    ]

    paths = [
        "modules/ui_shared/include/ui/presentation_sources/team_chat_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/team_chat_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/team_chat_action_sink.cpp",
    ]

    failures = 0
    for path in paths:
        if not exists(path):
            failures += fail(f"missing Team chat adapter: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(f"{path} contains forbidden Team adapter token {token}")
    return failures


def check_migrated_chat_path_is_locked() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"

    if not exists(header):
        failures += fail("ChatUiController header is missing")
    else:
        text = read_text(header)
        for token in [
            "ChatWorkspaceModel& chat_model",
            "ChatWorkspaceModel& team_chat_model",
            "chat_model_",
            "team_chat_model_",
            "chat_snapshot_buffer_",
            "team_chat_snapshot_buffer_",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController header missing migrated token: {token}")

    if not exists(source):
        failures += fail("ChatUiController source is missing")
    else:
        text = read_text(source)
        for token in [
            "chat_model_.selectConversation",
            "chat_model_.buildSnapshot",
            "chat_model_.markRead",
            "chat_model_.sendMessage",
            "team_chat_model_.buildSnapshot",
            "team_chat_model_.markRead",
            "team_chat_model_.sendMessage",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController source missing migrated token: {token}")
        for token in ["chat_model_.snapshot()", "team_chat_model_.snapshot()"]:
            if token in text:
                failures += fail(
                    f"ChatUiController uses stack-heavy snapshot API: {token}"
                )
        for token in ["service_.sendText", "service_.sendMessage", "service_.markConversationRead"]:
            if token in text:
                failures += fail(
                    f"ChatUiController migrated send/read path regressed to {token}"
                )
    return failures


def check_migrated_map_path_is_locked() -> int:
    failures = 0

    lvgl_path = "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp"
    if not exists(lvgl_path):
        failures += fail("LVGL GPS page runtime is missing")
    else:
        text = read_text(lvgl_path)
        for token in [
            "ui_presentation/map/map_workspace_model.h",
            "MapWorkspaceModel",
            "map_workspace_model().snapshot()",
            "map_workspace_model().centerOnSelf()",
        ]:
            if token not in text:
                failures += fail(f"LVGL GPS/map migrated path missing token: {token}")

    uconsole_header = "platform/linux/uconsole/include/uconsole/uconsole_map_workspace_model.h"
    uconsole_source = "platform/linux/uconsole/src/uconsole_map_workspace_model.cpp"
    if not exists(uconsole_header):
        failures += fail("uConsole map model header is missing")
    else:
        text = read_text(uconsole_header)
        for token in [
            "presentation_workspace",
            "MapWorkspaceModel presentation_model_",
            "LegacyMapPresentationSource",
            "LegacyMapActionSink",
        ]:
            if token not in text:
                failures += fail(f"uConsole map header missing migrated token: {token}")

    if not exists(uconsole_source):
        failures += fail("uConsole map model source is missing")
    else:
        text = read_text(uconsole_source)
        for token in [
            "presentation_model_.snapshot()",
            "syncPresentationWorkspace",
            "out.presentation_workspace",
        ]:
            if token not in text:
                failures += fail(f"uConsole map source missing migrated token: {token}")
        if "#include \"platform/ui/gps_runtime.h\"" in text:
            failures += fail("uConsole map model reintroduced direct gps_runtime include")

    return failures


def check_migrated_dashboard_status_is_locked() -> int:
    path = "modules/ui_shared/src/ui/menu/dashboard/dashboard_gps_widget.cpp"
    if not exists(path):
        return fail("LVGL dashboard GPS widget is missing")

    text = read_text(path)
    failures = 0
    for token in [
        "ui_presentation/gps/gps_status_model.h",
        "legacy_gps_status_source",
        "dashboard_gps_status_model().snapshot()",
        "status_snapshot.fix_valid",
        "status_snapshot.fix_label",
        "status_snapshot.satellites",
    ]:
        if token not in text:
            failures += fail(f"dashboard GPS migrated status path missing token: {token}")
    return failures


def check_uconsole_renderer_bridge_is_locked() -> int:
    failures = 0

    smoke = "legacy/app_implementations/linux_uconsole/tests/uconsole_presentation_workspace_smoke.cpp"
    if exists(smoke):
        text = read_text(smoke)
        for token in [
            "PresentationWorkspace",
            "PresentationWorkspaceProbe::snapshot",
            "workspace.map",
            "workspace.chat",
            "workspace.team_chat",
        ]:
            if token not in text:
                failures += fail(f"uConsole presentation workspace smoke missing token: {token}")
    else:
        failures += fail("uConsole presentation workspace smoke is missing")

    app_state = "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_uconsole_app_state.h"
    if exists(app_state):
        text = read_text(app_state)
        for token in [
            "UConsoleChatWorkspaceModel chat_model",
            "UConsoleMapWorkspaceModel map_model",
        ]:
            if token not in text:
                failures += fail(f"uConsole GTK app state missing model token: {token}")
    else:
        failures += fail("uConsole GTK app state is missing")

    return failures


def load_baseline() -> dict:
    if not BASELINE_PATH.exists():
        return {}
    return json.loads(BASELINE_PATH.read_text(encoding="utf-8"))


def check_legacy_baseline(strict_baseline: bool) -> int:
    baseline = load_baseline()
    islands = baseline.get("known_legacy_islands", [])
    failures = 0

    for island in islands:
        island_id = island.get("id", "<missing-id>")
        path_value = island.get("path", "")
        allowed_tokens = island.get("allowed_tokens", [])
        reason = island.get("reason", "")
        future_phase = island.get("future_phase", "")
        path = ROOT / path_value

        if not path.exists():
            failures += fail(f"baseline island {island_id} path missing: {path_value}")
            continue
        if not reason:
            failures += fail(f"baseline island {island_id} missing reason")
        if not future_phase:
            failures += fail(f"baseline island {island_id} missing future_phase")

        files = [path] if path.is_file() else list(iter_code_files(path))
        combined = "\n".join(
            file.read_text(encoding="utf-8", errors="ignore") for file in files
        )
        present = [token for token in allowed_tokens if token in combined]
        warn(
            f"{island_id}: {len(present)}/{len(allowed_tokens)} baseline tokens present"
        )
        if strict_baseline:
            missing = [token for token in allowed_tokens if token not in combined]
            for token in missing:
                failures += fail(f"baseline island {island_id} missing token: {token}")

    return failures


def check_docs() -> int:
    failures = 0

    audit = "docs/audits/RENDERER_BOUNDARY_AUDIT.md"
    if exists(audit):
        text = read_text(audit)
        for token in [
            "Renderer Rule",
            "Known Legacy Islands",
            "Strict Surfaces",
            "Migrated Path Locks",
            "Historical renderer couplings are recorded as a baseline",
        ]:
            if token not in text:
                failures += fail(f"renderer boundary audit missing token: {token}")

    baseline_doc = "docs/audits/RENDERER_HARDENING_BASELINE.md"
    if exists(baseline_doc):
        text = read_text(baseline_doc)
        for token in [
            "fail on strict surface violations",
            "report baseline legacy islands without failing",
            "tools/architecture/phase5_renderer_legacy_baseline.json",
            "Adding a new baseline entry is an architecture decision",
        ]:
            if token not in text:
                failures += fail(f"renderer hardening baseline doc missing token: {token}")

    summary = "docs/audits/PHASE5_RENDERER_HARDENING_SUMMARY.md"
    if exists(summary):
        text = read_text(summary)
        for token in [
            "Strict Surfaces",
            "Legacy Islands",
            "Thin Slices Migrated or Locked",
            "LVGL dashboard GPS fix chip",
            "phase5_renderer_legacy_baseline.json",
            "New renderer code must prefer presentation models",
        ]:
            if token not in text:
                failures += fail(f"renderer hardening summary missing token: {token}")

    chat_audit = "docs/audits/CHAT_UI_CONTROLLER_LEGACY_OWNERSHIP_AUDIT.md"
    if exists(chat_audit):
        text = read_text(chat_audit)
        for token in [
            "Renderer Hardening in Phase 5.9",
            "chat_model_.selectConversation",
            "chat_model_.buildSnapshot",
            "chat_model_.sendMessage",
            "team_chat_model_",
            "Do not add new renderer send/read logic",
        ]:
            if token not in text:
                failures += fail(f"Chat UI legacy audit missing renderer lock token: {token}")

    map_audit = "docs/audits/MAP_UI_LEGACY_OWNERSHIP_AUDIT.md"
    if exists(map_audit):
        text = read_text(map_audit)
        for token in [
            "Renderer Hardening in Phase 5.9",
            "map_workspace_model().snapshot()",
            "map_workspace_model().centerOnSelf()",
            "presentation_workspace",
            "platform/ui/gps_runtime.h",
            "tile/cache/contour loading and rendering",
        ]:
            if token not in text:
                failures += fail(f"Map UI legacy audit missing renderer lock token: {token}")

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check Phase 5.9 renderer hardening boundaries."
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Also require every baseline token to remain present.",
    )
    parser.add_argument(
        "--new-only",
        action="store_true",
        help="Reserved for future diff-aware checks; strict surfaces are still checked.",
    )
    args = parser.parse_args()

    failures = 0
    failures += check_required_files()
    failures += check_ui_presentation_is_renderer_portable()
    failures += check_ascii_probes_are_strict()
    failures += check_chat_presentation_adapters_are_renderer_free()
    failures += check_map_presentation_adapters_are_renderer_free()
    failures += check_team_chat_adapters_are_bounded()
    failures += check_migrated_chat_path_is_locked()
    failures += check_migrated_map_path_is_locked()
    failures += check_migrated_dashboard_status_is_locked()
    failures += check_uconsole_renderer_bridge_is_locked()
    failures += check_legacy_baseline(strict_baseline=args.strict)
    failures += check_docs()

    if args.new_only:
        print("[phase5-renderer-ready] --new-only currently checks strict surfaces")

    if failures == 0:
        print("[phase5-renderer-ready] OK")
        return 0

    print(f"[phase5-renderer-ready] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
