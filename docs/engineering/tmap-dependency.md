# Released TMAP dependency and navigation integration

Native navigation is integrated in the working firmware sources. See
[Native saved-route navigation](native-navigation.md) for the current acceptance
matrix, measured pipeline, UI/session tests and target/hardware evidence. The device audit below
records the KML implementation before that work; it is not the current completion
status of the native-route branch.

Trail Mate consumes the independent [TMAP SDK](https://github.com/vicliu624/tmap)
as a third-party dependency. The application owns storage adapters, UI policy,
GPS integration and task scheduling. SDK readers and algorithms are not maintained
as a second copy under `modules/core_tmap`.

## Locked release

- Version: `0.2.0`, experimental pre-release.
- Commit: `d74b084acb61653149dec1ac161df58a92278cff`.
- Published source archive SHA-256:
  `50cda6657a184587a1901aba965b885b335d7bc332793fcbd90cbab2bcfce371`.
- License/provenance: the pinned historical package declares AGPL-3.0-only and
  retains its LICENSE, COPYRIGHT and NOTICE. Eligible Liu Weikai (Vic Liu)-owned
  originals also have the [additional MIT grant](../../LICENSES/TMAP-MIT.txt)
  recorded in the [relicensing scope](../licensing/TMAP_RELICENSING.md).
  Third-party terms and valid prior grants remain applicable; the package pin
  and its original metadata are unchanged.

PlatformIO declares the immutable Git dependency in `platformio.ini` and
`modules/ui_map_runtime/library.json`. CMake's `cmake/TrailMateTmap.cmake`
downloads the release archive through FetchContent, verifies its hash and exposes
`tmap::tmap` to ESP-IDF, Linux and native test consumers. Normal builds do not
depend on a sibling source checkout. The documented FetchContent source override
is an explicit offline-development option, not the release configuration.

Application builds disable SDK tests, examples, host producers and desktop file
adapters, and enable the bounded-stack/no-exception configuration. The upstream
PlatformIO manifest excludes both desktop file-adapter implementations. Device
filesystem and external-memory adapters remain application code.

## What the SDK upgrade provides

The reader supports Center's native map containers, including display/search
sections and selected outdoor sections in the same file. DEM selection can follow
display zoom while analytical queries retain the finest stored grid.

The SDK also provides bounded leg sinks, temporary leg spools, self-contained
`.tmroute` files and on-file spatial/distance indexes. These interfaces make it
possible to release search/map memory before displaying a saved route. Updating
the dependency does not connect those interfaces to the device's existing KML UI.

See upstream [persistent-route API](https://github.com/vicliu624/tmap/blob/v0.2.0/docs/PERSISTENT_ROUTES.md)
and [20 km resource evidence](https://github.com/vicliu624/tmap/blob/v0.2.0/docs/ROUTE_ARCHIVE_EVIDENCE.md).
The SDK's 1,768 B saved-route reader measurement is not a measurement of Trail
Mate's current navigation implementation or of complete firmware RAM.

## Pre-native device navigation audit, 2026-10-15

The following rows describe current working-source wiring, not physical-device
acceptance. The full native route-archive workflow remains an integration task.

| Stage | Current implementation | Remaining work |
| --- | --- | --- |
| Endpoints | Tracker navigation dialog uses a current GPS fix or searched origin and a searched destination | Preserve named endpoints and routing policy with a native saved-route collection |
| Calculation | ESP Arduino worker runs bounded coordinate routing in PSRAM; explicit failure/cancellation | Integrate sink output, tested long-route budgets and suitable heuristics |
| Persistence | Streaming KML writer creates a pending file, checks flush and renames on save under `/routes/navigation` | Add native archive/spool file adapters, finalize/publish/recovery and archive metadata |
| Selection and activation | Saved KML list, preview, active-route configuration and manual stop | Add `.tmroute` list/load/delete and source-identity policy |
| Map drawing | Existing KML overlay uses a reduced set of at most 96 points | Query the saved route by viewport and clip candidates without losing detailed geometry |
| GPS progress | KML scan computes route position, remaining distance and ambiguity; independent scans gate estimated arrival | Adapt matching to bounded archive windows and test loops, intersections and stale fixes |
| Deviation | Full KML matching supplies distance bounds and an off-route notice | Explicit deviation/recovery policy and bounded manual/automatic replanning |
| Guidance and finish | Remaining-distance/progress and estimated route-end notices; manual stop exists | Maneuver lookahead/cues and an explicit completed-session transition |

`navigation::RouteCalculation` currently selects `HeapDijkstra`, defaults to
4,096 search states, 2,048 output legs, a 160 KiB search-storage limit and 50,000
expansions. It retains the leg array and search storage through geometry export.
Those defaults are not the SDK acceptance's integer ALT configuration and do not
establish support for the real 20 km London/Kunming requests. Increasing a limit
alone is not sufficient evidence of useful SD latency or available board PSRAM.

The KML navigation matcher scans complete source geometry in bounded chunks;
the display's reduced 96-point overlay is not the geometry used for that match.
It does not use the new archive's spatial index. Existing KML following should
remain available while native routes acquire their own bounded adapter path.

The runtime implementation is guarded by `ARDUINO_ARCH_ESP32` and requires PSRAM.
Other platforms currently receive unsupported stubs. The portable SDK's platform
independence does not establish firmware navigation parity across targets.

GitNexus reported an index 193 commits behind HEAD. The required `npx gitnexus
analyze` refresh was attempted but exited with code 1 after parser-worker timeout
retries, including the GPS runtime. The CMake helper and new route-calculation
symbols were absent from the available graph. This audit therefore uses direct
source inspection and the native regression suite; an empty graph result is not
treated as proof that an integration has no callers. This upgrade changes release
pins and documentation, not application functions or navigation state machines.

## Integration validation

`tests/tmap` uses the hash-verified published release. Its dependency contract
checks that CMake and both PlatformIO pins agree and that the copied reader/search
implementation is absent. Native tests cover administrative paths, released SDK
interfaces/external-memory refusal, platform-link isolation, coordinate routing,
Busy/cancellation and generated KML semantics.

Optional world/China inputs exercise the application map source: pixel reads,
annotations, search, media sessions, Busy, corruption, capacity and ownership.
Use the current Center SD delivery paths, not removed artifact locations:

```sh
cmake -S tests/tmap -B .codex-build/tmap-dependency -DCMAKE_BUILD_TYPE=Release \
  -DTRAILMATE_TMAP_WORLD_MAP=/data/sd/maps/tmap/osm/world-osm-z0-7.tmap \
  -DTRAILMATE_TMAP_CHINA_MAP=/data/sd/maps/tmap/osm/china-osm-z8-12.tmap
cmake --build .codex-build/tmap-dependency
ctest --test-dir .codex-build/tmap-dependency --output-on-failure
```

Large map datasets are not vendored or downloaded by the dependency helper.
The 2026-10-15 upgrade configured and built against the downloaded SDK 0.2.0
release, whose archive hash was checked again independently. All **10/10** native
CTest cases passed, including the current Center-produced world and China files
under `artifacts/sd-v2/maps/tmap/osm`. The real map-source probe retained a
14,024 B Workspace, 120 B Reader and 25,368 B two-package catalog payload on the
32-bit MinGW GCC 9.2 ABI. These are host typed sizes, not whole-device peaks.

The reproducible local evidence is retained in
`.codex-build/tmap-dependency/integration-0.2.0.{log,xml,json}`. Results of an
earlier SDK's firmware build must not be presented as validation of this release;
a full target build and physical navigation acceptance remain separate.

## Historical target evidence

An earlier `tlora_pager_sx1262` PlatformIO build on 2026-10-15 used SDK 0.1.1 and
reported 97,356 B static RAM and 4,662,485 B Flash. Those linker totals belong to
that source/configuration and are not the upgraded firmware's resource figures.
No new target build, flash, RAM/PSRAM peak, task-stack watermark, SD latency or
power measurement is implied by changing the dependency pins.
