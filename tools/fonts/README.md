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
expects Chinese glyph coverage to be generated into external font packs.

The reference bundle lives under:

```text
packs/locale-bundles/zh-Hans
```

Typical flow:

1. Refresh the ranked Pinyin glyph sources:

```bash
python tools/extract_pinyin_chars.py
```

2. Generate the core pack subset:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/locale-bundles/zh-Hans --font-pack-id zh-hans-core
```

3. Generate the extension pack subset:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/locale-bundles/zh-Hans --font-pack-id zh-hans-ext
```

4. Generate `font.bin` files with `lv_font_conv` using:

- font source: `tools/fonts/NotoSansCJKsc-Regular.otf`
- glyph subsets:
  - `packs/locale-bundles/zh-Hans/fonts/zh-hans-core/charset.txt`
  - `packs/locale-bundles/zh-Hans/fonts/zh-hans-ext/charset.txt`
- output format: `bin`
- size: `16`
- bpp: `2`

5. Copy the pack directories onto the SD card under `/trailmate/packs/...`.
