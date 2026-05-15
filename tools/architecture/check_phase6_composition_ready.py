#!/usr/bin/env python3
from pathlib import Path
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
BASELINE_PATH = ROOT / "tools/architecture/phase6_composition_legacy_baseline.json"


def exists(path: str) -> bool:
    return (ROOT / path).exists()


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def fail(message: str) -> int:
    print(f"[phase6-composition-ready] FAIL: {message}")
    return 1


def note(message: str) -> None:
    print(f"[phase6-composition-ready] baseline: {message}")


def iter_code_files(*roots: Path):
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
                yield path


def check_required_files() -> int:
    required = [
        "docs/specification/PRODUCT_COMPOSITION_ARCHITECTURE_SPEC.md",
        "docs/audits/PRODUCT_COMPOSITION_BOUNDARY_AUDIT.md",
        "docs/audits/APPCONTEXT_LEGACY_BRIDGE_AUDIT.md",
        "docs/audits/ESP_LVGL_COMPOSITION_BRIDGE_AUDIT.md",
        "docs/audits/PHASE6_COMPOSITION_CONFORMANCE_MATRIX.md",
        "docs/audits/PHASE6_COMPOSITION_READINESS_REPORT.md",
        "modules/product_composition/include/product_composition/app_services_bundle.h",
        "modules/product_composition/include/product_composition/presentation_bundle.h",
        "modules/product_composition/include/product_composition/target_app_shell.h",
        "modules/product_composition/include/product_composition/composition_status.h",
        "modules/product_composition/tests/test_presentation_bundle_shape.cpp",
        "legacy/app_implementations/linux_sim/src/linux_sim_composition_root.h",
        "legacy/app_implementations/linux_sim/src/linux_sim_composition_root.cpp",
        "legacy/app_implementations/linux_sim/tests/linux_sim_composition_root_smoke.cpp",
        "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.h",
        "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.cpp",
        "legacy/app_implementations/linux_uconsole/tests/uconsole_composition_root_smoke.cpp",
        "tools/architecture/check_phase6_composition_ready.py",
        "tools/architecture/phase6_composition_legacy_baseline.json",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
    return failures


def check_specification_language() -> int:
    docs = {
        "docs/specification/PRODUCT_COMPOSITION_ARCHITECTURE_SPEC.md": [
            "CompositionRoot wires",
            "Target chooses.",
            "Board describes.",
            "Platform adapts.",
            "Runtime schedules",
            "PresentationModel projects",
            "Renderer draws",
            "A composition root must not",
            "Existing `AppContext` is a legacy service locator",
            "No renderer may create app services",
        ],
        "docs/audits/PRODUCT_COMPOSITION_BOUNDARY_AUDIT.md": [
            "Object Graph Inventory",
            "Linux simulator",
            "uConsole",
            "ESP LVGL",
            "LegacyAppContextBridge",
        ],
        "docs/audits/APPCONTEXT_LEGACY_BRIDGE_AUDIT.md": [
            "legacy service locator",
            "Forbidden New Use",
            "New code must not",
            "AppServicesBundle",
            "PresentationBundle",
        ],
        "docs/audits/ESP_LVGL_COMPOSITION_BRIDGE_AUDIT.md": [
            "does not split the ESP boot chain yet",
            "LegacyAppContextBridge",
            "New ESP code must not",
            "Phase 6 Acceptance",
        ],
        "docs/audits/PHASE6_COMPOSITION_CONFORMANCE_MATRIX.md": [
            "Phase 6-ready does not mean all global state",
            "ready-with-legacy",
            "No area may remain blocked",
        ],
        "docs/audits/PHASE6_COMPOSITION_READINESS_REPORT.md": [
            "Phase 6 is ready to close",
            "This decision does not mean every target startup path is fully clean",
            "CompositionRoot wires",
            "Phase 6 composition checker exists and passes",
            "`AppContext` is compatibility-only",
        ],
    }

    failures = 0
    for path, tokens in docs.items():
        if not exists(path):
            continue
        text = read_text(path)
        for token in tokens:
            if token not in text:
                failures += fail(f"{path} missing composition token: {token}")
    return failures


def check_product_composition_contracts_are_pure() -> int:
    forbidden_tokens = [
        "lvgl.h",
        "gtk",
        "platform/",
        "platform\\",
        "app/app_facade",
        "app_facade_access",
        "app_context",
        "AppContext::",
        "Arduino.h",
        "Preferences",
        "RadioLib",
        "sqlite3",
        "MapTileCache",
    ]

    failures = 0
    root = ROOT / "modules/product_composition/include/product_composition"
    for path in iter_code_files(root):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden contract token {token}"
                )
    return failures


def check_bundles_are_explicit_not_locators() -> int:
    failures = 0

    app_bundle = "modules/product_composition/include/product_composition/app_services_bundle.h"
    if exists(app_bundle):
        text = strip_cpp_comments(read_text(app_bundle))
        for token in ["ChatService", "ContactService", "chat =", "contacts ="]:
            if token not in text:
                failures += fail(f"AppServicesBundle missing explicit field token: {token}")
        for token in ["get<", "template", "unordered_map", "std::map", "void*"]:
            if token in text:
                failures += fail(f"AppServicesBundle looks like a service locator: {token}")

    presentation_bundle = (
        "modules/product_composition/include/product_composition/presentation_bundle.h"
    )
    if exists(presentation_bundle):
        text = strip_cpp_comments(read_text(presentation_bundle))
        for token in ["PresentationWorkspace", "workspace", "hasInteractivePresentation"]:
            if token not in text:
                failures += fail(f"PresentationBundle missing graph token: {token}")
        for token in [
            "new ",
            "make_unique",
            "Legacy",
            "Source",
            "Sink",
            "ChatService",
            "MapTileCache",
        ]:
            if token in text:
                failures += fail(f"PresentationBundle constructs or imports dependencies: {token}")

    shell_contract = "modules/product_composition/include/product_composition/target_app_shell.h"
    if exists(shell_contract):
        text = strip_cpp_comments(read_text(shell_contract))
        for token in ["initialize()", "tick()", "shutdown()"]:
            if token not in text:
                failures += fail(f"ITargetAppShell missing lifecycle token: {token}")

    return failures


def check_ui_presentation_does_not_import_composition() -> int:
    failures = 0
    roots = [
        ROOT / "modules/ui_presentation/include",
        ROOT / "modules/ui_presentation/src",
    ]
    forbidden_tokens = [
        "product_composition/",
        "AppServicesBundle",
        "PresentationBundle",
        "CompositionRoot",
        "ITargetAppShell",
    ]
    for path in iter_code_files(*roots):
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for token in forbidden_tokens:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} imports product composition token {token}"
                )
    return failures


def check_linux_sim_root_shape() -> int:
    failures = 0
    header = "legacy/app_implementations/linux_sim/src/linux_sim_composition_root.h"
    source = "legacy/app_implementations/linux_sim/src/linux_sim_composition_root.cpp"
    smoke = "legacy/app_implementations/linux_sim/tests/linux_sim_composition_root_smoke.cpp"

    if exists(header):
        text = read_text(header)
        for token in [
            "LinuxSimCompositionRoot",
            "PresentationBundle",
            "DeviceStatusModel",
            "GpsStatusModel",
            "MeshStatusModel",
            "SettingsModel",
            "ChatWorkspaceModel",
            "MapWorkspaceModel",
        ]:
            if token not in text:
                failures += fail(f"LinuxSimCompositionRoot missing token: {token}")
    if exists(source):
        text = read_text(source)
        for token in [
            "workspace.device",
            "workspace.gps",
            "workspace.mesh",
            "workspace.settings",
            "workspace.chat",
            "workspace.team_chat",
            "workspace.map",
        ]:
            if token not in text:
                failures += fail(f"LinuxSimCompositionRoot does not wire: {token}")
        for token in ["AppContext", "appFacade", "platform/ui/gps_runtime.h", "lvgl.h", "gtk"]:
            if token in text:
                failures += fail(f"LinuxSimCompositionRoot contains forbidden token: {token}")
    if exists(smoke):
        text = read_text(smoke)
        for token in [
            "root.initialize()",
            "root.presentation()",
            "hasCoreStatusModels",
            "hasInteractivePresentation",
        ]:
            if token not in text:
                failures += fail(f"linux_sim composition smoke missing token: {token}")
    return failures


def check_uconsole_root_shape() -> int:
    failures = 0
    header = "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.h"
    source = "legacy/app_implementations/linux_uconsole/src/uconsole_composition_root.cpp"
    smoke = "legacy/app_implementations/linux_uconsole/tests/uconsole_composition_root_smoke.cpp"

    if exists(header):
        text = read_text(header)
        for token in [
            "UConsoleCompositionRoot",
            "LinuxAppServices",
            "UConsoleChatWorkspaceModel",
            "UConsoleMapWorkspaceModel",
            "AppServicesBundle",
            "PresentationBundle",
        ]:
            if token not in text:
                failures += fail(f"UConsoleCompositionRoot header missing token: {token}")
    if exists(source):
        text = read_text(source)
        for token in [
            "services_.initialize()",
            "app_services_.chat",
            "app_services_.contacts",
            "ChatPresentationSource",
            "LegacyChatActionSink",
            "ChatWorkspaceModel",
            "presentation_.workspace.chat",
            "presentation_.workspace.map",
            "map_model_.presentationModel()",
        ]:
            if token not in text:
                failures += fail(f"UConsoleCompositionRoot source missing token: {token}")
        for token in ["gtk", "lvgl.h", "MapTileCache", "platform/ui/gps_runtime.h"]:
            if token in text:
                failures += fail(f"UConsoleCompositionRoot source contains renderer/runtime token: {token}")
    if exists(smoke):
        text = read_text(smoke)
        for token in [
            "root.initialize()",
            "root.presentation()",
            "hasChatServices",
            "hasInteractivePresentation",
            "workspace.hasChat()",
            "workspace.hasMap()",
        ]:
            if token not in text:
                failures += fail(f"uConsole composition smoke missing token: {token}")
    return failures


def check_baseline() -> int:
    if not BASELINE_PATH.exists():
        return fail("missing phase6 composition legacy baseline")

    try:
        data = json.loads(BASELINE_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return fail(f"invalid phase6 baseline JSON: {exc}")

    surfaces = data.get("known_legacy_composition_surfaces", [])
    failures = 0
    if not surfaces:
        failures += fail("phase6 baseline has no known legacy composition surfaces")

    required_ids = {
        "esp-pio-app-context",
        "esp-idf-app-facade-runtime",
        "global-app-facade-access",
        "chat-page-runtime-presentation-bridge",
        "gps-page-runtime-presentation-bridge",
        "gtk-uconsole-app-state-legacy",
    }
    seen = {item.get("id") for item in surfaces}
    for required in sorted(required_ids):
        if required not in seen:
            failures += fail(f"phase6 baseline missing legacy surface id: {required}")

    for item in surfaces:
        path = item.get("path", "")
        if not path:
            failures += fail(f"baseline item {item.get('id', '<unknown>')} missing path")
            continue
        if not exists(path):
            failures += fail(f"baseline path does not exist: {path}")
        if not item.get("reason"):
            failures += fail(f"baseline item {item.get('id', path)} missing reason")

    if failures == 0:
        note(f"{len(surfaces)} known legacy composition surface(s)")
    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_specification_language()
    failures += check_product_composition_contracts_are_pure()
    failures += check_bundles_are_explicit_not_locators()
    failures += check_ui_presentation_does_not_import_composition()
    failures += check_linux_sim_root_shape()
    failures += check_uconsole_root_shape()
    failures += check_baseline()

    if failures == 0:
        print("[phase6-composition-ready] OK")
        return 0

    print(f"[phase6-composition-ready] {failures} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
