# zh-Hant Pack Bundle

This bundle keeps Traditional Chinese external to the firmware image.

It contains:

- locale pack: `zh-Hant`
- font pack: `zh-hant-cjk`

The locale manifest points to:

- `ui_font_pack=zh-hant-cjk`
- `content_font_pack=zh-hant-cjk`

Bundle-level package metadata lives in:

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `fonts/zh-hant-cjk/`
  - `manifest.ini`
  - `build.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/zh-Hant/`
  - `manifest.ini`
  - `strings.tsv`

## Generate The Font Pack

1. Refresh the subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/zh-Hant --font-pack-id zh-hant-cjk
```

2. Generate `font.bin`:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/zh-Hant/fonts/zh-hant-cjk/charset.txt `
  --output packs/zh-Hant/fonts/zh-hant-cjk/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Notes:

- This bundle is display-only for now. It does not declare an IME pack yet.
- `ranges.txt` and `estimated_ram_bytes` let the runtime decide when this pack can be activated or loaded as a supplement.
- `build.ini` is the source-of-truth for generating `font.bin` during Pages/package builds.
- `font.bin` is ignored by Git. Regenerate it whenever the subset changes.

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/zh-Hant-1.0.0.zip`
- `site/data/packs.json`

The zip is the bundle artifact for a future Extensions downloader. Its `payload/` directory unpacks into `/trailmate/packs/...`.

## Copy To SD Card

Copy the bundle contents so the SD card ends up with:

```text
/trailmate/packs/fonts/zh-hant-cjk/manifest.ini
/trailmate/packs/fonts/zh-hant-cjk/ranges.txt
/trailmate/packs/fonts/zh-hant-cjk/font.bin
/trailmate/packs/locales/zh-Hant/manifest.ini
/trailmate/packs/locales/zh-Hant/strings.tsv
```

After reboot, `繁體中文` appears in Settings. The font stays unloaded until the locale is activated or Traditional Chinese content needs coverage.
