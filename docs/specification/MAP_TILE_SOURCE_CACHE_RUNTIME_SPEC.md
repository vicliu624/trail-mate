# Map Tile Source / Cache Runtime Spec

## Rule

Tile lookup and cache ownership belong to map runtime/source adapters.

Renderers draw tiles. They do not own Trail Mate tile storage paths.

`MapWorkspaceSnapshot` carries viewport, layer, and tool state. It must not carry tile bitmap bytes, decoded image cache objects, or filesystem cache objects.

## Objects

| Object | Pattern | Responsibility | Forbidden |
| --- | --- | --- | --- |
| `MapTileRef` | Value Object | Identify layer/z/x/y | Filesystem path, LVGL object |
| `MapTileLayer` | Layer key | Identify OSM/Terrain/Satellite/Contour layers | Renderer details |
| `MapTileFormat` | Format enum | Identify PNG/JPEG payload format | Decode ownership |
| `MapTilePayload` | Data DTO | Optional byte payload returned by a source | Filesystem path ownership |
| `IMapTileSource` | Port | Lookup/read tile payload by `MapTileRef` | LVGL rendering |
| `IMapTileFileSystem` | Port | Filesystem operations needed by legacy source | Tile path policy |
| `IMapTileCache` | Repository / Cache port | Future cache contract | UI rendering |
| `MapTileResolver` | Strategy | Resolve `MapTileRef` to Trail Mate storage path | LVGL, filesystem reads |
| `LegacyFilesystemMapTileSource` | Anti-Corruption Adapter | Wrap current filesystem tile lookup | Spread path mapping back to renderer |

## Directory Mapping

`MapTileResolver` maps:

- `MapTileLayer::Osm` -> `/maps/base/osm/{z}/{x}/{y}.png`
- `MapTileLayer::Terrain` -> `/maps/base/terrain/{z}/{x}/{y}.png`
- `MapTileLayer::Satellite` -> `/maps/base/satellite/{z}/{x}/{y}.jpg`
- `MapTileLayer::ContourMajor500` -> `/maps/contour/major-500/{z}/{x}/{y}.png`
- `MapTileLayer::ContourMajor200` -> `/maps/contour/major-200/{z}/{x}/{y}.png`
- `MapTileLayer::ContourMajor100` -> `/maps/contour/major-100/{z}/{x}/{y}.png`
- `MapTileLayer::ContourMajor50` -> `/maps/contour/major-50/{z}/{x}/{y}.png`
- `MapTileLayer::ContourMajor25` -> `/maps/contour/major-25/{z}/{x}/{y}.png`

The resolver may prepend a target root prefix such as `A:` or a Linux SD root.

## Phase 7.10 First Slice

Phase 7.10 introduces:

- `MapTileRef`
- `MapTileLayer`
- `MapTileFormat`
- `MapTileStatus`
- `MapTilePayload`
- `MapTileLookupResult`
- `IMapTileSource`
- `IMapTileCache`
- `MapTileResolver`
- `LegacyFilesystemMapTileSource`

Existing platform map tile runtimes may keep LVGL drawing and decoded image cache logic temporarily, but path mapping must flow through `LegacyFilesystemMapTileSource`.

## Non-Goals

Phase 7.10 does not:

- implement online tile download
- change Trail Mate offline tile directory structure
- store tile bitmaps in `MapWorkspaceSnapshot`
- implement a full LRU cache
- redesign Map overlay
- move route/tracker ownership
- generate contour tiles
- change Trail Mate Center
- change UX Pack
- change repository layout
