#!/usr/bin/env python3
"""Generate the native 16px, 1bpp English GNU Unifont for T-Deck Pro.

The built-in firmware font intentionally contains ASCII only.  Locale packs
provide Chinese and all other localized glyphs from the same BDF pixel source.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from unifont_bdf import (
    FONT_DESCENT,
    NATIVE_PIXEL_SIZE,
    ensure_native_grid,
    glyph_bitmap_bytes,
    parse_bdf_glyphs,
    read_bdf,
)


ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "tools" / "fonts" / "unifont-17.0.05.bdf.gz"
OUTPUT = ROOT / "modules" / "ui_shared" / "src" / "ui" / "tdeck_pro" / "font" / "tdeck_pro_unifont_16.c"
ASCII_CODEPOINTS = tuple(range(0x20, 0x7F))


def c_bytes(data: bytes) -> list[str]:
    if not data:
        return ["    0x00,"]
    return [
        "    " + ", ".join(f"0x{value:02X}" for value in data[offset : offset + 12]) + ","
        for offset in range(0, len(data), 12)
    ]


def generate_source() -> str:
    glyphs = parse_bdf_glyphs(read_bdf(FONT), set(ASCII_CODEPOINTS))
    ensure_native_grid(glyphs)
    ordered = [glyphs[codepoint] for codepoint in ASCII_CODEPOINTS]

    bitmap_index = 0
    bitmap_lines: list[str] = []
    descriptor_lines = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},"]
    for glyph in ordered:
        pixels = glyph_bitmap_bytes(glyph)
        printable = chr(glyph.codepoint).replace("\\", "\\\\").replace('"', '\\"')
        bitmap_lines.append(f'    /* U+{glyph.codepoint:04X} "{printable}" - native BDF cells */')
        bitmap_lines.extend(c_bytes(pixels))
        bitmap_lines.append("")
        descriptor_lines.append(
            "    {"
            f".bitmap_index = {bitmap_index}, .adv_w = {glyph.advance * 16}, "
            f".box_w = {glyph.width}, .box_h = {glyph.height}, "
            f".ofs_x = {glyph.offset_x}, .ofs_y = {glyph.offset_y}"
            "},"
        )
        bitmap_index += len(pixels)

    return "\n".join(
        [
            "#if defined(ARDUINO_T_DECK_PRO)",
            "",
            "/*",
            " * Generated from GNU Unifont 17.0.05 BDF.",
            " * Native bitmap cells only: 8x16 ASCII; no scaling, hinting, or antialiasing.",
            " * Default firmware deliberately contains U+0020-U+007E only.",
            " */",
            "#include \"lvgl.h\"",
            "",
            "#ifndef TDECK_PRO_UNIFONT_16",
            "#define TDECK_PRO_UNIFONT_16 1",
            "#endif",
            "",
            "#if TDECK_PRO_UNIFONT_16",
            "",
            "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {",
            *bitmap_lines,
            "};",
            "",
            "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {",
            *descriptor_lines,
            "};",
            "",
            "static const lv_font_fmt_txt_cmap_t cmaps[] = {",
            "    {",
            "        .range_start = 32, .range_length = 95, .glyph_id_start = 1,",
            "        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,",
            "        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY",
            "    }",
            "};",
            "",
            "#if LVGL_VERSION_MAJOR == 8",
            "static lv_font_fmt_txt_glyph_cache_t cache;",
            "#endif",
            "",
            "#if LVGL_VERSION_MAJOR >= 8",
            "static const lv_font_fmt_txt_dsc_t font_dsc = {",
            "#else",
            "static lv_font_fmt_txt_dsc_t font_dsc = {",
            "#endif",
            "    .glyph_bitmap = glyph_bitmap,",
            "    .glyph_dsc = glyph_dsc,",
            "    .cmaps = cmaps,",
            "    .kern_dsc = NULL,",
            "    .kern_scale = 0,",
            "    .cmap_num = 1,",
            "    .bpp = 1,",
            "    .kern_classes = 0,",
            "    .bitmap_format = 0,",
            "#if LVGL_VERSION_MAJOR == 8",
            "    .cache = &cache",
            "#endif",
            "};",
            "",
            "#if LVGL_VERSION_MAJOR >= 8",
            "const lv_font_t tdeck_pro_unifont_16 = {",
            "#else",
            "lv_font_t tdeck_pro_unifont_16 = {",
            "#endif",
            "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,",
            "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,",
            f"    .line_height = {NATIVE_PIXEL_SIZE},",
            f"    .base_line = {-FONT_DESCENT},",
            "#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)",
            "    .subpx = LV_FONT_SUBPX_NONE,",
            "#endif",
            "#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8",
            "    .underline_position = -2,",
            "    .underline_thickness = 1,",
            "#endif",
            "    .dsc = &font_dsc,",
            "#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9",
            "    .fallback = NULL,",
            "#endif",
            "    .user_data = NULL,",
            "};",
            "",
            "#endif /* TDECK_PRO_UNIFONT_16 */",
            "#endif /* defined(ARDUINO_T_DECK_PRO) */",
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    if not FONT.is_file():
        raise SystemExit(f"Missing Unifont BDF source: {FONT}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate_source(), encoding="utf-8", newline="\n")
    print(f"Generated {args.output.relative_to(ROOT)} ({len(ASCII_CODEPOINTS)} English glyphs; native BDF cells)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
