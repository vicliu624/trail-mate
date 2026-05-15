#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required file: {rel}")


def require_absent(rel: str, failures: list[str]) -> None:
    if (ROOT / rel).exists():
        failures.append(f"old Chat delivery legacy implementation must be absent: {rel}")


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


def check_build_lists(failures: list[str]) -> None:
    old_sources = [
        "legacy_chat_delivery_action_bridge.cpp",
        "legacy_chat_delivery_event_bridge.cpp",
        "test_legacy_chat_delivery_action_bridge.cpp",
        "test_legacy_chat_delivery_event_bridge.cpp",
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
                failures.append(f"{rel} still references old Chat delivery legacy source/test: {token}")

    for rel in [
        "cmake/TrailMateLinuxSources.cmake",
        "legacy/app_implementations/linux_sim/CMakeLists.txt",
        "legacy/app_implementations/linux_uconsole/CMakeLists.txt",
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


def main() -> int:
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
        "docs/audits/CHAT_LEGACY_DELIVERY_BURN_DOWN_AUDIT.md",
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
    ]:
        require_file(rel, failures)

    for rel in [
        "modules/ui_legacy_adapters/src/legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_legacy_adapters/src/legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_event_bridge.cpp",
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

    check_legacy_headers_are_aliases(failures)
    check_main_code_no_legacy_chat_includes(failures)
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
            "burned-down to deprecated alias",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        [
            "Burned Down In Phase 9.4",
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
