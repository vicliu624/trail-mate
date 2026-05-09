from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path


PRINTF_RE = re.compile(r"%(?:[-+ #0]*\d*)?(?:\.\d+)?(?:hh|h|ll|l|z|t|j)?[diuoxXfFeEgGaAcspn%]")
ESCAPE_RE = re.compile(r"\\[ntr\\]")

ALLOWED_ENGLISH_EQUIVALENTS = {
    "%d%%",
    "%s  %s",
    "%s -",
    "Bluetooth",
    "CAD",
    "GNSS",
    "GPS",
    "HDOP",
    "ID: !%06lx",
    "ID: !%08lX",
    "ID: !%08lx",
    "ID: -",
    "PKI: -",
    "RSSI %.0f dBm",
    "RSSI -92 dBm",
    "RSSI: %.0f dBm",
    "RSSI: -",
    "RNode Bridge",
    "RX",
    "SF: %u",
    "SNR +%d",
    "SNR +12",
    "SNR %.0f",
    "SNR %.1f",
    "SNR -",
    "SSID",
    "TX",
    "English",
}

REAL_IME_BACKENDS = {
    "builtin-pinyin": {"zh-hans-pinyin"},
}


def parse_key_value_file(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith(";") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key.strip().lower()] = value.strip()
    return data


def parse_tsv(path: Path) -> tuple[dict[str, str], list[str], list[str]]:
    rows: dict[str, str] = {}
    order: list[str] = []
    malformed: list[str] = []
    for line_no, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not raw_line.strip() or raw_line.startswith("#"):
            continue
        if "\t" not in raw_line:
            malformed.append(f"{path}:{line_no}: missing tab")
            continue
        key, value = raw_line.split("\t", 1)
        order.append(key)
        rows[key] = value
    return rows, order, malformed


def placeholders(text: str) -> Counter[str]:
    return Counter(PRINTF_RE.findall(text) + ESCAPE_RE.findall(text))


def discover_locale_files(pack_root: Path) -> list[Path]:
    return sorted(pack_root.glob("*/locales/*/strings.tsv"))


def canonical_key_order(pack_root: Path) -> list[str]:
    preferred = pack_root / "zh-Hans" / "locales" / "zh-Hans" / "strings.tsv"
    if preferred.is_file():
        _rows, order, _malformed = parse_tsv(preferred)
        return order

    order: list[str] = []
    seen: set[str] = set()
    for strings_path in discover_locale_files(pack_root):
        _rows, local_order, _malformed = parse_tsv(strings_path)
        for key in local_order:
            if key not in seen:
                seen.add(key)
                order.append(key)
    return order


def validate_strings(pack_root: Path) -> list[str]:
    errors: list[str] = []
    canonical = canonical_key_order(pack_root)
    canonical_set = set(canonical)
    if not canonical:
        return [f"{pack_root}: no locale strings found"]

    for strings_path in discover_locale_files(pack_root):
        rows, order, malformed = parse_tsv(strings_path)
        errors.extend(malformed)

        duplicates = [key for key, count in Counter(order).items() if count > 1]
        missing = [key for key in canonical if key not in rows]
        extra = [key for key in order if key not in canonical_set]
        empty = [key for key, value in rows.items() if not value.strip()]
        placeholder_bad = [
            key for key, value in rows.items() if placeholders(key) != placeholders(value)
        ]

        manifest = parse_key_value_file(strings_path.parent / "manifest.ini")
        status = manifest.get("translation_status", "release").lower()
        english_fallback = [
            key
            for key, value in rows.items()
            if value.strip() == key.strip()
            and key not in ALLOWED_ENGLISH_EQUIVALENTS
            and re.search(r"[A-Za-z]", key)
        ]

        for key in duplicates:
            errors.append(f"{strings_path}: duplicate key: {key}")
        for key in missing:
            errors.append(f"{strings_path}: missing key: {key}")
        for key in extra:
            errors.append(f"{strings_path}: key not in canonical set: {key}")
        for key in empty:
            errors.append(f"{strings_path}: empty translation: {key}")
        for key in placeholder_bad:
            errors.append(f"{strings_path}: placeholder mismatch: {key}")
        if status == "release":
            for key in english_fallback:
                errors.append(f"{strings_path}: release locale keeps English fallback: {key}")

    return errors


def validate_manifests(pack_root: Path) -> list[str]:
    errors: list[str] = []

    font_ids: set[str] = set()
    for manifest_path in sorted(pack_root.glob("*/fonts/*/manifest.ini")):
        manifest = parse_key_value_file(manifest_path)
        font_ids.add(manifest.get("id", manifest_path.parent.name))

    ime_backends: dict[str, str] = {}
    for manifest_path in sorted(pack_root.glob("*/ime/*/manifest.ini")):
        manifest = parse_key_value_file(manifest_path)
        ime_id = manifest.get("id", manifest_path.parent.name)
        backend = manifest.get("backend", "none")
        if ime_id not in REAL_IME_BACKENDS.get(backend, set()):
            errors.append(f"{manifest_path}: unsupported runtime IME backend/id: {backend}/{ime_id}")
        ime_backends[ime_id] = backend

    for manifest_path in sorted(pack_root.glob("*/locales/*/manifest.ini")):
        manifest = parse_key_value_file(manifest_path)
        locale_id = manifest.get("id", manifest_path.parent.name)
        status = manifest.get("translation_status", "")
        if not status:
            errors.append(f"{manifest_path}: missing translation_status")
        elif status.lower() not in {"release", "review", "draft"}:
            errors.append(f"{manifest_path}: invalid translation_status={status}")

        for field in ("ui_font_pack", "content_font_pack"):
            font_id = manifest.get(field, "")
            if font_id and font_id not in font_ids:
                errors.append(f"{manifest_path}: missing {field} dependency: {font_id}")

        ime_id = manifest.get("ime_pack", "")
        if ime_id:
            backend = ime_backends.get(ime_id)
            if backend is None:
                errors.append(f"{manifest_path}: missing ime_pack dependency: {ime_id}")
            elif ime_id not in REAL_IME_BACKENDS.get(backend, set()):
                errors.append(f"{manifest_path}: locale {locale_id} depends on unsupported IME: {ime_id}")

        if manifest.get("direction", "ltr") == "rtl" and locale_id != "ar":
            errors.append(f"{manifest_path}: unexpected rtl direction for locale {locale_id}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Trail Mate locale pack structure.")
    parser.add_argument("--pack-root", default="packs")
    args = parser.parse_args()

    pack_root = Path(args.pack_root)
    errors = validate_strings(pack_root) + validate_manifests(pack_root)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print("locale pack validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
