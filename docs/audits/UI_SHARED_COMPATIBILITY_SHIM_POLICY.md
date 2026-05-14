# UI Shared Compatibility Shim Policy

## Status

Phase 8 Structural Consolidation policy.

`modules/ui_shared` is now a transitional umbrella for code that has not yet
been split into target modules. It is not the authority for newly extracted
runtime helpers, map helpers, legacy adapters, or LVGL UX pack renderers.

## Core Rule

Authoritative include paths live in the owning module.

Forwarding headers under `modules/ui_shared/include` may remain temporarily, but
they are compatibility shims only.

## Forwarding Header Pattern

A forwarding header may contain only:

- `#pragma once`
- one `#include` of the authoritative new module path

It must not contain:

- class or struct definitions
- namespace definitions
- inline implementation
- new typedefs or aliases
- ownership policy

Forwarding headers exist only so older target builds can migrate incrementally.
Main code must include the new owning module path.

## Owning Modules

| Surface | Authoritative module |
| --- | --- |
| Chat event pump / UI refresh sink | `modules/ui_chat_runtime` |
| Map tile source/cache/queue/resolver and overlay projector | `modules/ui_map_runtime` |
| GPS page runtime pump / UI refresh sink | `modules/ui_gps_runtime` |
| Bounded legacy adapters moved in Phase 8 | `modules/ui_legacy_adapters` |
| LVGL modal/picker/common UX renderers | `modules/ui_lvgl_ux_packs` |
| LVGL technical primitives | `modules/ui_lvgl_core` |

## ui_shared May Still Contain

- LVGL screens that have not been moved
- widgets that have not been split into `ui_lvgl_core`
- old presentation sources not yet classified for a target module
- existing compatibility surfaces
- forwarding headers for moved files

## ui_shared Must Not Receive

- new runtime helpers
- new legacy adapters
- new map tile helpers
- new GPS runtime helpers
- new chat runtime helpers
- new LVGL UX pack renderers

## Deprecated Alias Policy

`LegacyFilesystemMapTileSource` is a deprecated compatibility alias for
`FilesystemMapTileSource`.

New code must use:

```cpp
#include "ui_map_runtime/map_tiles/filesystem_map_tile_source.h"
ui::map_tiles::FilesystemMapTileSource
```

The old alias may appear only in the compatibility alias declaration, docs, and
explicit compatibility tests.

## Exit Conditions

Forwarding headers may be deleted after:

1. no production code includes their old `ui_shared` paths;
2. no tests use old paths except compatibility tests;
3. build manifests reference the owning modules explicitly;
4. `check_phase8_layout_ready.py` enforces the include path authority rule.
