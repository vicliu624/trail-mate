#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def exists(path: str) -> bool:
    return (ROOT / path).exists()


def fail(message: str) -> int:
    print(f"[phase5-ready] FAIL: {message}")
    return 1


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
        "docs/specification/CHAT_PRESENTATION_IDENTITY_SPEC.md",
        "docs/specification/CHAT_WORKSPACE_MODEL_SPEC.md",
        "docs/audits/CHAT_PENDING_FAILURE_AUDIT.md",
        "docs/audits/CHAT_UI_CONTROLLER_LEGACY_OWNERSHIP_AUDIT.md",
        "docs/audits/TEAM_CHAT_PRESENTATION_AUDIT.md",
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_model.h",
        "modules/ui_shared/include/ui/presentation_sources/chat_presentation_source.h",
        "modules/ui_shared/src/ui/presentation_sources/chat_presentation_source.cpp",
        "modules/ui_shared/tests/test_chat_presentation_source.cpp",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_action_sink.h",
        "modules/ui_shared/include/ui/presentation_sources/team_chat_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/team_chat_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/team_chat_action_sink.cpp",
        "modules/ui_shared/tests/test_team_chat_presentation_source.cpp",
        "modules/ui_shared/tests/test_team_chat_action_sink.cpp",
        "modules/chat_presentation_adapters/include/chat_presentation_adapters/chat_conversation_mapper.h",
        "docs/audits/MAP_PRESENTATION_BOUNDARY_AUDIT.md",
        "docs/specification/MAP_WORKSPACE_MODEL_SPEC.md",
        "docs/audits/MAP_UI_LEGACY_OWNERSHIP_AUDIT.md",
        "modules/ui_presentation/include/ui_presentation/map/map_workspace_snapshot.h",
        "modules/ui_presentation/include/ui_presentation/map/map_presentation_source.h",
        "modules/ui_presentation/include/ui_presentation/map/map_action_sink.h",
        "modules/ui_presentation/include/ui_presentation/map/map_workspace_model.h",
        "modules/ui_presentation/src/map/map_workspace_model.cpp",
        "modules/ui_presentation/tests/test_map_workspace_model.cpp",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_presentation_state.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_map_presentation_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_map_action_sink.cpp",
        "modules/ui_shared/tests/test_legacy_map_presentation_adapters.cpp",
        "legacy/app_implementations/linux_sim/tests/map_workspace_ascii_probe.cpp",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
    return failures


def check_removed_chat_read_projection_not_reintroduced() -> int:
    legacy_type = "Legacy" + "ChatPresentationSource"
    legacy_file_stem = "legacy_chat" + "_presentation_source"
    removed_paths = [
        "modules/ui_shared/include/ui/presentation_sources/" + legacy_file_stem + ".h",
        "modules/ui_shared/src/ui/presentation_sources/" + legacy_file_stem + ".cpp",
        "modules/ui_shared/tests/test_" + legacy_file_stem + ".cpp",
    ]

    failures = 0
    for path in removed_paths:
        if exists(path):
            failures += fail(f"removed chat presentation source path still exists: {path}")

    search_roots = [
        ROOT / "apps",
        ROOT / "cmake",
        ROOT / "docs",
        ROOT / "modules",
        ROOT / "tools" / "architecture",
    ]
    text_suffixes = {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hpp",
        ".cmake",
        ".ini",
        ".json",
        ".md",
        ".py",
        ".txt",
    }
    this_file = Path(__file__).resolve()
    for root in search_roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.resolve() == this_file:
                continue
            if path.suffix not in text_suffixes and path.name != "CMakeLists.txt":
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if legacy_type in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} references removed {legacy_type}"
                )
            if legacy_file_stem in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} references removed {legacy_file_stem}"
                )
    return failures


def check_ui_presentation_chat_is_portable() -> int:
    forbidden_tokens = [
        "chat/domain/",
        "chat/usecase/",
        "chat/ports/",
        "platform/",
        "lvgl.h",
        "gtk",
        "app_context",
        "app_facade",
        "ChatService",
        "ContactService",
        "MeshSession",
        "IMeshAdapter",
    ]

    failures = 0
    roots = [
        ROOT / "modules/ui_presentation/include/ui_presentation/chat",
        ROOT / "modules/ui_presentation/src/chat",
    ]
    for path in iter_code_files(*roots):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden token {token}"
                )
    return failures


def check_ui_presentation_map_is_portable() -> int:
    forbidden_tokens = [
        "platform/",
        "platform\\",
        "gps_runtime",
        "lvgl.h",
        "gtk",
        "app_context",
        "app_facade",
        "TeamUiStore",
        "team_ui_get_store",
        "MapTileCache",
        "map_tiles",
        "route_storage",
        "Store",
    ]

    failures = 0
    roots = [
        ROOT / "modules/ui_presentation/include/ui_presentation/map",
        ROOT / "modules/ui_presentation/src/map",
    ]
    for path in iter_code_files(*roots):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden map token {token}"
                )

    model_header = "modules/ui_presentation/include/ui_presentation/map/map_workspace_model.h"
    if exists(model_header):
        text = read_text(model_header)
        if "map_action_sink.h" not in text:
            failures += fail("MapWorkspaceModel does not include MapActionSink")
        if "map_presentation_source.h" not in text:
            failures += fail("MapWorkspaceModel does not include MapPresentationSource")

    audit = "docs/audits/MAP_PRESENTATION_BOUNDARY_AUDIT.md"
    if exists(audit):
        text = read_text(audit)
        for token in [
            "MapWorkspaceModel minimal",
            "does not rewrite tile loading",
            "Renderer must not directly read",
            "MapWorkspaceModel::snapshot()",
            "MapWorkspaceModel actions",
            "must not derive from LVGL",
        ]:
            if token not in text:
                failures += fail(f"map boundary audit missing token: {token}")

    return failures


def check_map_presentation_sources_are_bounded() -> int:
    forbidden_tokens = [
        "lvgl.h",
        "gtk/",
        "MapTileCache",
        "map_tiles",
        "route_storage",
        "map_contour",
        "RadioLib",
        "Arduino.h",
        "sendDirect",
        "sqlite3",
    ]

    failures = 0
    paths = [
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_map_presentation_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_map_action_sink.cpp",
    ]
    for path in paths:
        if not exists(path):
            continue
        text = read_text(path)
        code_text = strip_cpp_comments(text)
        for token in forbidden_tokens:
            if token in code_text:
                failures += fail(
                    f"{path} contains forbidden legacy map adapter token {token}"
                )

    source = "modules/ui_shared/src/ui/presentation_sources/legacy_map_presentation_source.cpp"
    if exists(source):
        text = read_text(source)
        if "buildMapWorkspaceSnapshot" not in text:
            failures += fail("LegacyMapPresentationSource does not build map snapshots")
        for token in ["setLayer(", "setViewport(", "setActiveTool(", "clearMeasurement("]:
            if token in text:
                failures += fail(f"LegacyMapPresentationSource contains action token {token}")

    sink = "modules/ui_shared/src/ui/presentation_sources/legacy_map_action_sink.cpp"
    if exists(sink):
        text = read_text(sink)
        if "MapWorkspaceSnapshot" in text:
            failures += fail("LegacyMapActionSink builds or references MapWorkspaceSnapshot")
        for token in ["buildMapWorkspaceSnapshot", "status_line", "TeamOverlaySummary"]:
            if token in text:
                failures += fail(f"LegacyMapActionSink contains projection token {token}")

    return failures


def check_lvgl_map_surface_uses_workspace_model() -> int:
    path = "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp"
    if not exists(path):
        return fail("GPS page runtime is missing")

    text = read_text(path)
    failures = 0
    for token in [
        "ui_presentation/map/map_workspace_model.h",
        "LegacyMapPresentationSource",
        "LegacyMapActionSink",
        "map_workspace_model().snapshot()",
        "map_workspace_model().centerOnSelf()",
    ]:
        if token not in text:
            failures += fail(f"LVGL map runtime missing MapWorkspaceModel token: {token}")
    return failures


def check_uconsole_map_surface_uses_workspace_model() -> int:
    header = "platform/linux/uconsole/include/uconsole/uconsole_map_workspace_model.h"
    source = "platform/linux/uconsole/src/uconsole_map_workspace_model.cpp"
    failures = 0

    if not exists(header):
        failures += fail("uConsole map model header is missing")
    else:
        text = read_text(header)
        for token in [
            "ui_presentation/map/map_workspace_model.h",
            "presentation_workspace",
            "MapWorkspaceModel presentation_model_",
            "LegacyMapPresentationSource",
            "LegacyMapActionSink",
            "LegacyGpsStatusSource",
        ]:
            if token not in text:
                failures += fail(f"uConsole map header missing token: {token}")

    if not exists(source):
        failures += fail("uConsole map model source is missing")
    else:
        text = read_text(source)
        for token in [
            "presentation_model_.snapshot()",
            "legacy_gps_source_.buildGpsStatusSnapshot",
            "syncPresentationWorkspace",
        ]:
            if token not in text:
                failures += fail(f"uConsole map source missing token: {token}")
        if "#include \"platform/ui/gps_runtime.h\"" in text:
            failures += fail("uConsole map model still includes gps_runtime directly")

    return failures


def check_map_ascii_probe_exists() -> int:
    path = "legacy/app_implementations/linux_sim/tests/map_workspace_ascii_probe.cpp"
    if not exists(path):
        return fail("Map ASCII/headless probe is missing")

    text = read_text(path)
    failures = 0
    for token in [
        "MapWorkspaceModel",
        "MAP CENTER:",
        "SELF:",
        "LAYERS:",
        "TEAM:",
    ]:
        if token not in text:
            failures += fail(f"Map ASCII/headless probe missing token: {token}")

    forbidden_tokens = [
        "lvgl.h",
        "gtk/",
        "gps_runtime",
        "MapTileCache",
        "map_tiles",
        "TeamUiStore",
    ]
    for token in forbidden_tokens:
        if token in text:
            failures += fail(f"Map ASCII/headless probe contains forbidden token {token}")
    return failures


def check_map_closeout_docs() -> int:
    failures = 0

    spec = "docs/specification/MAP_WORKSPACE_MODEL_SPEC.md"
    if not exists(spec):
        failures += fail("Map workspace model specification is missing")
    else:
        text = read_text(spec)
        for token in [
            "confirmed update semantics",
            "Tile availability remains owned by the legacy tile/cache runtime",
            "Full GPS receiver status belongs to `GpsStatusModel`",
            "TeamOverlaySummary is a projection summary only",
            "Phase 5.7 establishes the presentation boundary",
        ]:
            if token not in text:
                failures += fail(f"Map workspace model spec missing token: {token}")

    audit = "docs/audits/MAP_UI_LEGACY_OWNERSHIP_AUDIT.md"
    if not exists(audit):
        failures += fail("Map UI legacy ownership audit is missing")
    else:
        text = read_text(audit)
        for token in [
            "Migrated in Phase 5.7",
            "compact LVGL map runtime consumes `MapWorkspaceModel::snapshot()`",
            "`UConsoleMapWorkspaceModel` exposes a portable",
            "Remaining Legacy Ownership",
            "Route/tracker overlays remain legacy-owned",
            "Phase 5.7 does not remove",
        ]:
            if token not in text:
                failures += fail(f"Map UI legacy audit missing token: {token}")

    known = "tools/architecture/KNOWN_PRODUCT_ARCHITECTURE_VIOLATIONS.md"
    if exists(known):
        text = read_text(known)
        for token in [
            "Phase 5.7 Map Presentation Remaining Legacy Ownership",
            "`ui_presentation/map` snapshot/source/sink/model",
            "uConsole `presentation_workspace` bridge",
            "Rich Team map markers remain legacy-owned",
            "Tile/cache/route/rich overlay cleanup must be handled in later bounded",
        ]:
            if token not in text:
                failures += fail(f"known violations missing map token: {token}")

    return failures


def check_chat_presentation_adapters_are_pure_mappers() -> int:
    forbidden_tokens = [
        "ChatService",
        "ContactService",
        "TeamService",
        "MeshSession",
        "IMeshAdapter",
        "AppContext",
        "#include \"platform/",
        "#include <platform/",
        "lvgl.h",
        "gtk/",
        "sqlite3",
        "Preferences",
        "RadioLib",
        "Arduino.h",
        "board/",
        "boards/",
    ]

    failures = 0
    root = ROOT / "modules/chat_presentation_adapters"
    for path in iter_code_files(root):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden mapper token {token}"
                )

    header = "modules/chat_presentation_adapters/include/chat_presentation_adapters/chat_conversation_mapper.h"
    if exists(header):
        text = read_text(header)
        if "tryMapProtocol(ui::chat::ChatProtocolKind protocol" not in text:
            failures += fail("chat conversation mapper does not expose tryMapProtocol")
        if "chat::MeshProtocol mapProtocol(ui::chat::ChatProtocolKind" in text:
            failures += fail("chat conversation mapper still exposes unsafe UI protocol fallback")
    return failures


def check_chat_presentation_sources_are_bounded() -> int:
    forbidden_tokens = [
        "lvgl.h",
        "gtk/",
        "RadioLib",
        "Arduino.h",
        "board/",
        "boards/",
        "IMeshAdapter",
        "MeshSession",
        "DirectMessageService",
        "PeerIdentityService",
        "sendDirect",
    ]

    failures = 0
    roots = [
        ROOT / "modules/ui_shared/include/ui/presentation_sources",
        ROOT / "modules/ui_shared/src/ui/presentation_sources",
        ROOT / "platform",
    ]
    for path in iter_code_files(*roots):
        path_text = str(path).lower()
        if "presentation_sources" not in path_text or "chat" not in path.name.lower():
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden source/sink token {token}"
                )

    source = "modules/ui_shared/src/ui/presentation_sources/chat_presentation_source.cpp"
    if exists(source):
        text = read_text(source)
        for token in ["sendText(", "markConversationRead(", "switchChannel("]:
            if token in text:
                failures += fail(f"ChatPresentationSource contains action token {token}")
        for token in [
            "out = ui::chat::ChatWorkspaceSnapshot{}",
            "out = ChatWorkspaceSnapshot{}",
        ]:
            if token in text:
                failures += fail(f"ChatPresentationSource uses stack-heavy snapshot reset: {token}")
        if "(void)request.message_offset;" not in text:
            failures += fail("ChatPresentationSource does not explicitly ignore message_offset")
    return failures


def check_team_chat_presentation_context() -> int:
    failures = 0

    team_paths = [
        "modules/ui_shared/include/ui/presentation_sources/team_chat_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/team_chat_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/team_chat_action_sink.cpp",
    ]

    forbidden_tokens = [
        "toCoreConversationId",
        "LegacyChatActionSink",
        "#include \"chat/domain/chat_types.h\"",
        "#include <chat/domain/chat_types.h>",
        "sendDirect",
        "IMeshAdapter",
        "MeshSession",
        "lvgl.h",
        "gtk/",
    ]
    forbidden_patterns = [
        r"(?<!ui::)chat::ConversationId",
    ]

    for path in team_paths:
        if not exists(path):
            continue
        text = read_text(path)
        code_text = strip_cpp_comments(text)
        if "ConversationKind::Team" not in text:
            failures += fail(f"{path} does not explicitly use ConversationKind::Team")
        for token in forbidden_tokens:
            if token in code_text:
                failures += fail(
                    f"{path} contains forbidden Team presentation token {token}"
                )
        for pattern in forbidden_patterns:
            if re.search(pattern, code_text):
                failures += fail(
                    f"{path} contains forbidden Team presentation pattern {pattern}"
                )

    source = "modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp"
    if exists(source):
        text = read_text(source)
        for token in ["TeamRichPayloadProjector", "TeamRichPayloadDisplay", "display.summary"]:
            if token not in text:
                failures += fail(f"TeamChatPresentationSource missing projection token {token}")

    rich_projector = "modules/ui_shared/src/ui/team_presentation/team_rich_payload_projector.cpp"
    if exists(rich_projector):
        text = read_text(rich_projector)
        for token in ["TeamChatType::Text", "TeamChatType::Location", "TeamChatType::Command"]:
            if token not in text:
                failures += fail(f"TeamRichPayloadProjector missing projection token {token}")

    sink = "modules/ui_shared/src/ui/presentation_sources/team_chat_action_sink.cpp"
    if exists(sink):
        text = read_text(sink)
        if "ITeamChatCommandPort" not in text:
            failures += fail("TeamChatActionSink does not use ITeamChatCommandPort")
        if "team_ui_chatlog_append_structured" not in text:
            failures += fail("TeamChatActionSink does not append outgoing Team text projection")

    audit = "docs/audits/TEAM_CHAT_PRESENTATION_AUDIT.md"
    if exists(audit):
        text = read_text(audit)
        for token in [
            "Migrated in Phase 5.6-f",
            "Team text projection migrated to `TeamChatPresentationSource`",
            "Team text send migrated to `TeamChatActionSink`",
            "Team location entries are projected as textual `MessageRow` summaries",
            "Team command entries are projected as textual `MessageRow` summaries",
            "Team read/unread clear can be handled by `TeamChatActionSink::markRead`",
            "Remaining Legacy Ownership",
            "Team pending/failure structured delivery",
        ]:
            if token not in text:
                failures += fail(f"Team audit missing token: {token}")

    return failures


def check_chat_ui_controller_closeout() -> int:
    failures = 0
    controller = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    if exists(controller):
        text = read_text(controller)
        if "ui_presentation/chat/chat_workspace_model.h" not in text:
            failures += fail("ChatUiController does not include ChatWorkspaceModel")
        if "ChatWorkspaceModel&" not in text:
            failures += fail("ChatUiController constructor does not receive ChatWorkspaceModel&")
        if "legacy application controller" not in text:
            failures += fail("ChatUiController is not marked as a legacy controller")
        if "Phase 5.6-f" not in text:
            failures += fail("ChatUiController does not mark Team path for Phase 5.6-f")
        if "team_chat_model_" not in text:
            failures += fail("ChatUiController does not retain a dedicated team_chat_model_")

    audit = "docs/audits/CHAT_UI_CONTROLLER_LEGACY_OWNERSHIP_AUDIT.md"
    if exists(audit):
        text = read_text(audit)
        for token in [
            "Team text projection/send has started migrating to",
            "TeamChatPresentationSource` / `TeamChatActionSink",
            "Team structured pending/failure delivery",
        ]:
            if token not in text:
                failures += fail(f"Chat UI controller audit missing token: {token}")
    return failures


def check_known_violations_recorded() -> int:
    path = "tools/architecture/KNOWN_PRODUCT_ARCHITECTURE_VIOLATIONS.md"
    if not exists(path):
        return fail("known architecture violations file is missing")

    text = read_text(path)
    required_tokens = [
        "Phase 5.6 Chat Presentation Remaining Legacy Ownership",
        "still owns LVGL state machine",
        "Team text projection migrated to `TeamChatPresentationSource`",
        "Team text send migrated to `TeamChatActionSink`",
        "Team location/command entries are currently projected as textual",
        "Team location/command picker remains legacy-owned",
        "Team richer payload UI remains future work",
        "Team structured pending/failure remains future work",
        "Structured send failure is not yet preserved",
    ]
    failures = 0
    for token in required_tokens:
        if token not in text:
            failures += fail(f"known violations missing token: {token}")
    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_removed_chat_read_projection_not_reintroduced()
    failures += check_ui_presentation_chat_is_portable()
    failures += check_ui_presentation_map_is_portable()
    failures += check_map_presentation_sources_are_bounded()
    failures += check_lvgl_map_surface_uses_workspace_model()
    failures += check_uconsole_map_surface_uses_workspace_model()
    failures += check_map_ascii_probe_exists()
    failures += check_map_closeout_docs()
    failures += check_chat_presentation_adapters_are_pure_mappers()
    failures += check_chat_presentation_sources_are_bounded()
    failures += check_team_chat_presentation_context()
    failures += check_chat_ui_controller_closeout()
    failures += check_known_violations_recorded()

    if failures == 0:
        print("[phase5-ready] OK")
        return 0

    print(f"[phase5-ready] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
