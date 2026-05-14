# Map Tile Render Queue / Decoded Cache Spec

## Rule

Source owns tile bytes.

Decoder cache owns decoded image lifetime.

Render queue owns the current visible tile plan.

Renderer draws widgets from the visible plan. It must not own tile path policy.

## Objects

| Object | Pattern | Responsibility | Forbidden |
| --- | --- | --- | --- |
| `MapTileScreenRect` | View DTO | Screen-space tile rectangle | Filesystem path |
| `MapTileRenderState` | View state enum | Unknown / Missing / Loading / Ready / Error | Decode ownership |
| `MapTileRenderRef` | View DTO | Tile identity + screen rect + state | File reads, LVGL object lifecycle |
| `MapTileRenderQueue` | Read model / render queue | Fixed-capacity current viewport tile plan | `std::vector`, LVGL objects, filesystem reads |
| `IMapTileDecoderCache` | Cache port | Portable decoded cache boundary | LVGL handles, path mapping |
| `LvglDecodedTileCache` | Adapter / cache owner | ESP LVGL decoded image cache slots | Viewport calculation, filesystem path policy |

## Render Queue Contract

`MapTileRenderQueue` uses fixed capacity storage and exposes:

- `clear()`
- `push(item)`
- `size()`
- `items()`

It is C++11-compatible and does not allocate.

Platform map tile runtimes may populate the queue from existing `MapTile` records during this burn-down phase.

## Decoded Cache Contract

`IMapTileDecoderCache` exposes only portable cache operations:

- `clear()`
- `hasDecoded(ref)`

LVGL-specific decoded handles stay in `LvglDecodedTileCache` or other platform-specific adapters.

## Phase 7.11 First Slice

Phase 7.11 introduces:

- `MapTileScreenRect`
- `MapTileRenderState`
- `MapTileRenderRef`
- `MapTileRenderQueue`
- `IMapTileDecoderCache`
- ESP `LvglDecodedTileCache` wrapper around the existing decoded cache slots

The platform renderer may continue to own LVGL widget creation temporarily, but visible tile plan state must be projected into `MapTileRenderQueue`.

## Non-Goals

Phase 7.11 does not:

- implement map overlay
- move route/tracker ownership
- add measurement tools
- implement online tile download
- rewrite the Linux downloader
- implement a full LRU cache
- change map projection math
- change UX Pack
- change repository layout
