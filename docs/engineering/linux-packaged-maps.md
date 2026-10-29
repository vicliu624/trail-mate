# Native Linux TMAP rendering

The native map tile port now uses the existing `TmapMapTileSource` for packaged
rasters and annotations. It keeps the shared P4 viewport, annotation layout and
POI overlay. The platform supplies file access, aligned buffers, an owned worker
and LVGL image lifetime management. There is no CM0 board identity, display size,
device node or mount path in the package reader.

## Storage and source selection

`PackagedMapSession` receives the configured SD root. Its storage adapter reuses
the rooted `TmapFile` and bounded `TmapCatalog` introduced for native navigation.
Display packages use `maps/tmap/{osm,terrain,satellite}` and `maps/tmap`, matching
the production ESP layout. Logical package paths retain the shared catalog's
192-byte capacity; the physical root is not concatenated into that buffer.

The worker finishes catalog preparation before selecting a source mode. An empty
valid catalog selects the legacy image-directory implementation. Any admitted
TMAP packages select TMAP for the whole session. A missing tile in that mode
does not fall back to legacy files or start a network/DEM download. A corrupt
catalog reports an error instead of silently changing formats. Existing legacy
PNG/cache/generation behavior remains available when no TMAP packages exist.

File descriptors remain tied to the configured media root and named file inode.
The worker monitors Linux file events and root/file identities at bounded
intervals. Creation, replacement, deletion, repair and directory/media changes
invalidate the catalog and cached results. Directory timestamps alone are not
used as a file-inventory change detector. An unavailable event-watch descriptor
reports storage failure and retries; it cannot authorize legacy fallback.

## Worker, queues and pixels

Each viewport owns its session. UI calls only synchronize small metadata and
bounded request/cache bookkeeping. The worker performs catalog traversal,
coverage checks, CRC-checked pixel reads and resumable annotation reads. It does
not call LVGL. There are at most 192 cache/queue entries, covering the native
48-record viewport policy's base, major/minor contours and annotations. Queued
requests are deduplicated, completed entries use LRU eviction, and cancellation
invalidates earlier work through a generation counter.

The shared `NativePixelBuffer` counts allocations held by both queued results
and displayed images. Its process budget is 32 MiB; a constructor parameter can
impose a smaller ceiling. Allocation pressure produces bounded retries. RGB565
and RGBA tile data are not copied into a second LVGL pixel allocation. RGBA is
converted once to the LVGL byte order, with transparency retained. The existing
contour profile selector chooses the major/minor layers; minor draws before
major. No second zoom-profile table is introduced.

An LVGL image owns a descriptor and a shared payload reference. Its delete
callback drops the image cache before releasing that reference. Clearing a
viewport removes image objects, cancels queued work and releases payloads. Its
final context release joins the owned worker. A consumer that deliberately
retains a payload can outlive the session without a dangling pixel allocation.

## Shared annotations

TMAP annotations arrive as the existing bounded `TileHeader`/`Record` payload.
The worker completes resumable reads and validates the result before publication.
The UI projects records and road paths into viewport coordinates, then uses the
shared `AnnotationFrame` layout and `PoiOverlay` view. Stable feature identities,
typed road/place/POI records, priorities, collision handling and view compatibility
stay in those shared modules. The viewport allocator keeps its original PSRAM
branch on ESP and uses native heap allocation on Linux.

## Verification

`make_linux_packaged_map_fixture.py` extends the pinned SDK/schema search fixture
with real RGB565/RGBA raster sections, major/minor alpha contours, POI and road
annotations, and road geometry. It is test tooling and is not included in the
application or Debian runtime dependencies.

The native regression verifies wire pixels and alpha, catalog coverage, cache
reuse, missing/error states, annotation kinds/geometry, cancellation, retained
pixel lifetime, a smaller memory ceiling, package removal/restoration, corrupt
pixel CRC rejection and repair. A sparse file also verifies reading at a 3 GiB
offset through the real file adapter without allocating a 3 GiB payload.

The LVGL/SDL regression checks actual framebuffer pixels for the base, both
transparent contour layers and their order, RGBA source switching and repeated
cached conversion. It uses the host's real RGB565 output quantization. It also
checks projected/shared annotation layout and zero remaining pixel allocations
after image/context destruction. All three focused native tests passed.

The complete native suite passed 131/131 and Cardputer Zero passed 14/14. Both
ARM regressions passed on CM0, including the sparse 3 GiB offset read and zero
remaining pixel allocations after context release. The standard armhf deb passed
Lintian without diagnostics, installation/integrity checks and original-logo
verification. The P4 application rendered at 1232 x 568.

The installed P4 Route Preview reads the isolated fixture through TMAP, displays
its synthetic base and route detour, and shows the ASCII variant's `East Road`
label and `West Camp` POI. Injected direction keys pan the map and annotations.
Alt+F4 from that active fullscreen map exited with code 0 in 0.584 seconds. This
does not validate the physical key matrix or touchscreen. Unicode data is covered
by the original fixtures; Chinese glyph rendering was not validated because
the isolated profile has no CJK font pack. The fixture generator's optional
`--ascii` mode only changes test labels and leaves application code unchanged.

Exact artifact and installed executable hashes are recorded in the
[CM0 integration ledger](t-display-cm0-hardware-integration.md). Real cartographic
dataset coverage/performance and legacy annotation imports require separate
evidence; flat fixture colors are not a real-world map benchmark.
