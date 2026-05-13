#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]


def exists(path: str) -> bool:
    return (ROOT / path).exists()


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def fail(message: str) -> int:
    print(f"[phase7-runtime-ready] FAIL: {message}")
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
        "docs/audits/CHAT_DELIVERY_OWNERSHIP_AUDIT.md",
        "docs/specification/CHAT_DELIVERY_RUNTIME_SPEC.md",
        "docs/audits/TEAM_ACTION_OWNERSHIP_AUDIT.md",
        "docs/specification/TEAM_ACTION_RUNTIME_SPEC.md",
        "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md",
        "modules/core_chat/include/chat/delivery/chat_delivery_types.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_read_model.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_event_projector.h",
        "modules/core_chat/include/chat/delivery/legacy_chat_delivery_bridge.h",
        "modules/core_chat/src/delivery/chat_delivery_read_model.cpp",
        "modules/core_chat/src/delivery/chat_delivery_event_projector.cpp",
        "modules/core_chat/src/delivery/legacy_chat_delivery_bridge.cpp",
        "modules/core_chat/tests/test_chat_delivery_read_model.cpp",
        "modules/core_chat/tests/test_chat_delivery_event_projector.cpp",
        "modules/core_chat/tests/test_legacy_chat_delivery_bridge.cpp",
        "modules/ui_shared/include/ui/team_actions/team_action_types.h",
        "modules/ui_shared/include/ui/team_actions/team_action_sink.h",
        "modules/ui_shared/include/ui/team_actions/legacy_team_action_bridge.h",
        "modules/ui_shared/src/ui/team_actions/legacy_team_action_bridge.cpp",
        "modules/ui_shared/tests/test_team_action_types.cpp",
        "modules/ui_shared/tests/test_legacy_team_action_bridge.cpp",
        "tools/architecture/check_phase7_runtime_ownership_ready.py",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
    return failures


def check_docs() -> int:
    docs = {
        "docs/audits/CHAT_DELIVERY_OWNERSHIP_AUDIT.md": [
            "`ChatWorkspaceModel` must not own pending/failure",
            "Renderer must not infer delivery/failure",
            "ChatDeliveryReadModel / ChatDeliveryStateStore",
            "dedicated delivery read model",
        ],
        "docs/specification/CHAT_DELIVERY_RUNTIME_SPEC.md": [
            "Delivery state is runtime state, not UI state",
            "ChatDeliveryEventProjector",
            "ChatDeliveryReadModel",
            "LegacyChatPresentationSource",
            "Phase 7.1 does not make `ChatWorkspaceModel` own delivery state",
        ],
        "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md": [
            "Chat delivery / pending / failure",
            "runtime state",
            "not owned by",
            "composition-root ownership",
            "Team location/command action ownership",
            "LegacyTeamActionBridge",
        ],
        "docs/audits/TEAM_ACTION_OWNERSHIP_AUDIT.md": [
            "Team actions are command ownership",
            "Renderer/controller code may collect user input",
            "must not",
            "encode Team payloads",
            "LegacyTeamActionBridge",
        ],
        "docs/specification/TEAM_ACTION_RUNTIME_SPEC.md": [
            "Team action payload encoding belongs to Team action/runtime adapters",
            "TeamActionRequest",
            "ITeamActionSink",
            "ITeamLocationSource",
            "LegacyTeamActionBridge",
            "Phase 7.2 does not turn Team into DirectPeer or Channel chat",
        ],
    }

    failures = 0
    for path, tokens in docs.items():
        if not exists(path):
            continue
        text = read_text(path)
        for token in tokens:
            if token not in text:
                failures += fail(f"{path} missing runtime ownership token: {token}")
    return failures


def check_core_delivery_is_runtime_only() -> int:
    forbidden_tokens = [
        "ui_presentation",
        "ui/",
        "lvgl.h",
        "gtk",
        "ChatWorkspaceModel",
        "MessageRow",
        "Renderer",
        "MeshSession",
        "IMeshAdapter",
        "RadioLib",
        "Arduino.h",
        "Preferences",
    ]

    failures = 0
    roots = [
        ROOT / "modules/core_chat/include/chat/delivery",
        ROOT / "modules/core_chat/src/delivery",
    ]
    for path in iter_code_files(*roots):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden delivery token {token}"
                )
    return failures


def check_delivery_type_shape() -> int:
    failures = 0

    types = "modules/core_chat/include/chat/delivery/chat_delivery_types.h"
    if exists(types):
        text = read_text(types)
        for token in [
            "enum class DeliveryState",
            "Queued",
            "Sending",
            "Delivered",
            "enum class DeliveryFailureKind",
            "PeerKeyMissing",
            "LocalIdentityMissing",
            "RadioSendFailed",
            "AckTimeout",
            "ChatDeliveryRef",
            "ChatDeliveryRecord",
        ]:
            if token not in text:
                failures += fail(f"chat delivery types missing token: {token}")

    read_model = "modules/core_chat/include/chat/delivery/chat_delivery_read_model.h"
    if exists(read_model):
        text = read_text(read_model)
        for token in ["kMaxRecords", "upsert", "find", "clear", "clearAll"]:
            if token not in text:
                failures += fail(f"ChatDeliveryReadModel missing token: {token}")
        for token in ["std::vector", "sendText", "retry", "cancel"]:
            if token in text:
                failures += fail(f"ChatDeliveryReadModel owns behavior token: {token}")

    projector = (
        "modules/core_chat/include/chat/delivery/chat_delivery_event_projector.h"
    )
    if exists(projector):
        text = read_text(projector)
        for token in [
            "onQueued",
            "onSending",
            "onSent",
            "onDelivered",
            "onFailed",
            "onReceived",
            "SendFailureKind",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryEventProjector missing token: {token}")

    return failures


def check_presentation_source_enrichment() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/presentation_sources/legacy_chat_presentation_source.h"
    source = "modules/ui_shared/src/ui/presentation_sources/legacy_chat_presentation_source.cpp"

    if exists(header):
        text = read_text(header)
        for token in [
            "ChatDeliveryReadModel",
            "delivery_read_model = nullptr",
            "delivery_read_model_",
        ]:
            if token not in text:
                failures += fail(f"LegacyChatPresentationSource header missing token: {token}")

    if exists(source):
        text = read_text(source)
        for token in [
            "delivery_read_model_->find",
            "toDeliveryRef(message)",
            "mapDeliveryState",
            "mapDeliveryFailure",
            "row.delivery",
            "row.failure",
        ]:
            if token not in text:
                failures += fail(f"LegacyChatPresentationSource source missing token: {token}")
        for token in ["onQueued", "onFailed", "ChatDeliveryEventProjector"]:
            if token in strip_cpp_comments(text):
                failures += fail(
                    f"LegacyChatPresentationSource must not project events: {token}"
                )
    return failures


def check_composition_roots_own_delivery() -> int:
    failures = 0
    roots = {
        "apps/linux_sim/src/linux_sim_composition_root.h": [
            "ChatDeliveryReadModel delivery_read_model_",
            "ChatDeliveryEventProjector delivery_projector_",
            "deliveryReadModel()",
            "deliveryProjector()",
        ],
        "apps/linux_uconsole/src/uconsole_composition_root.h": [
            "ChatDeliveryReadModel delivery_read_model_",
            "ChatDeliveryEventProjector delivery_projector_",
            "deliveryReadModel()",
            "deliveryProjector()",
        ],
        "apps/linux_uconsole/src/uconsole_composition_root.cpp": [
            "&delivery_read_model_",
            "LegacyChatPresentationSource",
        ],
    }

    for path, tokens in roots.items():
        if not exists(path):
            failures += fail(f"missing composition root file: {path}")
            continue
        text = read_text(path)
        for token in tokens:
            if token not in text:
                failures += fail(f"{path} missing delivery ownership token: {token}")
    return failures


def check_ui_presentation_and_renderers_do_not_own_delivery() -> int:
    failures = 0

    for path in iter_code_files(
        ROOT / "modules/ui_presentation/include",
        ROOT / "modules/ui_presentation/src",
    ):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        if "chat/delivery" in text or "ChatDelivery" in text:
            failures += fail(
                f"{path.relative_to(ROOT)} imports chat delivery runtime"
            )

    renderer_roots = [
        ROOT / "modules/ui_shared/src/ui/screens",
        ROOT / "modules/ui_shared/src/ui/widgets",
        ROOT / "modules/ui_shared/src/ui/menu",
        ROOT / "apps/linux_uconsole/src/platform/gtk",
    ]
    for path in iter_code_files(*renderer_roots):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        if "chat/delivery" in text or "ChatDelivery" in text:
            failures += fail(
                f"{path.relative_to(ROOT)} must not own chat delivery runtime"
            )
    return failures


def check_team_action_type_shape() -> int:
    failures = 0

    types = "modules/ui_shared/include/ui/team_actions/team_action_types.h"
    if exists(types):
        text = read_text(types)
        for token in [
            "enum class TeamActionKind",
            "LocationMarker",
            "Command",
            "enum class TeamCommandKind",
            "TeamLocationMarkerRequest",
            "use_current_location",
            "TeamLocationSnapshot",
            "TeamCommandRequest",
            "TeamActionRequest",
        ]:
            if token not in text:
                failures += fail(f"Team action types missing token: {token}")
        for token in ["lv_obj_t", "lvgl.h", "TeamChatMessage", "DirectPeer"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"TeamActionRequest contains forbidden token: {token}")

    sink = "modules/ui_shared/include/ui/team_actions/team_action_sink.h"
    if exists(sink):
        text = read_text(sink)
        for token in ["ITeamActionSink", "sendTeamAction", "ITeamLocationSource", "currentTeamLocation"]:
            if token not in text:
                failures += fail(f"Team action sink missing token: {token}")
        for token in ["ChatWorkspaceModel", "lvgl.h", "LegacyChatActionSink"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"Team action sink contains forbidden token: {token}")

    return failures


def check_team_action_bridge_boundary() -> int:
    failures = 0
    bridge_files = [
        "modules/ui_shared/include/ui/team_actions/legacy_team_action_bridge.h",
        "modules/ui_shared/src/ui/team_actions/legacy_team_action_bridge.cpp",
    ]

    for path in bridge_files:
        if not exists(path):
            failures += fail(f"missing Team action bridge file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "lv_obj_t",
            "LegacyChatActionSink",
            "DirectPeer",
            "ConversationKind::Direct",
            "ConversationKind::Channel",
            "ChatWorkspaceModel",
            "MapWorkspaceModel",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden Team action token: {token}")

    source = "modules/ui_shared/src/ui/team_actions/legacy_team_action_bridge.cpp"
    if exists(source):
        text = read_text(source)
        for token in [
            "ITeamLocationSource",
            "currentTeamLocation",
            "encodeTeamChatLocation",
            "encodeTeamChatCommand",
            "team_ui_chatlog_append_structured",
            "sendTeamChat",
        ]:
            if token not in text:
                failures += fail(f"LegacyTeamActionBridge source missing token: {token}")

    return failures


def _function_body(text: str, start_marker: str, end_marker: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        return ""
    end = text.find(end_marker, start)
    if end < 0:
        return text[start:]
    return text[start:end]


def check_chat_ui_team_action_migration() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"

    if exists(header):
        text = read_text(header)
        for token in ["ITeamActionSink", "team_action_sink_"]:
            if token not in text:
                failures += fail(f"ChatUiController header missing Team action token: {token}")
    else:
        failures += fail("ChatUiController header is missing")

    if exists(source):
        text = read_text(source)
        for token in [
            "TeamActionRequest",
            "TeamActionKind::LocationMarker",
            "sendTeamAction",
            "team_chat_model_.sendMessage",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController source missing Team action token: {token}")
        for token in [
            '#include "platform/ui/gps_runtime.h"',
            '#include "team/usecase/team_controller.h"',
        ]:
            if token in text:
                failures += fail(f"ChatUiController source reintroduced runtime include: {token}")

        body = strip_cpp_comments(
            _function_body(
                text,
                "bool UiController::sendTeamLocationWithIcon",
                "void UiController::onTeamPositionIconSelected",
            )
        )
        if not body:
            failures += fail("ChatUiController sendTeamLocationWithIcon body missing")
        else:
            for token in [
                "encodeTeamChatLocation",
                "TeamChatMessage",
                "TeamChatLocation",
                "team_ui_chatlog_append_structured",
                "controller->onChat",
                "setKeysFromPsk",
                "platform::ui::gps::get_data",
                "app::teamFacade",
                "team_ui_get_store",
                "TeamController",
            ]:
                if token in body:
                    failures += fail(
                        f"ChatUiController location send still owns Team runtime token: {token}"
                    )
    else:
        failures += fail("ChatUiController source is missing")

    return failures


def check_chat_runtime_wires_team_action_sink() -> int:
    failures = 0
    path = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    if not exists(path):
        failures += fail("chat_page_runtime.cpp is missing")
        return failures

    text = read_text(path)
    for token in [
        "LegacyTeamActionBridge",
        "GpsTeamLocationSource",
        "s_team_action_sink",
        "s_team_location_source",
        "s_team_action_sink.get()",
        "platform/ui/gps_runtime.h",
    ]:
        if token not in text:
            failures += fail(f"chat_page_runtime.cpp missing Team action wiring token: {token}")
    return failures


def check_ui_presentation_does_not_own_team_actions() -> int:
    failures = 0
    for path in iter_code_files(
        ROOT / "modules/ui_presentation/include",
        ROOT / "modules/ui_presentation/src",
    ):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        if "team_actions" in text or "TeamActionRequest" in text:
            failures += fail(
                f"{path.relative_to(ROOT)} imports Team action runtime"
            )
    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_docs()
    failures += check_core_delivery_is_runtime_only()
    failures += check_delivery_type_shape()
    failures += check_presentation_source_enrichment()
    failures += check_composition_roots_own_delivery()
    failures += check_ui_presentation_and_renderers_do_not_own_delivery()
    failures += check_team_action_type_shape()
    failures += check_team_action_bridge_boundary()
    failures += check_chat_ui_team_action_migration()
    failures += check_chat_runtime_wires_team_action_sink()
    failures += check_ui_presentation_does_not_own_team_actions()

    if failures == 0:
        print("[phase7-runtime-ready] OK")
        return 0

    print(f"[phase7-runtime-ready] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
