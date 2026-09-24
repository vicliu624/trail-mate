"""Offline checks for the existing pack-builder/generator boundary."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))
import build_pack_repository as builder  # noqa: E402
import generate_binfont_with_lv_font_conv as generator  # noqa: E402


class FontBuildRecipe(unittest.TestCase):
    def test_outline_fallbacks_are_forwarded_in_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            (stage / "build.ini").write_text(
                "font=tools/fonts/NotoNaskhArabic-Regular.otf\n"
                "fallback_font=tools/fonts/NotoSans-Regular.ttf,tools/fonts/NotoSansCJKsc-Regular.otf\n"
                "size=16\nbpp=2\nno_compress=true\n", encoding="utf-8"
            )
            (stage / "charset.txt").write_text("ب…", encoding="utf-8")
            with patch.object(builder.subprocess, "run") as execute:
                builder.build_font_if_missing(stage, ROOT, "node", None)
            command = execute.call_args.args[0]
            fallbacks = [command[i + 1] for i, flag in enumerate(command) if flag == "--fallback-font"]
            self.assertEqual(fallbacks, [
                str(ROOT / "tools/fonts/NotoSans-Regular.ttf"),
                str(ROOT / "tools/fonts/NotoSansCJKsc-Regular.otf"),
            ])
            self.assertIn("--no-compress", command)

    def test_existing_single_font_recipe_stays_single(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            (stage / "build.ini").write_text("font=tools/fonts/NotoSans-Regular.ttf\n", encoding="utf-8")
            (stage / "charset.txt").write_text("ą", encoding="utf-8")
            with patch.object(builder.subprocess, "run") as execute:
                builder.build_font_if_missing(stage, ROOT, "node", None)
            self.assertNotIn("--fallback-font", execute.call_args.args[0])

    def test_native_pixel_font_rejects_outline_fallback(self):
        arguments = [
            "generator", "--font", str(ROOT / "tools/fonts/unifont-17.0.05.bdf.gz"),
            "--fallback-font", str(ROOT / "tools/fonts/NotoSans-Regular.ttf"),
            "--charset-file", str(ROOT / "packs/zh-Hans/fonts/tdeckpro-zh-hans-core/charset.txt"),
            "--output", "unused.bin",
        ]
        with patch.object(sys, "argv", arguments), patch.object(generator, "resolve_executable", return_value="node"):
            with patch.object(generator.subprocess, "run") as execute:
                with self.assertRaisesRegex(ValueError, "native BDF glyphs must not be resampled"):
                    generator.main()
                execute.assert_not_called()


if __name__ == "__main__":
    unittest.main()
