# Agenda implementation notes

## Scope and status

The authoritative requirements are in
`docs/specs/Agenda_Calendar_Implementation_Specification.md`.
This is a technical implementation record, not an acceptance report.
The sections below are chronological checkpoints, not the current storage design.
Registration, the main UI, reminder logic and local waypoint save/select have
implementation and native-test coverage. Internal-flash storage is not a supported
Agenda backend. SD lifecycle integration, device validation and final acceptance remain
incomplete. Earlier successful builds do not establish SD-backed acceptance.

### Current storage contract

- Agenda events and saved local waypoints reside on the SD card only.
- No available application-owned SD card means no event/waypoint operations or
  Agenda reminder delivery. Cached rows or scheduled reminders do not provide
  an offline mode.
- Removal or ownership loss invalidates storage readiness and cached results;
  incomplete writes must not be reported successful. Remount requires store
  validation and rebuilding queries/reminders from that card. Do not silently
  save a retained draft to a different card.
- Use the existing shared SD runtime. SPI and SDMMC retain its existing access
  policies; no device-specific Agenda storage path is permitted.
- Internal FFat/LittleFS/NVS are not record-store fallbacks. Agenda must not
  format internal flash or the SD card to recover an error.

The current UI media-loss policy ends the active Agenda editing/navigation
session and discards its unsaved draft, pending UI actions and suspended return
context. An active Map tree is closed before its request storage is released.
An active Agenda returns to the unavailable list; a suspended Agenda is not
opened over another application. Revalidation starts a new session rather than
restoring old-card IDs. Native LVGL tests cover active Map, queued Save and
suspended-editor invalidation on both display geometries. This establishes the
response to a storage-loss signal, not physical card-removal detection.

The SD record adapter binds to the media session validated by its owner. The
shared runtime checks the expected session under its filesystem lock before
opening a file or renaming a staged file. Closing and reopening files within a
store operation therefore cannot implicitly adopt a replacement card. The native
`tests/sd_record_io` fixture compiles the production adapter and injects replacement
at open and publication boundaries. Its simulated runtime verifies adapter token
propagation; it does not prove hardware detection or runtime lock behavior.

Internal-flash references in historical checkpoints below describe superseded
work, not instructions to retain or repair that backend. No SD migration or
on-device success is claimed by this documentation update.

## Map location annotations

Event locations remain part of `/agenda/events.dat`; there is no exported POI
file or duplicate waypoint record. Selecting a saved waypoint copies its location
into the event, so subsequent waypoint edits do not silently move the event.

The target supplies an optional `MapMarkerBinding` through the catalog and Map
route. `IMapMarkerSource` exposes status and a streamed read-only visitor; the
shared map renderer knows neither Agenda storage paths nor recurrence rules.
Other map previews do not opt into this binding.

All saved active records with locations are candidates, including past events.
At most 32 annotations are retained, ordered by viewport visibility, distance
from its center, then stable event ID. Empty and deleted records are excluded.
Unended event titles are red, ended titles gray; an invalid clock is neutral gray.
Recurring records project their next or ongoing occurrence without storing copies.
The event detail preview starts at zoom 16.

The viewport owns a compact cache and one aligned record scratch buffer. On ESP
the target allocator requires PSRAM and never falls back to internal SRAM.
The cache is released on viewport destruction or storage loss. Failed allocation
or reads clear/defer annotations without disabling the base map; retries are
throttled. A failed scan never publishes a partial result.

A 64-pixel query margin avoids rescanning during small pans. Overflowed queries
are reselected after movement so off-screen entries cannot starve visible ones.
Revision, storage readiness, clock validity, time transitions, zoom, coordinate
system and viewport coverage invalidate the cache. Drag previews only translate
annotations. Draw callbacks never access storage. The existing overlay layer
draws markers and titles directly, without per-event LVGL objects; LVGL still
uses transient draw-task allocations under the platform's normal allocator.

## Repository audit (2026-09-21)

| Product | Target profile | Declared / resolved UX pack | Page manifest | Layout |
| --- | --- | --- | --- | --- |
| Pager, SX1262 / LR1121 builds | `tlora_pager` | `pager_compact` / `compatibility` | `pager_compact_manifest` | `pager_compact`, 480 x 222 |
| T-Deck | `tdeck` | `deck_full` / `compatibility` | `deck_full_manifest` | `deck_wide`, 320 x 240 |
| Wio Tracker L2 | `wio_tracker_l2` | `deck_touch` / `deck_touch` | `deck_full_manifest` | `deck_wide`, 320 x 240 |

Sources: `modules/product_composition/src/target_profile.cpp` and
`target_ux_binding.cpp`. Input must use the corresponding UX bindings; Wio is
touch-only, whereas Deck has keyboard/trackball as well. Resolution is not an
input capability test. Watch also resolves to `compatibility`; adding Calendar
unconditionally to that pack would incorrectly widen target scope.

Current manifest counts: Pager 6, Deck 9. Current resolved screen counts:
compatibility 10, deck_touch 9. Both screen and binding registries have capacity
16. Calendar does not justify expanding either array. The separate app catalog
also has capacity 16 and must be checked with all optional apps enabled.

Registration traverses PageId, manifest, UX pack, ScreenId, MenuScreenId,
screen/menu adapter, binding factory and catalog. Inspecting or changing only
`app_catalog_builder.cpp` is insufficient. Non-target packs must remain unchanged.

### Ownership and existing integration points

- Embedded app composition is under `apps/esp32_lvgl/src`. Arduino startup and
  loop runtimes are separate. Agenda services/adapters will be owned here, not
  by the page or `AppServicesBundle`.
- `PresentationWorkspace` is the existing typed graph of externally owned
  models. Agenda source/sink contracts belong in `ui_presentation`.
- Tracker's shell/runtime/components/input/state files establish the current
  page split. Existing Tracker filesystem calls are not a precedent for new
  Agenda UI I/O.
- Global interruption precedent: `reticulum_call_overlay::tick()` is driven by
  the app loop independently of an open page. Agenda reminders need equivalent
  lifetime and safe focus restoration, with no interruption of an existing
  modal until it can be presented safely.
- Clock: `sys::epoch_seconds_now()` and `platform::ui::time` expose UTC and the
  configured timezone. The platform time runtime considers epochs before 2020
  invalid. Calendar arithmetic must not depend on LVGL or Arduino.
- Storage must use the shared SD runtime, with logical event path
  `/agenda/events.dat` relative to the card. The unrelated internal `ffat`
  partition at `/fs` is not an Agenda backend. Reuse SD file/ownership APIs,
  not the chat database or NVS configuration store. Do not repartition or format.
- `MapWorkspaceModel` already owns viewport/tool actions; `gps_page_shell`
  accepts a `RouteSpec` with a host and projection. There is currently no
  SelectLocation mode in `MapToolKind`. Add a semantic selection/return contract
  to that workflow, not another map renderer. Preserve only the small editor
  draft while the map tree exists.
- A bounded saved-waypoint query is not exposed by the inspected Map workspace
  contract. Locate its authoritative saved-item source and add a bounded adapter
  before implementing the picker; the Team snapshot's vectors are not a suitable
  Agenda-owned database.
- Use the supplied `modules/ui_shared/src/ui/assets/agenda.c` for the catalog icon.

### Build ownership

Reviewed entrypoints: `apps/esp32_lvgl/CMakeLists.txt`,
`cmake/TrailMateUxPacks.cmake`, `cmake/TrailMateLinuxSources.cmake`,
`builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake`, and `scripts/platformio-pre.py`.
Portable tests should compile the same core sources. Build integration must be
explicit for each enabled product; Linux/GTK/ASCII/headless/watch are not newly
enabled products.

### Analysis limitation

GitNexus reported an index 53 commits behind HEAD. `npx gitnexus analyze` was
attempted and exited 1 after native parser worker timeouts, including the large
GPS runtime. Do not interpret absent graph results as proof of zero impact.
Run symbol impact queries before modifying existing symbols, supplemented by
current source/caller checks and regression tests.

## Verification still required

- Portable record validation, recurrence boundaries, bounded queries, scheduler
  simultaneous due events/snooze/clock changes.
- Fixed-slot persistence/reload, corrupted records, interrupted writes, full
  store, deleted-slot reuse; no whole-database cache.
- Full target registration/capacity tests and explicit excluded-target tests.
- Actual 480 x 222 and 320 x 240 LVGL render/input tests, localization, editor
  cancel/save, reminder while page closed, and map pick/return lifecycle.
- Size assertions and target RAM/build evidence, then hardware validation.

## Portable core checkpoint

Implemented in `modules/core_agenda` (not yet wired into firmware):

- Fixed 256-byte, little-endian, versioned/CRC-protected record codec; 64 slots.
- Single-record CRUD service; tombstones retain IDs and vacant slots are reused.
- Calendar arithmetic and daily/weekly/monthly/yearly occurrence calculation.
  Nonexistent dates are skipped (January 31 does not become February 28;
  February 29 recurs in the next leap year). Start/end values represent local
  calendar seconds; timezone conversion belongs to the platform clock adapter.
- Bounded sorted query with `(start,event_id)` pagination and explicit overflow;
  month occupancy uses a 32-bit mask. Corrupt rows are reported as degraded in
  agenda queries; write operations fail closed on unreadable slots.
- Reminder scheduler with one next candidate and one explicit snooze. Ordinary
  ticks perform no store scans. Same-time reminders are ordered by ID; closing
  one does not consume the others. An occupied presentation sink defers delivery.
  Startup/material clock corrections skip already-past triggers. A second
  concurrent snooze is rejected, not silently substituted; the renderer must
  surface this outcome. Snooze/dismiss state is runtime-only in V1.

Host CTest currently passes five executables: codec, CRUD, recurrence/query,
reminder scheduler, and calendar limits. Date round-trips cover every date from
1970 through 2400, including non-leap centuries. Host sizes: EventRecord 208 B,
AgendaPage 1048 B, ReminderScheduler 136 B, CivilTime 8 B, month mask 4 B. These
are structure sizes, not a claim about total embedded idle RAM or storage I/O.
Real filesystem recovery, platform clock/timezone binding, product registration,
LVGL rendering/input, map integration and hardware tests remain outstanding.

## Persistence checkpoint (superseded internal-flash backend)

At this historical checkpoint, `platform/esp/common/storage/AgendaFileStore`
implemented the slot port using standard file I/O over the mounted internal
filesystem. That backend is not compliant with the current SD-only contract.
The record layout uses two independently
CRC-protected 256-byte banks per logical slot, with slot identity and generation
inside the checksum. File size is 33,024 bytes (256-byte header + 64 dual-bank
slots). The RAM object is 488 bytes on the host. It opens files only for an
operation and retains no event table. New files are initialized via a separate
temporary file; existing invalid headers, truncated files and future versions
are reported rather than reformatted or downgraded.

The former internal-filesystem mount policy is obsolete and must not be used as
an implementation recipe. Storage preparation must require an available,
application-owned SD card and validate the Agenda directory without formatting
any medium. `AgendaClock` uses the existing timezone policy and ESP monotonic timer.

Seven host tests now pass, including actual temporary-file create/update/delete,
reopen, capacity, slot reuse, both-bank corruption, future-format refusal and
wrong-slot detection. Interrupted-record testing replaces every possible prefix
of a physical-bank update and verifies recovery of an old or new valid record.
The former internal-filesystem mount-policy tests do not establish compliance
with SD requirements. Verification must cover missing media, removal, ownership
transfer, validated remount and replacement cards. Record-bank recovery tests do
not prove whole-filesystem recovery from physical power loss; FAT metadata and
first-initialization failures still require device-level validation.

## Presentation and composition checkpoint

`AgendaWorkspaceModel` implements portable source/action contracts. A single
fixed command slot separates UI acceptance from durable completion; save/delete
are executed by the composition pump, not inside an LVGL callback. Failed writes
must leave the editor open with its draft. The CRUD service retains only a store
reference; its 208-byte operation scratch record is temporary rather than idle
state. Target stack depth still needs an embedded build check.

`AgendaComposition` owns the service, scheduler, workspace model, and small
reminder presentation bridge. Its store/clock dependencies belong to the target
root. The typed `PresentationWorkspace` gains two nullable pointers; no Agenda
service was added to `AppServicesBundle` or AppContext. Existing non-Agenda
workspaces remain unbound and keep their previous interactive-status behavior.
The bridge holds only reminder identity/timing, not an EventRecord or UI tree.
The shell controls whether new reminders may be delivered while another modal
owns input. Popup actions carry a presentation revision, checked both when
queued and when executed, so stale callbacks cannot dismiss another occurrence.

Nine host CTest executables pass. The added composition test exercises a draft
whose lifetime ends before reminder delivery, deferred presentation without
per-tick database reads, simultaneous reminders, snooze capacity, stale popup
actions, and withdrawal between enqueue and execution. The existing
`test_presentation_bundle_shape.cpp` was also compiled and run unchanged.

Current MinGW host sizes are AgendaComposition 456 B, reminder bridge 48 B,
and the aggregate of composition + real file-store object + test clock 976 B.
The aggregate has a compile-time `< 1024` assertion; it does not instantiate or
cache an in-memory production event table. The two workspace binding pointers
cost an additional 8 B on this host. Embedded object sizes, filesystem runtime
costs, task stacks, and device behavior remain to be measured.

The portable composition is implemented and tested, but firmware startup/loop
wiring, full target registration, LVGL pages/popups, localization, and map
integration are not yet complete. These host tests are not UI or device acceptance.

## Target registration and first renderer checkpoint

Calendar is declared only in `pager_compact_manifest` and `deck_full_manifest`.
WIO's existing DeckTouch pack projects the latter directly. Pager and T-Deck
now select manifest-backed compatibility packs: these preserve every existing
compatibility screen and append enabled manifest screens without duplicates.
They remain marked as transitional (`final_ux_pack_available == false`). The
shared compatibility pack is unchanged, so Watch and Tab5 do not inherit the
new page. No screen/binding array capacity was increased.

Verified chain: TargetProfile -> PageManifest -> selected UX pack -> ScreenId
-> MenuScreenId -> ScreenBindingRegistry (`calendar`). Resulting screen/binding
counts are WIO 10, Pager 11, T-Deck 11; other target profiles exclude Calendar.
The AppCatalog descriptor and target runtime wiring are still outstanding.

The first LVGL renderer now implements the bounded Agenda list, occurrence
detail, and confirmed asynchronous deletion. Sources/actions are injected in
the shell Host. Page state is allocated on enter and released on exit; it does
not own services or storage. Input callbacks only request transitions. A page
timer applies them after the input callback, and TopBar Back participates in
the same focus group as page controls. Pagination advances after the last
actually rendered row, including space consumed by date section headings.

Native LVGL tests compile the production page, shared TopBar, and layout code.
The host stubs only locale-pack/font fallback, board power, and input-group
binding. Tests render actual 480 x 222 and 320 x 240 RGB565 framebuffers and
generate `agenda-home-480x222.bmp` / `agenda-home-320x240.bmp` in the UI build
directory. They verify button bounds, Back focus traversal, deferred page exit,
pagination, delete confirmation, persistence failure, and successful deletion.
The images were visually inspected. Pager uses the EncoderCompact profile to
select 24 px rows; the compact hybrid/touch layout uses 32 px rows. This does not
infer input capabilities from resolution.

Verification commands:

```text
cmake --build .codex-build/agenda-core
ctest --test-dir .codex-build/agenda-core --output-on-failure
cmake --build .codex-build/agenda-ui
ctest --test-dir .codex-build/agenda-ui --output-on-failure
```

The core build now runs 34 tests, including 24 existing UX/manifest/binding
regressions. The separate LVGL test passes both framebuffer profiles. Remaining
UI work includes creation/editing, date/time/reminder/repeat pickers, reminder
popup, location integration, and complete translation-pack entries. No firmware
or hardware validation has been performed for this checkpoint.

## Editor and picker checkpoint

The page now supports creation and editing through the injected presentation
source/action contracts, plus date, time, reminder and repeat pickers. Title
and note input is checked against the serialized UTF-8 byte limits without
silent truncation. Back can open a discard confirmation without rebuilding
the live text fields; Keep editing preserves even invalid oversized input.
Pending saves disable the actual textareas, and failed saves preserve the
draft and restore editing. Equal start/end clock times mean a 24-hour event;
an earlier end time falls on the next day.

Calendar selection uses the receiving Calendar object for bubbled events,
not the originating buttonmatrix. Passing that matrix to Calendar APIs was
reproduced as a host segmentation fault before the fix. Keyboard/encoder
focus belongs to the editable matrix rather than its container. Directional
keys remain with the widget in edit mode; Tab and Back retain navigation
semantics. Tests cover the matrix's actual bubbling selection event and
directional-key event, in addition to presentation commands.

The LVGL regression renders both real framebuffer geometries and covers CRUD,
picker transitions, duration preservation across date changes, UTF-8 overflow,
discard/keep editing, pending-save input state and write-failure recovery.
Calendar and time controls use the existing shared palette through local
styles; no global theme or LVGL library changes are required. The updated
time and date framebuffer images were visually inspected.

Host touch text-editor attachment is currently stubbed: these tests do not
prove the production touch keyboard or device encoder driver. Translation
pack/font fallback and board power are also stubbed. Location selection is
not yet enabled. Target startup/AppCatalog wiring, the page-independent
reminder popup, full localization, Map integration, firmware builds and device
validation remain outstanding.

## Target composition and catalog checkpoint

At this checkpoint, `esp32_lvgl_arduino_agenda` owned the now-superseded
internal-flash adapter, clock, service,
scheduler, presentation model, injected page Host and ordinary CallbackAppScreen
descriptor. Startup initializes it before building the catalog. The app loop
pumps it after the common loop shell, including that shell's overlay branch;
durable commands therefore do not depend on the Calendar page being alive.
AppContext and AppServicesBundle remain unchanged. A temporary
PresentationWorkspace binds the contracts without adding another permanent
workspace graph.

The descriptor is injected through `FeatureFlags::calendar_app`; callers that
leave it null retain their previous catalog. The production catalog test enables
all optional Pager entries: 15 existing apps plus Calendar equals the unchanged
capacity of 16, with Settings and Shutdown still present and no duplicate IDs.
`targetHasAgenda` checks the LVGL/display capability and the target's Calendar
manifest entry, not resolution. Unsupported Arduino targets compile an inert
adapter without linking Agenda storage, clock or UI dependencies.

Pager and T-Deck now resolve their actual TargetProfile and TargetUxBinding
instead of falling through to the generic `esp_idf` runtime config. Native tests
compile each of WIO, Pager and T-Deck configurations separately and verify the
selected pack exposes Calendar. The root integration test links the real
`agenda.c` asset, page and file-store implementation, substitutes only the clock
and mounted directory, saves while the page is closed and reopens the file to
verify persistence. It does not simulate flash power loss.

At this checkpoint reminder delivery remains disabled in the target adapter
until the global popup can safely own and restore input. This is intentionally
an incomplete runtime: successful compilation is not proof of the required
page-closed reminder scenario. Map/location, translation packs, production touch
keyboard testing and device acceptance are also still outstanding.

### Storage scratch and linked RAM verification

The file adapter no longer retains a second EventRecord. Both banks decode
into the caller's output; if bank 0 wins after reading bank 1, the adapter
rereads bank 0 and verifies its generation. That adds at most one 256-byte read
per logical slot and removes 208 bytes of permanent record storage (216 bytes
including target-root alignment). Initialization uses one bounded automatic
empty record. Slot layout, generation ordering, CRC, durable flush and alternate
bank writes are unchanged. Recovery tests also explicitly cover each bank
surviving corruption of the other.

WIO PlatformIO build completed successfully after this change, exit code 0.
The final ELF reports:

| Symbol / allocation | Bytes | Section |
| --- | ---: | --- |
| Agenda target root, including file adapter, composition, clock, Host and descriptor | 800 | `.dram0.bss` |
| Closed-page state pointer | 4 | `.dram0.bss` |
| Two manifest-backed UX pack objects | 48 | `.dram0.data` |
| Their two local-static initialization guards | 16 | `.dram0.bss` |
| Supplied Agenda bitmap | 12288 | `.flash.rodata` |

The charged idle object subtotal is 868 bytes. A compile-time root assertion
includes the page pointer, both new pack objects and their guards. Making the
pack constructor constexpr did not remove those objects from target RAM; the
subtotal includes them rather than claiming they reside in flash. This is an
object/section measurement, not a runtime heap watermark; shared filesystem
mount overhead and transient LVGL allocations still require device measurement.

Whole-firmware build output: static RAM 98,564 / 327,680 bytes; flash
4,107,425 / 6,291,456 bytes. These are current absolute totals, not a delta
against main. No firmware was uploaded. The native suites pass 38 core,
storage, registration and configuration cases plus 2 LVGL/catalog executables.

## Page-independent reminder presentation checkpoint

The target root now enables reminder delivery through a small injected popup
Host. The popup uses presentation source/action/reminder contracts; it neither
owns the scheduler nor accesses storage. Calendar may be closed throughout
delivery, Snooze and Done. A popup retains one event only while visible and
has a compile-time state limit below 512 bytes; its idle state is one pointer.

Presentation waits for existing overlays, interruption apps, pending shell
transitions and unobserved Calendar commands. Button events enqueue actions;
the following root tick submits the command and observes its result before
closing. Input groups and editing mode are restored only while the popup still
owns input and the previous group still exists. A higher-priority interruption
closes the visual without consuming its reminder or clearing the interruption's
overlay marker. Activation is ignored for the first 400 ms to reject a release
already in flight when the modal opened.

The native tests exercise the real composition, scheduler, presentation model
and LVGL popup at 480x222 and 320x240. Covered scenarios include simultaneous
reminders, monotonic 600-second snooze, refusal of a second concurrent snooze,
Done, interruption/reappearance, a destroyed previous focus group, a record
deleted after a click, and a save result consumed by the page before the due
popup may appear. The target-root integration also exercises persistence and
the complete page-closed reminder path. Shell activity flags, hardware power,
touch keyboard attachment and the clock are host substitutes; these tests do
not prove physical input drivers or the entire production shell lifecycle.

The compact three-action footer allows a two-line caption and uses the taller
320x240 viewport rather than reducing font size. Tests assert caption bounds
and separation between the snooze-capacity error and action buttons. Actual
framebuffers, including this error state, were visually inspected.

The Navigate callback is tested after successful dismissal and popup cleanup,
but the target's Map callback is not wired yet. Location editor selection,
production Map integration, translation pack coverage, production touch input
and device validation remain incomplete. No disabled Navigate button appears
on a location-free event.

The WIO build with the popup linked completed successfully (exit 0, 143.29 s).
Whole-firmware static RAM is 98,564 / 327,680 bytes and flash is
4,111,089 / 6,291,456 bytes. The linked root remains 800 bytes; page and popup
idle pointers are 4 bytes each. Including both 24-byte manifest pack objects
and their two 8-byte guards gives an Agenda-charged object subtotal of 872
bytes. Section alignment means the added pointer did not change the reported
whole-firmware RAM total; the subtotal still charges it explicitly. This does
not measure runtime filesystem heap or peak LVGL allocation. No upload was
performed. The native suites pass 38 core/regression cases and both UI/catalog
executables after the layout and command-handoff tests.

## Location source and Map selection model checkpoint

The Agenda page Host now receives an IAgendaLocationSource. The target owns a
small GpsAgendaLocationSource adapter over the existing IGpsStatusSource;
there is no page-to-hardware call, extra GPS timer or retained GPS snapshot.
Current position requires a valid presentation header, enabled/powered receiver,
valid fix and finite coordinates within geographic bounds. Conversion to E7
occurs only after validation. A failed read leaves the output and editor's
existing location unchanged.

The editor's Location field now opens an optional-location picker. Current
position sets a coordinate in the draft and clears any previous waypoint ID.
Its persisted label is a numeric coordinate rather than a translated phrase.
Remove location clears only location fields/flags. Both actions preserve the
other editor fields and require Save before changing the stored event. Native
UI tests at both geometries cover failure, replacement, Back, removal, save
and subsequent edit; the rendered picker framebuffers were inspected.

MapWorkspaceModel now exposes beginLocationSelection, pickLocation and
cancelLocationSelection with the generic SelectLocation tool. The result is
a bounded structure no larger than 24 bytes. Selection uses the presentation
viewport centre, not the self-position overlay; it accepts (0, 0), rejects
non-finite/out-of-range coordinates, prevents unrelated tool changes while
selecting and restores the previous tool after a successful Pick/Cancel.
Rejected sink operations no longer commit local viewport/tool state. Tests
cover rejection and retry, busy selection, invalid snapshots/coordinates,
zero coordinates, and the pre-existing Map model behaviour.

This is not yet the complete Map-selection workflow. The Map renderer's
Pick/Cancel controls, coordinate-system conversion, shell route handoff,
editor-tree destruction/draft-only retention and reconstruction still require
implementation and integration tests. No Calendar-specific renderer or tile
access has been introduced. Navigation remains unwired. Repository inspection
has not identified an independent saved-waypoint catalog; protocol waypoint
messages are not silently repurposed as such a catalog. The saved-waypoint
source remains unresolved. All 42 core/presentation/registration tests and
both LVGL/catalog test executables pass at this checkpoint.

The added runtime Map adapter regression links the real RuntimeMapWorkspaceSource,
RuntimeMapActionSink and MapWorkspaceModel. It verifies that a committed (0, 0)
viewport is not replaced by a subsequent GPS fix, that the sink sees the
selection tool and its restoration, and that layers remain unchanged.

The WIO location build completed with exit code 0 in 136.80 s: static RAM
98,596 / 327,680 bytes, flash 4,112,353 / 6,291,456 bytes. The root is now
808 bytes, closed page/popup pointers total 8 bytes, and the manifest objects
and guards remain 64 bytes. The existing Map model is 64 bytes, up by 24 from
its former 40-byte layout. Charging that growth yields 904 bytes of Agenda
idle object storage. The root assertion additionally reserves an alignment
unit beyond the selection result, conservatively charging 32 bytes for this
shared-model extension. No hardware upload or live GPS-fix validation was
performed; the reported RAM figures remain linked objects, not heap watermarks.

## Map selection lifecycle checkpoint

Agenda's target-owned UI Flow now connects Choose on map to the existing GPS
shell's Map projection. RouteSpec carries a caller-owned MapLocationRequest;
neither the Map contract nor the renderer depends on Agenda. Flow retains only
an editor draft and bounded return context during the visit. It destroys the
Agenda State, LVGL tree and input group before entering Map, then destroys Map
before reconstructing the editor. Pick updates the draft; Cancel preserves it.
Neither operation writes an event. Closing the app during a pending transition
or an active Map visit releases the return context. Entry failure restores the
draft with an error. Page transitions run outside widget callbacks.

The existing Map runtime provides a fixed viewport-centre crosshair, Pick and
Cancel in selection mode. Route/track controls are not exposed in that mode.
The rendered centre is converted back to WGS84 before E7 storage. Existing
forward coordinate formulas are shared with a bounded inverse; tests pin an
existing forward result, check round trips across all three coordinate modes,
and reject invalid/non-converging input. An explicit initial (0, 0) viewport
receives a valid default zoom instead of being mistaken for an absent centre.

Native lifecycle tests run the real Agenda page and Flow at 480x222 and
320x240, substituting only the Map shell. They assert editor destruction before
Map entry, draft and return-view preservation, Pick/Cancel, unavailable/failed
entry, input-group restoration, zero implicit writes and app exit in both
transition phases. A target-root test also verifies that a reminder due during
selection is presented after returning, without stealing Map input. These
tests do not validate the physical Map renderer, tile service or input drivers.
The updated location-picker framebuffers were inspected at both exact sizes.

All 43 core/presentation/regression cases and both LVGL/catalog executables
pass. The WIO Map-flow build completed with exit 0 in 131.07 seconds. Static
RAM is 98,636 / 327,680 bytes and flash is 4,118,613 / 6,291,456 bytes. Linked
Root storage is 832 bytes. Charging page/popup pointers (8), manifest objects
and guards (64), shared Map-model growth (24), and the three Map selection
pointers (12) gives 940 bytes of idle Agenda-related object storage. The
compile-time guard conservatively charges another alignment unit. These are
object sizes, not heap watermarks. No firmware upload was performed.

Navigation, a resolved saved-waypoint source, translation-pack coverage,
production input validation and the remaining target/device checks are still
incomplete; this checkpoint is not a Calendar V1 completion claim.

## Locale source coverage and detail-return correction

The 13 existing locale tables now contain 62 additional keys for Agenda,
Calendar's catalog label and the Map Pick action. Existing translation quality
statuses are unchanged: this change does not promote review locales. Seven
affected bundle versions receive a minor increment independently of firmware;
the minimum firmware compatibility bounds remain unchanged. Distribution
archives/catalogs have not been published, and the new translations still
require the normal language-quality and on-device layout review.

UI charset/range metadata was regenerated with the existing generator for the
eight affected primary fonts, including the T-Deck Pro Simplified Chinese
variant. Existing non-ASCII charset glyphs were checked against HEAD: none
were removed. Latin, Cyrillic and Arabic build recipes now seed their existing
charsets so future pack builds incorporate added translations while retaining
their previous glyph coverage. Binary font generation and actual glyph lookup
remain separate verification steps; declared ranges alone are not proof of
rendering. Firmware-private dictionaries or embedded CJK font assets were not
added.

The native UI CTest suite includes a Python source-coverage test using the
existing locale parser/validator. It checks Agenda's source keys in every
locale and advertised UI font ranges for the translated characters. All
three UI/catalog/locale tests and all 43 core/regression tests pass.

A new regression reproduced a Map-return bug: detail timestamps were read
from a list row index which no longer referred to the selected occurrence
after page reconstruction. Detail now owns one bounded Occurrence value;
the temporary Map return context preserves it with the editor draft. The test
opens a non-first weekly occurrence, edits without saving, cancels Map, then
discards the edit. It verifies both the original title and the selected
occurrence's start/end display at both screen geometries. The regression was
observed failing before the fix and passing after it. No event is written.
This adds only open-page/temporary-context state, not an idle event table.
The latest WIO binary predates this small detail-state correction; no new
firmware build, upload, commit or push was performed for this checkpoint.

## Detail metadata checkpoint

Event Detail now displays configured reminder and recurrence values. The
editor and detail share the same preset-key functions; no second preset table
or retained presentation state was added. Absent reminder/repeat fields are
omitted. A regression renders all optional fields with a long note at both
480x222 and 320x240, checks that the note remains above the fixed action row,
and verifies that unconfigured metadata is absent. Both exact-size
framebuffers were inspected and the three UI/catalog/locale tests pass.

Map navigation integration inspection found viewport positioning and existing
SelectedTarget overlays, but no generic destination-opening request in the
current shared Map contracts. The pending integration should extend that
shared boundary, not add Agenda-owned map state or imply that turn-by-turn
route planning already exists. Navigate remains unwired at this checkpoint.

## Detail-to-Map target checkpoint

The shared Map route now accepts a caller-owned `MapTargetRequest`, separate
from location selection. It focuses the existing workspace on a validated
WGS84 destination and adds an existing SelectedTarget annotation after optional
information filtering. A full annotation snapshot replaces the last non-self
item rather than dropping the destination or growing the fixed array. Normal
Map routes have no target request. Destination entry does not automatically
load a configured route which would recenter the viewport. This is destination
display, not turn-by-turn routing or route calculation.

Event Detail now exposes Navigate only for located events with an available
host action. The existing Flow coordinator defers teardown until outside the
UI callback, destroys the Agenda tree, opens Map, and reconstructs Detail on
Back. It preserves the selected occurrence and does not write the event.
Selection continues to reconstruct the editor instead. Entry failure returns
to the original view with an error. The return context remains below 1 KiB;
only one additional shared Map pointer is retained while idle.

Native tests cover zero/boundary/invalid coordinates, action-sink rejection,
selection conflicts, annotation capacity, both Detail return geometries,
entry failure, and no unintended event writes. The all-fields framebuffer
checks now include Navigate at 320x240 and 480x222. All 44 core/regression
tests and three UI/catalog/locale tests pass. The native Map-flow test still
substitutes the Map shell; it does not prove physical input or tile rendering.

Expanded UI testing exposed a pre-existing Agenda input bug: LV_EVENT_KEY
holds a key-value pointer, but the handler passed it to lv_indev_stop_processing
as an input-device pointer. A debugger data watchpoint observed corruption of
an adjacent host field. Page key handling now uses the active input device;
the popup distinguishes key events from pointer/click events. Both paths have
guarded key-payload regressions. These are firmware fixes, not test-only changes.

Reminder-popup navigation across application lifetimes remains unwired.
Saved-waypoint source integration and full device validation also remain open.

WIO `wio_tracker_l2` was rebuilt after the input fix: PlatformIO exited 0
after 132.33 seconds, static RAM 98,644/327,680 B and flash
4,120,177/6,291,456 B. The idle RAM static assertion remains satisfied.
No upload, commit, push or release was performed. Other target builds and
hardware behavior are not established by this WIO build.

## Reminder navigation and approved-target build checkpoint

Reminder Navigate now prepares a bounded return context before submitting
dismissal. Preparation or command failure leaves the reminder available and
cancels the prepared navigation. Successful dismissal closes the popup before
the target composition uses the existing application switch and Map route.
An already-open Agenda restores its view and draft; invocation from another
application returns to the default Agenda list. Picker values remain provisional.

All four approved targets built successfully in the same PlatformIO run (exit
0). These results precede the content-font and interruption changes below.

| Environment | Static RAM (B) | Flash (B) |
| --- | ---: | ---: |
| wio_tracker_l2 | 98,644 | 4,121,253 |
| tdeck | 96,040 | 4,043,773 |
| tlora_pager_sx1262 | 99,696 | 4,240,365 |
| tlora_pager_lr1121 | 100,112 | 4,242,033 |

## Font artifact and scope verification

Generated binaries for all eight affected primary fonts pass real LVGL glyph
lookup for the Agenda translations. Actual generation exposed missing Polish
letters, Arabic-pack punctuation and a mathematical comparison symbol. Latin
now uses Noto Sans with CJK fallback for missing symbols; Arabic retains Naskh
with Noto Sans fallback. The converter assigns each character to the first
source containing it, so fallback does not replace primary glyphs. Source and
pack licenses accompany the new font source. Distribution archives remain
unpublished; language quality and Arabic shaping are not proven by glyph lookup.
Full-charset lookup also passes for the other seven fonts. Arabic's existing
broad charset declaration fails at U+0605, despite Agenda's required characters
passing; full Arabic advertised-range coverage is not established.

User-entered Agenda labels now use the existing Content font scope, while
translated controls use the Ui scope. A native regression failed on the old
helper and passes on both layout profiles, for body and caption fonts.

The optional GNU font artifact test instruments the real LVGL loader, reports
retained allocation payload and load-time peak, and checks release on destroy.
The 32-bit retained payload measurements are:

| Font | Retained payload (B) | Manifest estimate (B) |
| --- | ---: | ---: |
| ar-naskh | 50,939 | 52,224 |
| cyrillic-eu | 3,900 | 5,120 |
| latin-ext-eu | 2,582 | 4,096 |
| ja-cjk | 33,331 | 34,816 |
| ko-cjk | 24,770 | 26,624 |
| tdeckpro-zh-hans-core | 137,092 | 139,264 |
| zh-hans-core | 222,663 | 224,256 |
| zh-hant-cjk | 37,675 | 38,912 |

`verify_font_artifacts.py` checks glyphs and requires a manifest estimate at
least the retained payload rounded up to 1 KiB plus a further 1 KiB allowance.
These are estimates, not physical-device heap watermarks or load-peak budgets;
allocator metadata, libc I/O and rendering allocations are not measured.
Ordinary CTest neither downloads dependencies nor regenerates fonts.

## Interrupted-page recovery checkpoint

The existing app runtime sets the interruption flag before calling the previous
application's exit callback. Agenda Flow now distinguishes that exit from a
normal close. It releases Agenda/Map widgets and input groups while retaining
one bounded recovery context. The shell must re-enter Calendar to resume it;
a suspended context cannot independently activate the application.
If an interruption superseded an exit-to-menu and the shell does not resume
Calendar, target composition discards the suspended context once the shell is
stable. Reopening Calendar then starts a new session. A root-level regression
reproduced stale-session resurrection before this cleanup was added.

Recovery includes the draft, selected occurrence, list query, provisional
date/time values, delete/discard confirmation and pending command sequence.
Live text has a separate bounded UTF-8 representation because LVGL's character
limit can exceed the persistent record's byte limit. Invalid oversized text is
restored for correction, never silently truncated or written. The context is
reserved before accepting input and reused for Map returns; it remains below
1 KiB. Normal close releases the reserve. Idle ownership grows by one pointer,
not by a persistent draft buffer.

Native regression first reproduced lost editor state after actual Flow exit
and enter callbacks. Both geometries now pass normal and maximum-size live text,
confirmation, picker, prepared-navigation cancellation, interrupted Map return,
and save success/failure during interruption. The test substitutes the external
interruption application and Map shell: it does not prove the production call
screen, keyboard integration or physical input. The latest native suites pass
44 core/regression and four UI/catalog/localization tests.

Saved-waypoint integration, full production input/Map/device validation,
physical memory measurements and the complete specification audit remain open.

Production touch-editor inspection identifies an additional recovery boundary:
the compact touch keyboard edits its own modal textarea until OK, whereas
Agenda's recovery snapshot currently reads the source field. Deleting the source
closes that modal without applying its unconfirmed text. A combined production
touch-editor/Agenda test and a shared editor recovery contract are still needed;
the page-field interruption tests do not cover this separate modal draft.

WIO was rebuilt with the completed page-field recovery and abandoned-session
cleanup: PlatformIO exit 0, 122.63 seconds, static RAM 98,652/327,680 B and flash
4,122,157/6,291,456 B. The idle and recovery-context assertions pass on ESP.
ESP stack hygiene and `git diff --check` pass. No firmware upload was performed;
the latest Pager and T-Deck builds still predate these recovery changes.

## Production touch-input integration checkpoint

The Agenda native UI target now compiles the real compact touch editor,
ImeWidget, PinyinIme and input-layout adapter instead of replacing
`attach_touch_text_editor` with a no-op. Board keyboard-device lookup and the
external language-pack source remain host fixtures; the fixture does not enable
a Pinyin dictionary and does not establish language-specific composition recovery.

At 320x240, the production DeckLandscape/Touch layout enables the compact
keyboard. A combined test opens Agenda's title and note editors, sends actual
buttonmatrix key events, checks keyboard bounds, verifies Cancel leaves the
source unchanged, verifies OK writes the field but not the store, and verifies
Save persists both fields without requiring a location. The four UI/catalog/
localization tests pass with this stronger input boundary.

Unconfirmed modal text across application interruption is still not restored.
The shared editor retains a separate draft and Cancel semantics; copying it into
the source on interruption would be an incorrect implicit commit. Recovery must
preserve that distinction and account for IME state and bounded temporary RAM.
This checkpoint changes tests only; it does not add a new firmware build result.

## Shared modal recovery implementation checkpoint

The compact touch editor now exposes capture/restore operations using caller-
provided text storage and a bounded `ImeEditState`. It retains no additional
global draft or widget tree. The IME metadata preserves mode, shift, cursor,
script selection, the eight-letter composition buffer and candidate state;
candidate collections are reconstructed rather than retained.

Agenda Flow captures the modal independently of its source field and restores
it after rebuilding the source. A regression first failed with missing modal
text, then passed after integration. Combined tests cover Cancel/OK semantics,
numeric/symbol layout, shift and cursor restoration, a deterministic two-entry
Pinyin fixture, and maximum-length four-byte UTF-8 source and modal strings.
All four native UI/catalog/localization tests pass. Physical touchscreen and
full language-pack dictionary behavior remain outside this evidence.

The initial integrated return context allocates 1,272 B on the 32-bit native
build (allocation immediate 0x4f8 in Flow::enter), exceeding the previous 1 KiB
recovery ceiling. A temporary 1.5 KiB ceiling keeps growth bounded; this is not
a completed RAM-budget acceptance. Compacting duplicated draft text and
mutually exclusive Map/modal state remains required before final validation.
No new firmware build or upload was performed for this checkpoint.

## Compact recovery text checkpoint

Agenda now stores transient source and modal drafts as bounded 21-bit Unicode
scalar snapshots. The persistent event format and input character limits are
unchanged. Invalid UTF-8 or excess characters reject capture without truncation;
restoration streams individual scalars into LVGL without a full decoded scratch
buffer. Shared touch-editor callbacks keep the encoding out of the shared IME.

The 32-bit native recovery allocation is now 1,008 B (0x3f0), down from 1,272 B.
The compile-time ceiling is restored to strictly below 1 KiB. This is a native
context measurement, not an ESP heap-watermark or firmware static-RAM result.

Five native UI/catalog/localization/snapshot tests pass, including the existing
maximum-length independent source/modal interruption cases. The snapshot test
round-trips every non-NUL Unicode scalar and checks invalid encodings, capacity
rejection, unchanged state on rejection, and early consumer termination. The
standalone shared touch-editor test and ESP stack-hygiene check also pass.
Firmware rebuild and hardware validation remain outstanding for these changes.

The subsequent WIO PlatformIO build completed with exit 0 in 124.02 seconds.
It includes shared modal recovery and compact text snapshots; the ESP recovery
context size assertion passes. Static RAM is 98,652/327,680 B, unchanged from
the previous WIO checkpoint; flash is 4,124,481/6,291,456 B. No upload was done.
The 44-test core/regression suite was rerun and passed. Pager/T-Deck rebuilds,
hardware validation and saved-waypoint product integration remain outstanding.

## Native framebuffer review checkpoint

The core build was refreshed before rerunning all 44 passing tests, including
the rebuilt disabled-target executable. Actual native LVGL framebuffer outputs
were visually inspected at their original sizes: the 320x240 editor, date and
time pickers, and snooze-conflict reminder; the 480x222 Agenda, full-field detail
and located reminder. These English fixtures show no text/button overlap. The
compact editor scrolls its fields while Save remains outside the scroll area.
This limited review does not prove all translations, physical input behaviour,
production font-pack layout, or complete device acceptance.

## T-Deck recovery build checkpoint

The latest T-Deck firmware, including shared IME/modal recovery and compact
Agenda snapshots, builds successfully: PlatformIO exit 0, 107.16 seconds,
static RAM 96,048/327,680 B and flash 4,047,005/6,291,456 B. This validates the
compiled target path, not physical keyboard/trackball behaviour. No upload was
performed. The eight existing font binaries were rechecked for Agenda-required
glyph mappings and declared RAM estimates; all passed. Pager builds and the
remaining product/device acceptance items are still open.

## Pager recovery build checkpoint

Both Pager environments now build the latest recovery implementation successfully
(combined PlatformIO exit 0). SX1262: 113.42 seconds, static RAM 99,704/327,680 B,
flash 4,243,529/6,291,456 B. LR1121: 128.26 seconds, static RAM 100,120/327,680 B,
flash 4,245,185/6,291,456 B. Along with the preceding WIO and T-Deck builds,
all four approved firmware environments compile the compact recovery path.
No uploads were performed. These builds do not establish physical input,
power-loss behaviour or runtime heap-watermark acceptance; saved-waypoint
integration also remains incomplete.

## Editor field focus recovery correction

An interruption regression exposed that focusing Date without activating it
left the draft's remembered field unchanged. Textareas remembered focus, but
the five picker field buttons did not. Those buttons now use the same existing
focus callback; no additional state or shared focus-manager branch is needed.
The regression first failed, then passed for Date, Time, Reminder, Location
and Repeat across both native profiles. All five UI test targets pass. This
small correction postdates the four firmware build checkpoints above and has
not yet been rebuilt for ESP or tested with physical input.

## Local waypoint integration checkpoint

Local named waypoints use a separate portable `core_waypoint` module. This
checkpoint used an internal-flash file adapter, which must be replaced by an
SD runtime adapter under the current contract. The 64-slot store scans records on demand; the
picker retrieves at most five rows. Its presentation model retains one pending
write, not a database cache. The composition root creates the bounded waypoint
session when Calendar opens and releases it after ordinary exit. Interrupted
Calendar sessions preserve pending results until the UI can consume them.

Selecting a waypoint copies its name, coordinates and identifier into the event
draft without saving the event. Naming and saving a location creates a waypoint
independently of event persistence. Compact touch-editor tests cover independent
source/modal text across interruption, Cancel without writeback, OK without
persistence, and explicit asynchronous Save followed by return to the editor.
Six waypoint tests and five UI tests pass. The 44-test core/registration suite
and ESP stack-hygiene check also pass.

All four approved PlatformIO environments now build the waypoint integration
and editor focus recovery correction successfully (exit code 0):

| Environment | Static RAM (bytes) | Flash (bytes) |
| --- | ---: | ---: |
| wio_tracker_l2 | 98,660 | 4,131,069 |
| tdeck | 96,056 | 4,053,221 |
| tlora_pager_sx1262 | 99,712 | 4,249,917 |
| tlora_pager_lr1121 | 100,128 | 4,251,561 |

These are link-time measurements, not runtime heap measurements. No firmware
was uploaded. Physical input, on-device persistence/power-loss behavior,
runtime heap watermarks, and final waypoint UI wording/layout remain unverified.

## Waypoint UI and recovery validation

Native LVGL fixtures now render the waypoint list, name editor and failed-save
state at both 320x240 and 480x222. The English outputs were inspected without
scaling; rows, buttons and error status remain inside the framebuffer. Tests
cover five-row pagination, return navigation and retention of an unsaved name
after an asynchronous write failure. This is not multilingual layout or physical
input acceptance.

The expanded lifecycle fixture exposed an aliasing bug in IME restoration:
`restoreEditState` accepted the textarea's internal text pointer, then changed
mode before copying it. Mode changes can replace that buffer. Restoration now
takes ownership through the existing `setText` path before changing mode. No
additional recovery buffer was added. The five Agenda UI tests and standalone
shared touch-editor test pass after this correction.

Waypoint validation now checks bounded UTF-8 as well as record identity and
coordinate ranges. Tests reject overlong, surrogate, out-of-range and truncated
sequences, including the fixed-field boundary, while accepting valid multibyte
names. All six waypoint tests pass. The on-disk format is unchanged.

Font cases were regenerated from current Agenda sources and all 13 locale files,
including the waypoint labels. All eight existing binary font artifacts contain
the required mapped glyphs and satisfy their declared retained-RAM estimates in
the 32-bit native loader probe. This does not establish shaping quality, visual
translation quality, or device heap watermarks. The firmware build table above
predates these latest validation and IME changes.
