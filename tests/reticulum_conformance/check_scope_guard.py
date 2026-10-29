#!/usr/bin/env python3
"""Guard the Reticulum conformance boundary.

This script is intentionally small and dev-only. It does not validate Reticulum
wire behavior. It prevents the first conformance slice from silently turning
`microReticulum` into a production dependency and checks that the deviation
ledger exists before deeper protocol work starts.
"""

from pathlib import Path
import json
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]

REQUIRED_FILES = [
    Path("docs/RETICULUM_CONFORMANCE_BASELINE.md"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/reticulum/reticulum_adapter.h"
    ),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/reticulum/reticulum_interfaces.h"
    ),
    Path("platform/esp/arduino_common/src/chat/infra/reticulum/reticulum_interfaces.cpp"),
    Path("tests/reticulum_conformance/README.md"),
    Path("tests/reticulum_conformance/deviations.md"),
    Path("tests/reticulum_conformance/fixtures/README.md"),
    Path("tests/reticulum_conformance/fixtures/microreticulum_announce_vectors.json"),
    Path("tests/reticulum_conformance/fixtures/trailmate_supported_subset_vectors.json"),
    Path("tests/reticulum_conformance/test_reticulum_announce_vectors.cpp"),
    Path("tests/reticulum_conformance/test_reticulum_supported_subset_vectors.cpp"),
    Path("tests/reticulum_conformance/test_reticulum_runtime_state_contract.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_destination_registry.h"
    ),
    Path("modules/core_chat/src/infra/lxmf/lxmf_destination_registry.cpp"),
    Path("modules/core_chat/src/infra/lxmf/lxmf_peer_directory.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_path_manager.h"
    ),
    Path("modules/core_chat/src/infra/lxmf/lxmf_path_manager.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_link_manager.h"
    ),
    Path("modules/core_chat/src/infra/lxmf/lxmf_link_manager.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_packet_router.h"
    ),
    Path("modules/core_chat/src/infra/lxmf/lxmf_packet_router.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_announce_ingestor.h"
    ),
    Path("platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_announce_ingestor.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_ping_service.h"
    ),
    Path("platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_ping_service.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_network_page_client.h"
    ),
    Path("platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_network_page_client.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_propagation_client.h"
    ),
    Path("platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_propagation_client.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_lxst_telephony_client.h"
    ),
    Path("platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_lxst_telephony_client.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_transport_runtime.h"
    ),
    Path("modules/core_chat/src/infra/lxmf/lxmf_transport_runtime.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_link_runtime.h"
    ),
    Path("modules/core_chat/src/infra/lxmf/lxmf_link_runtime.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_resource_runtime.h"
    ),
    Path("modules/core_chat/src/infra/lxmf/lxmf_resource_runtime.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_propagation_runtime.h"
    ),
    Path("platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_propagation_runtime.cpp"),
    Path("modules/core_chat/include/chat/infra/lxmf/lxmf_propagation_runtime.h"),
    Path("modules/core_chat/src/infra/lxmf/lxmf_propagation_runtime.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_propagation_service_runtime.h"
    ),
    Path("platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_propagation_service_runtime.cpp"),
    Path("modules/core_chat/include/chat/infra/lxmf/lxmf_propagation_service_runtime.h"),
    Path("modules/core_chat/src/infra/lxmf/lxmf_propagation_service_runtime.cpp"),
    Path("modules/core_chat/include/chat/infra/lxmf/lxmf_propagation_sync.h"),
    Path("modules/core_chat/include/chat/infra/lxmf/lxmf_propagation_stamp.h"),
    Path("modules/core_chat/include/chat/infra/lxmf/lxmf_delivery_planner.h"),
    Path("modules/core_chat/src/infra/lxmf/lxmf_delivery_planner.cpp"),
    Path("modules/core_chat/src/infra/lxmf/lxmf_propagation_sync.cpp"),
    Path(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/"
        "chat/infra/lxmf/lxmf_delivery_runtime.h"
    ),
    Path("modules/core_chat/src/infra/lxmf/lxmf_delivery_runtime.cpp"),
]

FIXTURE_FILES = [
    Path("tests/reticulum_conformance/fixtures/microreticulum_announce_vectors.json"),
    Path("tests/reticulum_conformance/fixtures/trailmate_supported_subset_vectors.json"),
]

REQUIRED_DEVIATIONS = [
    "RCNF-001",
    "RCNF-002",
    "RCNF-003",
    "RCNF-004",
    "RCNF-005",
    "RCNF-006",
    "RCNF-007",
    "RCNF-008",
]

ALLOWED_MICRORETICULUM_PREFIXES = (
    "docs/",
    "tests/reticulum_conformance/",
)

SKIP_DIRS = {
    ".git",
    ".gitnexus",
    ".pio",
    ".tmp",
    "build",
    "builds",
    "cmake-build-debug",
    "cmake-build-release",
}

TEXT_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hpp",
    ".hh",
    ".md",
    ".txt",
    ".cmake",
    ".json",
    ".ini",
    ".yml",
    ".yaml",
    ".py",
    ".toml",
}

ALLOWED_CANONICALITY = {
    "canonical",
    "secondary",
    "exploratory",
}

ALLOWED_FIXTURE_TYPES = {
    "hash-vector",
    "destination-vector",
    "packet-vector",
    "lxmf-vector",
    "link-service-vector",
    "resource-vector",
    "propagation-vector",
    "announce-vector",
    "proof-vector",
    "negative-vector",
    "trace-vector",
}

REQUIRED_METADATA_FIELDS = [
    "reference_name",
    "reference_repo",
    "reference_commit",
    "reference_file",
    "generated_at",
    "generation_command",
    "canonicality",
    "inputs",
    "expected_fields",
    "notes",
]

PROTOCOL_FACTORY = Path(
    "platform/esp/arduino_common/src/chat/infra/protocol_factory.cpp"
)

ESP_APP_CONFIG_STORE = Path(
    "platform/esp/arduino_common/src/app_config_store.cpp"
)

PRODUCT_RETICULUM_CONFIG_FILES = [
    Path("apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp"),
    Path("apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_logic.cpp"),
    Path("modules/ui_shared/src/ui/presentation_sources/runtime_device_status_source.cpp"),
    Path("modules/ui_shared/src/ui/screens/contacts/contacts_page_components.cpp"),
    Path("modules/ui_shared/src/ui/screens/contacts/contacts_page_runtime.cpp"),
    Path("modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp"),
    Path("platform/esp/arduino_common/include/app/app_context.h"),
    Path("platform/esp/arduino_common/src/app_context.cpp"),
    Path("platform/esp/arduino_common/src/rnode_kiss/rnode_kiss_service.cpp"),
    Path("platform/linux/common/src/app/linux_app_services.cpp"),
]

RETICULUM_ADAPTER_HEADER = Path(
    "platform/esp/arduino_common/include/platform/esp/arduino_common/"
    "chat/infra/reticulum/reticulum_adapter.h"
)

LXMF_ADAPTER_HEADER = Path(
    "platform/esp/arduino_common/include/platform/esp/arduino_common/"
    "chat/infra/lxmf/lxmf_adapter.h"
)

FORBIDDEN_FACTORY_TERMS = [
    "chat/infra/lxmf/lxmf_adapter.h",
    "chat/infra/rnode/rnode_adapter.h",
    "chat::lxmf::LxmfAdapter",
    "chat::rnode::RNodeAdapter",
]


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def iter_text_files():
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        raise SystemExit(result.returncode)

    for line in result.stdout.splitlines():
        path = ROOT / line
        if not path.is_file():
            continue
        if any(part in SKIP_DIRS for part in path.relative_to(ROOT).parts):
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        yield path


def check_required_files() -> list[str]:
    errors: list[str] = []
    for relative in REQUIRED_FILES:
        if not (ROOT / relative).is_file():
            errors.append(f"missing required conformance file: {relative.as_posix()}")
    return errors


def check_deviation_ids() -> list[str]:
    errors: list[str] = []
    ledger = ROOT / "tests/reticulum_conformance/deviations.md"
    if not ledger.is_file():
        return ["missing deviation ledger"]
    text = ledger.read_text(encoding="utf-8")
    for deviation_id in REQUIRED_DEVIATIONS:
        if deviation_id not in text:
            errors.append(f"missing deviation id: {deviation_id}")
    return errors


def check_microreticulum_boundary() -> list[str]:
    errors: list[str] = []
    for path in iter_text_files():
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "microReticulum" not in text:
            continue
        relative = rel(path)
        if not relative.startswith(ALLOWED_MICRORETICULUM_PREFIXES):
            errors.append(
                "microReticulum reference outside conformance/docs boundary: "
                f"{relative}"
            )
    return errors


def check_product_protocol_boundary() -> list[str]:
    errors: list[str] = []
    factory = ROOT / PROTOCOL_FACTORY
    if not factory.is_file():
        return [f"missing protocol factory: {PROTOCOL_FACTORY.as_posix()}"]

    factory_text = factory.read_text(encoding="utf-8")
    if "chat/infra/reticulum/reticulum_adapter.h" not in factory_text:
        errors.append(
            "ESP protocol factory must enter Reticulum through "
            "reticulum_adapter.h"
        )

    for term in FORBIDDEN_FACTORY_TERMS:
        if term in factory_text:
            errors.append(
                "ESP protocol factory must not expose internal implementation "
                f"term: {term}"
            )

    adapter = ROOT / RETICULUM_ADAPTER_HEADER
    if adapter.is_file():
        adapter_text = adapter.read_text(encoding="utf-8")
        if "class ReticulumAdapter final : public IMeshAdapter" not in adapter_text:
            errors.append(
                "Reticulum adapter boundary must be a product-level adapter "
                "class, not a type alias"
            )
        if "using ReticulumAdapter =" in adapter_text:
            errors.append(
                "Reticulum adapter boundary must not collapse back to an "
                "internal implementation alias"
            )

    return errors


def check_reticulum_group_source_boundary() -> list[str]:
    errors: list[str] = []
    store = ROOT / ESP_APP_CONFIG_STORE
    if not store.is_file():
        return [f"missing ESP app config store: {ESP_APP_CONFIG_STORE.as_posix()}"]

    text = store.read_text(encoding="utf-8")
    if "reticulum_groups[" in text:
        errors.append(
            "Reticulum groups must not be persisted in ESP NVS app config; "
            "the working configuration document is the source of truth"
        )
    if "reticulum_group_key(" in text:
        errors.append(
            "Reticulum group NVS key helpers must not be reintroduced; "
            "only legacy rtg*_ keys may be removed during save"
        )
    return errors


def check_product_reticulum_config_accessor_boundary() -> list[str]:
    errors: list[str] = []
    for relative in PRODUCT_RETICULUM_CONFIG_FILES:
        path = ROOT / relative
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        if "rnode_config" in text:
            errors.append(
                "Product Reticulum paths must use AppConfig::reticulumConfig() "
                f"instead of direct rnode_config storage access: {relative.as_posix()}"
            )

    return errors


def check_reticulum_interface_boundary() -> list[str]:
    errors: list[str] = []
    header = ROOT / LXMF_ADAPTER_HEADER
    if not header.is_file():
        return [f"missing LXMF adapter header: {LXMF_ADAPTER_HEADER.as_posix()}"]

    text = header.read_text(encoding="utf-8")
    if "ReticulumInterfaceSet interfaces_" not in text:
        errors.append(
            "LXMF runtime must depend on ReticulumInterfaceSet, not on a single "
            "raw carrier"
        )
    if "RNodeAdapter raw_" in text:
        errors.append(
            "LXMF runtime must not embed RNodeAdapter directly; use the "
            "Reticulum interface set boundary"
        )
    return errors


def check_lxmf_adapter_runtime_owner_boundary() -> list[str]:
    errors: list[str] = []
    header = ROOT / LXMF_ADAPTER_HEADER
    if not header.is_file():
        return [f"missing LXMF adapter header: {LXMF_ADAPTER_HEADER.as_posix()}"]

    text = header.read_text(encoding="utf-8")
    required_owner_fields = [
        "runtime::DestinationRegistry destination_registry_",
        "runtime::PathManager path_manager_",
        "runtime::LinkManager link_manager_",
        "runtime::AnnounceIngestor announce_ingestor_",
        "runtime::ReticulumPacketRouter packet_router_",
        "runtime::PingService ping_service_",
        "runtime::NetworkPageClient network_page_client_",
        "runtime::PropagationClient propagation_client_",
        "runtime::LxstTelephonyClient lxst_telephony_client_",
    ]
    for field in required_owner_fields:
        if field not in text:
            errors.append(f"LXMF adapter missing runtime owner field: {field}")

    forbidden_adapter_state = [
        "std::vector<PeerInfo> peers_",
        "TransportRuntime transport_",
        "LinkRuntime links_",
        "std::vector<PendingPingRequest> pending_ping_requests_",
        "std::vector<PendingNomadPageRequest> pending_nomad_page_requests_",
        "PropagationRuntime propagation_",
        "PropagationStampRuntime propagation_stamp_",
        "PeerInfo propagation_peer_scratch_",
        "call_wire_scratch_",
    ]
    for term in forbidden_adapter_state:
        if term in text:
            errors.append(
                "LXMF adapter must not directly own extracted runtime state: "
                f"{term}"
            )

    return errors


def check_fixture_metadata() -> list[str]:
    errors: list[str] = []
    for relative in FIXTURE_FILES:
        path = ROOT / relative
        if not path.is_file():
            continue
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            errors.append(f"{relative.as_posix()}: invalid JSON: {exc}")
            continue

        metadata = document.get("metadata")
        if not isinstance(metadata, dict):
            errors.append(f"{relative.as_posix()}: missing metadata object")
            continue

        for field in REQUIRED_METADATA_FIELDS:
            if field not in metadata:
                errors.append(f"{relative.as_posix()}: missing metadata.{field}")

        canonicality = metadata.get("canonicality")
        if canonicality not in ALLOWED_CANONICALITY:
            errors.append(
                f"{relative.as_posix()}: unsupported canonicality {canonicality!r}"
            )

        vectors = document.get("vectors")
        if not isinstance(vectors, list) or not vectors:
            errors.append(f"{relative.as_posix()}: vectors must be a non-empty list")
            continue

        for index, vector in enumerate(vectors):
            if not isinstance(vector, dict):
                errors.append(f"{relative.as_posix()}: vector {index} must be an object")
                continue
            name = vector.get("name", f"#{index}")
            fixture_type = vector.get("fixture_type")
            if fixture_type not in ALLOWED_FIXTURE_TYPES:
                errors.append(
                    f"{relative.as_posix()}: vector {name} has unsupported type "
                    f"{fixture_type!r}"
                )
            raw_hex = vector.get("raw_hex")
            if not isinstance(raw_hex, str) or len(raw_hex) == 0:
                errors.append(f"{relative.as_posix()}: vector {name} missing raw_hex")
            elif len(raw_hex) % 2 != 0:
                errors.append(f"{relative.as_posix()}: vector {name} raw_hex is odd")
            else:
                try:
                    bytes.fromhex(raw_hex)
                except ValueError:
                    errors.append(
                        f"{relative.as_posix()}: vector {name} raw_hex is not hex"
                    )

            expected = vector.get("expected")
            if not isinstance(expected, dict):
                errors.append(f"{relative.as_posix()}: vector {name} missing expected")
            elif fixture_type == "announce-vector":
                for field in [
                    "validate_announce",
                    "signature_valid",
                    "destination_hash_valid",
                ]:
                    if not isinstance(expected.get(field), bool):
                        errors.append(
                            f"{relative.as_posix()}: vector {name} missing "
                            f"expected.{field}"
                        )

    return errors


def main() -> int:
    errors = []
    errors.extend(check_required_files())
    errors.extend(check_deviation_ids())
    errors.extend(check_fixture_metadata())
    errors.extend(check_microreticulum_boundary())
    errors.extend(check_product_protocol_boundary())
    errors.extend(check_reticulum_group_source_boundary())
    errors.extend(check_product_reticulum_config_accessor_boundary())
    errors.extend(check_reticulum_interface_boundary())
    errors.extend(check_lxmf_adapter_runtime_owner_boundary())

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print("Reticulum conformance scope guard passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
