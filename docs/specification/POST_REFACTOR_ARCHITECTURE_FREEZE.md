# Post-Refactor Architecture Freeze

## Purpose

This document freezes the directory and ownership rules after the Phase 8-12
refactor. It is a guardrail for future feature work, not a request for new
architecture layers.

## Directory Rules

| Directory | Frozen role | Allowed | Forbidden |
| --- | --- | --- | --- |
| `apps/` | final product app shells | product startup shell, active UX pack selection, app-owned probes | legacy implementation roots, board-specific historical roots, runtime service implementation sprawl |
| `builds/` | build entrypoints | thin build wrappers and build target selection | product implementation, UX decisions, renderer ownership |
| `modules/` | stable runtime / presentation / renderer / descriptor modules | reusable presentation, runtime, descriptor, renderer-safe DTO, adapter ports | dependencies on `apps/`, product shell policy, board facts as UX policy |
| `platform/` | platform / HAL / SDK adapters | platform-specific SDK and HAL boundaries | app shell behavior, UX selection, product feature policy |
| `boards/` | hardware facts only | pins, capabilities, hardware descriptions | UX decisions, page/menu selection, runtime routing |
| `docs/` | specifications, audits, reports | architecture records, phase reports, deletion readiness | executable runtime policy |
| `tools/architecture/` | phase checkers and final guardrails | structural checker scripts | product behavior or build implementation |

## Frozen Pipeline

New UI/runtime work should enter through this shape:

`app shell -> UX pack -> presentation graph -> descriptor/adoption path -> renderer-safe consumption`

Fallback may exist only as compatibility containment and must not become the
default path again.

## Explicit Prohibitions

1. Renderers must not select UX packs.
2. Runtime entries must not call `buildMenuForUxPack`.
3. Runtime entries must not call `UxPackRegistry` or `findUxPackById`.
4. App shells must not create concrete runtime services such as chat, map, GPS,
   or protocol services.
5. Board facts must not decide UX.
6. Root `legacy/` and `legacy/app_implementations/` are forbidden.
7. `modules/` must not depend on `apps/`.
8. Renderers must not create legacy services.
9. GTK descriptor consumption must not create `GtkWidget`.
10. LVGL descriptor consumption must not include `lvgl.h`, create `lv_obj_t`,
    or branch on `BOARD_`.
11. New hardcoded page registries must not be primary sources.
12. New `Legacy*` bridges are forbidden for runtime work.
13. Active checkers must not require legacy roots to exist.
14. `docs/archive` must not contain a source archive.
15. New `legacy_source_descriptor` files are forbidden.
16. New transitional UI layers are forbidden.

## Removed Legacy Roots

Root `legacy/` has been removed. Historical legacy source roots are recorded
only in `docs/archive/REMOVED_LEGACY_ROOTS.md`. No active architecture layer may
be named `legacy/`.

## Post-Freeze Work Classification

Future work should be classified as one of:

- feature work through the frozen pipeline
- targeted fallback deletion
- targeted deprecated alias deletion
- platform/build maintenance
- documentation/checker maintenance

It should not be classified as another broad architecture refactor unless the
frozen ownership model is explicitly superseded.
