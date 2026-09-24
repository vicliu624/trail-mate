# Trail Mate Agenda / Calendar Implementation Specification

**Status:** Implementation specification  
**Audience:** AI coding agent / repository maintainer  
**Canonical path:** `docs/specs/Agenda_Calendar_Implementation_Specification.md`

---

# 1. Purpose

Implement a lightweight Agenda / Calendar feature for Trail Mate.

This document is an **implementation specification**, not a product brainstorm.

An implementation agent MUST follow the existing Trail Mate architecture and MUST NOT introduce a parallel architecture for Calendar.

The feature is intentionally small.

Its core purpose is:

```text
At a certain time,
remind the user about something.
```

Location is optional.

An available SD card is required for Agenda and saved local waypoints.
Without an available card, Agenda is unavailable; internal flash is not a fallback.

If an event has a location, Trail Mate may additionally provide:

```text
Reminder
    ↓
Navigate
    ↓
Existing Map
```

But an event without a location is fully valid.

Examples:

```text
15:00  Drink water
18:00  Radio check
20:30  Check battery
```

are first-class events.

---

# 2. Product definition

The canonical model is:

```text
Event
├── Title             required
├── Date / Time       required
├── Reminder          optional
├── Location          optional
├── Repeat            optional
└── Note              optional
```

Location MUST NOT be treated as a required part of event creation.

The minimum useful event is:

```text
Title + Time
```

Example:

```text
Radio check
18:00
```

The most common enhanced form is:

```text
Radio check
18:00
Reminder: 10 minutes before
```

Location is a secondary enhancement:

```text
Leave campsite
07:00
Location: Pine Creek
Reminder: 30 minutes before
```

---

# 3. Explicit non-goals

V1 MUST NOT implement:

```text
Week View
complex Day timeline
tasks / todo subsystem
multiple calendars
calendar colours/categories
meeting attendees
invitations
CalDAV
Google Calendar
Exchange
iCalendar synchronisation
natural-language parsing
attachments
per-event timezone
complex RRULE
recurrence exceptions
"This occurrence only"
"This and future"
event sharing
cloud synchronization
```

Do not implement functionality merely because desktop or mobile calendar applications normally have it.

Trail Mate is not a PDA calendar replacement.

---

# 4. Repository architecture that MUST be respected

Current Trail Mate architecture is conceptually:

```text
Target Manifest
      ↓
Product Composition
      ↓
App Services / domain services
      ↓
Presentation Models
      ↓
Shell / Renderer
```

Parallel concerns include:

```text
Platform Runtime
Storage Backends
Capability Drivers
Protocol Cores
UX Packs
```

Agenda must fit this architecture.

It MUST NOT create a new shortcut architecture such as:

```text
LVGL page
   ↓
Agenda singleton
   ↓
LittleFS
   ↓
GPS
   ↓
Map globals
```

That is explicitly forbidden.

---

# 5. Existing repository structures relevant to this implementation

Before changing code, the implementation agent MUST inspect at least:

```text
docs/specs/Cross_Platform_Product_Architecture_Specification.md

modules/ui_presentation/include/ui_presentation/page/page_manifest.h
modules/ui_presentation/src/page/page_manifest.cpp

modules/ui_presentation/include/ui_presentation/workspace/presentation_workspace.h

modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/screen_registry.h

modules/product_composition/include/product_composition/target_profile.h
modules/product_composition/include/product_composition/presentation_bundle.h
modules/product_composition/include/product_composition/app_services_bundle.h

modules/ui_shared/src/ui/app_catalog_builder.cpp

modules/ui_shared/src/ui/screens/tracker/
modules/ui_shared/src/ui/screens/gps/

docs/map/gps_page_feature_summary.md
docs/audits/UI_SHARED_SPLIT_AUDIT.md

platform/esp/arduino_common/include/platform/esp/arduino_common/storage/storage_runtime.h
```

Do not implement Agenda based only on this specification without checking the current versions of those files.

The repository may evolve after this document is written.

---

# 6. Do not extend AppServicesBundle casually

Current:

```cpp
product_composition::AppServicesBundle
```

is intentionally small.

Its source explicitly states that it must not become another global `AppContext`.

Therefore:

DO NOT automatically add:

```cpp
AgendaService* agenda;
```

to `AppServicesBundle`.

Agenda service lifetime should be owned by the appropriate **target composition root**.

The target composition should wire:

```text
Agenda domain/service
Agenda storage port
Agenda presentation source
Agenda action sink
Reminder runtime
```

The UI receives presentation contracts.

The UI does not own service lifetime.

Only add anything to `AppServicesBundle` if a repository-wide composition review shows that this is now the established pattern.

---

# 7. Module placement

Agenda is cross-platform product/domain behaviour.

Therefore create:

```text
modules/core_agenda/
```

Do NOT create:

```text
modules/agenda_runtime/
modules/agenda_storage/
modules/calendar_platform/
modules/ui_agenda_runtime/
```

unless later architecture work proves a reusable independent responsibility exists.

V1 should remain structurally small.

Suggested layout:

```text
modules/core_agenda/
├── include/agenda/
│   ├── domain/
│   │   ├── event.h
│   │   ├── recurrence.h
│   │   └── occurrence.h
│   │
│   ├── ports/
│   │   ├── agenda_store.h
│   │   ├── agenda_clock.h
│   │   └── reminder_sink.h
│   │
│   └── usecase/
│       ├── agenda_service.h
│       ├── agenda_query.h
│       └── reminder_scheduler.h
│
├── src/
│   ├── agenda_service.cpp
│   ├── agenda_query.cpp
│   ├── recurrence.cpp
│   └── reminder_scheduler.cpp
│
├── tests/
│
└── library.json
```

No LVGL, Arduino, ESP-IDF, LittleFS, FreeRTOS or board header may appear inside `core_agenda`.

---

# 8. UI presentation placement

Portable presentation contracts belong in the existing:

```text
modules/ui_presentation/
```

Suggested:

```text
modules/ui_presentation/include/ui_presentation/agenda/
├── agenda_snapshot.h
├── agenda_source.h
├── agenda_action_sink.h
└── agenda_editor_model.h
```

If implementation requires `.cpp`:

```text
modules/ui_presentation/src/agenda/
```

Agenda presentation models MUST be:

```text
small
bounded
renderer independent
non-LVGL
```

---

# 9. LVGL page placement

Current embedded LVGL screens still live primarily under:

```text
modules/ui_shared/src/ui/screens/
```

and `ui_shared` remains a transitional umbrella while UX-pack migration proceeds.

Therefore V1 Agenda page should follow the **current working repository structure**, not an imagined future structure.

Suggested:

```text
modules/ui_shared/include/ui/screens/agenda/
modules/ui_shared/src/ui/screens/agenda/
```

with approximately:

```text
agenda_page_shell.cpp
agenda_page_runtime.cpp
agenda_page_components.cpp
agenda_page_input.cpp
agenda_state.cpp
```

Do not create one enormous:

```text
calendar.cpp
```

The separation should follow existing Tracker/GPS patterns:

```text
shell
    enter / exit only

runtime
    source/sink binding
    page lifecycle
    refresh orchestration

components
    LVGL object construction/rendering

input
    keyboard/touch/focus semantics

state
    current page UI state only
```

Business state MUST NOT live in `agenda_state.cpp`.

---

# 10. UX pack integration

A new Calendar page is not complete merely because it appears in:

```text
app_catalog_builder.cpp
```

Current registration is multi-stage.

The implementation agent MUST inspect and update the full chain:

```text
PageId
   ↓
PageManifest
   ↓
UX Pack
   ↓
ScreenId
   ↓
ScreenRegistry
   ↓
ScreenBindingRegistry
   ↓
App Catalog / Shell binding
```

At minimum inspect:

```text
ui_presentation/page/page_manifest.*
ui_lvgl_ux_packs/ux/screen_registry.h
relevant *_ux_pack.cpp
app_catalog_builder.cpp
```

Add:

```cpp
PageId::Calendar
```

and corresponding:

```cpp
ScreenId::Calendar
```

only where appropriate.

---

# 11. Registry capacity warning

Current:

```cpp
ScreenRegistry::kMaxScreens = 16
```

and:

```cpp
ScreenBindingRegistry::kMaxBindings = 16
```

Adding Calendar consumes capacity.

The implementation agent MUST count actual target entries before modifying these values.

Do not silently increase capacity.

If a target would exceed 16:

1. determine why,
2. determine whether Calendar belongs on that target,
3. only then consider increasing capacity.

Memory impact must be documented before changing these fixed arrays.

---

# 12. Target enablement

Do not enable Calendar on every target automatically.

First determine which target profiles correspond to the intended embedded products.

Initial requirement:

```text
480×222 LVGL class
320×240 LVGL class
```

Resolution alone MUST NOT be used to infer product capability.

Use:

```cpp
product_composition::TargetProfile
```

including:

```text
target_id
ux_pack_id
page_manifest_id
layout_profile_id
input capabilities
renderer
```

Enable Calendar through the appropriate PageManifest(s).

Do not add Calendar to:

```text
headless
ASCII
GTK
watch
other products
```

unless explicitly required.

---

# 13. RAM policy

This is a hard architectural constraint.

Agenda must be:

```text
disk-backed
scan-based
bounded-memory
allocation-light
```

There MUST NOT be a complete in-memory event database.

Forbidden persistent/runtime patterns include:

```cpp
std::vector<Event> events;
std::vector<EventOccurrence> occurrences;
std::map<EventId, Event>;
std::unordered_map<EventId, Event>;
```

The implementation MUST NOT preload all calendar events into RAM.

---

# 14. RAM budgets

Additional Agenda business/runtime RAM targets:

```text
Idle Agenda runtime:
< 1 KiB

Agenda page business snapshot:
< 2 KiB

Event editor draft:
< 1 KiB

Reminder scheduler:
< 256 B preferred
< 512 B hard target

Month picker domain/presentation state:
< 256 B

Waypoint picker Agenda-owned state:
< 1 KiB
```

These numbers exclude existing shared LVGL engine memory and existing Map buffers.

Agenda MUST NOT require PSRAM.

If PSRAM is absent, Agenda must still function normally.

---

# 15. Prefer SD I/O over permanent RAM

Agenda is a low-frequency human interaction feature.

Therefore the design explicitly chooses:

```text
SD I/O
instead of
permanent RAM residency
```

Scanning several KiB or tens of KiB when the user opens Agenda is acceptable.

Keeping tens of KiB permanently allocated is not acceptable.

---

# 16. Event record

Avoid dynamic strings in persistent records.

Use a fixed-size record.

Example conceptual structure:

```cpp
struct EventRecord
{
    uint32_t id;

    int64_t start_time;
    int64_t end_time;

    uint32_t reminder_offset_sec;

    int32_t latitude_e7;
    int32_t longitude_e7;

    uint8_t state;
    uint8_t flags;
    uint8_t repeat_type;
    uint8_t location_type;

    char title[40];
    char location_name[32];
    char waypoint_id[16];
    char note[80];

    uint32_t crc32;
};
```

Exact packing/alignment may be adjusted after static-size verification.

Do not enlarge fields casually.

---

# 17. Event flags

Location, Reminder and Note are optional.

Suggested flags:

```cpp
enum EventFlags : uint8_t
{
    HasReminder = 1 << 0,
    HasLocation = 1 << 1,
    HasNote     = 1 << 2,
    HasEndTime  = 1 << 3,
};
```

A normal event may be:

```text
HasReminder = false
HasLocation = false
HasNote = false
```

and is completely valid.

---

# 18. Location storage

Do not use `double` in the persisted Agenda record.

Use:

```cpp
int32_t latitude_e7;
int32_t longitude_e7;
```

Example:

```text
24.8731000°
→
248731000
```

This provides more than sufficient precision for the feature while reducing record size and making serialization deterministic.

---

# 19. Active event limit

V1:

```text
MAX_ACTIVE_EVENTS = 64
```

This is intentional.

Trail Mate is not intended to hold a lifetime calendar archive.

Do not increase this limit unless actual use demonstrates the need.

The storage format should permit future extension without changing the conceptual API.

---

# 20. Storage format

Use fixed-size slots rather than a heap-style variable-length event file.

Logical file:

```text
/agenda/events.dat
```

Conceptually:

```text
Header
Slot 0
Slot 1
Slot 2
...
Slot 63
```

A record may be padded to a convenient fixed slot size if justified.

Example:

```text
256 bytes × 64 slots
=
16 KiB
```

This is an acceptable use of SD storage.

---

# 21. Storage states

Record state should distinguish:

```text
Empty
Active
Deleted
```

Deletion should mark a slot reusable.

Creation should reuse empty/deleted slots before extending anything.

V1 should not require compaction during ordinary operation.

---

# 22. AgendaStore port

Core Agenda must not know LittleFS.

Define a port roughly equivalent to:

```cpp
class IAgendaStore
{
public:
    virtual ~IAgendaStore() = default;

    virtual bool readSlot(
        uint16_t slot,
        EventRecord& out) = 0;

    virtual bool writeSlot(
        uint16_t slot,
        const EventRecord& record) = 0;

    virtual bool eraseSlot(
        uint16_t slot) = 0;

    virtual uint16_t slotCount() const = 0;
};
```

The exact interface may be adjusted to match existing repository port conventions.

It MUST remain bounded and streaming-oriented.

Do not expose:

```cpp
loadAllEvents()
```

---

# 23. Platform storage adapter

ESP implementation belongs under the existing ESP platform tree, for example:

```text
platform/esp/arduino_common/
```

in a feature-appropriate subdirectory.

The exact path MUST follow nearby platform adapter conventions after repository inspection.

The UI page MUST NOT directly call:

```text
LittleFS.open()
SD.open()
fopen()
```

Agenda storage operations belong to the platform adapter.

---

# 24. SD storage is mandatory

Agenda events and saved local waypoints MUST reside on the SD card.
Use `/agenda/events.dat` for events and `/agenda/waypoints.dat` for saved local
waypoints. These are separate stores on the same required medium; neither is
an internal-flash cache or a copy synchronized to internal flash.
Their logical paths are relative to the existing SD runtime, not an internal
flash mount. Agenda MUST NOT initialize, format or use internal FFat, LittleFS
or NVS as its record store or as an automatic fallback.

Without an available SD card, Agenda MUST report storage unavailable and MUST
NOT permit event queries, creation, editing, deletion or waypoint save/select.
Reminder delivery also requires available SD storage, even when a next reminder
was cached before card removal. A closed Calendar page does not disable reminders
while the card remains available.

On card removal, mount loss or storage ownership transfer away from the
application, invalidate storage readiness and cached list/reminder results.
Dismiss an active Agenda reminder instead of allowing actions against an absent
store. Never acknowledge an incomplete write as successful. A bounded unsaved
editor draft may remain for explicit cancellation or retry, but MUST NOT be
silently written to a subsequently inserted card.

After insertion and successful application-owned remount, reopen and validate
the stores before enabling operations. Rebuild queries and reminder scheduling
from the mounted card; do not reuse record IDs, command results or occurrences
from a previous card. A different card is a different store.

Reuse the existing shared SD runtime for file operations, ownership, availability
and synchronization. Shared-SPI and SDMMC access policies remain responsibilities
of that runtime. Do not add a WIO-specific path or a separate Agenda SDMMC path.

Agenda and waypoints have their own bounded record stores. Reusing SD runtime
infrastructure does not mean reusing chat databases or unrelated route storage.
Storage choice stays in platform adapters and target composition, not the core
domain or LVGL components. Never format a card to recover an Agenda error.

---

# 25. Streaming queries

Agenda query APIs MUST NOT return an unbounded vector.

Prefer visitor/callback or fixed-capacity output.

Conceptually:

```cpp
bool scanEvents(
    IEventVisitor& visitor);
```

or:

```cpp
template<size_t N>
bool collectAgenda(
    FixedAgendaSnapshot<N>& out);
```

The essential rule is:

> scan one persistent record at a time and retain only information required by the current view.

---

# 26. Agenda page snapshot

The Agenda page only needs a handful of rows.

Example hard limits:

```text
Today      5 rows
Tomorrow   3 rows
Upcoming   2 rows
```

Total:

```text
10 rows maximum
```

Actual geometry may reduce this further.

Do not allocate rows for events the user cannot see.

---

# 27. AgendaRow presentation model

Example:

```cpp
struct AgendaRow
{
    uint32_t event_id;
    int64_t occurrence_start;

    char time[6];
    char title[40];
    char location[24];

    uint8_t flags;
};
```

AgendaRow MUST NOT contain:

```text
note
full EventRecord
repeat rule expansion
waypoint object
map object
```

---

# 28. Agenda home is the primary page

Calendar opens directly to:

```text
Agenda
```

not Month View.

The Agenda home creation button uses the localized label `New`; `New Event`
may be used as the editor title, not the compact home button label.

Typical 480×222 content:

```text
TODAY

09:30  Team meeting
14:30  Check campsite
18:00  Radio check

TOMORROW

07:00  Leave campsite
```

Only show location if present.

Example with location:

```text
14:30  Check campsite      Pine Creek
```

Example without:

```text
18:00  Radio check
```

No placeholder such as:

```text
Location: none
```

should appear on the Agenda list.

---

# 29. Information density

480×222 is a very short landscape screen.

The implementation MUST optimize for vertical density.

Avoid:

```text
large cards
large icons
large paddings
two-line rows by default
decorative whitespace
multiple nested panels
```

Use:

```text
TopBar
section label
compact rows
thin separators
focused row highlight
minimal footer/help
```

For 480×222, ordinary event rows should target approximately one text line.

320×240 may use different layout geometry.

Do not scale the 480×222 design uniformly.

---

# 30. Visual style

Use the repository canonical firmware style:

```text
Amber      #EBA341
AmberDark  #C98118
WarmBG     #F6E6C6
PanelBG    #FAF0D8
Line       #E7C98F
Text       #6B4A1E
TextDim    #8A6A3A
Warn       #B94A2C
Ok         #3E7D3E
Info       #2D6FB6
```

Use existing shared theme/helpers where available.

Do not introduce Calendar-specific dark themes or additional decorative colours.

---

# 31. TopBar

Use:

```cpp
ui::widgets::TopBar
```

where the current target/page conventions support it.

Do not implement a Calendar-specific TopBar.

For the known 480×222 class, existing pages commonly reserve approximately 30 px for shared TopBar geometry.

However:

> geometry must be derived from the appropriate layout/profile and existing page convention, not from screen width alone.

---

# 32. Pages / states

V1 needs only the following user-facing states:

```text
1. Agenda
2. Event Detail
3. Event Editor
4. Date Picker
5. Time Picker
6. Reminder Picker
7. Repeat Picker
8. Location Picker
9. Waypoint Picker
10. Reminder Popup
```

`Map Select` is NOT a new Calendar page.

It is an operating mode of the existing Map workflow.

---

# 33. Event editor field order

Use:

```text
Title
Date
Time
Reminder
Location
Repeat
Note
```

This ordering is deliberate.

Location must not appear to be mandatory.

Required fields:

```text
Title
Date
Time
```

Everything else is optional.

---

# 34. Reminder presets

V1:

```text
None
At time
10 min before
30 min before
1 hour before
1 day before
```

No custom reminder editor.

---

# 35. Repeat presets

V1:

```text
None
Daily
Weekly
Monthly
Yearly
```

Do not implement recurrence exceptions.

Editing a recurring Event changes the whole series.

---

# 36. Recurrence must not expand into stored events

A weekly recurring event remains one record.

Never create:

```text
52 records
```

for one year of weekly events.

Queries calculate occurrences on demand.

---

# 37. Month picker

Month view is a picker, not a major browsing mode.

It needs only:

```text
current month
selected date
event-presence markers
```

Event-presence state for a month should preferably be represented as:

```cpp
uint32_t occupied_days;
```

One bit per day.

Do not construct 31 day objects containing event lists.

---

# 38. Reminder runtime

Reminder scheduling belongs to `core_agenda` use-case/runtime logic connected through target composition.

It must remain alive independently of the Calendar page.

The page may be destroyed while reminders continue to function, provided SD
storage remains available. Card removal suspends delivery and invalidates the
cached next reminder; validated remount triggers recalculation.

---

# 39. Reminder scheduler RAM model

The scheduler should retain only the next relevant reminder.

Conceptually:

```cpp
struct NextReminder
{
    bool valid;

    uint32_t event_id;

    int64_t occurrence_start;
    int64_t trigger_time;
};
```

Do not keep a queue of all future reminders.

---

# 40. Reminder recalculation

Rescan persistent events when:

```text
startup
event created
event edited
event deleted
reminder dismissed
snooze completed
clock materially changes
SD storage becomes unavailable (invalidate; do not scan)
SD storage becomes available after validated remount
```

Do NOT continuously rescan the event database every second.

Normal tick behaviour should be approximately:

```cpp
if (next.valid &&
    now >= next.trigger_time)
{
    trigger();
}
```

---

# 41. Reminder popup without location

This is the normal case.

Example:

```text
RADIO CHECK

18:00

[Snooze 10 min] [Done]
```

Do not display disabled Navigate controls.

---

# 42. Reminder popup with location

If:

```text
HasLocation == true
```

show:

```text
LEAVE CAMPSITE

07:00
Pine Creek

[Navigate] [Snooze] [Done]
```

Navigate is conditional.

---

# 43. Location priority

Location is **P2 enhancement**, not the foundation of Calendar.

Implementation priority:

```text
P0
Title
Date / Time
Agenda
Event CRUD
Reminder

P1
Repeat
Note

P2
Location
Waypoint
Map integration
Navigate
```

The feature should already be useful before P2 exists.

---

# 44. Location picker

When implemented, only:

```text
Current position
Choose on map
Saved waypoint
```

No:

```text
manual coordinates
recent places
address search
POI search
location history
```

in V1.

---

# 45. Map reuse

Current Map implementation already has a shared map viewport and MapWorkspaceModel.

Do not create another map renderer.

Do not reintroduce page-level tile ownership.

Do not touch filesystem map tile paths from Calendar.

Map integration must use the existing Map workspace/runtime boundaries.

---

# 46. Map selection

When Calendar needs a coordinate, existing Map should enter a temporary semantic mode equivalent to:

```text
SelectLocation
```

Do not duplicate the GPS page.

Selection UX:

```text
fixed centre crosshair
map pans underneath it
Pick
Cancel
```

Selected coordinate is the current viewport centre.

---

# 47. Map lifetime and RAM

Do not keep Event Editor's full LVGL tree alive underneath the Map page solely to return from location selection.

Before opening heavy Map UI, retain only a small draft:

```text
EventDraft
focused field / editor state
return context
```

Recreate the editor UI on return if that better matches existing shell lifecycle.

Prefer recomputation and reconstruction over simultaneously retaining multiple heavy object trees.

---

# 48. Waypoint query

Do not return all waypoints in:

```cpp
std::vector<Waypoint>
```

for Agenda.

Consume the existing waypoint source via bounded/paged access if available.

V1 includes saving named local waypoints and selecting them from SD storage.
The reusable waypoint store is independent of Agenda events and has no internal
flash fallback. Saving a waypoint does not implicitly save an event draft.

If the current repository does not expose an appropriate bounded interface, add the smallest adapter necessary.

A Waypoint picker should only keep one visible page in RAM.

---

# 49. Input

Input capability comes from target/UX profile.

Do NOT infer input style from:

```text
320×240
480×222
```

The same presentation semantics should support:

```text
touch
keyboard
direction keys
trackball
```

through existing UX bindings and page input conventions.

---

# 50. Localization

All user-visible strings must use the existing localization mechanism.

Do not hard-code final English UI labels directly into Agenda components.

Examples:

```text
Today
Tomorrow
Upcoming
New Event
Reminder
Repeat
Location
No plans today
Snooze
Done
Navigate
```

must enter the existing translation pipeline.

---

# 51. Build integration

Adding source files is not sufficient.

The implementation agent MUST inspect and update all applicable build paths.

At minimum inspect:

```text
apps/esp32_lvgl/CMakeLists.txt

cmake/TrailMateLinuxSources.cmake

cmake/TrailMateUxPacks.cmake

builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake

scripts/platformio-pre.py
```

Only modify paths actually required by enabled targets.

Do not assume PlatformIO, ESP-IDF and Linux automatically share new files.

---

# 52. Phase 1 — repository impact audit

Before adding production code:

1. identify intended 480×222 targets,
2. identify intended 320×240 targets,
3. identify their `page_manifest_id`,
4. identify their `ux_pack_id`,
5. identify current screen registry counts,
6. identify composition roots,
7. identify shared SD storage adapters, ownership and card-availability lifecycle,
8. identify current clock source,
9. identify reminder/modal presentation mechanism,
10. identify Map selection/navigation integration points.

Record findings in the implementation PR or development notes.

Do not guess these points.

---

# 53. Phase 2 — core Agenda

Implement only:

```text
EventRecord
IAgendaStore
AgendaService
occurrence calculation
bounded Agenda query
ReminderScheduler
```

Add unit tests.

No LVGL.

No Map integration.

---

# 54. Phase 3 — storage adapter

Implement the SD-backed platform adapter through the shared SD runtime.
Host filesystem fixtures are test adapters, not alternative firmware backends.

Validate:

```text
empty store
create
update
delete
restart/reload
invalid record
CRC failure
full store
SD absent at boot
card removal during query/write/reminder delivery
reinsertion and validated remount
replacement by a different card without cross-card state reuse
no internal-flash fallback or automatic formatting
```

Do not load all records during startup.

---

# 55. Phase 4 — minimum UI

Implement:

```text
Agenda
Event Detail
Event Editor
Date Picker
Time Picker
Reminder Picker
Reminder Popup
```

At this point the complete feature must already work with:

```text
no location
no map dependency
```

Required end-to-end scenario:

```text
Calendar
↓
New
↓
Title = Radio check
↓
18:00
↓
Reminder = 10 min before
↓
Save
↓
17:50 reminder appears
↓
Snooze / Done
```

This is the primary V1 path.

---

# 56. Phase 5 — Repeat

Add:

```text
Daily
Weekly
Monthly
Yearly
```

Keep storage unchanged except repeat metadata.

Test recurrence thoroughly.

---

# 57. Phase 6 — optional Location

Only after the basic Agenda is stable:

```text
Current position
Saved waypoint
Choose on map
Navigate
```

Location integration must not increase idle Agenda RAM meaningfully.

---

# 58. Phase 7 — UX pack / device validation

Validate separately on intended:

```text
480×222 profile
320×240 profile
```

Do not use screenshots scaled to approximately the target aspect ratio.

Actual screenshot/canvas validation must use the real framebuffer geometry.

---

# 59. Unit tests

Core tests must include:

```text
single event
point event
event with end time

today query
tomorrow query
upcoming query

stable ordering

daily recurrence
weekly recurrence
monthly recurrence
yearly recurrence

January 31 rollover
February 29 rollover

reminder calculation
reminder without location
reminder with location

storage full
deleted-slot reuse
CRC failure
reload

bounded result capacity
```

---

# 60. RAM tests

Add explicit tests/assertions where practical for bounded structures.

The implementation should make obvious at compile time that:

```text
AgendaSnapshot has fixed capacity
EventRecord has fixed size
Reminder scheduler does not contain dynamic collections
Month occupancy is bounded
```

Record:

```cpp
sizeof(EventRecord)
sizeof(AgendaSnapshot)
sizeof(ReminderScheduler state)
```

in a test or diagnostic assertion where appropriate.

Unexpected growth should be treated as a regression.

---

# 61. Forbidden implementation patterns

The implementation agent MUST NOT introduce:

```cpp
static std::vector<Event> g_events;
```

or equivalent.

MUST NOT introduce:

```cpp
std::vector<EventOccurrence>
```

for ordinary query paths.

MUST NOT create:

```text
CalendarMapPage
```

MUST NOT make LVGL read/write LittleFS directly.

MUST NOT make Calendar depend on PSRAM.

MUST NOT make Location mandatory.

MUST NOT keep all waypoint records in Agenda RAM.

MUST NOT implement recurrence by copying event records.

MUST NOT add Agenda service to arbitrary global contexts merely for convenience.

MUST NOT enable Calendar on every PageManifest without target review.

---

# 62. Definition of Done

Calendar V1 is complete when all of the following are true:

```text
Calendar is correctly registered through the PageManifest / UX Pack chain.

Only approved target manifests expose it.

Agenda is the default page.

Events can be created, viewed, edited and deleted.

An event requires only title + date/time.

Location is optional.

Reminder is optional.

Events and saved waypoints survive restart on the same SD card.

Agenda requires an available application-owned SD card.

No card means no event/waypoint operations or reminder delivery.

Removal invalidates cached state; validated remount rebuilds it from the card.

No incomplete write is reported successful or silently retried on another card.

No internal-flash fallback or automatic formatting exists.

No full event table resides in RAM.

Agenda queries are bounded.

Reminder scheduler retains only bounded state.

Reminder works while Calendar page is closed and SD storage remains available.

480×222 has a dedicated usable layout.

320×240 has a dedicated usable layout.

Input follows target UX capabilities.

All visible strings are localizable.

Existing Map behaviour is unchanged before location integration.

Map integration reuses the existing Map workspace.

Core unit tests pass.

Storage recovery tests pass.

RAM budgets are respected.
```

---

# 63. Architectural intent

The implementation must preserve this relationship:

```text
                 ┌───────────────┐
                 │  core_agenda  │
                 │               │
                 │ Event         │
                 │ Query         │
                 │ Reminder      │
                 └───────┬───────┘
                         │
             presentation source/sink
                         │
                         ▼
              ┌───────────────────┐
              │ ui_presentation   │
              │ bounded snapshot  │
              └─────────┬─────────┘
                        │
                        ▼
              ┌───────────────────┐
              │ LVGL Agenda page  │
              │ ui_shared / UX    │
              └───────────────────┘
```

Storage:

```text
core_agenda
    │
 IAgendaStore
    │
    ▼
platform adapter
    │
    ▼
shared SD runtime (SPI / SDMMC)
    │
    ▼
SD card (required; no internal-flash fallback)
```

Optional map path:

```text
Agenda Event
    │
HasLocation
    │
    ▼
presentation action
    │
    ▼
existing Map workspace/runtime
```

The Map does not become part of the Agenda core.

---

# 64. Final implementation principle

When this document does not explicitly define a feature decision, choose the option with:

```text
less permanent RAM
less dynamic allocation
fewer persistent states
fewer pages
fewer dependencies
smaller UI surface
better reuse of existing repository architecture
```

Do not use ambiguity as permission to add functionality.

The feature should remain useful even if Map integration is entirely removed.

The core product statement is:

> Trail Mate Agenda reminds the user about something at the right time. Location and navigation are optional enhancements.
