# ja Pack Bundle

This bundle keeps Japanese external to the firmware image.

It contains:

- locale pack: `ja`
- font pack: `ja-cjk`

The locale manifest points to:

- `ui_font_pack=ja-cjk`
- `content_font_pack=ja-cjk`

Bundle-level package metadata lives in:

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `fonts/ja-cjk/`
  - `manifest.ini`
  - `build.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/ja/`
  - `manifest.ini`
  - `strings.tsv`

## Generate The Font Pack

1. Refresh the subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/ja --font-pack-id ja-cjk
```

2. Generate `font.bin`:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/ja/fonts/ja-cjk/charset.txt `
  --output packs/ja/fonts/ja-cjk/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Notes:

- This bundle is display-only for now. It does not declare an IME pack yet.
- `build.ini` is the source-of-truth for generating `font.bin` during Pages/package builds.
- `font.bin` is ignored by Git. Regenerate it whenever the subset changes.
- The manifest carries `ranges.txt` and `estimated_ram_bytes` so the runtime can treat Japanese as an installable but lazily loaded coverage pack.

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/ja-1.0.0.zip`
- `site/data/packs.json`

The zip is the bundle artifact for a future Extensions downloader. Its `payload/` directory unpacks into `/trailmate/packs/...`.

## Copy To SD Card

Copy the bundle contents so the SD card ends up with:

```text
/trailmate/packs/fonts/ja-cjk/manifest.ini
/trailmate/packs/fonts/ja-cjk/ranges.txt
/trailmate/packs/fonts/ja-cjk/font.bin
/trailmate/packs/locales/ja/manifest.ini
/trailmate/packs/locales/ja/strings.tsv
```

After reboot, `日本語` appears in Settings. The font is loaded only when Japanese becomes the active locale or when Japanese content requires it.
