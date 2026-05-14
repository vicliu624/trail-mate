# Map Tile Source / Cache Ownership Audit

## Purpose

Phase 7.10 establishes ownership for map tile source, path resolution, and tile availability lookup.

The first burn-down slice moves Trail Mate offline tile directory mapping behind map tile source / resolver objects. It does not rewrite LVGL tile drawing or decoded image cache behavior.

## Current Event / Data Sources

| Question | Current answer before 7.10 | 7.10 owner |
| --- | --- | --- |
| Where is base tile path built? | `platform/*/ui/widgets/map/map_tiles.cpp` | `MapTileResolver` / `LegacyFilesystemMapTileSource` |
| Where is contour tile path built? | `platform/*/ui/widgets/map/map_tiles.cpp` | `MapTileResolver` / `LegacyFilesystemMapTileSource` |
| Who checks base tile availability? | Platform map tile runtime | `IMapTileSource::lookup(...)` |
| Who checks layer directories? | Platform map tile runtime | `LegacyFilesystemMapTileSource` |
| Who draws tile widgets? | LVGL map tile runtime | Still LVGL map tile runtime |
| Who owns decoded LVGL image cache? | ESP `map_tiles.cpp` global decode cache | Still contained legacy |
| Who owns Linux downloaded tile cache? | `platform::linux_runtime::MapTileCache` | Still contained legacy |
| Does `MapWorkspaceSnapshot` carry bitmap/cache objects? | No | Must remain no |

## Trail Mate Offline Directory Mapping

| Layer | Path |
| --- | --- |
| OSM | `/maps/base/osm/{z}/{x}/{y}.png` |
| Terrain | `/maps/base/terrain/{z}/{x}/{y}.png` |
| Satellite | `/maps/base/satellite/{z}/{x}/{y}.jpg` |
| Contour major 500 | `/maps/contour/major-500/{z}/{x}/{y}.png` |
| Contour major 200 | `/maps/contour/major-200/{z}/{x}/{y}.png` |
| Contour major 100 | `/maps/contour/major-100/{z}/{x}/{y}.png` |
| Contour major 50 | `/maps/contour/major-50/{z}/{x}/{y}.png` |
| Contour major 25 | `/maps/contour/major-25/{z}/{x}/{y}.png` |

## Boundary

`MapTileRef` identifies a tile by layer, zoom, x, and y. It does not contain a filesystem path.

`MapTileResolver` owns Trail Mate tile path mapping.

`LegacyFilesystemMapTileSource` owns filesystem-backed lookup and optional reads through `IMapTileFileSystem`.

Platform LVGL tile runtimes may continue to:

- calculate visible tiles
- create LVGL image objects
- draw placeholder cards
- run loader ticks
- keep current decoded image cache until a later cache hardening phase

Platform LVGL tile runtimes must not own Trail Mate directory mapping directly.

## Current Remaining Legacy

| Surface | Reason | Exit condition |
| --- | --- | --- |
| ESP decoded LVGL tile cache | Phase 7.10 first slice does not rewrite decoded image lifetime | Introduce a formal decoded tile cache owner or renderer-owned image lifecycle spec |
| Linux `MapTileCache` downloader | It is a Linux runtime cache/downloader, not just a filesystem source | Adapt it behind a stable `IMapTileSource` or rename as runtime tile cache adapter |
| LVGL tile object records | Renderer still owns widget records and loading cadence | Future map renderer/runtime split defines a tile render queue |
| uConsole workspace tile path fields | Linux workspace smoke still exposes paths for GTK map view | Future uConsole adapter consumes `MapTileRef` plus source lookup |

## Decision

Phase 7.10 moves tile path and availability ownership out of renderer-local path construction and into map tile resolver/source adapters.

It does not move bitmap data into `MapWorkspaceSnapshot`, and it does not introduce online download behavior.
