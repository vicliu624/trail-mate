# Font Assets

This directory stores source vector fonts used to generate embedded LVGL bitmap fonts.

## Noto Sans CJK SC

- File: `NotoSansCJKsc-Regular.otf`
- Source:
  `https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`
- License: SIL Open Font License 1.1 (OFL)

## Bitmap Compression Tool

Use the in-repo tool to convert an existing LVGL bitmap font (`lv_font_conv` output)
to LVGL compressed bitmap format (`bitmap_format = 2`):

```bash
./tools/compress_lvgl_bitmap_font.py src/ui/assets/fonts/lv_font_noto_cjk_16_2bpp.c
```

The tool validates each glyph via encode/decode round-trip before writing.
