# ko Pack Bundle

This bundle keeps Korean external to the firmware image.

It contains:

- locale pack: `ko`
- font pack: `ko-cjk`

The locale manifest points to:

- `ui_font_pack=ko-cjk`
- `content_font_pack=ko-cjk`

Bundle-level package metadata lives in:

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `fonts/ko-cjk/`
  - `manifest.ini`
  - `build.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/ko/`
  - `manifest.ini`
  - `strings.tsv`

## Generate The Font Pack

1. Refresh the subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/ko --font-pack-id ko-cjk
```

2. Generate `font.bin`:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/ko/fonts/ko-cjk/charset.txt `
  --output packs/ko/fonts/ko-cjk/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Notes:

- This bundle is display-only for now. It does not declare an IME pack yet.
- `build.ini` is the source-of-truth for generating `font.bin` during Pages/package builds.
- `font.bin` is ignored by Git. Regenerate it whenever the subset changes.
- The runtime uses `ranges.txt` and `estimated_ram_bytes` to decide when Korean can be activated or loaded as extra content coverage.

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/ko-1.0.0.zip`
- `site/data/packs.json`

The zip is the bundle artifact for a future Extensions downloader. Its `payload/` directory unpacks into `/trailmate/packs/...`.

## Copy To SD Card

Copy the bundle contents so the SD card ends up with:

```text
/trailmate/packs/fonts/ko-cjk/manifest.ini
/trailmate/packs/fonts/ko-cjk/ranges.txt
/trailmate/packs/fonts/ko-cjk/font.bin
/trailmate/packs/locales/ko/manifest.ini
/trailmate/packs/locales/ko/strings.tsv
```

After reboot, `한국어` appears in Settings. The font is loaded only when Korean becomes active or when Korean content needs coverage.
