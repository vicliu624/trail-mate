#!/usr/bin/env python3
"""Check the strict Phase 4.5 exit gate before Phase 5 UI work starts.

This checker is intentionally narrower than check_product_architecture_boundaries.py.
The global checker still reports historical Phase 1-4 drift. This script only
guards the minimum architecture facts that Phase 4.5 is meant to solidify.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os
import re
import sys


REPO_ROOT = Path(__file__).resolve().parents[2]

SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".ipp",
}

EXCLUDED_DIR_NAMES = {
    ".git",
    ".pio",
    ".pytest_cache",
    ".tmp",
    ".venv",
    "__pycache__",
    "build",
    "build-debug",
    "build-release",
    "cmake-build-debug",
    "dist",
    "managed_components",
}


@dataclass(frozen=True)
class Failure:
    check: str
    path: Path | None
    detail: str


def rel(path: Path | None) -> str:
    if path is None:
        return "-"
    try:
        return path.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def iter_source_files(root: Path):
    if not root.exists():
        return
    for current_root, dir_names, file_names in os.walk(root):
        dir_names[:] = [
            name for name in dir_names if name not in EXCLUDED_DIR_NAMES
        ]
        current_root_path = Path(current_root)
        for file_name in file_names:
            path = current_root_path / file_name
            if path.suffix.lower() in SOURCE_SUFFIXES:
                yield path


def parse_inline_yaml_list(value: str) -> list[str]:
    value = value.strip()
    if not (value.startswith("[") and value.endswith("]")):
        return []
    inner = value[1:-1].strip()
    if not inner:
        return []
    return [
        item.strip().strip("'\"")
        for item in inner.split(",")
        if item.strip()
    ]


def extract_yaml_list(text: str, key: str) -> list[str]:
    lines = text.splitlines()
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.startswith(f"{key}:"):
            continue

        indent = len(line) - len(line.lstrip())
        after_colon = line.split(":", 1)[1].strip()
        if after_colon.startswith("["):
            return parse_inline_yaml_list(after_colon)

        values: list[str] = []
        for next_line in lines[index + 1 :]:
            if not next_line.strip() or next_line.lstrip().startswith("#"):
                continue
            next_indent = len(next_line) - len(next_line.lstrip())
            next_stripped = next_line.strip()
            if next_indent <= indent and not next_stripped.startswith("-"):
                break
            if next_stripped.startswith("-"):
                values.append(next_stripped[1:].strip().strip("'\""))
        return values

    return []


def check_target_manifests(failures: list[Failure]) -> None:
    target_dir = REPO_ROOT / "docs" / "targets"
    manifests = sorted(target_dir.glob("*.capabilities.yaml"))
    template = target_dir / "TARGET_MANIFEST_TEMPLATE.yaml"
    if template.exists():
        manifests.append(template)

    if not manifests:
        failures.append(Failure("target-manifests", target_dir, "no target manifests found"))
        return

    required_modules = {"core_phone", "core_mesh"}
    required_lora_consumers = {"core_mesh.meshtastic", "core_mesh.meshcore"}
    required_gps_consumers = {
        "core_gps.nmea",
        "core_gps.location_service",
        "core_gps.time_authority",
    }
    required_ble_consumers = {"core_phone.meshtastic", "core_phone.meshcore"}

    for manifest in manifests:
        text = read_text(manifest)
        modules = set(extract_yaml_list(text, "enabled_modules"))
        missing_modules = sorted(required_modules - modules)
        if missing_modules:
            failures.append(
                Failure(
                    "target-manifest-enabled-modules",
                    manifest,
                    f"missing enabled_modules entries: {', '.join(missing_modules)}",
                )
            )

        for token in sorted(required_lora_consumers | required_gps_consumers):
            if token not in text:
                failures.append(
                    Failure("target-manifest-capability-bindings", manifest, f"missing {token}")
                )

        if text.count('radio_profile_source: "core_mesh"') < 2:
            failures.append(
                Failure(
                    "target-manifest-protocols",
                    manifest,
                    "Meshtastic and MeshCore must both use core_mesh as radio_profile_source",
                )
            )

        ble_core_count = text.count('ble_phone_core: "core_phone"')
        ble_disabled_count = text.count('ble_phone_core: "disabled_no_ble_stack"')
        if ble_core_count + ble_disabled_count < 2:
            failures.append(
                Failure(
                    "target-manifest-protocols",
                    manifest,
                    "Meshtastic and MeshCore must declare ble_phone_core ownership or no-BLE status",
                )
            )

        if ble_core_count > 0:
            for token in sorted(required_ble_consumers):
                if token not in text:
                    failures.append(
                        Failure("target-manifest-capability-bindings", manifest, f"missing {token}")
                    )


def check_core_phone_facade(failures: list[Failure]) -> None:
    forbidden = {
        "ChatService",
        "ContactService",
        "IMeshAdapter",
        "INodeStore",
        "getChatService",
        "getContactService",
        "getMeshAdapter",
        "getNodeStore",
    }
    roots = [
        REPO_ROOT / "modules" / "core_phone" / "include",
        REPO_ROOT / "modules" / "core_phone" / "src",
    ]
    for root in roots:
        for path in iter_source_files(root) or []:
            text = read_text(path)
            for token in forbidden:
                if token in text:
                    failures.append(
                        Failure(
                            "core-phone-facade-narrowing",
                            path,
                            f"core_phone must not expose app service object token {token}",
                        )
                    )

    fake_facade = REPO_ROOT / "modules" / "core_phone" / "tests" / "fake_phone_runtime_context.h"
    if not fake_facade.exists() or "public IPhoneAppFacade" not in read_text(fake_facade):
        failures.append(
            Failure(
                "core-phone-facade-tests",
                fake_facade,
                "fake phone runtime context must implement IPhoneAppFacade",
            )
        )


def check_core_mesh_platform_pollution(failures: list[Failure]) -> None:
    banned_include_prefixes = (
        "arduino.h",
        "preferences.h",
        "sqlite3.h",
        "freertos/",
        "zephyr/",
        "nrf_",
        "esp_",
        "radiolib",
        "lvgl.h",
        "gtk/",
        "bluefruit",
        "nimble",
        "platform/",
        "board/",
        "boards/",
    )
    include_re = re.compile(r"^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]", re.MULTILINE)
    root = REPO_ROOT / "modules" / "core_mesh"
    for path in iter_source_files(root) or []:
        text = read_text(path)
        for match in include_re.finditer(text):
            include = match.group(1).lower()
            if include.startswith(banned_include_prefixes):
                failures.append(
                    Failure(
                        "core-mesh-platform-include",
                        path,
                        f"forbidden core_mesh include: {match.group(1)}",
                    )
                )


def check_required_tests(failures: list[Failure]) -> None:
    required = [
        "modules/core_mesh/tests/test_peer_identity_service.cpp",
        "modules/core_mesh/tests/test_direct_message_service.cpp",
        "modules/core_mesh/tests/test_receive_packet_service.cpp",
        "modules/core_mesh/tests/test_meshtastic_protocol_strategy.cpp",
        "modules/core_mesh/tests/test_meshcore_protocol_strategy.cpp",
        "modules/core_phone/tests/test_phone_core_smoke.cpp",
        "modules/core_gps/tests/test_location_service.cpp",
        "modules/core_gps/tests/test_time_authority.cpp",
    ]
    for relative in required:
        path = REPO_ROOT / relative
        if not path.exists():
            failures.append(Failure("required-tests", path, "required Phase 4.5 test file missing"))


def check_active_mesh_paths(failures: list[Failure]) -> None:
    bridge = (
        REPO_ROOT
        / "platform"
        / "esp"
        / "arduino_common"
        / "src"
        / "mesh"
        / "esp_meshtastic_adapter_bridge.cpp"
    )
    adapter = (
        REPO_ROOT
        / "platform"
        / "esp"
        / "arduino_common"
        / "src"
        / "chat"
        / "infra"
        / "meshtastic"
        / "mt_adapter.cpp"
    )
    bridge_text = read_text(bridge) if bridge.exists() else ""
    adapter_text = read_text(adapter) if adapter.exists() else ""

    if "session_.sendDirect" not in bridge_text:
        failures.append(
            Failure(
                "mesh-active-send-path",
                bridge,
                "at least one adapter bridge must call MeshSession::sendDirect/session_.sendDirect",
            )
        )

    if "core_bridge_->sendDirect" not in adapter_text:
        failures.append(
            Failure(
                "mesh-active-send-entry",
                adapter,
                "at least one actual adapter entry must route sendDirect through the core bridge",
            )
        )

    if "receive_.onRadioPacket" not in bridge_text:
        failures.append(
            Failure(
                "mesh-active-receive-path",
                bridge,
                "at least one adapter bridge must call ReceivePacketService::onRadioPacket/receive_.onRadioPacket",
            )
        )

    if "core_bridge_->onRadioPacket" not in adapter_text:
        failures.append(
            Failure(
                "mesh-active-receive-entry",
                adapter,
                "at least one actual receive entry must route raw radio packets through the core bridge",
            )
        )


def check_burndown_docs(failures: list[Failure]) -> None:
    known = REPO_ROOT / "tools" / "architecture" / "KNOWN_PRODUCT_ARCHITECTURE_VIOLATIONS.md"
    known_text = read_text(known) if known.exists() else ""
    required_known_tokens = [
        "Remaining Phase 4.5 mesh ownership items",
        "PKI direct send remains legacy-owned",
        "Legacy receive remains authoritative",
    ]
    for token in required_known_tokens:
        if token not in known_text:
            failures.append(Failure("known-violations-burndown", known, f"missing explicit item: {token}"))

    if "nRF52 and Linux active radio paths" not in known_text or "MeshSession bridge" not in known_text:
        failures.append(
            Failure(
                "known-violations-burndown",
                known,
                "missing explicit nRF52/Linux MeshSession bridge remaining item",
            )
        )

    stale_tokens = [
        "At least one actual receive path must route through `ReceivePacketService`",
    ]
    for token in stale_tokens:
        if token in known_text:
            failures.append(Failure("known-violations-burndown", known, f"stale generic item remains: {token}"))

    gps_audit = REPO_ROOT / "docs" / "audits" / "GPS_CONSUMER_BOUNDARY_AUDIT.md"
    gps_text = read_text(gps_audit) if gps_audit.exists() else ""
    for token in ("ILocationSource", "ITimeAuthority", "Phase 5"):
        if token not in gps_text:
            failures.append(Failure("gps-consumer-audit", gps_audit, f"missing {token}"))


def run_checks() -> list[Failure]:
    failures: list[Failure] = []
    check_target_manifests(failures)
    check_core_phone_facade(failures)
    check_core_mesh_platform_pollution(failures)
    check_required_tests(failures)
    check_active_mesh_paths(failures)
    check_burndown_docs(failures)
    return failures


def main() -> int:
    failures = run_checks()
    if failures:
        print("Phase 4.5 readiness check: FAIL")
        print(f"Failures: {len(failures)}")
        print()
        for failure in failures:
            print(f"- [{failure.check}] {rel(failure.path)}")
            print(f"  {failure.detail}")
        return 1

    print("Phase 4.5 readiness check: PASS")
    print("- target manifests name core_phone/core_mesh ownership")
    print("- core_phone facade no longer exposes app service objects")
    print("- core_mesh has no platform SDK/UI/board includes")
    print("- required Phase 4.5 core tests are present")
    print("- at least one ESP Meshtastic send and receive active path enters core_mesh")
    print("- known violations are explicit burn-down items")
    return 0


if __name__ == "__main__":
    sys.exit(main())
