# Phase 7.10 Map Tile Source / Cache Burn-down Report

## Decision

Phase 7.10 moved Trail Mate map tile path mapping and filesystem-backed availability lookup into explicit map tile source/resolver objects.

This was a tile source/cache ownership slice. It did not rewrite LVGL tile rendering or decoded image cache behavior.

## Burned Down

| Surface | Result |
| --- | --- |
| Base tile directory mapping in platform map renderer code | routed through `MapTileResolver` |
| Contour tile directory mapping in platform map renderer code | routed through `MapTileResolver` |
| Base tile availability check as direct path policy | routed through `LegacyFilesystemMapTileSource::lookup(...)` |
| Layer directory availability checks as direct path policy | routed through `LegacyFilesystemMapTileSource` |
| Map tile identity containing path assumptions | replaced by `MapTileRef` value object |

## Still Contained

| Surface | Reason | Exit condition |
| --- | --- | --- |
| ESP decoded LVGL tile cache | First slice avoids rewriting decode/image lifetime | Introduce formal decoded tile cache owner or renderer queue |
| LVGL tile object records | Renderer still owns tile widgets | Separate tile render queue from tile source/cache |
| Linux `platform::linux_runtime::MapTileCache` downloader | Linux runtime has downloader/database responsibilities | Wrap or rename as stable runtime tile cache adapter |
| uConsole workspace path fields | GTK/uConsole map view still consumes filesystem paths | Consume tile refs and source lookup instead of direct path fields |

## Checker Changes

- Required map tile audit/spec/report.
- Required map tile types/source/cache/resolver headers.
- Required `LegacyFilesystemMapTileSource`.
- Required resolver/source smoke tests.
- Required platform map tile runtimes to call `LegacyFilesystemMapTileSource`.
- Forbid tile bitmap/cache objects in `MapWorkspaceSnapshot`.

## Remaining Work

| Work | Direction |
| --- | --- |
| Decoded tile cache ownership | Move ESP decoded tile cache behind a formal cache/renderer boundary |
| Linux downloader/cache adapter | Align `platform::linux_runtime::MapTileCache` with `IMapTileSource` / cache ports |
| uConsole path projection | Remove tile path fields from uConsole map workspace snapshots |
| Renderer tile queue | Make renderer consume source/cache through runtime-owned tile queue |
