#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def check_post_root_legacy_removed() -> int:
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools/architecture/check_no_root_legacy_ready.py")],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr).strip()
        print(f"[phase9-legacy-burndown] {output}")
        return 1
    print("[phase9-legacy-burndown] OK (root legacy removed)")
    return 0


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required file: {rel}")


def require_absent(rel: str, failures: list[str]) -> None:
    if (ROOT / rel).exists():
        failures.append(f"old legacy implementation must be absent: {rel}")


def require_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    if not (ROOT / rel).exists():
        failures.append(f"missing file for token check: {rel}")
        return
    text = read(rel)
    for token in tokens:
        if token not in text:
            failures.append(f"{rel} missing token: {token}")


def forbid_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    if not (ROOT / rel).exists():
        return
    text = read(rel)
    for token in tokens:
        if token in text:
            failures.append(f"{rel} contains forbidden token: {token}")


def iter_code_files(root: Path):
    if not root.exists():
        return
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
            yield path


def is_allowed_legacy_chat_include(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    name = path.name
    allowed = {
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge_legacy_alias.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_event_bridge_legacy_alias.cpp",
    }
    if rel in allowed:
        return True
    if rel in {
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_action_bridge.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_event_bridge.h",
    }:
        return True
    return (
        name.endswith("_legacy_alias_test.cpp")
        or name.endswith("_legacy_alias.cpp")
        or name.endswith("_compatibility_test.cpp")
    )


def is_allowed_legacy_key_map_include(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    name = path.name
    allowed = {
        "modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters_legacy_alias.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_map_overlay_source_legacy_alias.cpp",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_session.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_source.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_action_sink.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_map_overlay_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h",
    }
    if rel in allowed:
        return True
    return (
        name.endswith("_legacy_alias_test.cpp")
        or name.endswith("_legacy_alias.cpp")
        or name.endswith("_compatibility_test.cpp")
    )


def check_main_code_no_legacy_chat_includes(failures: list[str]) -> None:
    forbidden_includes = [
        '#include "ui_legacy_adapters/legacy_chat_delivery_action_bridge.h"',
        '#include "ui_legacy_adapters/legacy_chat_delivery_event_bridge.h"',
    ]
    for root_name in ["apps", "legacy/app_implementations", "modules", "platform", "boards"]:
        for path in iter_code_files(ROOT / root_name):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in forbidden_includes:
                if token in text and not is_allowed_legacy_chat_include(path):
                    failures.append(
                        f"{path.relative_to(ROOT).as_posix()} includes deprecated Chat delivery legacy header: {token}"
                    )


def check_main_code_no_legacy_key_map_includes(failures: list[str]) -> None:
    forbidden_includes = [
        '#include "ui_legacy_adapters/legacy_key_verification_source.h"',
        '#include "ui_legacy_adapters/legacy_key_verification_action_sink.h"',
        '#include "ui_legacy_adapters/legacy_key_verification_session.h"',
        '#include "ui_legacy_adapters/legacy_map_overlay_source.h"',
    ]
    for root_name in ["apps", "legacy/app_implementations", "modules", "platform", "boards"]:
        for path in iter_code_files(ROOT / root_name):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in forbidden_includes:
                if token in text and not is_allowed_legacy_key_map_include(path):
                    failures.append(
                        f"{path.relative_to(ROOT).as_posix()} includes deprecated KeyVerification/MapOverlay legacy header: {token}"
                    )


def check_legacy_headers_are_aliases(failures: list[str]) -> None:
    headers = {
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_action_bridge.h": [
            "ui_chat_runtime/chat_delivery_action_port_adapter.h",
            "using LegacyChatDeliveryActionBridge",
            "ChatDeliveryActionPortAdapter",
            "[[deprecated",
        ],
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_event_bridge.h": [
            "ui_chat_runtime/chat_delivery_event_projection_adapter.h",
            "using LegacyChatDeliveryEventBridge",
            "ChatDeliveryEventProjectionAdapter",
            "[[deprecated",
        ],
    }
    for rel, tokens in headers.items():
        require_tokens(rel, tokens, failures)
        forbid_tokens(
            rel,
            [
                "class LegacyChatDelivery",
                "struct LegacyChatDelivery",
                "handleMessageAction(",
                "onChatSendResult(",
                "onAckTimeout(",
                "ChatService&",
                "IChatDeliveryActionSink&",
            ],
            failures,
        )


def check_key_map_legacy_headers_are_aliases(failures: list[str]) -> None:
    headers = {
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_session.h": [
            "ui_key_verification_runtime/key_verification_session_adapter.h",
            "using LegacyKeyVerificationSession",
            "KeyVerificationSessionAdapter",
            "[[deprecated",
        ],
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_source.h": [
            "ui_key_verification_runtime/key_verification_presentation_source.h",
            "using LegacyKeyVerificationSource",
            "KeyVerificationPresentationSource",
            "[[deprecated",
        ],
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_action_sink.h": [
            "ui_key_verification_runtime/key_verification_action_sink.h",
            "using LegacyKeyVerificationActionSink",
            "KeyVerificationActionSink",
            "[[deprecated",
        ],
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_map_overlay_source.h": [
            "ui_map_runtime/map_overlay_snapshot_source.h",
            "using LegacyMapOverlaySource",
            "MapOverlaySnapshotSource",
            "[[deprecated",
        ],
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h": [
            "ui_key_verification_runtime/key_verification_session_adapter.h",
            "using LegacyKeyVerificationSession",
            "KeyVerificationSessionAdapter",
            "[[deprecated",
        ],
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h": [
            "ui_key_verification_runtime/key_verification_presentation_source.h",
            "using LegacyKeyVerificationSource",
            "KeyVerificationPresentationSource",
            "[[deprecated",
        ],
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h": [
            "ui_key_verification_runtime/key_verification_action_sink.h",
            "using LegacyKeyVerificationActionSink",
            "KeyVerificationActionSink",
            "[[deprecated",
        ],
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h": [
            "ui_map_runtime/map_overlay_snapshot_source.h",
            "using LegacyMapOverlaySource",
            "MapOverlaySnapshotSource",
            "[[deprecated",
        ],
    }
    for rel, tokens in headers.items():
        require_tokens(rel, tokens, failures)
        forbid_tokens(
            rel,
            [
                "class LegacyKeyVerification",
                "struct LegacyKeyVerification",
                "class LegacyMapOverlaySource",
                "struct LegacyMapOverlaySource",
                "buildKeyVerificationSnapshot(",
                "buildMapOverlaySnapshot(",
                "submitKeyVerificationNumber(",
                "setNodeKeyManuallyVerified(",
                "projectCurrentPosition(",
            ],
            failures,
        )


def check_build_lists(failures: list[str]) -> None:
    old_sources = [
        "legacy_chat_delivery_action_bridge.cpp",
        "legacy_chat_delivery_event_bridge.cpp",
        "test_legacy_chat_delivery_action_bridge.cpp",
        "test_legacy_chat_delivery_event_bridge.cpp",
        "legacy_key_verification_source.cpp",
        "legacy_key_verification_action_sink.cpp",
        "legacy_key_verification_session.cpp",
        "legacy_map_overlay_source.cpp",
        "test_legacy_key_verification_adapters.cpp",
        "test_legacy_map_overlay_source.cpp",
    ]
    checked = [
        "cmake/TrailMateLinuxSources.cmake",
        "apps/linux_sim_shell/CMakeLists.txt",
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        "legacy/app_implementations/linux_sim/CMakeLists.txt",
        "legacy/app_implementations/linux_uconsole/CMakeLists.txt",
        "legacy/app_implementations/esp_idf/CMakeLists.txt",
    ]
    for rel in checked:
        if not (ROOT / rel).exists():
            continue
        text = read(rel)
        for token in old_sources:
            if token in text:
                failures.append(f"{rel} still references old legacy source/test: {token}")

    for rel in [
        "cmake/TrailMateLinuxSources.cmake",
        "legacy/app_implementations/esp_idf/CMakeLists.txt",
    ]:
        require_tokens(
            rel,
            [
                "chat_delivery_action_port_adapter.cpp",
                "chat_delivery_event_projection_adapter.cpp",
            ],
            failures,
        )

    for rel in [
        "cmake/TrailMateLinuxSources.cmake",
        "legacy/app_implementations/esp_idf/CMakeLists.txt",
    ]:
        require_tokens(
            rel,
            [
                "key_verification_action_sink.cpp",
                "key_verification_presentation_source.cpp",
                "map_overlay_projection_adapter.cpp",
                "map_overlay_snapshot_source.cpp",
            ],
            failures,
        )


def main() -> int:
    if not (ROOT / "legacy").exists():
        return check_post_root_legacy_removed()

    failures: list[str] = []

    for rel in [
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_port.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port_adapter.h",
        "modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_projection_adapter.h",
        "modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp",
        "modules/ui_chat_runtime/tests/test_chat_delivery_action_port_adapter.cpp",
        "modules/ui_chat_runtime/tests/test_chat_delivery_event_projection_adapter.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge_legacy_alias.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_event_bridge_legacy_alias.cpp",
        "modules/ui_key_verification_runtime/README.md",
        "modules/ui_key_verification_runtime/library.json",
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_session_adapter.h",
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_presentation_source.h",
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_action_sink.h",
        "modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp",
        "modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp",
        "modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp",
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay_snapshot_source.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay_projection_adapter.h",
        "modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp",
        "modules/ui_map_runtime/src/map_overlay_projection_adapter.cpp",
        "modules/ui_map_runtime/tests/test_map_overlay_snapshot_source.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters_legacy_alias.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_map_overlay_source_legacy_alias.cpp",
        "docs/audits/CHAT_LEGACY_DELIVERY_BURN_DOWN_AUDIT.md",
        "docs/audits/KEY_VERIFICATION_MAP_OVERLAY_LEGACY_BURN_DOWN_AUDIT.md",
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
    ]:
        require_file(rel, failures)

    for rel in [
        "modules/ui_legacy_adapters/src/legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_legacy_adapters/src/legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_legacy_adapters/src/legacy_key_verification_source.cpp",
        "modules/ui_legacy_adapters/src/legacy_key_verification_action_sink.cpp",
        "modules/ui_legacy_adapters/src/legacy_key_verification_session.cpp",
        "modules/ui_legacy_adapters/src/legacy_map_overlay_source.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_map_overlay_source.cpp",
    ]:
        require_absent(rel, failures)

    require_tokens(
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port.h",
        [
            "IChatDeliveryActionPort",
            "ChatDeliveryActionRequest",
            "ChatDeliveryActionResult",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_port.h",
        [
            "IChatDeliveryEventPort",
            "ChatDeliveryEvent",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port_adapter.h",
        [
            "class ChatDeliveryActionPortAdapter",
            "IChatDeliveryActionPort&",
            "handleMessageAction",
            "toDeliveryRef",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_projection_adapter.h",
        [
            "class ChatDeliveryEventProjectionAdapter",
            "IChatDeliveryEventPort&",
            "onChatSendResult",
            "onAckTimeout",
        ],
        failures,
    )
    for rel in [
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_port.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port_adapter.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_projection_adapter.h",
        "modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp",
        "modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp",
    ]:
        forbid_tokens(
            rel,
            [
                "LegacyChatDeliveryActionBridge",
                "LegacyChatDeliveryEventBridge",
                "ui_legacy_adapters/legacy_chat_delivery_action_bridge.h",
                "ui_legacy_adapters/legacy_chat_delivery_event_bridge.h",
            ],
            failures,
        )

    require_tokens(
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_session_adapter.h",
        [
            "struct KeyVerificationSessionAdapter",
            "VerificationProtocol",
            "VerificationState",
            "VerificationPromptKind",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_presentation_source.h",
        [
            "class KeyVerificationPresentationSource",
            "IKeyVerificationPresentationSource",
            "buildKeyVerificationSnapshot",
            "onNumberRequest",
            "onFinal",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_action_sink.h",
        [
            "class KeyVerificationActionSink",
            "IKeyVerificationActionSink",
            "accept",
            "submitNumber",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay_projection_adapter.h",
        [
            "class MapOverlayProjectionAdapter",
            "IMapOverlayGpsSource",
            "IMapOverlayTeamSource",
            "project",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay_snapshot_source.h",
        [
            "class MapOverlaySnapshotSource",
            "IMapOverlayPresentationSource",
            "MapOverlayProjectionAdapter",
            "buildMapOverlaySnapshot",
        ],
        failures,
    )
    for rel in [
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_session_adapter.h",
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_presentation_source.h",
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_action_sink.h",
        "modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp",
        "modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp",
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay_snapshot_source.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay_projection_adapter.h",
        "modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp",
        "modules/ui_map_runtime/src/map_overlay_projection_adapter.cpp",
    ]:
        forbid_tokens(
            rel,
            [
                "LegacyKeyVerificationSource",
                "LegacyKeyVerificationActionSink",
                "LegacyKeyVerificationSession",
                "LegacyMapOverlaySource",
                "ui_legacy_adapters/legacy_key_verification_source.h",
                "ui_legacy_adapters/legacy_key_verification_action_sink.h",
                "ui_legacy_adapters/legacy_key_verification_session.h",
                "ui_legacy_adapters/legacy_map_overlay_source.h",
                "lvgl.h",
                "GtkWidget",
                "lv_obj_t",
            ],
            failures,
        )

    check_legacy_headers_are_aliases(failures)
    check_key_map_legacy_headers_are_aliases(failures)
    check_main_code_no_legacy_chat_includes(failures)
    check_main_code_no_legacy_key_map_includes(failures)
    check_build_lists(failures)

    require_tokens(
        "docs/audits/CHAT_LEGACY_DELIVERY_BURN_DOWN_AUDIT.md",
        [
            "Current compatibility header",
            "Former implementation source",
            "Current callers",
            "Replacement",
            "Migration decision",
            "main runtime callers",
            "deprecated forwarding aliases",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
        [
            "ChatDeliveryActionPortAdapter",
            "ChatDeliveryEventProjectionAdapter",
            "KeyVerificationPresentationSource",
            "KeyVerificationActionSink",
            "KeyVerificationSessionAdapter",
            "MapOverlaySnapshotSource",
            "MapOverlayProjectionAdapter",
            "burned-down to deprecated alias",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        [
            "Burned Down In Phase 9.4",
            "Burned Down In Phase 9.5",
            "main runtime callers removed",
            "deprecated aliases",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
        [
            "Chat LegacyDelivery bridges",
            "burned down to deprecated alias",
            "ChatDeliveryActionPortAdapter",
            "ChatDeliveryEventProjectionAdapter",
            "KeyVerificationPresentationSource",
            "KeyVerificationActionSink",
            "MapOverlaySnapshotSource",
            "MapOverlayProjectionAdapter",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/KEY_VERIFICATION_MAP_OVERLAY_LEGACY_BURN_DOWN_AUDIT.md",
        [
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
            "LegacyKeyVerificationSession",
            "LegacyMapOverlaySource",
            "current callers",
            "main runtime callers",
            "test callers",
            "docs references",
            "replacement",
            "migration decision",
            "deprecated compatibility aliases only",
        ],
        failures,
    )

    if failures:
        for failure in failures:
            print(f"[phase9-legacy-burndown] {failure}")
        return 1

    print("[phase9-legacy-burndown] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
