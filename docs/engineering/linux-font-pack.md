# Optional Linux font and language package

TrailMate's native Linux LVGL applications on Cardputer Zero and T-Display-CM0
can use `trailmate-linux-fonts` alongside the application package. This is a
separate, architecture-independent Debian package, available with compatible
TrailMate releases starting at 1.2.0. The application remains usable in English
without it.

The package supplies complete source fonts instead of the selected glyph subsets
used by constrained firmware. It installs read-only resources under
`/usr/share/trailmate/packs`. User-installed resources retain precedence, and
font installation does not copy resources into the user's SD/data directory.

| Font | Unicode mappings | Runtime size | Use |
| --- | ---: | ---: | --- |
| Noto Sans CJK SC | 44,810 | 16px | Chinese and Korean content, Simplified Chinese UI |
| Noto Sans CJK JP | 44,810 | 16px | Japanese glyph variants |
| Noto Sans | 2,840 | 16px | Additional Latin and Cyrillic content |
| TrailMate Color Emoji 14 | 1,510 | 14 ppem bitmap strike | Compact color emoji |

The CJK counts include Han, kana, Hangul and other characters; they do not mean
that every Unicode Han extension is covered. For example, the supplied CJK font
includes U+20BB7 but does not include U+20000. Coverage files are generated from
the actual font cmap, rather than claiming entire blocks.

English is built in. Simplified Chinese is currently the only additional
firmware language marked `translation_status=release`; the package preserves
that release policy. Languages still marked for review are not silently enabled.
The Chinese translation table is shared with the ESP resource packages.

Linux text input remains owned by the existing operating-system/toolkit input
method integration. The generated display-language manifest therefore does not
depend on the ESP Pinyin engine. This package does not alter Cardputer Zero's
Fcitx5/input frontend contract.

## Installation

Install the compatible application first, then install the matching font DEB
downloaded from the same published GitHub Release:

```sh
sudo apt install ./trailmate-linux-fonts_1.2.0_all.deb
```

Restart TrailMate to rescan system resources. Simplified Chinese then appears in
the display-language settings. Removing the optional package lets the application
fall back to English on its next start; identity, messages and other user data
remain in their existing locations.

This package targets native Linux LVGL applications. The uConsole GTK frontend
continues to use the operating system's font configuration. ESP retains its
existing binfont sources, loading rules and memory profiles.

## Loading and lifecycle

Linux enables LVGL's FreeType bitmap backend. Native manifests use
`source=freetype`, `targets=linux`, an explicit `pixel_size` between 8 and 64,
and actual coverage ranges. Unsupported font sources are rejected explicitly.
The system pack root is scanned after user resources, including released display
languages. Existing user IDs win.

Each face uses bounded caches of 128 glyph metrics and rendered images. Font
metadata may be larger than those caches; CJK manifests conservatively account
for 32 MiB per face. Linux uses 64 MiB budgets for active locale fonts and content
supplements while retaining the established `extended` repository-policy name.
Existing per-target map cache settings are retained. These limits do not describe
total application RSS. No ESP budget is enlarged.

Color emoji are prepared before a monochrome fallback can satisfy the same
character and precede it in UI/content fallback chains. ASCII primary fonts retain
their own metrics. Typical emoji images are approximately 17 by 16 pixels; compact
line boxes avoid importing the original font's 109 ppem dimensions into the UI.

`shutdown_resource_packs()` runs on the LVGL owner after widgets are detached and
before `lv_deinit()`. It releases owned fonts, deferred work and font bindings.
This supports clean UI destruction and subsequent LVGL initialization in the same
process. FreeType fonts use the matching FreeType destructor.

The color font is an OFL 1.1 derivative of pinned Google Noto Color Emoji. Its
original license, source SHA256, generator versions and derivative SHA256 are
included. See [the source recipe](../../tools/fonts/linux/README.md). LVGL's scalar
fallback does not implement every emoji ZWJ, flag or modifier sequence; retaining
OpenType tables is not a claim of complete sequence shaping.

## Build and verification

Use an isolated Python environment for the font tools:

```sh
python3 -m venv .codex-build/linux-font-tools
.codex-build/linux-font-tools/bin/python -m pip install -r tools/fonts/linux/requirements.txt
version=$(python3 scripts/project_version.py --debian)
.codex-build/linux-font-tools/bin/python tools/build_linux_font_pack.py \
  --stage .codex-build/linux-font-package-stage --version "$version" \
  --deb "build/linux-fonts/trailmate-linux-fonts_${version}_all.deb"
python3 scripts/verify_linux_font_pack.py --directory build/linux-fonts --version "$version"
```

The builder requires an empty staging directory. The artifact verifier checks
package identity/version, architecture, root ownership, permissions, path scope,
licenses, source hashes and language dependencies. It does not install the package
or execute application code.

Cardputer Zero CI builds a separate font fixture and exercises the production
registry and LVGL renderer. It checks actual CJK glyphs, color pixels, primary
ASCII selection, user override precedence, repeated locale changes, descriptor
stability and a second LVGL lifetime. Native screenshot capture accepts an optional
fifth argument selecting the locale; such captures require a private
`TRAIL_MATE_SETTINGS_ROOT` and disable packet radio in that isolated fixture.

`tools/validate_locale_packs.py` also audits directly translated UTF-8 source
literals and declared menu names against the released Chinese table. It understands comments, raw example
strings, concatenated literals and nested label factories. Dynamic model strings
still require visual review. Parser regressions run with:

```sh
python3 -m unittest discover -s tests/ui_localization
python3 tools/validate_locale_packs.py
```

Release packaging creates the font DEB before publishing the application's own
GitHub Release. Its SHA256 is included with the other release assets. Store
submission still requires that published release and successful tagged CI; a
font package or application artifact from an unfinished workflow is insufficient.
