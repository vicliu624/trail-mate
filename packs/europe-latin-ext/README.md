# Europe Latin-Extended Pack Bundle

This bundle provides a shared Latin Extended font pack plus locale packs for:

- `de`
- `es`
- `fr`
- `it`
- `nl`
- `pl`
- `pt-PT`

All of them point to the same external font pack:

- `ui_font_pack=latin-ext-eu`
- `content_font_pack=latin-ext-eu`

That keeps the install footprint small while still giving the runtime one reusable pack for accented interface text and mixed-language content.

Bundle-level package metadata lives in:

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `fonts/latin-ext-eu/`
  - `manifest.ini`
  - `build.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/<locale-id>/`
  - `manifest.ini`
  - `strings.tsv`

## Generate The Shared Font Pack

1. Refresh the subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/europe-latin-ext --font-pack-id latin-ext-eu
```

2. Generate `font.bin`:

```powershell
python tools/generate_binfont_with_lv_font_conv.py `
  --font tools/fonts/NotoSansCJKsc-Regular.otf `
  --charset-file packs/europe-latin-ext/fonts/latin-ext-eu/charset.txt `
  --output packs/europe-latin-ext/fonts/latin-ext-eu/font.bin `
  --size 16 `
  --bpp 2 `
  --node-exe C:\Users\VicLi\AppData\Local\nodejs22\current\node.exe `
  --no-compress
```

Notes:

- `font.bin` is ignored by Git. Keep it locally or regenerate it when the subset changes.
- `build.ini` is the source-of-truth for generating `font.bin` during Pages/package builds.
- `ranges.txt` and `estimated_ram_bytes` are used by the runtime to decide whether the pack fits the current memory profile.
- The locale string tables are intentionally partial. Missing keys fall back to built-in English.

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/europe-latin-ext-1.0.0.zip`
- `site/data/packs.json`

The zip is the bundle artifact for a future Extensions downloader. Its `payload/` directory unpacks into `/trailmate/packs/...`.

## Copy To SD Card

Copy the bundle contents so the SD card ends up with:

```text
/trailmate/packs/fonts/latin-ext-eu/manifest.ini
/trailmate/packs/fonts/latin-ext-eu/ranges.txt
/trailmate/packs/fonts/latin-ext-eu/font.bin
/trailmate/packs/locales/de/manifest.ini
/trailmate/packs/locales/de/strings.tsv
/trailmate/packs/locales/es/manifest.ini
/trailmate/packs/locales/es/strings.tsv
/trailmate/packs/locales/fr/manifest.ini
/trailmate/packs/locales/fr/strings.tsv
/trailmate/packs/locales/it/manifest.ini
/trailmate/packs/locales/it/strings.tsv
/trailmate/packs/locales/nl/manifest.ini
/trailmate/packs/locales/nl/strings.tsv
/trailmate/packs/locales/pl/manifest.ini
/trailmate/packs/locales/pl/strings.tsv
/trailmate/packs/locales/pt-PT/manifest.ini
/trailmate/packs/locales/pt-PT/strings.tsv
```

After reboot, these locales are cataloged immediately, but the external font is only loaded when one of them becomes active or is needed as content coverage.
