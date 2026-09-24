"""Source-key and advertised-glyph coverage, not a native-language review.

Uses the existing locale validator/parser. This test deliberately reads live
Agenda sources rather than a second hand-maintained copy of the key list.
"""

from pathlib import Path
import json
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
from validate_locale_packs import (  # noqa: E402
    discover_locale_files,
    parse_key_value_file,
    parse_tsv,
    range_spans_include,
    validate_strings,
)


def agenda_keys() -> set[str]:
    keys = {"Calendar", "Pick"}  # App catalog and generic Map selection action.
    source = ROOT / "modules/ui_shared/src/ui/screens/agenda"
    for path in source.glob("*.cpp"):
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.lstrip().startswith(("#include", "//")) or "static_assert(" in line:
                continue
            # Dates/times/numeric coordinates are data, not translated prose.
            if "std::snprintf(" in line:
                continue
            for key in re.findall(r'"([^"\r\n]+)"', line):
                if re.search("[A-Za-z]", key):
                    keys.add(key)
    return keys


def advertised_ranges(path: Path) -> list[tuple[int, int]]:
    # Runtime range files may include comment lines (e.g. the Arabic pack).
    text = ",".join(line.split("#", 1)[0] for line in path.read_text(encoding="utf-8").splitlines())
    spans = []
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        bounds = item.split("-", 1)
        spans.append((int(bounds[0], 16), int(bounds[-1], 16)))
    return spans


class AgendaLocaleCoverage(unittest.TestCase):
    def test_keys_and_structure(self):
        self.assertEqual(validate_strings(ROOT / "packs"), [])
        keys = agenda_keys()
        self.assertIn("Choose on map", keys)
        self.assertIn("Another reminder is already snoozed", keys)
        for path in discover_locale_files(ROOT / "packs"):
            with self.subTest(locale=path.parent.name):
                rows, _, _ = parse_tsv(path)
                self.assertEqual(sorted(keys - rows.keys()), [])

    def test_ui_font_ranges_cover_agenda_translations(self):
        fonts = {}
        for path in (ROOT / "packs").glob("*/fonts/*/manifest.ini"):
            manifest = parse_key_value_file(path)
            fonts[manifest.get("id", path.parent.name)] = path.parent
        keys = agenda_keys()
        for path in discover_locale_files(ROOT / "packs"):
            rows, _, _ = parse_tsv(path)
            manifest = parse_key_value_file(path.parent / "manifest.ini")
            for field in ("ui_font_pack", "tdeck_pro_ui_font_pack"):
                font_id = manifest.get(field)
                if not font_id:
                    continue
                with self.subTest(locale=path.parent.name, font=font_id):
                    ranges = advertised_ranges(fonts[font_id] / "ranges.txt")
                    chars = {ch for key in keys for ch in rows.get(key, "") if ord(ch) >= 128}
                    missing = sorted(ch for ch in chars if not range_spans_include(ranges, ch))
                    self.assertEqual(missing, [], "Missing advertised UI glyphs: " + "".join(missing))


def export_binary_font_cases(directory: Path) -> None:
    """Generate artifact-test inputs from live translations, never a second key list."""
    fonts = {}
    for path in (ROOT / "packs").glob("*/fonts/*/manifest.ini"):
        manifest = parse_key_value_file(path)
        fonts[manifest.get("id", path.parent.name)] = path.parent
    characters = {}
    keys = agenda_keys()
    for path in discover_locale_files(ROOT / "packs"):
        rows, _, _ = parse_tsv(path)
        manifest = parse_key_value_file(path.parent / "manifest.ini")
        for field in ("ui_font_pack", "tdeck_pro_ui_font_pack"):
            font_id = manifest.get(field)
            if font_id:
                characters.setdefault(font_id, set()).update(
                    ch for key in keys for ch in rows.get(key, "") if ord(ch) >= 128
                )
    directory.mkdir(parents=True, exist_ok=True)
    cases = []
    for font_id, chars in sorted(characters.items()):
        font_dir = fonts[font_id]
        charset = directory / (font_dir.name + "-agenda.txt")
        charset.write_text("".join(sorted(chars)) + "\n", encoding="utf-8")
        cases.append({"directory": str(font_dir), "charset": str(charset.resolve())})
    (directory / "cases.json").write_text(json.dumps(cases, indent=2), encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--export-font-cases":
        export_binary_font_cases(Path(sys.argv[2]))
    else:
        unittest.main()
