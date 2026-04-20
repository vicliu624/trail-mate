# Locale, Font, and IME Packs

## Goals

The pack system exists to solve four problems at the same time:

1. Keep the firmware image minimal. English stays built in, large script assets move to SD.
2. Keep `installed` separate from `loaded`. A pack being present on SD must not mean its font is already in RAM.
3. Let mixed-language content render correctly. A Spanish UI still needs to show a Chinese contact name if that font pack is installed.
4. Bound RAM usage across very different devices, including no-PSRAM targets.

This is now a first-class runtime architecture, not a collection of text-based bypasses.

## Core Model

Trail Mate treats localization as three explicit pack types:

- `Locale Pack`
  Owns translated UI strings and declares which UI font pack, content font pack, and optional IME pack it needs.
- `Font Pack`
  Owns glyph coverage metadata plus the external `font.bin` asset.
- `IME Pack`
  Declares script-specific input behavior such as Pinyin.

The Settings page selects a `Locale Pack`. It does not directly choose a font or IME.

## Built-In vs External

The firmware intentionally ships with the smallest built-in baseline:

- built-in locale packs: `en`
- built-in font packs: `builtin-latin-ui`
- built-in IME packs: none

English is therefore always available, even when the SD card has no language packs at all.

External packs are discovered from the SD card under:

```text
/trailmate/packs/fonts/<font-pack-id>/manifest.ini
/trailmate/packs/locales/<locale-id>/manifest.ini
/trailmate/packs/ime/<ime-pack-id>/manifest.ini
```

At boot the registry is built in two phases:

1. Catalog every built-in pack and every external pack manifest.
2. Resolve dependencies and decide which locales are allowed on the current memory profile.

Important: manifest discovery is cheap. External fonts are not loaded into RAM during cataloging.

## Three Layouts

The same localization asset now exists in three different representations, and keeping them
separate is intentional:

### Repository Source Bundle

What lives under `packs/<bundle-id>/` in Git.

- contains runtime manifests
- contains human-facing package metadata
- contains build-only files such as `charset.txt` and `build.ini`
- may omit tracked `font.bin`, because the Pages build can regenerate it

### Installed Runtime Layout

What the firmware actually scans on SD:

```text
/trailmate/packs/fonts/<font-pack-id>/...
/trailmate/packs/locales/<locale-id>/...
/trailmate/packs/ime/<ime-pack-id>/...
```

This is the only layout the runtime registry understands.

### Distribution Package

What the website and future Extensions page should traffic in:

- one zip per installable bundle
- one package manifest with version and compatibility metadata
- one description file for UI presentation
- one remote catalog entry for discovery and update checks

The runtime does not scan zip files directly. The package manager layer downloads a zip,
unpacks its payload into the installed runtime layout, then asks the runtime registry to
refresh.

## Installed Is Not Loaded

This distinction is the center of the design.

- `Installed`
  The manifest exists on SD and the registry knows the pack exists.
- `Loaded`
  The external `font.bin` has actually been passed to `lv_binfont_create()` and now consumes runtime RAM.

The runtime behaves like this:

1. The active locale is resolved from `settings/display_locale`.
2. The active UI font pack is loaded immediately when that locale is activated.
3. The active content font pack is loaded lazily, only when content-scope text needs it.
4. Additional content supplement packs are loaded lazily if the current text contains codepoints not covered by the active content chain.
5. Changing locale unloads all runtime-loaded external fonts and rebuilds the chains from scratch.

That means a device can have many packs installed while only one or two are resident in RAM.

## UI Scope vs Content Scope

The runtime keeps two different fallback chains on purpose.

### UI Chain

Used for static application chrome:

- menu labels
- settings labels
- headings
- buttons
- other translated interface text

The chain is:

```text
screen-selected Latin base font -> active UI font pack
```

### Content Chain

Used for user-generated or externally received text:

- chat sender lines
- chat message previews and bodies
- contact names
- team member names
- node names and descriptions
- locale display names shown in the selector

The chain is:

```text
screen-selected Latin base font
-> active content font pack
-> active UI font pack
-> lazily loaded content supplement packs
```

This is what lets a non-Chinese UI still render Chinese content when the corresponding pack is installed.

## Memory Profiles

Pack availability is constrained by a board-specific memory profile in shared runtime code.

Current profiles:

- `constrained`
  Locale font budget `128 KiB`, content supplements disabled, decoded map cache `2` tiles, cache not retained on page exit.
- `standard`
  Locale font budget `768 KiB`, content supplement budget `640 KiB`, at most `1` supplement pack, decoded map cache `4` tiles, cache not retained.
- `extended`
  Locale font budget `2 MiB`, content supplement budget `2 MiB`, at most `3` supplement packs, decoded map cache `12` tiles, cache retained on page exit.

Current board mapping:

- `extended`: `Tab5`, `T-Display P4`
- `standard`: `T-Deck`, `T-Deck Pro`
- `constrained`: everything else, including no-PSRAM and pager-class targets

The locale budget is checked against the actual active locale cost:

```text
unique(UI font pack, content font pack)
```

The supplement budget is separate and only applies to extra content packs that are pulled in later for mixed-script content.

## Persistence

The active locale is stored as:

- namespace: `settings`
- key: `display_locale`

Legacy ESP installs using the old integer `display_language` key are migrated once to the new string key. After migration, the legacy key is removed.

## Manifest Schema

Manifests are plain `key=value` files.

### Font Pack Manifest

```ini
kind=font
id=zh-hans-cjk
display_name=Simplified Chinese CJK
usage=both
estimated_ram_bytes=476075
source=binfont
file=font.bin
ranges=ranges.txt
```

Fields:

- `id`
  Stable pack identifier.
- `display_name`
  Human-readable name used for diagnostics.
- `usage`
  One of `ui`, `content`, or `both`.
- `estimated_ram_bytes`
  Expected runtime RAM cost after loading with LVGL binfont loader. This is used for profile decisions and supplement planning.
- `source`
  Currently `binfont` for external files, or `builtin` for an alias to a compiled font pack.
- `file`
  Path to `font.bin`, relative to the pack directory.
- `ranges`
  Coverage metadata file, relative to the pack directory. This is used for codepoint planning, not for rendering.

If `estimated_ram_bytes` is missing or `0`, the runtime treats the pack as having unknown cost and cannot budget it accurately. Repository packs should always provide it.

### IME Pack Manifest

```ini
kind=ime
id=zh-hans-pinyin
display_name=Pinyin
backend=builtin-pinyin
```

Today the shipped backend implementation is:

- `builtin-pinyin`

The backend lives in firmware code. The IME pack manifest is the runtime registration layer that exposes it.

### Locale Pack Manifest

```ini
kind=locale
id=zh-Hans
display_name=Simplified Chinese
native_name=简体中文
ui_font_pack=zh-hans-cjk
content_font_pack=zh-hans-cjk
ime_pack=zh-hans-pinyin
strings=strings.tsv
```

Fields:

- `id`
  Locale identifier shown in persistence and logs.
- `display_name`
  English-facing name.
- `native_name`
  Native self-name shown in the selector.
- `ui_font_pack`
  Font pack used for interface chrome in this locale.
- `content_font_pack`
  Font pack preferred for content surfaces in this locale.
- `ime_pack`
  Optional IME dependency.
- `strings`
  Path to the locale TSV file, relative to the pack directory.

If `content_font_pack` is omitted, it defaults to `ui_font_pack`.
If `ui_font_pack` is omitted, it defaults to `builtin-latin-ui`.

## String Table Format

Locale strings are stored as TSV:

```text
English source string<TAB>Localized string
```

Supported escapes:

- `\\n`
- `\\t`
- `\\r`
- `\\\\`

Example:

```text
Settings	Paramètres
Send this code and compare:\\n	Envoyez ce code et comparez:\\n
```

The English source string remains the stable lookup key in code.

## Failure Behavior

If a locale pack cannot be used:

- missing font pack dependency: locale is skipped
- missing IME dependency: locale is skipped
- active locale font cost exceeds the current memory profile: locale is skipped
- persisted locale id no longer resolves: runtime falls back to `en`

The persisted `display_locale` value is not erased just because a removable pack is absent. If the pack returns later, the locale becomes selectable again.

If a content supplement cannot be loaded, the UI still stays alive. That specific text may render as missing glyphs, but the active locale remains unchanged.

## Repository Bundles

The repository ships source bundles under:

- `packs/europe-latin-ext`
- `packs/zh-Hans`
- `packs/zh-Hant`
- `packs/ja`
- `packs/ko`

Each source bundle contains:

- `package.ini`
- `DESCRIPTION.txt`
- generation notes in `README.md`
- runtime payload trees under `fonts/`, `locales/`, and optional `ime/`
- `build.ini` plus `charset.txt` in each external font-pack directory
- `ranges.txt` for runtime coverage planning

`font.bin` is intentionally not part of the source-of-truth layout. If it already exists
locally, it will be used. If it is absent, the Pages pack build regenerates it from the
bundled `charset.txt` and `build.ini` metadata before producing the zip archive.

## Package Manifest

Every source bundle now carries a bundle-level `package.ini`.

Example:

```ini
kind=package
id=zh-Hans
package_type=locale-bundle
version=1.0.0
display_name=Simplified Chinese
summary=Full Simplified Chinese locale bundle with external CJK font coverage and the built-in Pinyin IME backend.
description=DESCRIPTION.txt
readme=README.md
author=Trail Mate
homepage=https://vicliu624.github.io/trail-mate/
min_firmware_version=0.1.18-alpha
supported_memory_profiles=standard,extended
tags=language,cjk,chinese,ime
```

Fields:

- `id`
  Stable package identifier used by the remote catalog and future installed-index file.
- `package_type`
  High-level package role. Today repository bundles use `locale-bundle`.
- `version`
  Package version, independent from the firmware tag.
- `display_name`
  Human-facing package name for the Extensions UI.
- `summary`
  Short one-line description for list views.
- `description`
  Plain-text description file relative to the bundle root.
- `readme`
  Developer-facing documentation file relative to the bundle root.
- `author`
  Package publisher string.
- `homepage`
  Project or package home page.
- `min_firmware_version`
  Minimum firmware version expected to understand the bundle contract correctly.
- `supported_memory_profiles`
  Declared compatibility hint for `constrained`, `standard`, and/or `extended` devices.
- `tags`
  Search and grouping metadata for a future Extensions browser.

## Font Build Metadata

Each external font-pack directory may also declare a `build.ini` used only by tooling.

Example:

```ini
font=tools/fonts/NotoSansCJKsc-Regular.otf
size=16
bpp=2
no_compress=true
```

Fields:

- `font`
  Source font file in the repository.
- `size`
  Pixel size passed to the binfont generator.
- `bpp`
  Bits-per-pixel for the generated font.
- `no_compress`
  Whether the generated `font.bin` should disable lv_font_conv RLE compression.

This file is build metadata only. The runtime never reads it.

## Distribution Archive Layout

The GitHub Pages build now produces one zip per bundle under:

```text
site/assets/packs/<package-id>-<version>.zip
```

Each archive contains:

```text
package.ini
DESCRIPTION.txt
README.md
payload/fonts/<font-pack-id>/...
payload/locales/<locale-id>/...
payload/ime/<ime-pack-id>/...
```

Inside `payload/`, only installable runtime files are included. Build-only files such as
`charset.txt`, `build.ini`, and `.gitignore` are excluded from the archive.

## Remote Catalog

The Pages build also produces:

```text
site/data/packs.json
```

This catalog is the discovery surface for a future Extensions UI. Each entry contains:

- package id, version, display name, summary, and long description
- compatibility hints such as `min_firmware_version` and supported memory profiles
- the list of locale/font/IME records provided by the bundle
- archive path, size, and SHA-256 for download and update checks
- estimated runtime font RAM totals for planning before install

The catalog intentionally sits above the runtime registry. It describes downloadable bundles,
not currently loaded fonts.

## Installed Package Index

The runtime registry still catalogs installed unpacked resources directly from `/trailmate/packs`.
For package management, the corresponding installer layer should additionally maintain:

```text
/trailmate/packs/.index/installed.json
```

That file is expected to record:

- installed package id
- installed package version
- install time
- source archive SHA-256

Future update prompts should compare this installed index against `site/data/packs.json`
instead of guessing based on directory names.

## Current IME Coverage

Today only Simplified Chinese declares an IME pack in the repository bundle:

- `zh-Hans` -> `zh-hans-pinyin`

Traditional Chinese, Japanese, and Korean bundles are display-only for now. They intentionally keep input on the existing `EN` / `123` path until dedicated IME packs exist.

## Adding A New Language

1. Decide whether the locale can reuse an existing font pack or needs a new one.
2. Add a source bundle `packs/<bundle-id>/` with `package.ini`, `DESCRIPTION.txt`, and `README.md`.
3. Generate `charset.txt` and `ranges.txt` for each external font pack in that bundle.
4. Write `build.ini` for each generated font pack.
5. Write the runtime font manifest with `usage`, `estimated_ram_bytes`, and `ranges`.
6. Write the locale pack manifest with `ui_font_pack` and `content_font_pack`.
7. Add `ime_pack` only if the script truly needs extra input behavior.
8. Run `python scripts/build_pack_repository.py --pack-root packs --site-root site`.
9. Either install manually by copying the runtime payload to SD, or ship the generated zip through Pages for the future Extensions installer.
10. Reboot and select the locale from Settings.

## Design Rules

This architecture intentionally avoids:

- integer language enums
- eager loading of every installed font at boot
- a single global "non-ASCII => swap font" shortcut
- conflating UI chrome text with user content text
- per-platform localization implementations drifting apart
- mixing runtime manifests with package-distribution metadata
- forcing the runtime registry to understand remote catalogs or zip archives

The dependency graph stays explicit:

```text
Locale Pack -> UI Font Pack
Locale Pack -> Content Font Pack
Locale Pack -> IME Pack
Content text -> Optional supplement font packs
```

That makes the code easier to reason about and gives Trail Mate a path to broader language support without forcing every device to pay the same firmware and RAM cost.
