# zh-Hans Pack Bundle

This bundle keeps Simplified Chinese outside the firmware image.

## Bundle Layout

- `fonts/zh-hans-cjk/`
  - `manifest.ini`
  - `charset.txt`
  - `ranges.txt`
  - `font.bin` after generation
- `locales/zh-Hans/`
  - `manifest.ini`
  - `strings.tsv`
- `ime/zh-hans-pinyin/`
  - `manifest.ini`

## Generate The Font Pack

1. Refresh the glyph subset files:

```bash
python tools/build_locale_pack_charset.py --pack-root packs/zh-Hans --extra-chars-file tools/pinyin_chars.txt
```

2. Generate `font.bin` with the helper wrapper around `lv_font_conv`.

PowerShell example:

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

- `charset.txt` and `ranges.txt` are derived from the locale strings plus the built-in Pinyin candidate set.
- The helper downloads `lv_font_conv` into a temporary directory for the conversion run, so you do not need to keep a local extracted copy in the repository.
- `font.bin` is intentionally not committed. Regenerate it whenever the localized strings or IME glyph set changes.

## Copy To SD Card

Copy the contents of this bundle onto the SD card so the final layout becomes:

```text
/trailmate/packs/fonts/zh-hans-cjk/manifest.ini
/trailmate/packs/fonts/zh-hans-cjk/font.bin
/trailmate/packs/locales/zh-Hans/manifest.ini
/trailmate/packs/locales/zh-Hans/strings.tsv
/trailmate/packs/ime/zh-hans-pinyin/manifest.ini
```

Then reboot the device. The Settings page will show `简体中文` once the pack is discovered.
