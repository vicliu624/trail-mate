# Europe Cyrillic-Extended Pack Bundle

This bundle keeps Russian support external to the firmware image.

It contains:

- locale pack: `ru`
- font pack: `cyrillic-eu`

The Russian translations were contributed by polarikus.

## Credits

Russian translations were contributed by polarikus, based on the
polarikus/trail-mate Russian localization PR:

https://github.com/polarikus/trail-mate/pull/1

The locale manifest points to:

- `ui_font_pack=cyrillic-eu`
- `content_font_pack=cyrillic-eu`

Bundle-level package metadata lives in:

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `fonts/cyrillic-eu/`
  - `manifest.ini`
  - `build.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/ru/`
  - `manifest.ini`
  - `strings.tsv`

## Generate The Font Pack

1. Refresh the subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/europe-cyrillic-ext --font-pack-id cyrillic-eu
```

2. Generate `font.bin`:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/europe-cyrillic-ext/fonts/cyrillic-eu/charset.txt `
  --output packs/europe-cyrillic-ext/fonts/cyrillic-eu/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Notes:

- This bundle is display-only for now. It does not declare an IME pack yet.
- `build.ini` is the source-of-truth for generating `font.bin` during Pages/package builds.
- `font.bin` is ignored by Git. Regenerate it whenever the subset changes.
- `ranges.txt` and `estimated_ram_bytes` let the runtime decide whether the pack fits the active memory profile.
- The locale string table is intentionally partial. Missing keys fall back to built-in English.

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/europe-cyrillic-ext-1.0.0.zip`
- `site/data/packs.json`

The zip is the bundle artifact for a future Extensions downloader. Its `payload/` directory unpacks into `/trailmate/packs/...`.

## Copy To SD Card

Copy the bundle contents so the SD card ends up with:

```text
/trailmate/packs/fonts/cyrillic-eu/manifest.ini
/trailmate/packs/fonts/cyrillic-eu/ranges.txt
/trailmate/packs/fonts/cyrillic-eu/font.bin
/trailmate/packs/locales/ru/manifest.ini
/trailmate/packs/locales/ru/strings.tsv
```

After reboot, `Русский` appears in Settings. The font is loaded only when Russian becomes the active locale or when Russian content requires it.
