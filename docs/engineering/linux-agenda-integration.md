# Linux Agenda integration

## Current scope

The native storage, clock and `LinuxAgendaSource` worker component are connected
to the Linux LVGL application and compact shell. The catalog receives an owned
Calendar descriptor, Map marker binding and shared page Host/Flow. The P4
dashboard borrows the same source and the shell pumps the reminder popup even
when Calendar is closed. Native waypoint persistence, saving a location as a
waypoint and selecting saved waypoints are bound through the shared interfaces;
manual coordinates, valid current GPS fixes and Map selection use existing
ports. Physical input, audio and suspend/wake still require device validation.

## Durable store

`LinuxAgendaStore` implements the existing `agenda::IAgendaStore` contract using
an explicit composition-provided SQLite path. It has one worker owner and a
database-adjacent exclusive flock; it never unlinks the ownership file. The
store does not hydrate a record table or accept UI-thread access.

Each logical slot contains the core's canonical 256-byte, versioned, CRC-checked
event encoding. The core's slot limit initializes the metadata and complete slot
extent in one transaction. Serialized C++ layouts and target-specific paths are
not introduced. Writes return success only after a synchronous SQLite commit.
Deletes preserve the event ID in a tombstone, retaining the core service's ID
high-water behavior across deletion, reuse and reopen.

Unsupported metadata versions and damaged/missing table or slot extents refuse
initialization without formatting. CRC errors, unsupported record versions and
invalid records are reported per slot and never repaired by an ordinary write.
Healthy slots remain readable, allowing the existing reminder scheduler to
report degraded storage while delivering healthy reminders. Other application
tables are preserved. Busy writers and failed transactions leave prior data
intact; the connection's busy wait is bounded and belongs off the UI thread.

## Product clock

`LinuxAgendaClock` supplies local calendar seconds, a revision derived from the
product timezone profile/effective offset, and Linux boot time including system
suspend. It reuses the established timezone rules and system timezone fallback.
Invalid or unrepresentable wall time remains invalid; it is never narrowed into
a different date. The codec's calendar range does not imply that every platform
libc `time_t` can represent that full range.

The Linux fallback now uses `tzset` and `localtime_r` with the effective GMT
offset. Previously a `gmtime` call could overwrite the `localtime` static buffer
before it was copied, producing zero offset. Local-time translation also checks
for `time_t` overflow. Tests exercise DST forward/backward transitions, runtime
TZ changes, half-hour offsets, same-offset profile changes, concurrent reads,
invalid clocks and overflow.

## Background source

`LinuxAgendaSource` owns the real store, clock and shared Agenda composition on
one worker. Its frontend implements the existing Agenda source/action, reminder
and map-marker contracts. One command can be pending at a time; accepted saves
remain Pending until the durable operation and publication complete. Shutdown
drains an accepted command before joining. The component's clock/scheduler keep
running without an open Calendar page, while presentation permission defaults
to false until the shell explicitly permits a reminder.

The worker publishes sealed Linux memory files containing the canonical encoded
slots. Readers pin an immutable projection and reuse the core presentation/query
logic through a read-only port. The port rejects writes, decodes one record at a
time and retains each slot's error status. No decoded record table is hydrated,
and frontend queries neither reopen SQLite nor sample the platform clock. File
descriptors are close-on-exec; a factory returning an ordinary file is refused
before any truncation. Snapshots remain coherent while the next publication is
blocked, and a committed write is never reported as failed merely because the
read projection cannot be rebuilt. Readiness becomes false until recovery.

Storage-header health is checked on refresh and periodically; future schemas
remain unavailable and are never formatted. Reminder actions name a published
revision, preventing stale popups from dismissing or snoozing a new occurrence.

## App binding and lifetime

`AgendaRuntimeBinding` owns both native sources, shared page Host/Flow, descriptor and
Map marker binding. Both application entry paths construct it after the facade
and inject its descriptor/markers into the shared catalog. No device ID, SQL
path or store construction enters the shared page implementation. CM0 has a
native manifest extending the P4 page set; the ESP P4 product manifest remains
unchanged. The UX packs can present Calendar when a composition supplies it.

The owner releases UI bindings before LVGL teardown, cancels references in the
application transition manager and drains the worker. The stopped source and
descriptor remain alive until the menu tree is destroyed, honoring borrowed
dashboard/Map pointers. Programmatic reminder navigation opens the shell's app
container through the runtime router. New popups defer while audio focus or an
outbound voice message owns input; their own notification sound does not close
an already visible popup.

Cardputer Zero appends Calendar after the existing compact routes to preserve
keyboard navigation order. Time controls use font/viewport metrics and a
scrollable field area above a fixed footer. The date grid selects a smaller
profile font when its cells cannot fit the body font.

The Linux UI now links only the shared localization/resource-pack provider.
The obsolete English-only adapter was still in the source list and could cause
duplicate definitions depending on link order. Frame owners call the shared
frame-completion hook after LVGL, enabling deferred font/locale operations.

`LinuxWaypointStore` implements the core waypoint store with separate versioned
metadata and a complete fixed slot extent in the composition-provided database.
Slots use the shared 64-byte CRC codec. Full synchronous transactions preserve
updates, deletion tombstones and the ID high-water mark across reopen. Invalid
records, future schemas and damaged slots refuse ordinary mutations. A separate
database-adjacent ownership flock excludes competing waypoint writers without
preventing Agenda from using its own tables.

`LinuxWaypointSource` owns this connection on one worker, admits one command at
a time and drains accepted work on shutdown. UI queries reuse core pagination
and lookup over sealed memory-file projections, decoding one record at a time;
they never open SQLite or wait for a database writer. A durable commit remains
successful if projection creation fails; unavailable projections are retried.
The root binds both waypoint interfaces to the existing editor and keeps the
stopped source alive through widget teardown. The compact location picker puts
its choices in a scrollable viewport above a fixed removal button.

## Verification scope

Native regression tests exercise the real SQLite adapter through AgendaService,
query and ReminderScheduler: create/edit/delete/reopen, stable IDs after deleting
all records, capacity/reuse, injected transaction refusal, writer contention,
ownership conflicts, future metadata/record preservation, damaged rows and
healthy reminders alongside a corrupt row. The clock and store tests use
ASan/UBSan/leak checks. The existing core codec, service, recurrence/query,
reminder and calendar-limit tests also run in the Linux suite. Backend tests do
not establish physical suspend/wake or device reminder UX.

The source integration test additionally exercises real durable saves and
queries, sealed read projections, a blocked publication with responsive readers,
post-commit projection failure/recovery, single-command admission, ownership
conflicts, shutdown draining, reminder permission/snooze/dismissal, stale popup
revisions, future-schema invalidation and degraded-slot propagation. It runs
with ASan/UBSan/leak checks on the worker and shared presentation model. The
source-stage native suite passed 79/79 checks; the actual armhf source test passed under QEMU.

The P4 UI integration test opens the catalog descriptor, performs create/edit/
delete through the real widgets, round-trips UTF-8 text, checks audio-focus
deferral and drives a reminder into Map and back. It also closes with an exit
timer pending and destroys the stopped binding after LVGL, with ASan/UBSan/leak
checks on the binding, flow and application runtime. A separate Cardputer test
uses the actual compact keyboard queue for Calendar entry, editing, picker
navigation, save, reopen and delete, and checks time-field/footer bounds. Both
geometries also save a supplied coordinate as a durable waypoint, remove the
event location and select the saved waypoint through actual page actions. The
Cardputer test uses the keyboard queue and checks location viewport/footer
separation. The supplied test coordinate is a presentation-boundary fixture,
not evidence of a physical GPS fix or a Map coordinate-selection gesture.
Rendered screenshots cover both geometries. These are software-session tests,
not evidence of physical touch, key mapping, sound or hardware wake.

After application binding, the complete native suite passed 80/80 tests and the
Cardputer suite 14/14. The actual armhf P4 UI test also passed under QEMU,
including reminder-to-Map navigation and return. The extracted Release package
starts the real mesh worker with hardware RF disabled and renders the expected
1232 x 568 frame. These checks do not validate the CM0's physical peripherals.

The waypoint regression additionally covers UTF-8 names and coordinate bounds,
capacity, stable pagination, update/reopen, ID retention after deleting every
record, transaction refusal without publishing an ID, competing owners, bounded
writer contention with responsive frontend reads, shutdown draining, CRC damage,
future metadata and missing slot extent. Store/source code runs under ASan,
UBSan and leak checks in native builds.

After waypoint integration, the full native suite passed 81/81 tests, including
the new store/source regression and extended P4 page flow. The compact suite
passed 14/14, including keyboard waypoint saving/selection. Both the actual
armhf waypoint regression and extended P4 UI test passed under QEMU. The rebuilt
Debian package preserves the repository Logo and README, and its extracted
application initializes the real mesh identity, Agenda slot extent and waypoint
slot extent while rendering a 1232 x 568 frame with hardware RF disabled.
