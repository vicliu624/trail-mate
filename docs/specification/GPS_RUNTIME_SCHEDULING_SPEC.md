# GPS Runtime Scheduling Spec

## Ownership Rule

GPS page cadence belongs to runtime scheduling, not renderers and not page
widgets.

`GpsPageRuntimePump` owns:

- active/inactive flag
- refresh interval
- last refresh timestamp
- model refresh notification order

It does not own:

- LVGL widgets
- hardware GPS adapters
- route/tracker stores
- map tile loading

## UI Refresh Port

`IGpsUiRefreshSink` exposes the minimal UI notification:

```cpp
virtual void onGpsRuntimeUpdated() = 0;
```

It must not expose GPS source, parser, hardware adapter, or snapshots.

## Refresh Model Port

`IGpsStatusRefreshModel` exposes:

```cpp
virtual void refresh() = 0;
```

The current implementation may adapt a legacy page refresh function. The pump
still owns cadence even if the underlying model remains pull-on-render.

## Pump Behavior

Inactive:

- `update(now_ms)` is a no-op.

Active first update:

- call `model.refresh()`
- call `ui.onGpsRuntimeUpdated()` if a sink exists
- record `now_ms`

Active subsequent update:

- refresh only when `now_ms - last_update_ms >= interval_ms`

When set inactive:

- clear last update state so the next active update refreshes immediately.

## Runtime Wiring

Page runtime may keep LVGL timers as tick hooks. The timer callback must call
the pump; the callback must not encode refresh cadence policy itself.

## Non-goals

- No GPS hardware adapter rewrite.
- No NMEA parser rewrite.
- No route/tracker rewrite.
- No map tile loader rewrite.
- No GNSS skyplot rewrite.
- No new thread model.
