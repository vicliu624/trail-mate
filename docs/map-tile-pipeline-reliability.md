# Map tile request and rendering lifecycle

This document describes the shared map pipeline, the boundaries between its
components, and the tests that protect those boundaries. Storage-specific
transfer policy is documented in [SD transport optimization](sd-transport-optimization.md).

## Request flow

The ESP integration is in
[map_tiles.cpp](../platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp).

1. `tile_loader_step()` selects a visible tile without an image, a confirmed
   missing flag, or an outstanding request. Its retry deadline must have elapsed.
2. `request_base_tile_async()` submits it through `MapTileAsyncHost`.
   An accepted or duplicate request supplies a generation and command ID;
   these are stored with the tile's pending flag.
3. `MapTileCommandQueue` records the request in one of 16 slots. A duplicate
   tile in the same generation reuses the existing ID. Popping a command
   changes its slot from queued to in flight; it does not free the slot.
4. The single worker reserves a completion slot before reading. If no slot
   is available, it retains the command and waits without a filesystem lock.
5. The worker reads the source and publishes a ready, failed, or retry result.
   The completion owns its payload. Payload allocation failure produces a
   failed result rather than silently dropping the request.
6. The UI dequeues the result. `popEvent()` or `popEventIf()` completes the
   matching command slot, making it available for another request.
7. For a current result, `apply_map_tile_event()` checks the tile's generation
   and ID, clears pending, and either decodes/renders or sets failure/retry
   state. Maintenance releases obsolete results.

For example, request `(generation=2, id=17)` still occupies its command slot
after the read finishes. The slot is released on result consumption or
generation cancellation. A late result for ID 17 cannot clear the pending
state of a replacement request with ID 18.

## Component responsibilities

| Code | Responsibility |
| --- | --- |
| [map_tile_request_queue.h](../modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_request_queue.h) | Fixed request slots, duplicate detection, queued/in-flight state, matching completion |
| [map_tile_completion_queue.h](../modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_completion_queue.h) | Result reservations, cancellation and stale reservation tokens |
| [owned_map_tile_event.h](../modules/ui_map_runtime/include/ui_map_runtime/map_tiles/owned_map_tile_event.h) | Move-only payload ownership |
| [ESP queue adapters](../platform/esp/arduino_common/include/platform/esp/arduino_common/map_tiles) | FreeRTOS synchronization and capacity notifications |
| [map_viewport.cpp](../modules/ui_shared/src/ui/widgets/map/map_viewport.cpp) | Gesture lifecycle, preview/committed view, loader timer |
| [sd_card_runtime.cpp](../platform/esp/arduino_common/src/storage/sd_card_runtime.cpp) | File access, synchronization and transport policy integration |

Map request/result handling is shared by WIO, T-Deck and Pager. Shared-SPI
and SDMMC decisions remain in storage. Allocation/free and file I/O occur
outside queue locks. Payload ownership moves from producer to queue to UI.

## Capacity, cancellation and retries

| Condition | Behavior |
| --- | --- |
| Command slots are full | Submission returns backpressure; UI retries later |
| Completion slots are full | Worker waits before file I/O; it does not read and then drop the result |
| User is dragging | Maintenance removes obsolete results; valid results wait for rendering to resume |
| Result belongs to an invisible tile or outdated request | Maintenance releases its payload and clears matching pending state |
| Generation is cancelled during a read | Publication is invalidated; the hardware read itself is not forcibly aborted |
| Producer still owns a cancelled reservation | Slot is retained until acknowledgement; reservation epochs reject stale tokens |
| Last viewport closes | Active generation is cancelled; a capacity-blocked worker is woken for shutdown |
| File access is busy or fails | Deliver retry/error; do not treat this as proof that the file is absent |

Ordinary panning does not create a new generation or immediately cancel all
old queued reads. Outdated completions are cleaned up by visibility/request
checks. There is no automatic request-expiration timer.

After a failed open, storage probes the path under the same filesystem lock.
Successful opens do not perform this extra directory scan. Probe scratch is
allocated lazily with PSRAM preference, not on the ESP task stack. SDMMC
retains the first block I/O error for the operation even if a later metadata
read succeeds; SPI uses its scoped card-error state.

A corrupt FAT long-name checksum or unsupported path form produces uncertain
evidence, not a confirmed missing file. An explicit end-of-directory marker
is needed to prove absence; full directories without it remain uncertain.
POI manifest/index errors remain retryable rather than permanently marking
the package as checked and unavailable.

## Gesture handling and UI scheduling

`loader_timer_cb()` runs maintenance before checking the gesture flags.
Maintenance may discard obsolete results, but cannot start file I/O, decode
images or update LVGL objects. Rendering pauses while any of these are set:

```cpp
impl->gesture_pressed || impl->gesture_dragging || impl->drag_preview_active
```

During dragging, the viewport translates existing objects for preview.
On release/cancellation, the map callback finishes the gesture and requests
a committed viewport update. `apply_model()` clears preview state; subsequent
loader steps can consume valid results and render tiles again.

The map's object-level event handler uses `lv_event_stop_bubbling(e)`, not
`lv_indev_stop_processing()`. In LVGL 9.4, setting the input-device stop flag
inside an object `PRESSING` callback can suppress delivery of a later
`RELEASED` event. The map would then remain marked as dragging and keep
skipping the loader. Stopping only current-event bubbling preserves release
delivery. The gesture surface is already non-scrollable.

The loader applies at most one image event per drain, with a 60 ms cooldown
and a 4 ms admission budget. Failed/control events do not consume the image
quota or cooldown. The worker retains its 32 ms spacing. The admission
budget cannot interrupt a synchronous PNG decode already running.

## Diagnostic output

`TRAIL_MATE_MAP_DIAGNOSTICS=1` explicitly enables detailed `[MAPD]` records;
it is disabled by default. Arduino builds write these through `Serial`
rather than assuming libc stdout and USB CDC are the same channel.
Shared UI calls only the `platform/ui/map_diagnostics.h` contract. Output
selection belongs to the Arduino, ESP-IDF and Linux platform implementations;
the shared diagnostic header contains no SDK includes or platform switches.

| Record | Meaning |
| --- | --- |
| `heartbeat` | Every two seconds, including during a rendering pause: gesture flags, valid focus, zoom and pan |
| `gesture-end`, `commit-view` | Release/cancellation followed by committing the viewport |
| `submit` | Tile/layer, generation, request ID and submission status |
| `worker-start`, `worker-end` | Start/end of a worker execution |
| `worker-capacity-wait` | Worker is waiting for result space without a file lock |
| `consume`, `discard`, `pending-clear` | Result delivery, obsolete-result cleanup and matching pending state |
| `decode-fail`, `event-fail`, `render` | Failure stage or image-object binding result |
| `tile`, `viewport` | Visibility, image/hidden state, request ID, retry deadline, actual/projected positions and cache references |
| `queues` | Queued/in-flight counts, result occupancy, pressure, consumption/render counts and free heap |

Only interpret queue counts when `cmd_ok`/`result_ok` are true. `inflight`
includes results awaiting consumption, not just a currently executing read.
A successful `render` record does not prove panel refresh completion.

For a stall, first check whether gesture flags return to zero after release.
Then compare queue occupancy with capacity, and match IDs across submission,
worker execution and UI consumption. If results are consumed but tiles are
not visible, compare positions/visibility across commit and eviction, and
inspect decode errors. Do not infer a full queue from a frozen image alone.

Serial output adds execution time. Disable detailed diagnostics before
throughput comparisons. Heartbeats and capacity-wait records are rate-limited;
lifecycle records are per request or commit, not per SD sector.

### Timing summaries

The separate `[GPS][MAP][perf]` summary uses the libc console and is emitted
by the idle loader at most every five seconds when results have been consumed.

| Field | Measurement |
| --- | --- |
| `cmd` | Submission to successful execution attempt, including completion-capacity waiting |
| `worker` | Source work plus payload allocation/copy up to publication |
| `lock`, `open`, `read` | File stages; subsets of worker time, not extra times to add to it |
| `result` | Payload-ready timestamp to dequeue, including gesture pauses |
| `decode` | Decode/cache preparation, including failed attempts |
| `apply` | LVGL update/bookkeeping, not physical display completion |

Times are integer milliseconds, average/maximum. `file_n` counts measured
file accesses; source-only results do not dilute those averages. `consumed`
includes obsolete-result cleanup; `rendered` counts successful raster
applications. Queue high water and reservation-pressure counts are lifetime
values; timing/submission summaries are interval values.

SDMMC may additionally emit `[GPS][MAP][block]` with read-call count, sectors,
largest batch and hardware-call time. Failed attempts are included, so these
are not successful-throughput measurements. Shared SPI does not fabricate
equivalent counters when the adapter cannot supply them.

## Regression tests

```sh
cmake -S tests/map_tile_pipeline -B .codex-build/map-tile-pipeline
cmake --build .codex-build/map-tile-pipeline
ctest --test-dir .codex-build/map-tile-pipeline --output-on-failure
python tests/map_tile_pipeline/test_ui_contract.py

cmake -S tests/map_gesture -B .codex-build/map-gesture -DLVGL_DIR=/path/to/lvgl-9.4 -DCMAKE_BUILD_TYPE=Debug
cmake --build .codex-build/map-gesture
ctest --test-dir .codex-build/map-gesture --output-on-failure
```

The pipeline tests cover bounded requests/results, reservation exhaustion
before I/O, duplicates, cancellation/publication races, stale tokens,
allocation failure, source errors, timing aggregation and POI retries.
The ESP queue adapter is production code; RTOS synchronization is replaced
by host primitives.
Diagnostic tests compile the Arduino output adapter with a host serial stub,
check formatted output when enabled, and verify that disabled diagnostic
arguments are not evaluated. UI source checks protect the platform boundary.

The UI source checks guard maintenance-before-pause ordering and prohibit
board/transport branches in the map pipeline. The real-LVGL gesture test
compiles the production callback and feeds pointer samples through LVGL;
it does not invoke the release callback directly. A tap and 100 repeated
drags, including movement outside the surface, must clear gesture state and
emit exactly one end/cancellation per drag.

These tests do not emulate the touch controller or panel scanout, and do not
measure real-card throughput. Device validation should cover repeated pans,
returning to earlier regions, zoom changes and closing/reopening the map.
