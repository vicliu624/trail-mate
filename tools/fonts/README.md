# Font Assets

This directory stores source fonts used to generate Trail Mate font assets.

## GNU Unifont for T-Deck Pro

- Pixel source: `unifont-17.0.05.bdf.gz`
- Source: `https://unifoundry.com/unifont/index.html`
- Release: GNU Unifont 17.0.05
- License: GNU GPL v2 or later with the GNU font embedding exception, or SIL
  Open Font License 1.1.
- T-Deck Pro output: an English-only 16px, 1bpp subset at
  `modules/ui_shared/src/ui/tdeck_pro/font/tdeck_pro_unifont_16.c`.
  Localized glyphs remain downloadable `font.bin` packs. The `zh-Hans` bundle
  supplies matching Pro-only `tdeckpro-zh-hans-core` and
  `tdeckpro-zh-hans-ext` subsets without changing the Noto packs used on other
  displays.

The generator reads the BDF rows directly and writes them as LVGL 1bpp pixels;
it does not invoke a scalable-font renderer, hinting stage, or resampler. Regenerate
the committed source from the repository root with:

```bash
python tools/generate_tdeck_pro_unifont.py
```

The generator rejects any glyph that is not a native 8x16/16x16 Unifont cell.
Its generated source contains exactly ASCII `U+0020` through `U+007E`; Chinese
and every other localized glyph remain in downloadable font packs.

## Noto Sans CJK SC

- File: `NotoSansCJKsc-Regular.otf`
- Source:
  `https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`
- License: SIL Open Font License 1.1 (OFL)

## Noto Naskh Arabic

- File: `NotoNaskhArabic-Regular.otf`
- Source:
  `https://raw.githubusercontent.com/googlefonts/noto-fonts/main/unhinted/otf/NotoNaskhArabic/NotoNaskhArabic-Regular.otf`
- License: SIL Open Font License 1.1 (OFL)

## Noto Emoji Monochrome

- File: `NotoEmoji-Regular.ttf`
- Source:
  `https://raw.githubusercontent.com/zjaco13/Noto-Emoji-Monochrome/main/fonts/NotoEmoji-Regular.ttf`
- Upstream project:
  `https://github.com/googlefonts/noto-emoji`
- License: SIL Open Font License 1.1 (OFL)

## Bitmap Compression Tool

Use the in-repo tool to convert an existing LVGL bitmap font (`lv_font_conv` output)
to LVGL compressed bitmap format (`bitmap_format = 2`):

```bash
./tools/compress_lvgl_bitmap_font.py path/to/generated_lvgl_font.c
```

The tool validates each glyph via encode/decode round-trip before writing.

## Built-In Emoji Catalogue

The reviewed 324-item Emoji catalogue is not an external pack. Its source of
truth is `tools/emoji_candidates_trailmate.json`; the generated C++ table and
16px/2bpp binfont are compiled into the firmware. The catalogue is grouped for
Pager use into Common, Radio, Nav, Weather, Survive, Rescue, Camp, People, and
Animals.

After changing the manifest, regenerate both the charset and the embedded font
from the repository root:

```bash
python tools/generate_builtin_emoji_data.py \
  --manifest tools/emoji_candidates_trailmate.json \
  --font tools/fonts/NotoEmoji-Regular.ttf \
  --charset-output <temporary charset.txt>

python tools/generate_binfont_with_lv_font_conv.py \
  --font tools/fonts/NotoEmoji-Regular.ttf \
  --charset-file <temporary charset.txt> \
  --output <temporary emoji.bin> \
  --size 16 --bpp 2

python tools/generate_builtin_emoji_data.py \
  --manifest tools/emoji_candidates_trailmate.json \
  --font tools/fonts/NotoEmoji-Regular.ttf \
  --binfont <temporary emoji.bin> \
  --output modules/ui_shared/src/ui/widgets/text_candidate_builtin_emoji_data.h
```

The generator rejects duplicate candidates, category count mismatches, an
unexpected total, and codepoints unavailable in `NotoEmoji-Regular.ttf`.

## External Pack Workflow

Simplified Chinese no longer ships as a compiled-in UI font. The repository now
expects Chinese glyph coverage to be generated into external font packs.

The reference bundle lives under:

```text
packs/zh-Hans
```

Typical flow:

1. Refresh the ranked Pinyin glyph sources:

```bash
python tools/extract_pinyin_chars.py
```

2. Generate the core pack subset:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/zh-Hans --font-pack-id zh-hans-core
```

3. Generate the extension pack subset:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/zh-Hans --font-pack-id zh-hans-ext
```

4. Generate T-Deck Pro `font.bin` files through the pack builder using:

- font source for the T-Deck Pro pixel profile:
  `tools/fonts/unifont-17.0.05.bdf.gz`
- glyph subsets:
  - `packs/zh-Hans/fonts/tdeckpro-zh-hans-core/charset.txt`
  - `packs/zh-Hans/fonts/tdeckpro-zh-hans-ext/charset.txt`
- output format: `bin`
- size: `16`
- bpp: `1`

   For `.bdf` input the wrapper feeds the original BDF rows directly to the
   LVGL binary-font writer. It never converts the glyphs through TrueType,
   FreeType, antialiasing, or scaling.

5. Copy the pack directories onto the SD card under `/trailmate/packs/...`.
