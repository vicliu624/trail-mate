# On-device TMAP creation

CM0 and Cardputer Zero expose **Extensions → Make TMAP** directly in the bottom
action bar, without opening Menu. It also opens directly with **T** while on
Extensions; the button displays
**T Make TMAP** and H Help lists the shortcut. Text editors keep literal T input.
The shared LVGL wizard uses five pages: global administrative area, map content,
save name, review, and progress/result. This follows Center's area-first authoring
flow. Administrative search is the default; Coordinates switches to explicit
WGS84 bounds and back. Results show country and administrative context to
distinguish namesakes. Only actual boundary extents are accepted; point-only
geocoding results are not turned into invented administrative boundaries.

Enter a place name and press Enter or activate Search. Tab moves focus and Enter
activates the selected control. Map name, filename and output defaults are
prefilled after selecting an area. Advanced contains the stable package key,
revision, output budget and optional local PBF input. Balanced adjusts zoom for
the chosen area; Overview, Walking, Detailed and custom zoom remain available.
Read-only panels scroll with arrow keys. Closing the page leaves a job running;
Cancel stops it. Reopening shows the job, and a completed job offers View map
and New map. Before the first source bytes arrive, the job shows the elapsed
wait for the map server; it then reports received KiB and later producer stages.

## Sources and outputs

The normal flow downloads OSM data for the selected enclosing rectangle into its
private job workspace. The installed `osmium-tool` converts that XML stream to
PBF before the existing Center producer runs. A user does not need to obtain or
copy a PBF first. Downloading, conversion, production, verification and installation
all run away from the UI thread and can be cancelled. Network or producer errors
remain visible and do not publish an incomplete map.

Global administrative lookup uses the [Photon API](https://github.com/komoot/photon/blob/master/docs/api-v1.md).
Searches are explicit, serialized and spaced by at least a second, with a bounded
cache of successful queries. A new search cancels obsolete work; an older response
cannot replace the latest results. `TRAIL_MATE_AREA_SEARCH_URL` can select another
compatible deployment without rebuilding. Map acquisition uses an Overpass
interpreter, currently defaulting to the [Private.coffee public instance](https://wiki.openstreetmap.org/wiki/Overpass_API#Public_Overpass_API_instances);
`TRAIL_MATE_MAP_SOURCE_URL` can select another interpreter. The original default
returned timeouts and HTTP 502 during device validation. Attribution
is shown in Review. Public services can be unavailable or throttle requests; the
UI exposes retry and coordinate entry instead of treating a failure as a result.

Online jobs on the small device are limited to 2500 square kilometres and a
64 MiB source response. Larger selections must be reduced or use an appropriate
local PBF through Advanced. The default map output budget is 128 MiB and is
independent of the source response limit. The estimate also checks working space.
Bounds use WGS84 and must not cross the antimeridian.

Optional local `.osm.pbf` files are scanned beneath `map-sources` or `maps/sources`
in the configured storage root. `TRAIL_MATE_TMAP_SOURCE_ROOT` remains available
for a development source directory. Advanced lists these inputs alongside Online
download; local-file scans remain asynchronous.

OSM, terrain and satellite layers use Center's existing PBF rendering and raster
sources. Terrain and satellite may fetch source data over the system network.
Their source cache belongs to the private job workspace and is deleted with it
after success, failure or cancellation. POI, search and independent labels are retained in the
authoritative OSM package, including when only another raster layer is requested.
The current wizard produces Center's display/search TMAP profile; the separate
experimental navigation/region-production CLI and contour production are not
new wizard modes.

Completed files use the existing `maps/tmap/osm`, `terrain` and `satellite`
layout. Referenced font resources are installed before their maps. The normal map
catalog's existing directory/file watcher discovers newly published maps. Existing
map filenames are refused; select another name instead of silently replacing a
map. Keep a package key stable for updates to the same region and choose its
revision explicitly.

Search for a small city or district, select the correctly disambiguated result,
choose OSM and Balanced, then review and create. A manual example uses West
`102.70`, South `25.00`, East `102.71`, North `25.01`; those numbers are optional
input, not a default inferred location. After success, return to Map → Map Layer
and select the produced base layer, move to the covered region and use a zoom
in its produced range. The completed job also offers View map, which opens the
existing map renderer at the generated area's centre and zoom, with Back returning
to the result. No manual output copy or separate import is required.

Only the original PBF and final map resources need to remain after a normal job.
Final output includes the `.tmap` files and any referenced resources under
`trailmate/packs/fonts`; deleting those fonts can break map labels. Plans,
temporary indexes, raster downloads and staging output belong to the private
workspace and are removed after success, failure or cancellation. Center's geometry
and font-summary JSON reports are not installed as map payloads. The app installation/runtime, its regular settings and unrelated map
caches are separate from creator intermediates.

Interrupted jobs are recovered automatically on service startup. Producer work
and publication staging both live beneath the configured storage root's private
`trailmate/.tmap-jobs/job-XXXXXX` directories. The namespace has a checked ownership
record and a process lease. Recovery only removes job directories in that marked
namespace, traverses relative to directory descriptors without following symlinks,
and never scans input PBF or final map directories for deletion. It preserves
unknown siblings and refuses unmarked nonempty namespaces. A small `.owner`
control file remains; it is not a retained rendering cache.

The producer inherits the lease. If the app dies while its producer is still
alive, a newly started service waits and retries recovery every two seconds until
that process releases the lease. After power restoration the lease is released by
the OS and abandoned work is recovered on startup. Files already published as
final maps/fonts remain intact; recovery does not roll back published outputs.
Legacy unmarked cache folders or generic `.creating` files from older versions
are not blindly deleted because their ownership cannot be proved. This version
no longer creates generic staging files beside final maps.

Estimates count uncompressed RGB565 tiles and conservative working space. Indexes,
font resources, geometry complexity and actual source processing affect final
size and duration; deduplication can reduce pixels. The producer enforces output
and free-space budgets again before processing. A failed/cancelled job discards
its private workspace and rolls back only files created by that job.

## Ownership and reuse

The application does not fork the TMAP writer, OSM importer, renderer or search
index format. `LinuxTmapCreation` invokes the existing Center `TmapTool` in a
separate process, using a captured PBF descriptor, a private JSON plan, copied
progress and fixed build/verify commands. Center Core owns production and format
verification; the existing independent TMAP SDK remains the device reader.

The portable `tmap_creation::Service` seam contains plans/status only. Composition
registers the Linux worker for CM0's `tmap_creation=true` profile and Cardputer's
startup. Unsupported ESP32/nRF targets have no registered producer, hidden entry
and no producer thread/runtime dependency. Other Linux compositions must opt in.
All filesystem scans, rendering, copying and child-process control run off the UI
thread. One job owns its state, and storage publication uses descriptor-rooted
I/O, an exclusive storage lease and no-replace publication.

## Producer packaging

Publish the existing Center project for the target; do not copy its source into
the firmware repository:

```powershell
./tools/publish_tmap_producer.ps1 -CenterRoot ../trail-mate-center `
  -OutputDirectory ./build/tmap-producer/linux-arm -Runtime linux-arm
```

Use `linux-arm` for CM0, `linux-arm64` for the Cardputer package builder, and
`linux-x64` for native integration tests. The self-contained output includes its
runtime, Skia assets, Center license and a producer/hash manifest. CMake's
`TRAIL_MATE_TMAP_PRODUCER_DIR` installs it under `/usr/lib/trailmate/tmap-producer`
and checks its architecture. CM0 and Cardputer package metadata include their
distribution's ICU dependency when bundling this producer, and the Cardputer
package depends on `osmium-tool` for online source conversion. No .NET SDK or Center
checkout is required on the device. An explicit `TRAIL_MATE_TMAP_PRODUCER`
executable override is available for development.

The Debian Lintian overrides are restricted to SkiaSharp's bundled raster codecs
and the published SQLite native asset's debug symbols. These upstream runtime
binaries are preserved so their hashes continue to match the producer manifest.

The Cardputer `build_cardputer_zero_deb.sh` helper also accepts
`TRAIL_MATE_TMAP_PRODUCER_DIR` and transfers the published bundle to its existing
ARM64 Docker builder. A developer build without a bundle reports an unavailable
producer; it cannot claim a working production creator.

## Validation

Native contract cases cover invalid plans, budget refusal, single-job ownership,
producer failure, cancellation, workspace cleanup and preservation of an existing
destination. A small real OSM PBF is processed by the published Center producer;
the resulting map is independently discovered/read by the application's TMAP SDK
and its POI is found through the normal map-search runtime. Both 1232×568 and
320×170 wizard tests cover every step, form preservation, background continuation,
cancellation and unsupported-service behavior. The online fixture also exercises
namesakes, invalid and point-only search results, caching, obsolete searches,
source download/conversion, HTTP failures, response limits and cancellation. Its
successful output is read and searched through the ordinary device SDK.
Release/device results are recorded
in the CM0 acceptance audit after those checks run.
