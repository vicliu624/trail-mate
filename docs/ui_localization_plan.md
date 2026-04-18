# UI Localization Plan

## Goal

Add runtime-selectable UI localization for English and Chinese across the LVGL-based Trail Mate interface.

- Default language: English
- Supported languages: English / Chinese
- Language switch entry point: `Settings > System > Display Language`
- Switch behavior: apply immediately without reboot

## Scope

This task covers user-facing UI text produced by the device firmware itself, including:

- Main menu app names
- Shared LVGL screens under `modules/ui_shared`
- ESP-specific LVGL screens under `platform/esp/arduino_common/src/ui`
- Shared notifications / prompts / modal labels
- Settings categories, items, enum labels, action labels, and validation messages
- Menu dashboard widgets
- Screen saver prompts and device-side status notices

This task does not translate:

- Incoming user messages
- Contact names, node names, callsigns, channel names, or other user-generated content
- Protocol brand names such as `Meshtastic`, `MeshCore`, `LXMF`, and `RNode`
- README / docs / release notes

## Design

### 1. Translation Dictionary

Use a shared localization module in `modules/ui_shared` with:

- `ui::i18n::Language`
- persistent current language state
- `ui::i18n::tr(const char* english)` as the canonical dictionary lookup

The dictionary uses the existing English source text as the canonical lookup key and returns:

- the original English string when language is English
- the translated Chinese string when language is Chinese and a translation exists
- the original English string as a fallback when no translation entry exists

This approach minimizes risk while retrofitting a large existing UI codebase with many hardcoded literals.

### 2. Persistence

Persist the current UI language under the `settings` namespace:

- key: `display_language`
- value: `0 = English`, `1 = Chinese`

### 3. Runtime Refresh

Changing the language should:

- persist the new language
- refresh main menu labels
- rebuild the currently active app screen asynchronously

This avoids requiring every widget to subscribe to a language-change event individually.

### 4. Font Handling

Localized labels must automatically switch to the CJK font when the translated text contains non-ASCII characters.

Use the shared font helpers so that:

- ASCII labels keep their existing UI font
- Chinese labels switch to the Noto CJK font when available
- boards compiled without CJK glyph support still fall back safely to the existing font configuration

## Implementation Steps

1. Add the shared localization module and persistence helpers.
2. Add menu/app refresh support so language changes take effect immediately.
3. Add `Display Language` to `Settings > System`.
4. Route shared settings labels, options, prompts, and validation messages through localization.
5. Localize menu titles, dashboard labels, shared widgets, modal buttons, and notifications.
6. Localize page-level fixed strings across Contacts / Chat / GPS / Tracker / PC Link / USB / SSTV / GNSS / Walkie Talkie / placeholder pages.
7. Localize ESP-specific prompts such as screen-saver text and battery / event notifications.
8. Run formatting and CI-equivalent builds, then fix any regressions.

## Acceptance Criteria

- English is the default UI language on clean startup.
- The language can be changed in `Settings > System > Display Language`.
- Changing the language updates the current screen immediately.
- Returning to the main menu shows localized app names.
- Shared prompts such as `Back`, `Save`, `Cancel`, `Loading...`, system toasts, and screen titles are localized.
- Chinese text renders legibly with CJK glyph coverage on supported targets.
- Existing PlatformIO CI builds still pass.
