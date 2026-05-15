#!/usr/bin/env python3
"""Report cross-platform product architecture boundary drift.

Phase 1 is intentionally non-blocking. By default this script prints a report
and exits 0 even when it finds violations. Use --strict when a later phase is
ready to make these checks fail CI.
"""

from __future__ import annotations

from dataclasses import dataclass
import argparse
import os
from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parents[2]
KNOWN_VIOLATIONS = (
    "tools/architecture/KNOWN_PRODUCT_ARCHITECTURE_VIOLATIONS.md"
)

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
class Rule:
    name: str
    roots: tuple[str, ...]
    patterns: tuple[re.Pattern[str], ...]
    path_contains: tuple[str, ...] = ()
    path_regex: re.Pattern[str] | None = None
    excludes: tuple[str, ...] = ()


@dataclass(frozen=True)
class Violation:
    rule_name: str
    path: Path
    line_number: int
    line: str


def compile_patterns(patterns: tuple[str, ...]) -> tuple[re.Pattern[str], ...]:
    return tuple(re.compile(pattern, re.IGNORECASE) for pattern in patterns)


RULES = (
    Rule(
        name="core-module-must-not-include-platform-ui-board",
        roots=("modules",),
        path_regex=re.compile(r"^modules/core_[^/\\]+/"),
        patterns=compile_patterns(
            (
                r"#include\s*[<\"]Arduino\.h[>\"]",
                r"#include\s*[<\"]Preferences[>\"]",
                r"#include\s*[<\"]SD\.h[>\"]",
                r"#include\s*[<\"]FS\.h[>\"]",
                r"#include\s*[<\"]freertos/",
                r"#include\s*[<\"]lvgl",
                r"#include\s*[<\"]gtk/",
                r"#include\s*[<\"]platform/",
                r"#include\s*[<\"]board/",
                r"#include\s*[<\"]boards/",
                r"#include\s*[<\"]display/",
                r"#include\s*[<\"]ui/",
            )
        ),
        excludes=(
            "modules/core_chat/generated",
            "modules/core_chat/third_party",
        ),
    ),
    Rule(
        name="board-package-must-not-own-business",
        roots=("boards", "platform/esp/boards", "platform/nrf52/boards", "platform/linux/boards"),
        patterns=compile_patterns(
            (
                r"\bChatService\b",
                r"\bMeshService\b",
                r"\bPhoneCore\b",
                r"\bDirectMessage\b",
                r"\bAppConfig\b",
                r"\blv_obj_t\b",
                r"\bGtkWidget\b",
            )
        ),
    ),
    Rule(
        name="core-mesh-must-not-include-platform-sdk-or-ui",
        roots=("modules/core_mesh",),
        patterns=compile_patterns(
            (
                r"#include\s*[<\"]Arduino\.h[>\"]",
                r"#include\s*[<\"]Preferences[>\"]",
                r"#include\s*[<\"]sqlite3\.h[>\"]",
                r"#include\s*[<\"]freertos/",
                r"#include\s*[<\"]zephyr/",
                r"#include\s*[<\"]nrf_",
                r"#include\s*[<\"]esp_",
                r"#include\s*[<\"]RadioLib",
                r"#include\s*[<\"]lvgl\.h[>\"]",
                r"#include\s*[<\"]gtk/",
                r"#include\s*[<\"]bluefruit",
                r"#include\s*[<\"]Bluefruit",
                r"#include\s*[<\"]NimBLE",
                r"#include\s*[<\"]platform/",
                r"#include\s*[<\"]board/",
                r"#include\s*[<\"]boards/",
            )
        ),
    ),
    Rule(
        name="ui-presentation-must-not-include-toolkit-platform-or-storage",
        roots=("modules/ui_presentation",),
        patterns=compile_patterns(
            (
                r"#include\s*[<\"]lvgl\.h[>\"]",
                r"#include\s*[<\"]gtk/",
                r"#include\s*[<\"]ncurses",
                r"#include\s*[<\"]Arduino\.h[>\"]",
                r"#include\s*[<\"]Preferences[>\"]",
                r"#include\s*[<\"]sqlite3\.h[>\"]",
                r"#include\s*[<\"]RadioLib",
                r"#include\s*[<\"]platform/",
                r"#include\s*[<\"]chat/domain/chat_types\.h[>\"]",
                r"#include\s*[<\"]apps/",
                r"#include\s*[<\"]board/",
                r"#include\s*[<\"]boards/",
            )
        ),
    ),
    Rule(
        name="chat-presentation-adapters-must-stay-pure-mappers",
        roots=("modules/chat_presentation_adapters",),
        patterns=compile_patterns(
            (
                r"\bChatService\b",
                r"\bContactService\b",
                r"\bTeamService\b",
                r"\bMeshSession\b",
                r"\bIMeshAdapter\b",
                r"\bAppContext\b",
                r"#include\s*[<\"]platform/",
                r"#include\s*[<\"]lvgl\.h[>\"]",
                r"#include\s*[<\"]gtk/",
                r"#include\s*[<\"]sqlite3\.h[>\"]",
                r"#include\s*[<\"]Preferences[>\"]",
                r"#include\s*[<\"]RadioLib",
                r"#include\s*[<\"]Arduino\.h[>\"]",
                r"#include\s*[<\"]board/",
                r"#include\s*[<\"]boards/",
            )
        ),
    ),
    Rule(
        name="chat-presentation-source-sink-adapters-must-not-touch-renderer-or-radio",
        roots=("modules/ui_shared", "platform"),
        path_contains=("/presentation_sources/", "\\presentation_sources\\"),
        path_regex=re.compile(r".*chat.*\.(c|cc|cpp|cxx|h|hh|hpp)$", re.IGNORECASE),
        patterns=compile_patterns(
            (
                r"#include\s*[<\"]lvgl\.h[>\"]",
                r"#include\s*[<\"]gtk/",
                r"#include\s*[<\"]sqlite3\.h[>\"]",
                r"#include\s*[<\"]Preferences[>\"]",
                r"#include\s*[<\"]RadioLib",
                r"#include\s*[<\"]Arduino\.h[>\"]",
                r"#include\s*[<\"]board/",
                r"#include\s*[<\"]boards/",
                r"\blv_obj_t\b",
                r"\bGtkWidget\b",
                r"\bIPacketRadio\b",
                r"\bDirectMessageService\b",
                r"\bPeerIdentityService\b",
            )
        ),
    ),
    Rule(
        name="platform-ble-must-not-own-phone-protocol",
        roots=("platform",),
        path_contains=("/ble/", "\\ble\\"),
        patterns=compile_patterns(
            (
                r"\bhandleToRadio(Packet)?\b",
                r"\bToRadio\b",
                r"\bFromRadio\b",
                r"\bFromRadio\s+encode\b",
                r"\bToRadio\s+decode\b",
                r"\bAdminMessage\b",
                r"\bContactService\b",
                r"\bChatService\b",
                r"\bDirectMessage\b",
                r"\bboard\.gps\b",
                r"\bgat562_board\b",
                r"#include\s*[<\"]board/",
                r"#include\s*[<\"]boards/",
                r"\bgetBoard\s*\(",
                r"\bgps::gps_is_",
                r"\bfillMeshtasticGpsFix",
                r"\bloadTimezone",
                r"\bgetTimezone",
                r"\bsetTimezone",
                r"\bctx_\s*\.\s*getConfig\s*\(",
                r"\bctx_\s*\.\s*saveConfig\s*\(",
                r"\bctx_\s*\.\s*applyMeshConfig\s*\(",
                r"\bctx_\s*\.\s*applyPositionConfig\s*\(",
            )
        ),
        excludes=(
            "platform/esp/arduino_common/include/ble/app_phone_facade.h",
            "platform/esp/arduino_common/src/ble/app_phone_facade.cpp",
            "platform/nrf52/arduino_common/include/ble/app_phone_facade.h",
            "platform/nrf52/arduino_common/src/ble/app_phone_facade.cpp",
        ),
    ),
    Rule(
        name="platform-radio-must-not-own-message-or-key-policy",
        roots=("platform", "boards"),
        path_regex=re.compile(r"(^|[/\\]).*radio.*\.(c|cc|cpp|cxx|h|hh|hpp)$", re.IGNORECASE),
        patterns=compile_patterns(
            (
                r"\bDirectMessage\b",
                r"\bPeerKey\b",
                r"\bLocalIdentity\b",
                r"\bPkiFlow\b",
                r"\bAdminMessage\b",
                r"\bContactService\b",
                r"\bChatService\b",
                r"\bPhoneCore\b",
                r"\bToRadio\b",
                r"\bFromRadio\b",
            )
        ),
    ),
    Rule(
        name="platform-mesh-runtime-shell-must-not-own-crypto-or-peer-policy",
        roots=("platform",),
        path_contains=("/mesh/", "\\mesh\\"),
        patterns=compile_patterns(
            (
                r"\bAES\b",
                r"\bCurve25519\b",
                r"\bEd25519\b",
                r"\bpeer trust\b",
                r"\btrust peer\b",
                r"\bsavePeer(Key|PubKey)",
                r"\bloadPeer(Key|PubKey)",
                r"\bgenerateIdentity\b",
                r"\bdecrypt direct packet\b",
                r"\bencrypt direct packet\b",
            )
        ),
    ),
    Rule(
        name="legacy-mesh-adapter-thickness-must-shrink",
        roots=("platform",),
        path_regex=re.compile(r"(^|[/\\])(mt_adapter|meshcore_adapter)\.(c|cc|cpp|cxx|h|hh|hpp)$", re.IGNORECASE),
        patterns=compile_patterns(
            (
                r"\bsavePeer(Key|PubKey)",
                r"\bloadPeer(Key|PubKey)",
                r"\bgenerateIdentity\b",
                r"\binitPkiKeys\b",
                r"\bloadPkiNodeKeys\b",
                r"\bsavePkiNodeKey\b",
                r"\bdecryptPkiPayload\b",
                r"\bencryptPkiPayload\b",
                r"\bderiveIdentitySecret\b",
                r"\btryDecryptPeerPayload\b",
            )
        ),
    ),
    Rule(
        name="platform-ble-must-not-read-gps-driver-directly",
        roots=("platform",),
        path_contains=("/ble/", "\\ble\\"),
        patterns=compile_patterns(
            (
                r"\bboard\.gps\b",
                r"\bGpsService\b",
                r"\bNmeaParser\b",
                r"\bsettimeofday\b",
                r"\bgps_uart\b",
                r"\bgps_get_data\b",
                r"#include\s*[<\"]platform/ui/gps_runtime\.h[>\"]",
                r"#include\s*[<\"]platform/[^>\"]+/gps/",
            )
        ),
        excludes=(
            "platform/esp/arduino_common/include/ble/app_phone_facade.h",
            "platform/esp/arduino_common/src/ble/app_phone_facade.cpp",
            "platform/nrf52/arduino_common/include/ble/app_phone_facade.h",
            "platform/nrf52/arduino_common/src/ble/app_phone_facade.cpp",
        ),
    ),
    Rule(
        name="ui-must-not-read-gps-driver-directly",
        roots=("modules/ui_shared", "legacy/app_implementations/linux_uconsole", "platform/esp/arduino_common", "platform/linux"),
        path_contains=("/ui/", "\\ui\\", "/gtk/", "\\gtk\\"),
        patterns=compile_patterns(
            (
                r"\bGpsService\b",
                r"\bIGnssByteStream\b",
                r"\bNmeaParser\b",
                r"\bgps_driver\b",
                r"\bgps_uart\b",
                r"#include\s*[<\"]platform/[^>\"]+/gps/",
            )
        ),
    ),
    Rule(
        name="mesh-team-must-use-location-source-not-gps-driver",
        roots=("modules/core_team", "modules/core_chat"),
        patterns=compile_patterns(
            (
                r"\bGpsService\b",
                r"\bIGnssByteStream\b",
                r"\bNmeaParser\b",
                r"\bboard\.gps\b",
                r"#include\s*[<\"]platform/ui/gps_runtime\.h[>\"]",
                r"#include\s*[<\"]platform/[^>\"]+/gps/",
            )
        ),
    ),
    Rule(
        name="platform-gps-shell-must-not-own-ui-or-business",
        roots=("platform",),
        path_contains=("/gps/", "\\gps\\"),
        patterns=compile_patterns(
            (
                r"\blv_obj_t\b",
                r"\bGtkWidget\b",
                r"\bChatService\b",
                r"\bContactService\b",
                r"\bDirectMessage\b",
                r"\bMeshtasticPhoneCore\b",
                r"\bMeshCorePhoneCore\b",
            )
        ),
    ),
    Rule(
        name="lvgl-ui-must-not-access-storage-radio-gps-board-directly",
        roots=("modules/ui_shared", "platform/esp/arduino_common", "legacy/app_implementations/esp_pio"),
        path_contains=("/ui/", "\\ui\\"),
        patterns=compile_patterns(
            (
                r"\bsqlite\b",
                r"\bPreferences\b",
                r"\bRadioLib\b",
                r"\bsx1262\b",
                r"\bgps\s*uart\b",
                r"#include\s*[<\"]board/",
                r"#include\s*[<\"]boards/",
            )
        ),
    ),
    Rule(
        name="gtk-ui-must-not-own-protocol-store-radio",
        roots=("legacy/app_implementations/linux_uconsole", "platform/linux"),
        path_contains=("/gtk/", "\\gtk\\"),
        patterns=compile_patterns(
            (
                r"\bprotocol parser\b",
                r"\bstore direct mutation\b",
                r"\bradio direct access\b",
                r"#include\s*[<\"]chat/infra/",
                r"#include\s*[<\"]platform/linux/sx126x_radio\.h[>\"]",
            )
        ),
    ),
    Rule(
        name="ascii-ui-must-not-access-radio-gps-store-directly",
        roots=("apps", "legacy/app_implementations", "platform/linux"),
        path_regex=re.compile(r"(^|[/\\]).*(ascii|terminal|tui).*\.(c|cc|cpp|cxx|h|hh|hpp)$", re.IGNORECASE),
        patterns=compile_patterns(
            (
                r"\bradio\b",
                r"\bgps\b",
                r"\bstore\b",
                r"#include\s*[<\"]platform/linux/sx126x_radio\.h[>\"]",
                r"#include\s*[<\"]platform/ui/gps_runtime\.h[>\"]",
            )
        ),
    ),
    Rule(
        name="target-board-macros-should-stay-in-composition-roots",
        roots=("apps", "modules", "platform", "boards", "src"),
        patterns=compile_patterns(
            (
                r"\bBOARD_[A-Z0-9_]+",
                r"\bTARGET_[A-Z0-9_]+",
                r"\bCONFIG_IDF_TARGET[A-Z0-9_]*",
            )
        ),
        excludes=(
            "boards/",
            "platform/esp/boards/",
            "legacy/app_implementations/esp_idf/CMakeLists.txt",
        ),
    ),
)


def iter_source_files(root: Path):
    for current_root, dir_names, file_names in os.walk(root):
        dir_names[:] = [
            name for name in dir_names if name not in EXCLUDED_DIR_NAMES
        ]
        current_root_path = Path(current_root)
        for file_name in file_names:
            path = current_root_path / file_name
            if path.suffix.lower() in SOURCE_SUFFIXES:
                yield path


def relative_posix(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def is_excluded(relative: str, excludes: tuple[str, ...]) -> bool:
    return any(
        relative == prefix.rstrip("/")
        or relative.startswith(prefix.rstrip("/") + "/")
        for prefix in excludes
    )


def path_matches(rule: Rule, relative: str) -> bool:
    if rule.path_contains and not any(token in relative for token in rule.path_contains):
        return False
    if rule.path_regex is not None and rule.path_regex.search(relative) is None:
        return False
    return True


def collect_violations() -> list[Violation]:
    violations: list[Violation] = []
    seen: set[tuple[str, Path]] = set()

    for rule in RULES:
        for root_name in rule.roots:
            root = REPO_ROOT / root_name
            if not root.exists():
                continue
            for path in iter_source_files(root):
                relative = relative_posix(path)
                if is_excluded(relative, rule.excludes):
                    continue
                if not path_matches(rule, relative):
                    continue
                key = (rule.name, path)
                if key in seen:
                    continue
                seen.add(key)
                lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
                for line_number, line in enumerate(lines, start=1):
                    if any(pattern.search(line) for pattern in rule.patterns):
                        violations.append(
                            Violation(
                                rule_name=rule.name,
                                path=path,
                                line_number=line_number,
                                line=line.strip(),
                            )
                        )
    return violations


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Report Trail Mate product architecture boundary drift."
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit 1 when violations are found.",
    )
    parser.add_argument(
        "--max",
        type=int,
        default=200,
        help="Maximum violation lines to print.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    violations = collect_violations()

    if not violations:
        print("Product architecture boundary report: no violations found.")
        return 0

    print("Product architecture boundary report")
    print("Phase 1 mode: non-blocking report.")
    print(f"Known historical violations log: {KNOWN_VIOLATIONS}")
    print(f"Violations found: {len(violations)}")
    print()

    for violation in violations[: args.max]:
        relative = relative_posix(violation.path)
        print(f"- [{violation.rule_name}] {relative}:{violation.line_number}")
        print(f"  {violation.line}")

    if len(violations) > args.max:
        print()
        print(f"... {len(violations) - args.max} more not shown; increase --max to inspect.")

    if args.strict:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
