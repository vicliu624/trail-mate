#!/usr/bin/env python3
from pathlib import Path
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


def check_required_files() -> int:
    required = [
        "docs/specification/CHAT_PRESENTATION_IDENTITY_SPEC.md",
        "docs/specification/CHAT_WORKSPACE_MODEL_SPEC.md",
        "docs/audits/CHAT_PENDING_FAILURE_AUDIT.md",
        "docs/audits/CHAT_UI_CONTROLLER_LEGACY_OWNERSHIP_AUDIT.md",
        "docs/audits/TEAM_CHAT_PRESENTATION_AUDIT.md",
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_model.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_action_sink.h",
        "modules/chat_presentation_adapters/include/chat_presentation_adapters/chat_conversation_mapper.h",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
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

    source = "modules/ui_shared/src/ui/presentation_sources/legacy_chat_presentation_source.cpp"
    if exists(source):
        text = read_text(source)
        for token in ["sendText(", "markConversationRead(", "switchChannel("]:
            if token in text:
                failures += fail(f"LegacyChatPresentationSource contains action token {token}")
        if "(void)request.message_offset;" not in text:
            failures += fail("LegacyChatPresentationSource does not explicitly ignore message_offset")
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
    return failures


def check_known_violations_recorded() -> int:
    path = "tools/architecture/KNOWN_PRODUCT_ARCHITECTURE_VIOLATIONS.md"
    if not exists(path):
        return fail("known architecture violations file is missing")

    text = read_text(path)
    required_tokens = [
        "Phase 5.6 Chat Presentation Remaining Legacy Ownership",
        "still owns LVGL state machine",
        "Team chat remains legacy-owned",
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
    failures += check_ui_presentation_chat_is_portable()
    failures += check_chat_presentation_adapters_are_pure_mappers()
    failures += check_chat_presentation_sources_are_bounded()
    failures += check_chat_ui_controller_closeout()
    failures += check_known_violations_recorded()

    if failures == 0:
        print("[phase5-ready] OK")
        return 0

    print(f"[phase5-ready] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
