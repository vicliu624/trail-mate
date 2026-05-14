# Map Tile Render Queue / Decoded Cache Audit

## Purpose

Phase 7.11 establishes explicit ownership boundaries for map tile visible render planning and decoded tile cache lifetime.

This phase builds on Phase 7.10 tile source/path ownership. It does not introduce map overlay, route/tracker ownership, online download, or a complete LRU rewrite.

## Current Ownership Before 7.11

| Question | Current answer before 7.11 | 7.11 owner / boundary |
| --- | --- | --- |
| Where are visible tiles calculated? | Platform `calculate_required_tiles(...)` writes directly into `std::vector<MapTile>` | `MapTileRenderQueue` is the explicit visible tile plan projection |
| Where are tile widgets created/destroyed? | Platform LVGL map tile runtime | Still contained in platform renderer |
| Where is ESP decoded image cache owned? | File-scope globals in ESP `map_tiles.cpp` | `LvglDecodedTileCache` object wrapping existing LVGL cache slots |
| Where is tile loading cadence owned? | Platform `tile_loader_step(...)` timer path | Still contained in platform map tile runtime |
| Does renderer decide tile filesystem paths? | No after 7.10 | Must remain behind `IMapTileSource` / `MapTileResolver` |
| How are missing/loading/ready states represented? | Implicit fields on `MapTile` | `MapTileRenderState` in `MapTileRenderRef` |

## Visible Tile Plan

`MapTileRenderQueue` is a small fixed-capacity read model for the current viewport.

It contains `MapTileRenderRef` rows:

- tile identity via `MapTileRef`
- screen rectangle via `MapTileScreenRect`
- render state via `MapTileRenderState`

It does not create LVGL objects, decode images, read files, or build tile paths.

## Decoded Cache Boundary

`IMapTileDecoderCache` is the portable cache port. It exposes only:

- `clear()`
- `hasDecoded(ref)`

It intentionally does not expose `lv_image_dsc_t*`.

`LvglDecodedTileCache` is the ESP LVGL-specific adapter. It wraps the existing decoded tile slots and keeps LVGL types inside the platform map tile runtime.

## Still Contained

| Surface | Reason | Exit condition |
| --- | --- | --- |
| LVGL tile widget records | Phase 7.11 does not rewrite widget lifecycle | Dedicated `MapTileRenderer` owns widget records and consumes only `MapTileRenderQueue` |
| Platform tile loader cadence | Timer behavior is still coupled to platform renderer | Map runtime owns loader cadence and render queue updates |
| Linux downloader/cache | This phase does not rewrite Linux runtime downloader | Linux cache is adapted behind source/cache ports |
| Route/tracker overlays | Explicit non-goal | Later map overlay phase defines overlay runtime ownership |

## Decision

Phase 7.11 makes the tile render plan and decoded cache owner explicit without changing map projection math or adding overlay features.
