# zh-Hans Pack Bundle

This bundle keeps Simplified Chinese fully external to the firmware image.

It contains:

- locale pack: `zh-Hans`
- font pack: `zh-hans-cjk`
- IME pack: `zh-hans-pinyin`

The locale manifest points to:

- `ui_font_pack=zh-hans-cjk`
- `content_font_pack=zh-hans-cjk`
- `ime_pack=zh-hans-pinyin`

Bundle-level package metadata lives in:

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `fonts/zh-hans-cjk/`
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

## Generate The Font Pack

1. Refresh the subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/zh-Hans --extra-chars-file tools/pinyin_chars.txt
```

2. Generate `font.bin`:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/zh-Hans/fonts/zh-hans-cjk/charset.txt `
  --output packs/zh-Hans/fonts/zh-hans-cjk/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Notes:

- `charset.txt` and `ranges.txt` are derived from translated strings plus the built-in Pinyin candidate set.
- `estimated_ram_bytes` in the font manifest reflects the runtime LVGL load cost, not the raw file size.
- `build.ini` is the source-of-truth for generating `font.bin` during Pages/package builds.
- `font.bin` is ignored by Git. Regenerate it whenever the strings or IME glyph subset changes.

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/zh-Hans-1.0.0.zip`
- `site/data/packs.json`

The zip is the bundle artifact for a future Extensions downloader. Its `payload/` directory unpacks into `/trailmate/packs/...`.

## Copy To SD Card

Copy the bundle contents so the SD card ends up with:

```text
/trailmate/packs/fonts/zh-hans-cjk/manifest.ini
/trailmate/packs/fonts/zh-hans-cjk/ranges.txt
/trailmate/packs/fonts/zh-hans-cjk/font.bin
/trailmate/packs/locales/zh-Hans/manifest.ini
/trailmate/packs/locales/zh-Hans/strings.tsv
/trailmate/packs/ime/zh-hans-pinyin/manifest.ini
```

After reboot, `简体中文` appears in Settings. The font is only loaded when the locale becomes active or when Chinese content needs to be rendered as a supplement on a different UI locale.
