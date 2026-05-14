# GPS Runtime Scheduling Ownership Audit

## Purpose

Phase 7.13 assigns GPS page refresh cadence to a runtime pump instead of page
widget code or renderer code.

## Current State

| Question | Before 7.13 | 7.13 owner |
| --- | --- | --- |
| GPS page update cadence | LVGL timer callbacks directly called refresh logic | `GpsPageRuntimePump` |
| GPS page active/inactive | Page enter/exit implicitly created/deleted timers | Page runtime calls `GpsPageRuntimePump::setActive(...)` |
| GPS UI refresh notification | Timer callbacks directly refreshed UI | `IGpsUiRefreshSink` |
| GPS status refresh model | Page-specific functions and legacy presentation sources | `IGpsStatusRefreshModel` port |
| LVGL timer role | Cadence owner | Tick hook only |

## ESP Branch

The ESP GPS page still contains the large legacy map/GPS workflow. 7.13 does not
rewrite it. The previous timer callback body is now represented by a local
`IGpsStatusRefreshModel` adapter, and the LVGL timer calls:

`gps_runtime_pump().update(sys::millis_now())`

The pump owns the interval and active/inactive state. The existing page logic
remains contained behind the model adapter.

## Linux Branch

The Linux compact map page now uses a `GpsPageRuntimePump` with a refresh sink
that calls `refresh_view()`. Its LVGL timer is a tick hook, not the owner of
refresh cadence.

## Not Renderer Responsibility

GPS renderers and map widgets must not own:

- GPS poll interval policy
- active/inactive scheduling
- hardware source access
- page timer semantics

## Still Contained

- ESP tile loader timer cadence remains tile-rendering runtime legacy from
  Phase 7.11, not GPS refresh cadence.
- ESP title timer remains a UI title refresh timer and can be moved in a later
  UI runtime pass.
- GNSS skyplot has its own page runtime and is not included in 7.13.
