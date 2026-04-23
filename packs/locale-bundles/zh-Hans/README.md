# zh-Hans Pack Bundle

This bundle keeps Simplified Chinese external to the firmware image and splits the CJK
coverage into a smaller core font plus an on-demand extension font.

It contains:

- locale pack: `zh-Hans`
- font pack: `zh-hans-core`
- font pack: `zh-hans-ext`
- IME pack: `zh-hans-pinyin`

The locale manifest points to:

- `ui_font_pack=zh-hans-core`
- `content_font_pack=zh-hans-core`
- `preferred_content_supplement_packs=zh-hans-ext`
- `ime_pack=zh-hans-pinyin`

Bundle-level package metadata lives in:

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `fonts/zh-hans-core/`
  - `manifest.ini`
  - `build.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `fonts/zh-hans-ext/`
  - `manifest.ini`
  - `build.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/zh-Hans/`
  - `manifest.ini`
  - `strings.tsv`
- `ime/zh-hans-pinyin/`
  - `manifest.ini`

## Pack Strategy

- `zh-hans-core` contains locale strings, manifest-facing names, and the highest-ranked
  glyphs from the built-in Pinyin dictionary.
- `zh-hans-ext` contains the remaining glyphs from the Pinyin dictionary that are not
  already present in `zh-hans-core`.
- On Chinese UI locale, the runtime loads `zh-hans-core` immediately and only brings in
  `zh-hans-ext` when displayed content requires additional coverage.
- On non-Chinese UI locales, the runtime can load one or both packs as content
  supplements when Chinese text appears.

## Refresh The Ranked Glyph Sources

```bash
python tools/extract_pinyin_chars.py
```

This writes:

- `tools/pinyin_chars.txt`
- `tools/pinyin_ranked_chars.txt`

## Generate The Font Pack Charset Files

Core pack:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/locale-bundles/zh-Hans --font-pack-id zh-hans-core
```

Extension pack:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/locale-bundles/zh-Hans --font-pack-id zh-hans-ext
```

The `build.ini` files are the source of truth. `scripts/build_pack_repository.py` uses
them to regenerate `charset.txt`, `ranges.txt`, and `font.bin` during repository builds.

## Generate The Font Binaries Manually

Core pack:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/locale-bundles/zh-Hans/fonts/zh-hans-core/charset.txt `
  --output packs/locale-bundles/zh-Hans/fonts/zh-hans-core/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Extension pack:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/locale-bundles/zh-Hans/fonts/zh-hans-ext/charset.txt `
  --output packs/locale-bundles/zh-Hans/fonts/zh-hans-ext/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Notes:

- `estimated_ram_bytes` in each font manifest should track the generated `font.bin` size.
- `zh-hans-core` is intentionally the smaller steady-state font. `zh-hans-ext` is a
  content supplement, not a UI font.
- `font.bin` files are ignored by Git. Regenerate them whenever strings or IME glyph
  data change.

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/zh-Hans-1.0.0.zip`
- `site/data/packs.json`

The zip is the bundle artifact used by the Extensions downloader. Its `payload/`
directory unpacks into `/trailmate/packs/...`.

## Copy To SD Card

Copy the bundle contents so the SD card ends up with:

```text
/trailmate/packs/fonts/zh-hans-core/manifest.ini
/trailmate/packs/fonts/zh-hans-core/ranges.txt
/trailmate/packs/fonts/zh-hans-core/font.bin
/trailmate/packs/fonts/zh-hans-ext/manifest.ini
/trailmate/packs/fonts/zh-hans-ext/ranges.txt
/trailmate/packs/fonts/zh-hans-ext/font.bin
/trailmate/packs/locales/zh-Hans/manifest.ini
/trailmate/packs/locales/zh-Hans/strings.tsv
/trailmate/packs/ime/zh-hans-pinyin/manifest.ini
```

After reboot, `简体中文` appears in Settings. The runtime loads the core pack when the
locale becomes active, and it can add the extension pack later when extra Chinese glyphs
are needed on chat/content surfaces.
