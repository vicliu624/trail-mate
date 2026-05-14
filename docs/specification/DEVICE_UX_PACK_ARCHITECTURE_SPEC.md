# Device UX Pack Architecture Specification

## Purpose

Phase 8 introduces explicit device UX packs so Trail Mate can support multiple
screen sizes, input styles, and product depths without scattering device checks
inside pages and renderers.

Device differences are not just layout differences. They include page set,
feature depth, input model, workflow shortcuts, map capability, chat depth,
team actions, and status-only experiences.

Phase 8.1 defines the vocabulary and boundaries only. It does not split
`ui_shared`, create UX pack modules, or move existing LVGL screens.

Phase 8 UX Pack Runtime Binding establishes the first executable selection
chain:

```text
Target/AppShell
  -> ux_pack_id
    -> UxPackRegistry
      -> IUxPack
        -> ScreenRegistry / UxFeatureSet / InputBindingSet
```

This binding names the current default UI as `CompatibilityUxPack`, adds
`UConsoleDesktopUxPack`, `TinyNodeStatusUxPack`, and `SimulatorFullUxPack`, and
requires app shell validation to resolve the configured UX pack through
`findUxPackById`.

## Core Rule

```text
Board describes.
Target chooses.
UX Pack presents.
Renderer draws.
```

Expanded chain:

```text
Board describes hardware facts.
Target chooses product intent and UX profile.
DeviceUxProfile declares UX capability decisions.
UxPack builds page set, input bindings, and renderer choices.
ScreenFactory creates screens for that pack.
Renderer draws snapshots and widgets.
```

## Core Concepts

### DeviceUxProfile

`DeviceUxProfile` is a product UX declaration for a target class.

It describes:

- screen class
- input model
- primary workflows
- feature depth
- hidden or degraded features
- selected `UxPack`
- selected `LayoutProfile`
- selected `ScreenSet`
- selected `InputBindingSet`

It does not describe board pins, buses, SDK APIs, or storage paths.

### UxPack

`UxPack` is a strategy-like UI product package for a device family.

It decides:

- which screens exist
- which workflows are first-class
- which features are compact, status-only, hidden, or unavailable
- which renderers are used
- which input bindings are active
- which page transitions are available
- which modal and picker implementations are used

Candidate future packs:

```text
deck_full
pager_compact
watch_quick
cardputer_wide
tab5_touch
tiny_node_status
```

Current executable baseline packs:

```text
compatibility
uconsole_desktop
tiny_node_status
simulator_full
```

### UxFeatureSet

`UxFeatureSet` expresses product-to-UX decisions.

It may classify a feature as:

- full
- compact
- quick action
- status only
- hidden
- unavailable

Examples:

| Feature | Full device | Compact device | Status node |
| --- | --- | --- | --- |
| Map | full pan/zoom/offline tiles | compact current-position view | hidden |
| Chat | full conversation and actions | quick reply / recent messages | status only |
| Team actions | location, command, picker | quick location | hidden |
| GPS | full status and sky view | compact status | status only |

Feature decisions belong to the target UX profile and UX pack, not to board
facts or renderers.

### ScreenSet

`ScreenSet` lists screens available in a UX pack.

It defines navigation membership, default screen, screen ordering, modal
availability, and whether a screen is full, compact, or delegated to a status
surface.

### InputBindingSet

`InputBindingSet` maps device input affordances to presentation actions.

It may describe:

- rotary encoder bindings
- keyboard bindings
- touch gestures
- button chords
- long-press behavior
- paging behavior
- quick actions

It must not call platform input APIs directly. Platform adapters emit input
events; the binding set maps them into UX actions.

### ScreenFactory

`ScreenFactory` is an abstract factory owned by a UX pack.

It creates screens, modals, pickers, and renderer variants selected by that UX
pack. It must not create product services, read board pins, or reach into build
entrypoints.

### LayoutProfile

`LayoutProfile` describes visual and spatial constraints:

- viewport class
- density
- navigation form
- available chrome
- touch or non-touch layout assumptions
- modal sizing
- list density
- map viewport behavior

`LayoutProfile` is not the whole UX decision. It is one input to `UxPack`.

## Layering

Target layering:

```text
ui_presentation
  portable snapshots/models

ui_lvgl_core
  LVGL technical primitives

ui_lvgl_ux_packs
  device-specific pages/features/layouts/input

apps/target
  chooses UX pack

boards
  describe hardware facts
```

Detailed responsibilities:

| Layer | Owns | Must not own |
| --- | --- | --- |
| `ui_presentation` | portable snapshots, models, read DTOs, action DTOs | LVGL, GTK, filesystem, platform headers, board macros |
| `ui_lvgl_core` | LVGL themes, primitive widgets, navigation host, input host helpers, screen host | product feature decisions, target selection, board facts |
| `ui_lvgl_ux_packs` | device-specific screen sets, feature depth, renderer variants, modal variants, input binding sets | HAL details, SDK build entrypoints, protocol policy |
| `apps/target` | UX pack choice, target profile choice, platform and board composition | screen internals, LVGL widget trees, board pin facts |
| `boards` | hardware facts | product feature decisions, UX screen selection |

## Allowed Flow

```text
Target profile
  -> DeviceUxProfile
    -> UxPack
      -> ScreenSet + InputBindingSet + LayoutProfile
        -> ScreenFactory
          -> Renderer / widgets
            -> LVGL
```

Presentation snapshots flow in the opposite runtime data direction:

```text
runtime source
  -> presentation source / projector
    -> snapshot / read DTO
      -> UX pack renderer
        -> LVGL / GTK / ASCII
```

The renderer consumes the chosen shape. It does not choose the target, board,
feature set, or data source.

## Forbidden Patterns

Do not branch inside pages on `BOARD_xxx` macros.

Renderer must not detect concrete device.

Board facts must not decide product features.

Do not put all device UI in one `chat_page.cpp` / `map_page.cpp`.

Do not treat layout profile as the full UX decision.

Do not put PlatformIO, ESP-IDF, or CMake entrypoint logic inside a UX pack.

Do not let a UX pack read filesystem tile paths, GPS runtime, Team store, Chat
service, or board pin maps directly.

## Device UX Pack Examples

| UX pack | Typical device class | Intent |
| --- | --- | --- |
| `deck_full` | keyboard/touch deck | full chat, full map, team actions, settings |
| `pager_compact` | LoRa pager | compact navigation, quick messages, current GPS, status views |
| `watch_quick` | watch-sized touch device | glanceable status, quick actions, compact team location |
| `cardputer_wide` | keyboard landscape device | dense chat and status, compact map |
| `tab5_touch` | larger touch display | full map, richer team, dashboard-first UX |
| `tiny_node_status` | small node/status display | telemetry, radio/GPS/battery, no full workflows |

These are architecture examples, not required Phase 8.1 directories.

## Relationship To Phase 7

Phase 7 moved ownership out of controllers and renderers:

- chat delivery runtime
- chat event pump
- Team action ownership
- Team rich payload presentation
- Team position picker renderer
- map tile source/cache
- map render queue/cache
- map overlay source
- GPS runtime scheduling

Phase 8 uses those boundaries to decide which objects become shared portable
models, LVGL core primitives, UX pack renderers, runtime modules, or legacy
adapters.

## Non-Goals

Phase 8.1 does not:

- implement a `UxPack` class
- create `ui_lvgl_core`
- create `ui_lvgl_ux_packs`
- split LVGL screens
- change navigation behavior
- change build entrypoints
- remove `Legacy*` adapters
- rewrite page renderers

## Review Rules

When adding or moving UI code, ask:

- Is this portable presentation state, LVGL primitive, UX pack decision,
  renderer implementation, runtime helper, platform adapter, or board fact?
- Does this device difference change only geometry, or does it change workflow?
- Is a renderer choosing a feature that should be chosen by `DeviceUxProfile`?
- Is a board manifest making a product decision?
- Is one page accumulating device-specific branches that should be a UX pack
  split?

Phase 8 should make device UX differences explicit before making directory
movement broad.
