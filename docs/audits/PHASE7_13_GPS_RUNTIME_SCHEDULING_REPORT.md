# Phase 7.13 GPS Runtime Scheduling Report

## Burned Down

| Surface | Outcome |
| --- | --- |
| GPS page refresh cadence | Added `GpsPageRuntimePump` with active/inactive interval ownership |
| Direct timer-to-refresh ownership | GPS page timer callbacks now tick the runtime pump |
| UI refresh port | Added `IGpsUiRefreshSink` |
| Refresh model port | Added `IGpsStatusRefreshModel` |

## Runtime Wiring

ESP GPS page runtime uses a local refresh-model adapter to keep the existing
workflow stable while moving cadence ownership into `GpsPageRuntimePump`.

Linux GPS page runtime uses a sink that refreshes the compact map view after the
pump accepts a cadence tick.

## Still Contained

| Surface | Removal condition |
| --- | --- |
| ESP tile loader timer | Dedicated map tile runtime owns loader cadence independent of GPS page lifecycle |
| ESP title timer | Title/status refresh moves behind a UI runtime pump |
| ESP route/tracker page callbacks | Route/tracker presentation sources replace direct draw callback state |
| GNSS skyplot timer | GNSS skyplot receives its own runtime scheduling pass |

## Checker Result

Phase 7 checker now requires `GpsPageRuntimePump`, `IGpsUiRefreshSink`, pump
tests, and 7.13 documentation. It also checks that GPS page runtime uses the
pump in timer callbacks and active/inactive lifecycle.
