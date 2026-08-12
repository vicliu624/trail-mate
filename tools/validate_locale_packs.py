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
    "builtin-keyboard-layout": {"ru-cyrillic-keyboard"},
}

REAL_IME_LAYOUTS = {
    "ru-cyrillic-keyboard": "ru-cyrillic",
}

CJK_REQUIRED_PUNCTUATION_PATH = Path("common/cjk-punctuation.txt")
CJK_REQUIRED_PUNCTUATION_BUILD_REF = "packs/common/cjk-punctuation.txt"
CJK_PRIMARY_FONT_PACKS = (
    ("zh-Hans/fonts/zh-hans-core", False),
    ("zh-Hans/fonts/tdeckpro-zh-hans-core", False),
    ("zh-Hant/fonts/zh-hant-cjk", True),
    ("ja/fonts/ja-cjk", True),
    ("ko/fonts/ko-cjk", True),
)


def parse_key_value_file(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith(";") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key.strip().lower()] = value.strip()
    return data


def split_csv(value: str | None) -> list[str]:
    if not value:
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


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


def read_font_chars(path: Path) -> set[str]:
    return {
        ch
        for ch in path.read_text(encoding="utf-8")
        if not ch.isspace() and ord(ch) >= 0x80
    }


def read_range_spans(path: Path) -> list[tuple[int, int]]:
    spans: list[tuple[int, int]] = []
    for token in path.read_text(encoding="utf-8").replace("\n", ",").split(","):
        item = token.strip()
        if not item:
            continue
        if "-" in item:
            start_text, end_text = item.split("-", 1)
            start = int(start_text, 16)
            end = int(end_text, 16)
        else:
            start = end = int(item, 16)
        spans.append((start, end))
    return spans


def range_spans_include(spans: list[tuple[int, int]], ch: str) -> bool:
    code = ord(ch)
    return any(start <= code <= end for start, end in spans)


def validate_cjk_required_punctuation(pack_root: Path) -> list[str]:
    errors: list[str] = []
    required_path = pack_root / CJK_REQUIRED_PUNCTUATION_PATH
    if not required_path.is_file():
        return [f"{required_path}: missing shared CJK punctuation source"]

    required_chars = read_font_chars(required_path)
    if not required_chars:
        errors.append(f"{required_path}: empty shared CJK punctuation source")

    for font_pack, requires_seed in CJK_PRIMARY_FONT_PACKS:
        font_dir = pack_root / font_pack
        build_path = font_dir / "build.ini"
        charset_path = font_dir / "charset.txt"
        ranges_path = font_dir / "ranges.txt"

        if not build_path.is_file():
            errors.append(f"{build_path}: missing build.ini")
            continue

        build = parse_key_value_file(build_path)
        extra_refs = {
            item.replace("\\", "/") for item in split_csv(build.get("extra_chars_file"))
        }
        if CJK_REQUIRED_PUNCTUATION_BUILD_REF not in extra_refs:
            errors.append(
                f"{build_path}: missing extra_chars_file={CJK_REQUIRED_PUNCTUATION_BUILD_REF}"
            )

        seed_refs = {
            item.replace("\\", "/") for item in split_csv(build.get("seed_charset_file"))
        }
        if requires_seed and "charset.txt" not in seed_refs:
            errors.append(f"{build_path}: missing seed_charset_file=charset.txt")

        if not charset_path.is_file():
            errors.append(f"{charset_path}: missing charset.txt")
            continue
        charset_chars = read_font_chars(charset_path)
        missing_charset = sorted(required_chars - charset_chars, key=ord)
        if missing_charset:
            errors.append(
                f"{charset_path}: missing shared CJK punctuation: {''.join(missing_charset)}"
            )

        if not ranges_path.is_file():
            errors.append(f"{ranges_path}: missing ranges.txt")
            continue
        range_spans = read_range_spans(ranges_path)
        missing_ranges = [
            ch for ch in sorted(required_chars, key=ord) if not range_spans_include(range_spans, ch)
        ]
        if missing_ranges:
            errors.append(
                f"{ranges_path}: missing shared CJK punctuation: {''.join(missing_ranges)}"
            )

    return errors


def validate_manifests(pack_root: Path) -> list[str]:
    errors: list[str] = []

    font_ids: set[str] = set()
    font_manifests: dict[str, tuple[Path, dict[str, str]]] = {}
    for manifest_path in sorted(pack_root.glob("*/fonts/*/manifest.ini")):
        manifest = parse_key_value_file(manifest_path)
        font_id = manifest.get("id", manifest_path.parent.name)
        font_ids.add(font_id)
        font_manifests[font_id] = (manifest_path, manifest)

    for font_id, (manifest_path, manifest) in font_manifests.items():
        targets = split_csv(manifest.get("targets"))
        if not targets:
            continue
        if targets != ["tdeck-pro"]:
            errors.append(f"{manifest_path}: unsupported targets declaration: {manifest.get('targets', '')}")
        if not font_id.startswith("tdeckpro-"):
            errors.append(f"{manifest_path}: target-specific font id must start with tdeckpro-")

        build_path = manifest_path.parent / "build.ini"
        build = parse_key_value_file(build_path) if build_path.is_file() else {}
        expected = {
            "font": "tools/fonts/unifont-17.0.05.bdf.gz",
            "size": "16",
            "bpp": "1",
            "no_compress": "true",
            "no_prefilter": "true",
            "no_kerning": "true",
        }
        for key, expected_value in expected.items():
            if build.get(key) != expected_value:
                errors.append(
                    f"{build_path}: T-Deck Pro pixel font requires {key}={expected_value}"
                )

    ime_backends: dict[str, str] = {}
    for manifest_path in sorted(pack_root.glob("*/ime/*/manifest.ini")):
        manifest = parse_key_value_file(manifest_path)
        ime_id = manifest.get("id", manifest_path.parent.name)
        backend = manifest.get("backend", "none")
        if ime_id not in REAL_IME_BACKENDS.get(backend, set()):
            errors.append(f"{manifest_path}: unsupported runtime IME backend/id: {backend}/{ime_id}")
        expected_layout = REAL_IME_LAYOUTS.get(ime_id, "")
        layout = manifest.get("layout", "")
        if expected_layout and layout != expected_layout:
            errors.append(f"{manifest_path}: expected layout={expected_layout} for IME {ime_id}")
        elif not expected_layout and layout:
            errors.append(f"{manifest_path}: unexpected layout for non-layout IME: {ime_id}")
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

        pro_ui_font_id = manifest.get("tdeck_pro_ui_font_pack", "")
        pro_content_font_id = manifest.get("tdeck_pro_content_font_pack", pro_ui_font_id)
        pro_supplement_ids = split_csv(manifest.get("tdeck_pro_preferred_content_supplement_packs"))
        if pro_ui_font_id:
            for field, font_id in (
                ("tdeck_pro_ui_font_pack", pro_ui_font_id),
                ("tdeck_pro_content_font_pack", pro_content_font_id),
            ):
                font_entry = font_manifests.get(font_id)
                if font_entry is None:
                    errors.append(f"{manifest_path}: missing {field} dependency: {font_id}")
                elif "tdeck-pro" not in split_csv(font_entry[1].get("targets")):
                    errors.append(f"{manifest_path}: {field} must target tdeck-pro: {font_id}")
            for font_id in pro_supplement_ids:
                font_entry = font_manifests.get(font_id)
                if font_entry is None:
                    errors.append(
                        f"{manifest_path}: missing tdeck_pro_preferred_content_supplement_packs dependency: {font_id}"
                    )
                elif "tdeck-pro" not in split_csv(font_entry[1].get("targets")):
                    errors.append(
                        f"{manifest_path}: T-Deck Pro supplement must target tdeck-pro: {font_id}"
                    )

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
    errors = (
        validate_strings(pack_root)
        + validate_manifests(pack_root)
        + validate_cjk_required_punctuation(pack_root)
    )
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print("locale pack validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
