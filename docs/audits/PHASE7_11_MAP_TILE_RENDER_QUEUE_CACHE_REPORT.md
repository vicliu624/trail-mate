# Phase 7.11 Map Tile Render Queue / Decoded Cache Report

## Decision

Phase 7.11 made the current map tile render plan and ESP decoded tile cache ownership explicit.

This was a burn-down slice, not a renderer rewrite.

## Burned Down

| Surface | Result |
| --- | --- |
| Implicit visible tile plan only in platform `MapTile` records | projected into `MapTileRenderQueue` |
| Missing/loading/ready tile state only as renderer-local flags | represented by `MapTileRenderState` |
| ESP decoded tile cache as loose file-scope slot array | wrapped by `LvglDecodedTileCache` |
| Portable decoded cache boundary missing | introduced `IMapTileDecoderCache` |

## Still Contained

| Surface | Reason | Exit condition |
| --- | --- | --- |
| LVGL tile object records | Existing renderer still creates and evicts widgets | `MapTileRenderer` consumes `MapTileRenderQueue` and owns only widget lifecycle |
| Platform tile loading cadence | Existing timer path still calls `tile_loader_step(...)` | Map runtime owns loader cadence and queue refresh |
| Linux downloader/cache | Not part of 7.11 scope | Adapt behind source/cache ports |
| Route/tracker overlays | Explicit non-goal | Future overlay runtime ownership phase |

## Checker Changes

- Required map tile render queue audit/spec/report.
- Required `map_tile_render_queue.h/.cpp` and smoke test.
- Required `map_tile_decoder_cache.h`.
- Required platform tile runtimes to project `MapTileRenderQueue`.
- Required ESP decoded cache wrapper token `LvglDecodedTileCache`.

## Remaining Work

| Work | Direction |
| --- | --- |
| Dedicated `MapTileRenderer` | Move LVGL widget records behind a renderer object that consumes queue rows |
| Loader cadence ownership | Move timer/source/cache/queue update cadence into map runtime |
| Linux downloader/cache adapter | Wrap Linux downloader behind formal source/cache ports |
| Overlay ownership | Add route/tracker/team overlays only after tile renderer boundary is cleaner |
