# Font Assets

This directory stores source vector fonts used to generate Trail Mate font assets.

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

## External Pack Workflow

Simplified Chinese no longer ships as a compiled-in UI font. The repository now
expects Chinese glyph coverage to be generated into an external font pack.

The reference bundle lives under:

```text
packs/zh-Hans
```

Typical flow:

1. Refresh the subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/zh-Hans --extra-chars-file tools/pinyin_chars.txt
```

2. Generate `packs/zh-Hans/fonts/zh-hans-cjk/font.bin` with `lv_font_conv`
   using:

- font source: `tools/fonts/NotoSansCJKsc-Regular.otf`
- glyph subset: `packs/zh-Hans/fonts/zh-hans-cjk/charset.txt` or `ranges.txt`
- output format: `bin`
- size: `16`
- bpp: `2`

3. Copy the pack directories onto the SD card under `/trailmate/packs/...`.
