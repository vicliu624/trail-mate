# Europe Latin-Extended Pack Bundle

This bundle provides a first-wave set of European locale packs while reusing one shared external font pack for Latin Extended glyph coverage.

## Bundle Layout

- `fonts/latin-ext-eu/`
  - `manifest.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/fr/`
  - `manifest.ini`
  - `strings.tsv`
- `locales/de/`
  - `manifest.ini`
  - `strings.tsv`
- `locales/es/`
  - `manifest.ini`
  - `strings.tsv`
- `locales/it/`
  - `manifest.ini`
  - `strings.tsv`
- `locales/pt-PT/`
  - `manifest.ini`
  - `strings.tsv`
- `locales/nl/`
  - `manifest.ini`
  - `strings.tsv`
- `locales/pl/`
  - `manifest.ini`
  - `strings.tsv`

## Generate The Shared Font Pack

1. Refresh the glyph subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/europe-latin-ext --font-pack-id latin-ext-eu
```

2. Generate `font.bin` with the helper wrapper around `lv_font_conv`.

PowerShell example:

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

- All locale packs in this bundle depend on the same `latin-ext-eu` font pack.
- The locale string tables are intentionally partial. Any untranslated key falls back to the built-in English source string.
- `font.bin` is intentionally not committed. Regenerate it whenever the localized strings change.

## Copy To SD Card

Copy the contents of this bundle onto the SD card so the final layout becomes:

```text
/trailmate/packs/fonts/latin-ext-eu/manifest.ini
/trailmate/packs/fonts/latin-ext-eu/font.bin
/trailmate/packs/locales/fr/manifest.ini
/trailmate/packs/locales/fr/strings.tsv
/trailmate/packs/locales/de/manifest.ini
/trailmate/packs/locales/de/strings.tsv
/trailmate/packs/locales/es/manifest.ini
/trailmate/packs/locales/es/strings.tsv
/trailmate/packs/locales/it/manifest.ini
/trailmate/packs/locales/it/strings.tsv
/trailmate/packs/locales/pt-PT/manifest.ini
/trailmate/packs/locales/pt-PT/strings.tsv
/trailmate/packs/locales/nl/manifest.ini
/trailmate/packs/locales/nl/strings.tsv
/trailmate/packs/locales/pl/manifest.ini
/trailmate/packs/locales/pl/strings.tsv
```

Then reboot the device. The Settings page will show the native locale names once the packs are discovered.
