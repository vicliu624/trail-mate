# Phase 11 Renderer Descriptor Consumption Report

## Scope

Phase 11 moves primary descriptors into renderer-facing consumption surfaces.
It does not delete fallback, rewrite GTK widgets, rewrite LVGL menu/page
renderers, implement a full navigation stack, add UX packs, or let renderers
choose UX packs.

The Phase 11 distinction is:

- Phase 10 made descriptor/adoption paths primary.
- Phase 11 makes renderer-facing surfaces consume those primary descriptors.

## LinuxSim

renderer descriptor path:

`LinuxSimRuntimeEntry -> LinuxSimRuntimeRenderer -> AsciiDescriptorRenderer -> AsciiRuntimeEntryAdoption`

Status:

- `AsciiDescriptorRenderer` consumes `AsciiRuntimeEntryAdoption` descriptors.
- `LinuxSimRuntimeRenderer` consumes `LinuxSimRuntimeEntry`.
- When `LinuxSimRuntimeEntry::usingPrimaryScreenGraph()` is true, the renderer
  renders from `entry.adoption()`.

Fallback path:

- `LinuxSimRuntimeRenderer::renderFallback(...)` is reached only when
  `entry.fallbackUsed()` is true.
- fallback remains available but is not the default renderer data source.

Not done:

- hardcoded ASCII renderer deletion
- full navigation stack replacement

## GTK

Page registry descriptor path:

`LinuxUConsoleGtkPageRegistryAdoption -> LinuxUConsoleGtkPageRegistryRenderer -> GtkDescriptorPageRegistry -> GtkRuntimeEntryAdoption`

Status:

- `GtkDescriptorPageRegistry` consumes `GtkRuntimeEntryAdoption` descriptors
  and produces `GtkDescriptorPage` rows.
- `LinuxUConsoleGtkPageRegistryRenderer` consumes
  `LinuxUConsoleGtkPageRegistryAdoption`.
- When `LinuxUConsoleGtkPageRegistryAdoption::usingPrimaryScreenGraph()` is
  true, the renderer consumes descriptor pages.

Fallback path:

- `LinuxUConsoleGtkPageRegistryRenderer::renderFallback(...)` is reached only
  when `adoption.fallbackUsed()` is true.
- hardcoded GTK page registry remains fallback-only.

Not done:

- GTK widget tree rewrite
- full page switching replacement

## LVGL

Descriptor renderer path:

`LvglPrimaryScreenGraphRuntime -> LvglDescriptorRendererProbe -> LvglDescriptorMenuModel -> LvglRuntimeEntryAdoption`

Status:

- `LvglDescriptorMenuModel` consumes `LvglPrimaryScreenGraphRuntime`.
- `LvglDescriptorRendererProbe` consumes `LvglPrimaryScreenGraphRuntime` and
  exposes renderer-safe `LvglDescriptorMenuItem` rows.
- The path does not include `lvgl.h`, does not create `lv_obj_t`, and does not
  branch on `BOARD_`.

Fallback path:

- `LvglDescriptorRendererProbe::loadFallback(...)` is reached only when
  `runtime.fallbackUsed()` is true.
- hardcoded LVGL menu/page creation remains fallback-only.

Not done:

- real LVGL widget/menu rewrite
- device-specific renderer migration
- fallback deletion

## Guardrails

Renderer consumption paths must not call:

- `UxPackRegistry`
- `findUxPackById`
- `buildMenuForUxPack`

Renderer consumption paths must not create:

- `GtkWidget`
- `lv_obj_t`

Renderer consumption paths must not construct `MenuModel` or select UX packs.
UX selection and `PresentationBundle` construction remain upstream.
