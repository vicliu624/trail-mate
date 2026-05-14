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
        "docs/audits/CHAT_DELIVERY_EVENT_PROJECTION_AUDIT.md",
        "docs/audits/CHAT_DELIVERY_ACTION_OWNERSHIP_AUDIT.md",
        "docs/audits/KEY_VERIFICATION_OWNERSHIP_AUDIT.md",
        "docs/audits/CHAT_RUNTIME_EVENT_PUMP_AUDIT.md",
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
        "docs/audits/CHAT_UI_CONTROLLER_BURNDOWN_AUDIT.md",
        "docs/audits/PHASE7_6_LEGACY_BURNDOWN_REPORT.md",
        "docs/audits/PHASE7_7_EVENT_PUMP_BURNDOWN_REPORT.md",
        "docs/specification/CHAT_DELIVERY_RUNTIME_SPEC.md",
        "docs/specification/CHAT_DELIVERY_ACTION_RUNTIME_SPEC.md",
        "docs/specification/KEY_VERIFICATION_RUNTIME_SPEC.md",
        "docs/specification/CHAT_RUNTIME_EVENT_PUMP_SPEC.md",
        "docs/audits/TEAM_ACTION_OWNERSHIP_AUDIT.md",
        "docs/specification/TEAM_ACTION_RUNTIME_SPEC.md",
        "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md",
        "modules/core_chat/include/chat/delivery/chat_delivery_types.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_read_model.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_event_projector.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_event_port.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_action_types.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_action_sink.h",
        "modules/core_chat/include/chat/delivery/chat_delivery_action_service.h",
        "modules/core_chat/include/chat/delivery/legacy_chat_delivery_bridge.h",
        "modules/core_chat/include/chat/delivery/legacy_chat_send_result_mapper.h",
        "modules/core_chat/src/delivery/chat_delivery_read_model.cpp",
        "modules/core_chat/src/delivery/chat_delivery_event_projector.cpp",
        "modules/core_chat/src/delivery/chat_delivery_event_port.cpp",
        "modules/core_chat/src/delivery/chat_delivery_action_service.cpp",
        "modules/core_chat/src/delivery/legacy_chat_delivery_bridge.cpp",
        "modules/core_chat/src/delivery/legacy_chat_send_result_mapper.cpp",
        "modules/core_chat/tests/test_chat_delivery_read_model.cpp",
        "modules/core_chat/tests/test_chat_delivery_event_projector.cpp",
        "modules/core_chat/tests/test_chat_delivery_event_port.cpp",
        "modules/core_chat/tests/test_chat_delivery_action_service.cpp",
        "modules/core_chat/tests/test_legacy_chat_delivery_bridge.cpp",
        "modules/core_chat/tests/test_legacy_chat_send_result_mapper.cpp",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_shared/tests/test_legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_event_bridge.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_shared/tests/test_legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_shared/include/ui/team_actions/team_action_types.h",
        "modules/ui_shared/include/ui/team_actions/team_action_sink.h",
        "modules/ui_shared/include/ui/team_actions/legacy_team_action_bridge.h",
        "modules/ui_shared/src/ui/team_actions/legacy_team_action_bridge.cpp",
        "modules/ui_shared/tests/test_team_action_types.cpp",
        "modules/ui_shared/tests/test_legacy_team_action_bridge.cpp",
        "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_snapshot.h",
        "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_source.h",
        "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_action_sink.h",
        "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_model.h",
        "modules/ui_presentation/src/key_verification/key_verification_model.cpp",
        "modules/ui_presentation/tests/test_key_verification_model.cpp",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_action_sink.cpp",
        "modules/ui_shared/tests/test_legacy_key_verification_adapters.cpp",
        "modules/ui_shared/include/ui/screens/chat/key_verification_modal_renderer.h",
        "modules/ui_shared/src/ui/screens/chat/key_verification_modal_renderer.cpp",
        "modules/ui_shared/include/ui/screens/chat/chat_ui_refresh_sink.h",
        "modules/ui_shared/include/ui/screens/chat/chat_page_runtime_event_pump.h",
        "modules/ui_shared/src/ui/screens/chat/chat_page_runtime_event_pump.cpp",
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
            "IChatDeliveryEventPort",
            "ProjectingChatDeliveryEventPort",
            "LegacyChatDeliveryEventBridge",
            "LegacyChatPresentationSource",
            "Phase 7.1 does not make `ChatWorkspaceModel` own delivery state",
        ],
        "docs/audits/CHAT_DELIVERY_ACTION_OWNERSHIP_AUDIT.md": [
            "retry",
            "cancel pending",
            "clear failure",
            "ChatDeliveryActionRequest",
            "ChatDeliveryActionService",
            "Renderers and controllers must not directly clear delivery records",
        ],
        "docs/specification/CHAT_DELIVERY_ACTION_RUNTIME_SPEC.md": [
            "Delivery actions belong to runtime/action sinks",
            "ChatDeliveryActionRequest",
            "IChatDeliveryActionSink",
            "IRetryChatMessagePort",
            "ChatDeliveryActionService",
            "LegacyChatDeliveryActionBridge",
            "`ChatWorkspaceModel` must not own retry, cancel, or clear-failure state",
        ],
        "docs/audits/CHAT_DELIVERY_EVENT_PROJECTION_AUDIT.md": [
            "ChatSendResultEvent",
            "`msg_id`",
            "`success`",
            "success=false",
            "AckTimeout",
            "LegacyChatDeliveryEventBridge",
        ],
        "docs/audits/PHASE7_RUNTIME_OWNERSHIP_REGISTER.md": [
            "Chat delivery / pending / failure",
            "runtime state",
            "not owned by",
            "composition-root ownership",
            "Team location/command action ownership",
            "LegacyTeamActionBridge",
            "Chat retry/cancel/clear failure actions",
            "ChatDeliveryActionService",
            "key verification workflow",
            "KeyVerificationModel",
        ],
        "docs/audits/KEY_VERIFICATION_OWNERSHIP_AUDIT.md": [
            "Key verification is a standalone runtime/presentation workflow",
            "`ChatWorkspaceModel`",
            "`MessageRow`",
            "renderer-local modal state",
            "`IMeshAdapter::submitKeyVerificationNumber(...)`",
            "`ContactService::setNodeKeyManuallyVerified(...)`",
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
        ],
        "docs/specification/KEY_VERIFICATION_RUNTIME_SPEC.md": [
            "Key verification is an independent runtime/presentation workflow",
            "KeyVerificationSnapshot",
            "IKeyVerificationPresentationSource",
            "IKeyVerificationActionSink",
            "KeyVerificationModel",
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
            "Phase 7.5 does not add key verification to `ChatWorkspaceModel` or",
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
        "docs/audits/CHAT_RUNTIME_EVENT_PUMP_AUDIT.md": [
            "Phase 7.7 moves Chat runtime scheduling and event projection out of `ChatUiController`",
            "ChatSendResultEvent",
            "LegacyChatDeliveryEventBridge",
            "LegacyKeyVerificationSource",
            "ChatService::processIncoming()",
            "ChatService::flushStore()",
            "IChatUiRefreshSink",
            "ChatPageRuntimeEventPump",
        ],
        "docs/specification/CHAT_RUNTIME_EVENT_PUMP_SPEC.md": [
            "Runtime event projection belongs to the event pump",
            "UI refresh belongs to the controller",
            "IChatUiRefreshSink",
            "ChatPageRuntimeEventPump",
            "ChatPageRuntimeFacade",
            "ChatSendResultEvent",
            "ChatService::processIncoming()",
            "ChatService::flushStore()",
        ],
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md": [
            "Remaining callers",
            "Removal condition",
            "Target phase",
            "Status",
            "LegacyChatDeliveryEventBridge",
            "LegacyChatDeliveryActionBridge",
            "LegacyTeamActionBridge",
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
            "ChatUiController` key verification modal rendering",
            "ChatUiController` Team payload encoding",
            "ChatUiController` delivery mutation",
            "ChatUiController` runtime event pump",
        ],
        "docs/audits/CHAT_UI_CONTROLLER_BURNDOWN_AUDIT.md": [
            "Temporary UI Responsibilities",
            "Migrated Runtime Responsibilities",
            "Remaining Legacy Responsibilities",
            "Team location/command payload encoding",
            "Key verification modal rendering",
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "ChatDeliveryActionService",
        ],
        "docs/audits/PHASE7_6_LEGACY_BURNDOWN_REPORT.md": [
            "Phase 7.6 reduced Chat / Team / key-verification legacy ownership surfaces",
            "Burned Down",
            "Still Contained",
            "Checker Changes",
            "Remaining Work",
        ],
        "docs/audits/PHASE7_7_EVENT_PUMP_BURNDOWN_REPORT.md": [
            "Phase 7.7 moved Chat runtime event projection and ChatService scheduling out of `ChatUiController`",
            "ChatPageRuntimeEventPump",
            "ChatPageRuntimeFacade",
            "ChatService::processIncoming()",
            "ChatService::flushStore()",
            "Burned Down",
            "Still Contained",
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


def check_legacy_burndown_register() -> int:
    failures = 0
    path = "docs/audits/LEGACY_BURNDOWN_REGISTER.md"
    if not exists(path):
        return failures

    text = read_text(path)
    header = (
        "| Legacy surface | New owner | Remaining callers | Removal condition | "
        "Target phase | Status |"
    )
    if header not in text:
        failures += fail("LEGACY_BURNDOWN_REGISTER.md missing required table header")

    required_surfaces = [
        "LegacyChatDeliveryEventBridge",
        "LegacyChatDeliveryActionBridge",
        "LegacyTeamActionBridge",
        "LegacyKeyVerificationSource",
        "LegacyKeyVerificationActionSink",
        "ChatUiController` key verification modal rendering",
        "ChatUiController` Team payload encoding",
        "ChatUiController` delivery mutation",
        "ChatUiController` runtime event pump",
    ]
    for surface in required_surfaces:
        if surface not in text:
            failures += fail(f"LEGACY_BURNDOWN_REGISTER.md missing surface: {surface}")

    for line in text.splitlines():
        if not line.startswith("| `") or line.startswith("| ---"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 6:
            failures += fail(f"legacy burn-down row has too few cells: {line}")
            continue
        surface, _owner, callers, removal_condition, target_phase, status = cells[:6]
        if not callers or callers.lower() in {"none", "n/a"}:
            failures += fail(f"{surface} missing remaining caller/none rationale")
        if not removal_condition or removal_condition.lower() in {"none", "n/a"}:
            failures += fail(f"{surface} missing removal condition")
        if not target_phase or target_phase.lower() in {"none", "n/a"}:
            failures += fail(f"{surface} missing target phase")
        if status not in {"contained", "burned-down", "remaining legacy"}:
            failures += fail(f"{surface} has unsupported burn-down status: {status}")

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

    event_port = "modules/core_chat/include/chat/delivery/chat_delivery_event_port.h"
    if exists(event_port):
        text = read_text(event_port)
        for token in [
            "ChatDeliveryEvent",
            "IChatDeliveryEventPort",
            "publishDeliveryEvent",
            "ProjectingChatDeliveryEventPort",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryEventPort missing token: {token}")
        for token in ["ChatWorkspaceModel", "MessageRow", "lvgl.h", "sendText"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"ChatDeliveryEventPort owns forbidden token: {token}")

    action_types = "modules/core_chat/include/chat/delivery/chat_delivery_action_types.h"
    if exists(action_types):
        text = read_text(action_types)
        for token in [
            "enum class ChatDeliveryActionKind",
            "Retry",
            "CancelPending",
            "ClearFailure",
            "ChatDeliveryActionRequest",
            "ChatDeliveryActionFailure",
            "InvalidRef",
            "Unsupported",
            "NotRetryable",
            "ChatDeliveryActionResult",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryActionTypes missing token: {token}")
        for token in ["UiActionResult", "lvgl.h", "ChatWorkspaceModel", "MessageRow"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"ChatDeliveryActionTypes owns forbidden token: {token}")

    action_sink = "modules/core_chat/include/chat/delivery/chat_delivery_action_sink.h"
    if exists(action_sink):
        text = read_text(action_sink)
        for token in ["IChatDeliveryActionSink", "handleDeliveryAction"]:
            if token not in text:
                failures += fail(f"ChatDeliveryActionSink missing token: {token}")

    action_service_header = (
        "modules/core_chat/include/chat/delivery/chat_delivery_action_service.h"
    )
    action_service_source = (
        "modules/core_chat/src/delivery/chat_delivery_action_service.cpp"
    )
    for path in [action_service_header, action_service_source]:
        if exists(path):
            text = strip_cpp_comments(read_text(path))
            for token in ["lvgl.h", "gtk", "ChatWorkspaceModel", "MessageRow", "ui_presentation", "UiActionResult"]:
                if token in text:
                    failures += fail(
                        f"{path} contains forbidden delivery action token: {token}"
                    )
    if exists(action_service_header):
        text = read_text(action_service_header)
        for token in [
            "IRetryChatMessagePort",
            "retryMessage",
            "ChatDeliveryActionService",
            "IChatDeliveryActionSink",
            "ChatDeliveryReadModel& read_model_",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryActionService header missing token: {token}")
    if exists(action_service_source):
        text = read_text(action_service_source)
        for token in [
            "ChatDeliveryActionKind::Retry",
            "ChatDeliveryActionKind::CancelPending",
            "ChatDeliveryActionKind::ClearFailure",
            "read_model_.clear",
            "retry_port_->retryMessage",
            "DeliveryState::Queued",
            "DeliveryState::Sending",
            "DeliveryState::Failed",
        ]:
            if token not in text:
                failures += fail(f"ChatDeliveryActionService source missing token: {token}")

    mapper = "modules/core_chat/include/chat/delivery/legacy_chat_send_result_mapper.h"
    if exists(mapper):
        text = read_text(mapper)
        for token in [
            "LegacyChatSendFailure",
            "PeerKeyMissing",
            "LocalIdentityMissing",
            "RadioSendFailed",
            "AckTimeout",
            "UnsupportedProtocol",
            "Rejected",
            "mapLegacyChatSendResult",
            "makeAckTimeoutDeliveryEvent",
        ]:
            if token not in text:
                failures += fail(f"LegacyChatSendResultMapper missing token: {token}")

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
            "ProjectingChatDeliveryEventPort delivery_event_port_",
            "ChatDeliveryActionService delivery_action_service_",
            "deliveryReadModel()",
            "deliveryProjector()",
            "deliveryEventPort()",
            "deliveryActionSink()",
        ],
        "apps/linux_uconsole/src/uconsole_composition_root.h": [
            "ChatDeliveryReadModel delivery_read_model_",
            "ChatDeliveryEventProjector delivery_projector_",
            "ProjectingChatDeliveryEventPort delivery_event_port_",
            "ChatDeliveryActionService delivery_action_service_",
            "deliveryReadModel()",
            "deliveryProjector()",
            "deliveryEventPort()",
            "deliveryActionSink()",
        ],
        "apps/linux_uconsole/src/uconsole_composition_root.cpp": [
            "&delivery_read_model_",
            "LegacyChatPresentationSource",
            "deliveryEventPort()",
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
        if path.name == "chat_page_runtime.cpp":
            continue
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        forbidden_delivery_tokens = [
            '#include "chat/delivery',
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "ProjectingChatDeliveryEventPort",
        ]
        for token in forbidden_delivery_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} must not own chat delivery runtime token {token}"
                )
        if (
            "LegacyChatDeliveryEventBridge" in text
            and path.name != "chat_page_runtime_event_pump.cpp"
        ):
            failures += fail(
                f"{path.relative_to(ROOT)} contains delivery event bridge outside approved event pump"
            )
    return failures


def check_delivery_event_bridge_boundary() -> int:
    failures = 0
    bridge_files = [
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_event_bridge.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_event_bridge.cpp",
    ]
    for path in bridge_files:
        if not exists(path):
            failures += fail(f"missing delivery event bridge file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "gtk",
            "ChatWorkspaceModel",
            "MessageRow",
            "sendText(",
            "sendMessage(",
            "retry",
            "Renderer",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden delivery bridge token {token}")

    source = "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_event_bridge.cpp"
    if exists(source):
        text = read_text(source)
        for token in [
            "onChatSendResult",
            "onAckTimeout",
            "mapLegacyChatSendResult",
            "publishDeliveryEvent",
            "toDeliveryRef",
            "getMessage",
        ]:
            if token not in text:
                failures += fail(f"LegacyChatDeliveryEventBridge missing token: {token}")

    controller = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "LegacyChatDeliveryEventBridge",
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "ProjectingChatDeliveryEventPort",
            "delivery_event_bridge",
            "delivery_read_model",
            "delivery_projector",
        ]:
            if token in text:
                failures += fail(f"ChatUiController owns delivery runtime token: {token}")

    event_pump = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime_event_pump.cpp"
    if exists(event_pump):
        text = read_text(event_pump)
        for token in [
            "LegacyChatDeliveryEventBridge",
            "delivery_bridge_->onChatSendResult",
        ]:
            if token not in text:
                failures += fail(f"ChatPageRuntimeEventPump missing delivery projection token: {token}")

    runtime = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    if exists(runtime):
        text = read_text(runtime)
        for token in [
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "ProjectingChatDeliveryEventPort",
            "LegacyChatDeliveryEventBridge",
            "s_delivery_event_bridge",
            "s_delivery_read_model",
        ]:
            if token not in text:
                failures += fail(f"chat_page_runtime.cpp missing delivery runtime wiring token: {token}")

    return failures


def check_delivery_action_bridge_boundary() -> int:
    failures = 0
    bridge_files = [
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_action_bridge.cpp",
    ]
    for path in bridge_files:
        if not exists(path):
            failures += fail(f"missing delivery action bridge file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "gtk",
            "ChatWorkspaceModel",
            "MessageRow",
            "ChatDeliveryReadModel",
            "read_model",
            "sendText(",
            "sendMessage(",
            "Renderer",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden delivery action bridge token {token}")

    header = "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h"
    if exists(header):
        text = read_text(header)
        for token in [
            "LegacyChatDeliveryActionBridge",
            "IChatDeliveryActionSink",
            "handleMessageAction",
            "retryMessage",
            "cancelPending",
            "clearFailure",
        ]:
            if token not in text:
                failures += fail(f"LegacyChatDeliveryActionBridge header missing token: {token}")

    source = "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_action_bridge.cpp"
    if exists(source):
        text = read_text(source)
        for token in [
            "toDeliveryRef",
            "MessageRef",
            "ChatDeliveryActionRequest",
            "handleDeliveryAction",
            "ChatDeliveryActionKind::Retry",
            "ChatDeliveryActionKind::CancelPending",
            "ChatDeliveryActionKind::ClearFailure",
        ]:
            if token not in text:
                failures += fail(f"LegacyChatDeliveryActionBridge source missing token: {token}")

    runtime = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    if exists(runtime):
        text = read_text(runtime)
        for token in [
            "ChatDeliveryActionService",
            "LegacyChatDeliveryActionBridge",
            "s_delivery_action_service",
            "s_delivery_action_bridge",
        ]:
            if token not in text:
                failures += fail(f"chat_page_runtime.cpp missing delivery action wiring token: {token}")

    controller = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    if exists(controller):
        text = strip_cpp_comments(read_text(controller))
        for token in [
            "ChatDeliveryActionService",
            "ChatDeliveryActionRequest",
            "IChatDeliveryActionSink",
            "LegacyChatDeliveryActionBridge",
            "s_delivery_action",
        ]:
            if token in text:
                failures += fail(f"ChatUiController owns delivery action token: {token}")

    for path in [
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_model.h",
        "modules/ui_presentation/src/chat/chat_workspace_model.cpp",
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_snapshot.h",
    ]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "ChatDeliveryAction",
            "IChatDeliveryActionSink",
            "Retry",
            "CancelPending",
            "ClearFailure",
        ]:
            if token in text:
                failures += fail(f"{path} owns delivery action token: {token}")

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


def check_chat_ui_legacy_burndown() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"

    controller_forbidden = [
        "encodeTeamChatLocation",
        "encodeTeamChatCommand",
        "TeamChatMessage",
        "setKeysFromPsk",
        "team_ui_chatlog_append_structured",
        "platform::ui::gps::get_data",
        "app::messagingFacade().getMeshAdapter()",
        "submitKeyVerificationNumber",
        "setNodeKeyManuallyVerified",
        "ChatDeliveryReadModel",
        "ChatDeliveryEventProjector",
        "ChatDeliveryActionService",
    ]

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in controller_forbidden:
            if token in text:
                failures += fail(
                    f"{path} contains forbidden Phase 7.6 legacy ownership token: {token}"
                )

    if exists(source):
        text = strip_cpp_comments(read_text(source))
        required = [
            "KeyVerificationModalCallbacks",
            "key_verify_modal_",
            "submitKeyVerificationInput",
            "key_verification_model_->submitNumber",
            "key_verification_model_->accept",
        ]
        for token in required:
            if token not in text:
                failures += fail(f"ChatUiController missing burn-down token: {token}")

    renderer_header = "modules/ui_shared/include/ui/screens/chat/key_verification_modal_renderer.h"
    renderer_source = "modules/ui_shared/src/ui/screens/chat/key_verification_modal_renderer.cpp"
    for path in [renderer_header, renderer_source]:
        if not exists(path):
            failures += fail(f"missing key verification modal renderer file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "ContactService",
            "IMeshAdapter",
            "LegacyKeyVerificationSession",
            "ChatService",
            "ChatWorkspaceModel",
            "MessageRow",
            "submitKeyVerificationNumber",
            "setNodeKeyManuallyVerified",
            "accept(",
            "reject(",
            "refresh(",
            "submitNumber",
        ]:
            if token in text:
                failures += fail(
                    f"{path} contains forbidden modal renderer ownership token: {token}"
                )
    if exists(renderer_header):
        text = read_text(renderer_header)
        for token in [
            "KeyVerificationModalRefs",
            "KeyVerificationModalCallbacks",
            "renderKeyVerificationModal",
            "destroyKeyVerificationModal",
            "clearKeyVerificationError",
        ]:
            if token not in text:
                failures += fail(f"key verification modal renderer missing token: {token}")

    return failures


def check_chat_runtime_event_pump_boundary() -> int:
    failures = 0
    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    runtime = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    pump_header = "modules/ui_shared/include/ui/screens/chat/chat_page_runtime_event_pump.h"
    pump_source = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime_event_pump.cpp"
    refresh_sink = "modules/ui_shared/include/ui/screens/chat/chat_ui_refresh_sink.h"

    for path in [header, source]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "public IChatUiRuntime",
            "onChatEvent",
            "processIncoming(",
            "flushStore(",
            "delivery_event_bridge_",
            "LegacyChatDeliveryEventBridge",
            "key_verification_source_",
            "LegacyKeyVerificationSource",
            "onNumberRequest",
            "onNumberInform",
            "onFinal",
            "KeyVerificationNumberRequestEvent",
            "KeyVerificationNumberInformEvent",
            "KeyVerificationFinalEvent",
        ]:
            if token in text:
                failures += fail(
                    f"{path} still owns runtime event pump token: {token}"
                )

    if exists(header):
        text = read_text(header)
        for token in [
            "public IChatUiRefreshSink",
            "onRuntimeMessageArrived",
            "onRuntimeSendResult",
            "onRuntimeUnreadChanged",
            "showKeyVerification",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController header missing refresh sink token: {token}")

    if exists(refresh_sink):
        text = strip_cpp_comments(read_text(refresh_sink))
        for token in [
            "IChatUiRefreshSink",
            "onRuntimeMessageArrived",
            "onRuntimeSendResult",
            "onRuntimeUnreadChanged",
            "showKeyVerification",
        ]:
            if token not in text:
                failures += fail(f"IChatUiRefreshSink missing token: {token}")
        for token in [
            "ChatService",
            "ChatDeliveryReadModel",
            "ChatDeliveryEventProjector",
            "LegacyKeyVerificationSource",
            "sys::Event",
        ]:
            if token in text:
                failures += fail(f"IChatUiRefreshSink exposes runtime token: {token}")

    for path in [pump_header, pump_source]:
        if not exists(path):
            failures += fail(f"missing ChatPageRuntimeEventPump file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in ["lvgl.h", "lv_obj_t", "ChatWorkspaceModel", "MessageRow", "encodeTeamChat"]:
            if token in text:
                failures += fail(f"{path} contains forbidden event pump UI token: {token}")

    if exists(pump_source):
        text = read_text(pump_source)
        for token in [
            "service_.processIncoming()",
            "service_.flushStore()",
            "delivery_bridge_->onChatSendResult",
            "key_verification_source_->onNumberRequest",
            "key_verification_source_->onNumberInform",
            "key_verification_source_->onFinal",
            "ui_->onRuntimeMessageArrived",
            "ui_->onRuntimeSendResult",
            "ui_->onRuntimeUnreadChanged",
            "ui_->showKeyVerification",
            "delete event",
        ]:
            if token not in text:
                failures += fail(f"ChatPageRuntimeEventPump source missing token: {token}")

    if exists(runtime):
        text = read_text(runtime)
        for token in [
            "ChatPageRuntimeFacade",
            "s_event_pump",
            "s_runtime_facade",
            "ChatPageRuntimeEventPump",
            "event_pump_.update();",
            "controller_.update();",
            "event_pump_.handleEvent(event);",
            "setChatUiRuntime(s_runtime_facade.get())",
        ]:
            if token not in text:
                failures += fail(f"chat_page_runtime.cpp missing event pump wiring token: {token}")
        if "setChatUiRuntime(s_ui_controller.get())" in text:
            failures += fail("chat_page_runtime.cpp still registers controller directly")

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


def check_key_verification_type_shape() -> int:
    failures = 0

    snapshot = "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_snapshot.h"
    if exists(snapshot):
        text = read_text(snapshot)
        for token in [
            "enum class VerificationProtocol",
            "Meshtastic",
            "MeshCore",
            "enum class VerificationState",
            "Pending",
            "Verified",
            "enum class VerificationFailureKind",
            "MissingPeerKey",
            "MissingLocalIdentity",
            "enum class VerificationPromptKind",
            "EnterNumber",
            "ShowNumber",
            "CompareCode",
            "KeyVerificationSnapshot",
            "can_accept",
            "can_reject",
            "can_refresh",
            "can_copy_code",
            "requires_number",
        ]:
            if token not in text:
                failures += fail(f"KeyVerificationSnapshot missing token: {token}")
        for token in ["private", "public_key", "PSK", "packet", "ChatWorkspaceModel", "MessageRow"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"KeyVerificationSnapshot owns forbidden token: {token}")

    model_header = "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_model.h"
    model_source = "modules/ui_presentation/src/key_verification/key_verification_model.cpp"
    for path in [model_header, model_source]:
        if exists(path):
            text = strip_cpp_comments(read_text(path))
            for token in [
                "ChatService",
                "IMeshAdapter",
                "ContactService",
                "lvgl.h",
                "gtk",
                "sys/event_bus",
                "ChatWorkspaceModel",
                "MessageRow",
                "platform/",
                "setNodeKeyManuallyVerified",
                "submitKeyVerificationNumber",
            ]:
                if token in text:
                    failures += fail(f"{path} contains forbidden key verification model token: {token}")

    if exists(model_header):
        text = read_text(model_header)
        for token in [
            "KeyVerificationModel",
            "selectPeer",
            "clearSelection",
            "snapshot",
            "accept",
            "reject",
            "refresh",
            "copyCode",
            "submitNumber",
            "IKeyVerificationPresentationSource& source_",
            "IKeyVerificationActionSink& sink_",
        ]:
            if token not in text:
                failures += fail(f"KeyVerificationModel missing token: {token}")

    source = "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_source.h"
    if exists(source):
        text = read_text(source)
        for token in ["KeyVerificationRequest", "IKeyVerificationPresentationSource", "buildKeyVerificationSnapshot"]:
            if token not in text:
                failures += fail(f"KeyVerificationSource missing token: {token}")

    sink = "modules/ui_presentation/include/ui_presentation/key_verification/key_verification_action_sink.h"
    if exists(sink):
        text = read_text(sink)
        for token in ["IKeyVerificationActionSink", "accept", "reject", "refresh", "copyCode", "submitNumber"]:
            if token not in text:
                failures += fail(f"KeyVerificationActionSink missing token: {token}")

    return failures


def check_key_verification_bridge_boundary() -> int:
    failures = 0
    bridge_files = [
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_source.cpp",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_action_sink.cpp",
    ]
    for path in bridge_files:
        if not exists(path):
            failures += fail(f"missing key verification bridge file: {path}")
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "lvgl.h",
            "gtk",
            "ChatWorkspaceModel",
            "MessageRow",
            "ChatDeliveryReadModel",
            "LegacyChatActionSink",
            "sendMessage(",
            "sendText(",
            "Renderer",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden key verification bridge token {token}")

    source_header = "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h"
    source_impl = "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_source.cpp"
    if exists(source_header):
        text = read_text(source_header)
        for token in [
            "LegacyKeyVerificationSource",
            "IKeyVerificationPresentationSource",
            "onNumberRequest",
            "onNumberInform",
            "onFinal",
            "onPeerKeyMissing",
        ]:
            if token not in text:
                failures += fail(f"LegacyKeyVerificationSource header missing token: {token}")
    if exists(source_impl):
        text = read_text(source_impl)
        for token in [
            "buildKeyVerificationSnapshot",
            "VerificationPromptKind::EnterNumber",
            "VerificationPromptKind::ShowNumber",
            "VerificationPromptKind::CompareCode",
            "getContactName",
            "backendProtocol",
        ]:
            if token not in text:
                failures += fail(f"LegacyKeyVerificationSource source missing token: {token}")
        for token in ["submitKeyVerificationNumber", "setNodeKeyManuallyVerified"]:
            if token in strip_cpp_comments(text):
                failures += fail(f"LegacyKeyVerificationSource mutates key verification runtime: {token}")

    sink_header = "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h"
    sink_impl = "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_action_sink.cpp"
    if exists(sink_header):
        text = read_text(sink_header)
        for token in [
            "LegacyKeyVerificationActionSink",
            "IKeyVerificationActionSink",
            "accept",
            "reject",
            "refresh",
            "copyCode",
            "submitNumber",
        ]:
            if token not in text:
                failures += fail(f"LegacyKeyVerificationActionSink header missing token: {token}")
    if exists(sink_impl):
        text = read_text(sink_impl)
        for token in [
            "startKeyVerification",
            "submitKeyVerificationNumber",
            "setNodeKeyManuallyVerified",
            "VerificationState::Verified",
            "VerificationState::Rejected",
        ]:
            if token not in text:
                failures += fail(f"LegacyKeyVerificationActionSink source missing token: {token}")

    return failures


def check_chat_ui_key_verification_migration() -> int:
    failures = 0

    header = "modules/ui_shared/include/ui/screens/chat/chat_ui_controller.h"
    source = "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"

    if exists(header):
        text = strip_cpp_comments(read_text(header))
        for token in ["KeyVerificationModel", "key_verification_model_"]:
            if token not in text:
                failures += fail(f"ChatUiController header missing key verification token: {token}")
        for token in ["LegacyKeyVerificationSource", "key_verification_source_"]:
            if token in text:
                failures += fail(f"ChatUiController header still owns key verification source token: {token}")
        for token in [
            "key_verify_node_id_",
            "key_verify_nonce_",
            "key_verify_expects_number_",
            "key_verify_can_trust_",
            "openKeyVerificationNumberModal",
            "openKeyVerificationInfoModal",
            "openKeyVerificationFinalModal",
        ]:
            if token in text:
                failures += fail(f"ChatUiController header still owns key verification token: {token}")
    else:
        failures += fail("ChatUiController header is missing")

    if exists(source):
        text = strip_cpp_comments(read_text(source))
        for token in [
            "renderKeyVerificationModal",
            "KeyVerificationModalCallbacks",
            "key_verification_model_->submitNumber",
            "key_verification_model_->accept",
            "submitKeyVerificationInput",
            "showKeyVerification",
        ]:
            if token not in text:
                failures += fail(f"ChatUiController source missing key verification token: {token}")
        for token in [
            "key_verify_node_id_",
            "key_verify_nonce_",
            "key_verify_expects_number_",
            "key_verify_can_trust_",
            "submitKeyVerificationNumber",
            "setNodeKeyManuallyVerified",
            "KeyVerificationNumberRequestEvent",
            "KeyVerificationNumberInformEvent",
            "KeyVerificationFinalEvent",
            "key_verification_source_->onNumberRequest",
            "key_verification_source_->onNumberInform",
            "key_verification_source_->onFinal",
            "key_verification_model_->selectPeer",
        ]:
            if token in text:
                failures += fail(f"ChatUiController source still owns key verification token: {token}")
        if "getMeshAdapter()" in text:
            failures += fail("ChatUiController key verification path still reads mesh adapter directly")
    else:
        failures += fail("ChatUiController source is missing")

    runtime = "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    if exists(runtime):
        text = read_text(runtime)
        for token in [
            "LegacyKeyVerificationSession",
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
            "KeyVerificationModel",
            "s_key_verification_session",
            "s_key_verification_source",
            "s_key_verification_sink",
            "s_key_verification_model",
        ]:
            if token not in text:
                failures += fail(f"chat_page_runtime.cpp missing key verification wiring token: {token}")
    else:
        failures += fail("chat_page_runtime.cpp is missing")

    for path in [
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_model.h",
        "modules/ui_presentation/src/chat/chat_workspace_model.cpp",
        "modules/ui_presentation/include/ui_presentation/chat/chat_workspace_snapshot.h",
    ]:
        if not exists(path):
            continue
        text = strip_cpp_comments(read_text(path))
        for token in [
            "KeyVerification",
            "VerificationPrompt",
            "verification_code",
            "can_accept_key",
            "MessageKeyVerification",
        ]:
            if token in text:
                failures += fail(f"{path} owns key verification token: {token}")

    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_docs()
    failures += check_legacy_burndown_register()
    failures += check_core_delivery_is_runtime_only()
    failures += check_delivery_type_shape()
    failures += check_presentation_source_enrichment()
    failures += check_composition_roots_own_delivery()
    failures += check_ui_presentation_and_renderers_do_not_own_delivery()
    failures += check_delivery_event_bridge_boundary()
    failures += check_delivery_action_bridge_boundary()
    failures += check_team_action_type_shape()
    failures += check_team_action_bridge_boundary()
    failures += check_chat_ui_team_action_migration()
    failures += check_chat_ui_legacy_burndown()
    failures += check_chat_runtime_event_pump_boundary()
    failures += check_chat_runtime_wires_team_action_sink()
    failures += check_ui_presentation_does_not_own_team_actions()
    failures += check_key_verification_type_shape()
    failures += check_key_verification_bridge_boundary()
    failures += check_chat_ui_key_verification_migration()

    if failures == 0:
        print("[phase7-runtime-ready] OK")
        return 0

    print(f"[phase7-runtime-ready] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
