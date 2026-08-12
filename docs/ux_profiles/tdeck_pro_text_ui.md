# T-Deck Pro Text UI Contract

## Scope

This document defines the product presentation for the LilyGo T-Deck Pro
(`240x320`, portrait, monochrome e-paper).  It intentionally replaces the
previous T-Deck Pro icon-grid presentation.

The UI is a presentation adapter for the existing app catalogue and screen
lifecycle.  It must not create a second catalogue, protocol state, message
state, location state, or settings store.

## Non-negotiable visual rules

- Use a fixed 16-pixel GNU Unifont-derived bitmap font for T-Deck Pro text.
- Render in two colours only: white paper and black ink.
- Render normal UI geometry on the integer pixel grid: one-pixel rules,
  rectangular frames, and inverse-video focus.
- Do not use application icons, icon grids, rounded cards, colour-coded
  chips, shadows, gradients, image transforms, or scaled bitmap assets.
- The main screen is a 240x320 text list.  It is not an enlarged 128x64 or
  192x176 layout.

## Separation of responsibilities

| Layer | Owns | Must not own |
| --- | --- | --- |
| App catalogue and capability `AppScreen` | Available capabilities and the active-app lifecycle | T-Deck Pro layout |
| T-Deck Pro text shell | 240x320 menu layout, selection, focus, text labels, input hints | Application data or page actions |
| T-Deck Pro `TextAppAdapter` | Transient page widgets and keyboard focus for an adapted capability | Protocol, message, location, or configuration state |
| Existing presentation source/action sink | Typed state projection and domain operation | Board-specific visual identity |
| Legacy shared page | Domain-specific content and controls for targets that select it | T-Deck Pro visual identity |
| T-Deck Pro board/display | EPD frame transfer, dirty-region coalescing, refresh policy | Navigation or UI data |

## Main-screen geometry

All coordinates are in physical 240x320 pixels.

- Outer margin: 8px.
- Header: `x=8, y=8, w=224, h=40`; title at the first 16px line and compact
  live status on the second line; bottom rule at `y=47`.
- App list viewport: `x=8, y=56, w=224, h=208`; eleven 16px text rows plus
  a 1px separator below the focused row.  The selected row is inverse video.
- Detail strip: `x=8, y=272, w=224, h=16`; carries the selected app's
  description/name and page position.
- Command bar: `x=8, y=296, w=224, h=16`, separated by a 1px top rule;
  `ENT OPEN`, `ESC BACK`, and keyboard-navigation hints are text.

## Input and navigation contract

- Up/Down changes only the visual selection in the text shell.
- Enter/keyboard Select calls the selected existing `AppScreen::enter` via
  the shared lifecycle.
- Escape/Back returns via the shared `ui_request_exit_to_menu` lifecycle.
- Touch may activate a row, but touch geometry never changes the keyboard
  focus model.
- An adapted page is rendered once on entry and after an explicit action. It
  has no periodic display-refresh timer: a static page must leave the e-paper
  panel electrically quiet rather than generate a visible pulse.

## Adapted capability pages

The catalogue selects `TextAppAdapter` only for T-Deck Pro. It preserves the
capability's stable id and localized name while selecting a different visual
projection; it does not enter the old colour page and then restyle it.

- **Map:** `RuntimeGpsStatusSource` and `RuntimeMapWorkspaceSource`; Center,
  Zoom, and Terrain call `RuntimeMapActionSink`.
- **Chat:** `ChatPresentationSource` → `ChatWorkspaceModel` reads the
  conversation/message projection; select, mark-read and send call
  `RuntimeChatActionSink`.
- **Team:** `TeamUiSnapshotStore` supplies membership, key-readiness and
  member state; create/join/leave reuse the established Team reducer,
  pairing port and durable key store. Team-scoped messaging uses the separate
  `TeamChatPresentationSource` / `TeamChatActionSink` path rather than
  pretending that it is a normal channel conversation.
- **Contacts:** `ContactService` supplies saved-contact and nearby-node
  projections. The selected node is routed through the existing conversation
  mapper and then opened in the text Chat adapter; the adapter refuses a
  cross-protocol route instead of silently sending over the wrong network.
- **Sky Plot:** `RuntimeGpsStatusSource`; GPS enable/disable calls the
  settings action sink.
- **Network:** `RuntimeDeviceStatusSource` and `RuntimeMeshStatusSource`.
- **Settings:** `RuntimeSettingsSource`; the exposed GPS toggle calls
  `RuntimeSettingsActionSink`.
- **Extensions:** `ui::runtime::packs` owns both the installed-index and the
  asynchronous package worker. The text page shows installed packs offline,
  fetches the catalog only after an explicit `RELOAD`, and begins compatible
  installs through `start_install_package`. A later explicit page action
  observes worker completion and reloads the active locale, so a downloaded
  Chinese `font.bin` becomes the fallback for the built-in English Unifont
  without an idle display polling timer.
- **Protocol Probe:** the existing radio scanner owns candidate progression,
  packet classification, verification and config application. The Pro control
  boundary starts/stops that scanner and exposes an explicit snapshot only;
  its 35ms radio worker never mutates an LVGL object. `SYNC` alone repaints
  the text page, while `APPLY` requires a second `CONFIRM` action before the
  selected radio profile can be committed.
- **Tracker, Walkie, SSTV, USB Storage:** their existing
  `platform::ui::*` capability contracts expose the current status and own
  the Start/Stop (and Walkie monitor) operations. The text page neither owns
  a second session state nor creates an auxiliary device service.

This list is deliberately explicit. A capability is not considered migrated
just because its old page happens to inherit the Pro default font. Each
additional page must name its existing read projection and action port before
it is placed behind the text adapter.

## Explicit exclusions

- The former `tdeckpro_epd` PNG/descriptor asset family is deleted.
- New shell labels use a compiled, 1bpp 16px GNU Unifont **English-only**
  subset. It is emitted at its native pixel grid and is never scaled by LVGL.
  All localized glyphs, including Simplified Chinese, remain downloaded/SD
  `font.bin` packs and are attached through the existing i18n fallback chain.
- The former T-Deck Pro icon-grid profile, image-scale guards, EPD colour
  theme, and UNSCII-specific UI branches are deleted.
- No page is allowed to introduce a duplicate domain model merely to fit the
  text UI.  A page-specific text view may be added only as a projection of
  existing shared page state and lifecycle.

## Font policy

GNU Unifont is a dual-width 8x16/16x16 monochrome font. The firmware ships a
reviewed 1bpp subset sufficient for the shell's Latin UI vocabulary. Localized
content is carried by the existing downloadable font-pack mechanism: the
`zh-Hans` bundle provides T-Deck-Pro-only `tdeckpro-zh-hans-core` and
`tdeckpro-zh-hans-ext` Unifont subsets. The UI must fail over to the active
locale font chain rather than rasterizing or scaling text.

## Verification conditions

1. A Pro build has no include/reference to `tdeckpro_epd` assets or generated
   icon descriptors.
2. The main screen renders a text-only 240x320 list with inverse focus and
   no image widgets.
3. Selecting every app enters the shared active-app lifecycle. Adapted entries
    render their named typed projections and do not instantiate the legacy
   colour page. Protocol Probe's radio worker may execute at its required
   cadence, but only an explicit `SYNC` action can cause an EPD text redraw.
4. T-Deck uses its original menu presentation and does not select the Pro
   shell.
5. A T-Deck Pro hardware run demonstrates stable idle ink (no timer-driven
   refresh without changed pixels) and legible, unscaled 16px glyphs.
