# Locale, Font, and IME Packs

## Why This Exists

Trail Mate used to treat UI language as a fixed firmware enum:

- strings were compiled into the firmware
- the active language was stored as an integer
- CJK rendering was selected with ad hoc `non-ASCII => swap the whole font` logic
- the touch IME always assumed a built-in Pinyin path

That model was easy to start with, but it mixed three different responsibilities:

1. Locale selection
2. Font coverage
3. Script-specific text input

Those are now split into explicit runtime resources:

- `Locale Pack`: owns translated UI strings and declares which font/IME packs it depends on
- `Font Pack`: owns glyph coverage for a writing system
- `IME Pack`: owns script-specific input behavior

The user-facing language selector operates on `Locale Pack`, not directly on fonts or IME engines.

## Current Runtime Model

At boot, Trail Mate builds a runtime registry from two sources:

1. Built-in packs compiled into the firmware
2. External packs discovered on SD card under `/trailmate/packs`

The firmware now keeps the built-in set intentionally minimal:

- built-in locale packs: `en`
- built-in font packs: `builtin-latin-ui`
- built-in IME packs: none

Simplified Chinese is no longer registered as a built-in locale. It is expected
to arrive as an external pack bundle.

The active locale is persisted as a string key:

- namespace: `settings`
- key: `display_locale`

This replaced the old `display_language` integer key. Existing ESP installs are migrated once from the legacy key to the new string key, then the old key is removed.

## Pack Responsibilities

### Locale Pack

A locale pack defines:

- locale id, for example `en` or `zh-Hans`
- display name / native name shown in the settings UI
- translated string table
- dependent font pack id
- optional dependent IME pack id

Locale packs do not embed fonts or IME logic directly.

### Font Pack

A font pack defines:

- font pack id
- display name
- one fallback font resource used by the shared UI

The shared UI now applies fonts through a composed fallback chain instead of switching on text content. Base Latin UI fonts stay in place, and the active font pack is attached as the fallback font.

### IME Pack

An IME pack defines:

- IME pack id
- display name
- backend id

The current implementation ships one built-in backend implementation:

- `builtin-pinyin`

That backend is code, not a registered built-in pack. To expose it in the UI you
still need an IME pack manifest, typically from SD.

If the active locale does not declare an IME pack, the touch IME only cycles between:

- `EN`
- `123`

If the active locale declares a script IME pack, the touch IME exposes the extra script mode.

## SD Card Layout

External packs are discovered from these directories:

```text
/trailmate/packs/fonts/<font-pack-id>/manifest.ini
/trailmate/packs/locales/<locale-id>/manifest.ini
/trailmate/packs/ime/<ime-pack-id>/manifest.ini
```

The firmware loads packs in this order:

1. Built-in font packs
2. Built-in locale packs
3. External font packs
4. External IME packs
5. External locale packs

Locale packs whose declared dependencies are missing are not registered.

## Manifest Format

Manifests are simple `key=value` files.

### Font Pack Manifest

```ini
kind=font
id=example-cjk
display_name=Example CJK
source=binfont
file=font.bin
```

Supported sources:

- `binfont`: load an external LVGL binary font file from SD
- `builtin`: alias an already compiled font pack

Example builtin alias:

```ini
kind=font
id=alias-latin
display_name=Alias Latin
source=builtin
builtin_id=builtin-latin-ui
```

### IME Pack Manifest

```ini
kind=ime
id=example-pinyin
display_name=Pinyin
backend=builtin-pinyin
```

Today `backend` is metadata plus behavior selection. The only shipped backend is `builtin-pinyin`.

### Locale Pack Manifest

```ini
kind=locale
id=zh-Hans
display_name=Chinese
native_name=简体中文
font_pack=example-cjk
ime_pack=example-pinyin
strings=strings.tsv
```

Fields:

- `font_pack` is required in practice; if omitted, the locale falls back to `builtin-latin-ui`
- `ime_pack` is optional
- `strings` points to a TSV file relative to the locale pack directory

## String Table Format

Locale string tables use a TSV format:

```text
English source string<TAB>Localized string
```

Escape sequences supported in each field:

- `\\n`
- `\\t`
- `\\\\`

Example:

```text
Settings	Parametres
Run	Executer
Send this code and compare:\\n	Envoyez ce code et comparez:\\n
```

The lookup key is always the English source string already used by the UI code.

## Built-In Packs

The firmware currently ships these built-in packs:

- Locale packs:
  - `en`
- Font packs:
  - `builtin-latin-ui`
- IME packs:
  - none

The firmware still contains the `builtin-pinyin` backend implementation so an
external IME pack can point to it, but that backend is not registered as a
default runtime pack on its own.

## Repository Bundle

The repository includes a complete Simplified Chinese example bundle under:

```text
packs/zh-Hans
```

That bundle contains:

- `locales/zh-Hans/strings.tsv`
- `locales/zh-Hans/manifest.ini`
- `fonts/zh-hans-cjk/manifest.ini`
- `ime/zh-hans-pinyin/manifest.ini`
- generated subset inputs `charset.txt` and `ranges.txt`
- generation notes in `packs/zh-Hans/README.md`

`font.bin` is intentionally generated locally rather than committed. Build it
from the bundled subset files, then copy the resulting directories onto the SD
card under `/trailmate/packs/...`.

The repository also includes a first-wave European bundle under:

```text
packs/europe-latin-ext
```

That bundle contains:

- one shared `latin-ext-eu` external font pack for Latin Extended glyph coverage
- locale packs for `fr`, `de`, `es`, `it`, `pt-PT`, `nl`, and `pl`
- generated subset inputs `charset.txt` and `ranges.txt`
- generation notes in `packs/europe-latin-ext/README.md`

As with `zh-Hans`, the generated `font.bin` stays local and should be copied to
SD alongside the manifests and string tables.

## UI Behavior

### Settings

The Settings page no longer hardcodes a language enum. It now:

- rebuilds the locale option list from the runtime registry
- persists the selected locale by locale id string
- shows raw locale display names instead of routing locale names through the translation table

### Fonts

The shared UI font path now works like this:

1. Pick the normal Latin base font for the current UI role
2. Attach the active font pack as the fallback font
3. Reuse that composed font across screens

This keeps UI intent clear:

- layout still chooses a base font for size and tone
- locale choice decides glyph coverage

### IME

The IME no longer assumes that script input is always available.

- locales without an IME pack expose `EN` and `123`
- locales with a script IME pack expose the extra script mode

## Failure Behavior

If an external locale pack disappears or its dependencies cannot be resolved:

- the pack is skipped during registry build
- the active locale falls back to `en` if available
- the persisted locale string is left untouched so the locale can be restored automatically if the pack returns later

If an external font pack fails to load, any locale that depends on it is also skipped.

## Recommended Workflow For Adding A New Language

1. Add or reuse a font pack that covers the script.
2. Add an IME pack only if the script needs input beyond `EN` and `123`.
3. Add a locale pack that points at those dependencies.
4. Put the translated strings into `strings.tsv`.
5. Copy the pack directory onto the SD card under `/trailmate/packs/...`.
6. Boot the device and choose the locale from Settings.

## Design Constraints

This architecture intentionally avoids:

- integer language enums as the persisted model
- duplicated "language plugin that secretly contains everything"
- bypass font switches based on text heuristics
- special-case persistence paths for locale selection

The goal is one clear dependency chain:

```text
Locale Pack -> Font Pack
Locale Pack -> IME Pack
```

That keeps the code easier to reason about and makes future language expansion SD-driven instead of firmware-only.
