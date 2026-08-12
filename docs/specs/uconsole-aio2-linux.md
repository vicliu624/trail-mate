# uConsole AIO2 Linux Specification

Status: baseline draft
Updated: 2026-05-09

This document defines the architectural baseline for a future Trail Mate Linux
target running on a ClockworkPi uConsole terminal with an AIO2 module installed.

It is a constraint document for future implementation. It is not a feature
checklist, not a shell scaffold, and not a UI mockup.

## 1. Purpose

The uConsole/AIO2 target has a different interaction surface from the current
Cardputer Zero Linux work. It has a larger display, a keyboard-oriented usage
model, and a stronger expectation of desktop-like software behavior.

It also has a different capability envelope. A Linux handheld has materially
more CPU, memory, storage, networking, process, and background-task capacity
than the MCU firmware targets. The uConsole/AIO2 line must therefore preserve
room for Linux-native product features instead of being limited to MCU feature
parity.

Without a separate specification, the shortest implementation path would be to
treat it as another large-screen LVGL profile. That would create the wrong
boundary: the compact handheld shell would keep defining the product shape even
when the target needs a different shell.

This specification prevents that drift.

## 2. Current Confusions

- `uConsole/AIO2` is not just another `linux_rpi` runtime parameter.
- `uConsole/AIO2` is not just the Cardputer Zero simulator with a larger
  geometry.
- `uConsole/AIO2` is not just another `modules/ui_shared` menu profile.
- `AIO2` is not a UI concept. It is a hardware/capability provider.
- `desktop-like UI` does not mean a separate copy of Trail Mate business logic.
- `LVGL reuse` does not mean reusing the same compact app-grid interaction
  model.
- `shared core` does not mean every Linux feature must be representable on MCU
  targets.
- `MCU compatibility` is not the product ceiling for the Linux line.
- `apps/linux_unoq` is only a placeholder for UNO Q today. It must not silently
  become the uConsole target unless the engineer explicitly decides those are
  the same product target.

## 3. Candidate Distinctions

### App Services

The service composition layer owns application capabilities such as:

- chat service
- contact service
- team service
- config persistence
- runtime/event ticking
- local mesh or real mesh adapter selection
- demo/local/device mode composition

This layer must not own LVGL object graphs, screen navigation, desktop layout,
or AIO2 device details.

### Platform Runtime

The platform runtime layer owns OS and hardware integration:

- Linux paths and storage roots
- input devices
- display/display-host setup
- power and battery hooks
- radio/GPS/audio/network adapters
- AIO2 module integration

This layer may know Linux and AIO2 details. It must not define product screens
or application navigation.

### Presentation Model

The presentation model layer owns UI-facing state and actions:

- chat list state
- conversation state
- contact detail state
- team status state
- map/sidebar state
- settings sections
- user actions such as send message, select contact, toggle mode, clear store

This layer should be stable enough that compact LVGL pages and a desktop-like
uConsole shell can consume the same state/actions through different layouts.

It must not expose LVGL objects, SDL objects, Linux device paths, or AIO2 driver
objects.

### Shell UI

The shell UI owns the interaction surface:

- compact handheld shell for small-screen targets
- desktop-class shell for uConsole/AIO2

These shells may share presentation models and assets where useful, but they do
not have to share layout structure.

### Capability Tier

The capability tier distinguishes what a target can reasonably support:

- MCU targets: constrained memory, storage, background work, and UI density.
- Compact Linux targets: stronger runtime but still handheld/small-screen.
- uConsole/AIO2: desktop-class handheld with room for richer workflows,
  background services, larger local data, and more advanced integrations.

Capability tier is not a business domain object. It is a product/runtime
constraint used to decide where features may live and which shells are expected
to expose them.

### Linux-Native Feature Plane

Linux-native features are valid product growth areas that may never exist on MCU
targets.

Examples include:

- richer local search and filtering
- larger local message/contact/map indexes
- background synchronization or import/export jobs
- multi-pane operational dashboards
- richer map/cache/package management
- local diagnostics and logs
- external tool integration
- file-oriented workflows
- higher-frequency telemetry visualization

These features may reuse shared core where appropriate, but they are not
required to force their full behavior into MCU-compatible contracts.

## 4. Invalid Distinctions

The following cuts are explicitly invalid:

- `small screen vs big screen` as the only architectural distinction.
- `LVGL profile` as the boundary for uConsole behavior.
- `AIO2 screen` as a product concept.
- `desktop shell` as a forked copy of chat/contact/team services.
- `apps/linux_rpi` as the place to accumulate every Linux device variant.
- `ui_shared` as the mandatory owner of every possible UI layout.
- `MinimalLinuxAppFacade` as both the service composition root and the final UI
  boundary for all Linux targets.
- `MCU parity` as the acceptance definition for Linux product work.
- `shared module` as a place to hide Linux-only expansion just because the code
  is useful.

## 5. Baseline Decisions

- The uConsole/AIO2 target is a desktop-class Linux handheld target.
- It shares Trail Mate app services with the Linux line.
- It does not inherit Cardputer Zero's compact menu UX as its primary
  interaction model.
- AIO2 support belongs in platform/runtime adapters, not in the shell UI.
- The Linux line may grow features that are materially different from the MCU
  firmware targets.
- Shared cross-target services define the common product foundation, not the
  upper bound of the Linux product.
- Linux-only features must have explicit ownership and contracts so they do not
  leak backward into MCU builds by accident.
- The first implementation should extract app service composition before
  building a rich uConsole UI.
- The existing `MinimalLinuxAppFacade` may remain as a compatibility adapter for
  the current LVGL shared shell.
- `LinuxAppServices` is the current Linux app service composition boundary;
  uConsole work should depend on it or presentation models above it, while
  compact shells use `MinimalLinuxAppFacade` as a compatibility adapter.
- UI toolkit choice is a shell/platform decision, not an app-service boundary.
- Linux/uConsole local state is persisted through SQLite, not ad hoc per-file
  key/value stores.
- Linux/uConsole map base tiles may be fetched online and cached locally using
  the existing `maps/base/{osm,terrain,satellite}/{z}/{x}/{y}` layout, with
  cache metadata stored in SQLite.
- Linux/uConsole contour lines are a map overlay, not a base map source. The
  GTK map shell must read transparent PNG contour tiles from
  `maps/contour/{major|minor}-{interval}/{z}/{x}/{y}.png`, with the same zoom
  profile selection used by Trail Mate Center (`z8 major-500`, `z9 major-200`,
  `z10 major-500/minor-100`, `z11 major-200/minor-50`,
  `z12 major-100/minor-50`, `z13..14 major-100/minor-20`,
  `z15..16 major-50/minor-10`, and `z17+ major-25` plus optional `minor-5`).
- Earthdata credentials are Linux/uConsole map data-source credentials. They
  are persisted in SQLite settings, not in the cross-target `AppConfig` blob,
  and must not be presented as a rendering toggle.
- A missing contour generation backend must be visible as missing cached
  contour tiles or missing Earthdata credentials. The UI must not imply that
  contour generation is working when only cached overlay rendering exists.
- BLE is not a Linux/uConsole product capability; Linux shells must report it
  as unused/unsupported rather than exposing a disabled fake toggle.
- A future uConsole shell should depend on presentation models/actions, not on
  the compact handheld page implementation.
- The current LVGL uConsole shell is a bring-up and fallback shell. It is not a
  commitment that the long-term uConsole product UI must stay on LVGL.
- `UConsoleChatWorkspaceModel` is the first concrete uConsole presentation/action
  slice; future GTK work should reuse this boundary before adding toolkit
  objects.
- The current verified uConsole framebuffer reports `720x1280`; the fallback
  LVGL shell should default to that physical orientation until a rotation-aware
  display adapter exists.
- The likely app shell name is `apps/linux_uconsole`. Using
  `apps/linux_unoq` for this target requires an explicit decision that UNO Q and
  uConsole/AIO2 are the same product target in this repository.

## 6. Normative Target Layers

### Layer A: Linux App Services

Likely future location:

- `platform/linux/common/include/app/`
- `platform/linux/common/src/app/`

Responsibilities:

- compose chat/contact/team/config/runtime services
- select demo/local/device service implementations
- own lifecycle for service startup/shutdown
- expose service access through stable app-facing interfaces

Forbidden knowledge:

- LVGL object graph
- shell navigation
- compact menu layout
- desktop window layout
- SDL geometry
- fbdev/evdev concrete paths
- AIO2 driver details

### Layer B: Facade Compatibility

Likely future location:

- `platform/linux/common/include/app/linux_app_facade.h`
- `platform/linux/common/src/app/linux_app_facade.cpp`

Responsibilities:

- adapt the Linux app services to the existing `app::IAppFacade` interface
- keep the current compact LVGL UI working during migration
- preserve old access patterns until presentation models replace them feature by
  feature

Rule:

- This adapter must shrink over time. New uConsole UI work should not deepen its
  role as the universal boundary.

### Layer C: Presentation Models and Actions

Likely future locations:

- feature-specific shared presentation modules under `modules/*`
- or a carefully scoped shared presentation area once repeated patterns are
  proven

Responsibilities:

- read app service state into UI-ready value objects
- expose user actions as UI-independent commands
- normalize formatting and selection state that both shells need

Forbidden knowledge:

- LVGL types
- Linux paths
- AIO2 driver objects
- app shell entrypoints
- compact menu layout assumptions

### Layer D: Shell UI Implementations

Compact handheld shell:

- may continue using the current shared LVGL app menu and page shells
- remains optimized for small displays and directional/hybrid navigation

uConsole desktop-class shell:

- owns a different layout and navigation model
- should prefer multi-pane, keyboard-friendly, information-dense workflows
- may use LVGL initially if that keeps implementation risk low
- must remain replaceable by another desktop UI technology if a future decision
  warrants it

Rule:

- Sharing presentation state is encouraged. Forcing identical layout structure is
  not.

Packaging rule:

- Linux device shells should provide standard Debian packages when targeting
  Debian-family handheld environments. The package should install a normal
  command under `/usr/bin` and keep launch/runtime options explicit rather than
  hiding framebuffer, input, or capability assumptions in ad hoc scripts.

### Layer E: AIO2 Platform Adapters

Likely future locations:

- `platform/linux/aio2/`
- or `platform/linux/uconsole/` if the adapters are inseparable from the
  uConsole target

Responsibilities:

- expose AIO2 hardware capabilities through platform contracts
- report honest capability status
- keep driver/device concerns below the app service and presentation layers

Current AIO2 LoRa binding facts:

- The HackerGadgets AIO2 SX1262 path is a board-level binding, not generic
  Linux SPI auto-detect.
- The LoRa power gate is `GPIO16`; GPS power is `GPIO27`.
- The SX1262 control lines are `Reset=GPIO25`, `Busy=GPIO24`, and
  `IRQ/DIO1=GPIO26`.
- The radio requires `DIO2_AS_RF_SWITCH=true` and `DIO3_TCXO_VOLTAGE=1.8`.
- The expected SPI endpoint is `spidev1.0`, which requires the
  `dtoverlay=spi1-1cs` boot overlay. Treating an unrelated visible spidev node
  such as `spidev4.0` as the LoRa endpoint is invalid unless the board overlay
  explicitly proves that wiring.
- GPS power (`GPIO27`) and a GPS fix are separate facts. On the verified CM4
  AIO2 image, `/dev/ttyS0` at `9600` is the receiver default after GPIO27 is
  asserted. The Settings → GPS receiver path and baud form a persisted override
  choice; it rejects the ClockworkPi USB CDC control serial. An explicit
  `TRAIL_MATE_GPS_DEVICE`, NMEA file, or position injection remains
  authoritative. NMEA traffic without a valid fix is reported as `No fix`, not
  as a working position; antenna state and sky visibility remain physical facts.
- The uConsole startup binding sets `TRAIL_MATE_GPS_AUTO_SERIAL=0` explicitly,
  even when inherited process state requested probing. This prevents the shared
  Linux candidate list from reintroducing the uConsole USB control serial.

Forbidden ownership:

- screen composition
- product navigation
- domain rules
- chat/contact/team business state

### Layer F: Linux-Native Feature Modules

Likely future locations:

- `modules/linux_*` only if the feature is platform-neutral within Linux but not
  MCU-compatible
- `platform/linux/common` if the feature is an OS/runtime adapter
- `apps/linux_uconsole` only if the behavior is shell composition or
  target-specific workflow

Responsibilities:

- provide features that are justified by Linux capacity
- preserve clear contracts to app services and presentation models
- keep MCU builds free from Linux-only dependencies

Allowed examples:

- search/indexing services over local stores
- background import/export workers
- advanced map package management
- online map tile fetch/cache workers with SQLite metadata
- diagnostic log viewers
- Linux host integration helpers

Rules:

- Linux-native modules may depend on Linux-capable contracts.
- They must not become implicit requirements for MCU builds.
- They must not duplicate shared core services when an extension around shared
  services is sufficient.
- They must be named and documented as Linux-native so future contributors do
  not mistake them for cross-target foundation.

## 7. Legal Dependency Direction

The intended direction is:

```text
apps/linux_uconsole
    -> uConsole shell UI
    -> presentation models/actions
    -> Linux app services / core services
    -> optional Linux-native feature modules
    -> platform contracts
    -> platform/linux/common + platform/linux/aio2 adapters

compact Linux shells
    -> existing shared LVGL shell
    -> MinimalLinuxAppFacade compatibility adapter
    -> Linux app services / core services
```

Rules:

- App services must not depend on shell UI implementations.
- Presentation models must not depend on LVGL or AIO2 driver details.
- Shell UIs must not instantiate chat/contact/team services directly.
- AIO2 adapters must not define product screens.
- The compact shell and uConsole shell must be able to evolve separately while
  sharing service composition.
- Linux-native feature modules may extend the product beyond MCU behavior, but
  their dependencies must remain out of MCU app shells.
- Shared core should hold common domain truth. Linux-native modules should hold
  Linux-only scale, indexing, background work, integration, and workflow
  extensions.
- Inbound chat message identity is shared domain truth. A platform adapter may
  suppress repeated RF frames as a transport optimization, but it must not be
  the only owner of "this chat message was already received" behavior. The
  `ChatService` ingress boundary is responsible for preventing duplicate
  `(protocol, channel, sender, peer, message id)` text messages from entering
  the model/store, and unread aggregation must derive from shared conversation
  metadata rather than toolkit-local state.

## 8. uConsole UI Product Rules

The uConsole shell should feel closer to desktop software than to compact
firmware UI.

Expected characteristics:

- persistent navigation or command surface
- multi-pane layouts where useful
- keyboard-first interaction paths
- dense but readable information hierarchy
- compact menu-bar navigation instead of a large hero/header region
- persistent bottom status bar for AIO2, LoRa, GPS, storage, and background
  work state
- stable side panels or in-workspace panels for team, device, or capability
  details when they add useful density
- menu-bar workspaces must be real navigation targets. Hardware, Data, and
  Settings may start as read-only state surfaces, but they must not be disabled
  placeholder buttons.
- Overview, if present, is an operational dashboard: compact location/map
  context, current hardware state, message status, team activity timeline, and
  runtime details. It must not become a decorative landing page.
- long-running task visibility for imports, sync, indexing, diagnostics, or
  package management
- workflows that assume larger local storage and richer local datasets
- direct access to common workflows without returning through a tiny app grid
- honest disabled/simulated/unsupported capability states

Feature layout direction:

- Chat: conversation list, active conversation, and detail/context panel.
- Contacts: searchable table/list plus detail and trust/protocol status.
- Map: map canvas plus team/device/status sidebars.
- Team: roster, pairing/status, and recent activity panels.
- Settings: grouped sections or tabs, not a long small-screen settings page.

Linux-native feature direction:

- Search: global or feature-local search over contacts, messages, map packages,
  routes, and logs.
- Data management: import/export, local package management, and visible storage
  state.
- Diagnostics: runtime logs, device capability status, network/radio state, and
  background task status.
- Maps: larger cache management, richer layer/package workflows, and side-panel
  inspection.
- Team operations: denser roster/activity views and history-oriented workflows.

Non-goals:

- marketing-style landing screens
- oversized hero pages
- large header/status regions that consume the uConsole vertical workspace
- decorative dashboard cards that reduce operational density
- forcing every page into the current compact 12-entry app menu model

### GTK Workbench Information Architecture

The production uConsole GTK shell uses a workbench layout, not independent
small-screen pages stacked into a larger window.

Global surfaces:

- the top menu bar is only for app identity and workspace navigation;
- the bottom status bar is only for live runtime state that should remain
  visible everywhere;
- workspace bodies must not repeat large page titles, subtitles, or tutorial
  prose;
- each workspace must express its primary task through one dominant pane plus
  secondary rails or inspectors when useful.

Normative GTK layout geometry:

- Layout numbers are product constraints, not incidental implementation
  guesses. GTK page code must reference the named constants in
  `apps/linux_uconsole/src/platform/gtk/gtk_uconsole_layout_spec.h` instead of
  inventing local hard-coded pane widths.
- Long runtime strings must never determine pane width. Coordinates, tile
  status, cache counters, paths, failures, and protocol facts must wrap,
  split into separate data rows, truncate, or move secondary detail to a
  tooltip/log surface.
- The bottom status bar is a global surface and must remain visible on every
  workspace. Workspace bodies reserve status-bar space; no page-level layout may
  push the status bar below the visible window.
- There is no Overview/Dashboard workspace. It duplicated live information
  without providing a field task, so it is not a launch surface or a navigation
  destination. The initial workspace is Map.
- Location is operated in Map and GPS & sky plot; conversations are operated in
  Chat; packet history is operated in Logs; radio configuration and diagnostics
  are operated in Radio tools. The global status bar is the only cross-workspace
  runtime summary.
- Chat is three columns: narrow conversation rail, dominant transcript/composer
  column, narrow node/contact inspector rail. Conversation rail is `216px`; node
  inspector rail is `220px`; only the transcript/composer column expands.
- Map is canvas-first. The tile viewport owns all available workspace area by
  default. Layer controls and measurement tools are explicit buttons in a
  compact overlay toolbar; their detailed inspectors are overlay drawers, not
  permanently reserved rails. Opening a drawer may cover tiles temporarily but
  must never shrink the tile viewport.
- The layer and tools overlay drawers have a maximum width of `236px`; their
  toolbar is compact and must not become a second navigation rail.
- Map overlay drawers own their vertical overflow. If controls, tile status,
  contour status, cache status, or tools exceed the visible height, the drawer
  scrolls internally. Closing it returns every pixel of workspace width to the
  map and must not force the global window/body to scroll.
- The map tile viewport must fill the available canvas. Fixed-aspect frames,
  letterboxing, permanent grey bands, or any other decorative area between the
  map workspace and the rendered tile surface are invalid. Grey may appear only
  as a transient missing/loading tile placeholder inside the tile grid.
- Map coordinates must be displayed as separate rows, such as `lat:` and `lon:`,
  not as a single sentence. Tile/cache/download status must be rendered as
  short multi-row data items, not as one long prose line.
- If a label, switch, button, or status field needs more room than its drawer,
  the fix is wrapping, a tooltip, or moving secondary detail to Logs/Data.
  Shortening established domain labels such as `Terrain`, `Satellite`, region
  codes, protocol names, or radio parameter names is invalid unless the
  abbreviation is already standard in that domain. Changing overlay drawer
  geometry requires updating this specification first.
- The default uConsole GTK shell is designed for a landscape desktop-class
  handheld workspace. Persistent chrome must not visually dominate the map,
  transcript, or overview center column; map inspectors appear only when the
  user explicitly opens them.

Workspace rules:

- Overview is an operational dashboard. It prioritizes location/map context,
  hardware state, message state, team activity, and runtime details in that
  order. It must not become a decorative landing page.
- Map is a canvas-first workspace. The map should fill the body, with compact
  overlays for source/layer tools and status. Tile borders, card-like tile cells,
  and large control panels are invalid. The map view has its own user-controlled
  center; drag/pan changes the map view center and must recalculate tiles and
  overlays from the model rather than moving only GTK widgets. Pointer-context
  actions such as right-click "center here" and "zoom here" operate on the
  coordinate under the pointer, not on the global map center.
- Chat is a four-part workbench: narrow thread rail, dominant transcript,
  node/contact inspector, and compact compose surface. The inspector projects
  real `ContactService` node facts such as NodeInfo names, source, signal, and
  Position. It must not derive its own node model from chat rows.
- Hardware is a status matrix plus capability/driver detail surface. It must
  distinguish hardware endpoint presence, driver binding, and runtime readiness.
- Data is a storage and cache operations surface. Counts and cache health are
  primary; paths and roots are secondary details.
- Logs is a diagnostics surface. Packet direction, parsed fields, and raw hex
  must be visually distinct without forcing the user to read long prose.
- Settings is a control plane. It uses grouped navigation and compact aligned
  rows; it must not be a single long embedded-device settings scroll. Protocol
  settings must be conditional: Meshtastic, MeshCore, and raw LoRa/RNode/LXMF
  controls are not simultaneously valid user surfaces. Meshtastic region and
  modem preset controls must use their protocol labels such as `CN`, `EU_433`,
  `ANZ`, and `LongFast`; exposing their stored enum integers as the primary UI
  is invalid. Region-specific radio constraints, such as TX power limits, must
  be applied from the protocol region table rather than duplicated in the GTK
  view.
- On Linux, the SX126x driver is platform-specific, but Meshtastic RF parameter
  derivation is not. Frequency, bandwidth, spreading factor, coding rate,
  preamble, sync word, and TX power limits must come from the same shared
  Meshtastic radio-configuration helper used by the ESP environment. Linux may
  map that shared result into `Sx126xLoRaConfig`, but it must not maintain a
  separate Meshtastic RF model.
- Meshtastic node-fact parsing is shared protocol semantics. `NODEINFO_APP`,
  legacy `User`, embedded `Position`, standalone `POSITION_APP`, `via_mqtt`,
  device metrics, and public-key presence must be decoded by `core_chat`
  helpers and then projected into platform events or `ContactService`.
  Platform adapters may supply transport context such as sender, channel, RSSI,
  SNR, and hop count, but they must not fork their own incompatible NodeInfo
  parser.

Visual hierarchy rules:

- repeated cards are allowed only for repeated records such as messages, log
  entries, hardware units, or timeline items;
- page sections are panes or rails, not nested decorative cards;
- controls use compact toolbars, grouped rows, and fixed control widths so the
  layout does not jump as data refreshes;
- status colors should mark severity or source, not decorate every surface;
- empty, disabled, unsupported, and unbound states are honest product states and
  must remain visible without mock data.

## 9. UI Technology Assessment

The UI technology is replaceable only if the service and presentation layers stay
free of toolkit objects. A uConsole shell may be implemented with LVGL, Qt, GTK,
Slint, or a web runtime, but none of those choices may define chat/contact/team
service ownership or presentation-model contracts.

### LVGL

LVGL remains useful for:

- fast bring-up on a raw framebuffer
- CI smoke coverage with a small dependency surface
- compact Linux and MCU-adjacent UI parity checks
- fallback shells when no desktop session, compositor, GPU path, or package
  stack is available

LVGL should not be treated as the default long-term uConsole product UI when the
product starts requiring richer desktop behavior. It is weaker for dense tables,
complex text input, native keyboard shortcuts, accessibility, advanced
windowing, theming, inspectors, large searchable lists, and long-running
desktop-style workflows.

Decision rule:

- keep LVGL when the requirement is direct framebuffer, minimal dependencies,
  compact-shell parity, or hardware bring-up;
- evaluate a native Linux UI stack when requirements include rich search,
  editable tables/lists, multi-pane operational views, diagnostics, import/export
  managers, larger local datasets, shortcut-heavy keyboard workflows, or desktop
  accessibility/window integration.

### GTK 4

GTK 4 is the preferred long-term candidate for a production uConsole
desktop-class UI when the target runs a conventional Linux user session with
Wayland or X11 available.

Reasons:

- it fits the user's desired desktop-software direction better than a compact
  firmware-style shell
- it is native to the Linux desktop ecosystem
- it supports dense lists, search, forms, dialogs, keyboard shortcuts, and
  conventional app workflows more naturally than LVGL
- it keeps the product direction aligned with Linux application behavior instead
  of embedded-widget parity

Costs:

- direct C++ integration will need either plain C bindings, gtkmm, or a thin C
  adapter layer
- deployment should assume a compositor/session unless a separate embedded GTK
  strategy is proven
- it is not the right fallback for raw framebuffer bring-up

### Slint

Slint is a plausible lighter alternative if GTK deployment, compositor
requirements, or C++ binding strategy become too costly. It fits embedded Linux
and C++ integration better than many desktop toolkits, but its ecosystem and
widget depth are smaller. It is worth evaluating after the first real
presentation-model slice exists.

### Qt 6 / QML

Qt 6/QML remains technically strong for embedded Linux, GPU-backed UIs, and C++
service bindings, but it is not the preferred direction for this project because
the user does not want the uConsole product UI to be Qt-based. It should only be
reopened if GTK and Slint both fail hard requirements.

### Web Runtime

A web UI, Tauri, or Electron-like stack gives high UI velocity and rich layout
capability. It should be considered only if storage, memory, startup time,
battery, packaging, and offline deployment budgets are acceptable for the actual
uConsole/AIO2 environment.

### Current Recommendation

- Keep the LVGL uConsole shell as the first buildable target and fallback.
- Do not deepen dependencies on compact LVGL pages or the compact app grid.
- Move real feature work through `LinuxAppServices` and presentation models so a
  later GTK/Slint/web shell can reuse the same app surface.
- Evaluate GTK 4 first when the product requirements move from shell bring-up
  into rich desktop workflows.
- Use Slint as the lighter second candidate if GTK's deployment or binding cost
  is unacceptable.

## 10. Migration Program

Future implementation should proceed in this order.

1. Keep this specification as the baseline.
2. Extract Linux app/service composition out of `MinimalLinuxAppFacade` into a
   UI-independent class such as `LinuxAppServices` or `LinuxAppCore`.
3. Keep `MinimalLinuxAppFacade` as an adapter implementing the existing
   `app::IAppFacade` contract for current LVGL pages.
4. Add one presentation-model slice first, preferably Chat or Contacts.
5. Build the first uConsole shell against that slice instead of directly
   consuming compact LVGL pages.
6. Define the first Linux-native extension point only after the shared
   app-service boundary is clear.
7. Add the uConsole app shell, likely `apps/linux_uconsole`, after the service
   and presentation seams exist.
8. Add AIO2 capability adapters below platform contracts.
9. Add CI/build smoke for the new app shell only after the target structure is
   real enough to compile.

## 11. Acceptance Checks

The uConsole/AIO2 work is aligned with this specification only when these
statements are becoming true:

- Changing the uConsole shell layout does not require changing chat/contact/team
  services.
- The compact Cardputer Zero LVGL shell still works after app service extraction.
- The uConsole shell can avoid the compact 12-entry app menu as its primary
  interaction model.
- AIO2 code does not enter shared core or presentation models.
- Presentation models expose state/actions without LVGL types.
- Feature work starts from service/presentation contracts, not from copying
  compact UI pages.
- Linux-only feature work is allowed, but it is explicitly owned and does not
  force MCU targets to carry Linux assumptions.
- Shared core remains the common foundation, while Linux-native modules can
  express scale, indexing, background jobs, and desktop-class workflows.
- Capability state is honest: unsupported, simulated, and real hardware-backed
  behavior are distinguishable.
- Overview/dashboard surfaces show stored or runtime-backed data only. Empty
  location, hardware, message, and team states are valid product states, not
  placeholders to hide with mock data.
- Hardware state distinguishes endpoint presence from driver readiness. A
  detected uConsole/AIO2 serial, SPI, or I2C endpoint must not be reported as
  missing merely because Trail Mate has not bound the GPS or LoRa protocol
  driver yet.
- Replacing the uConsole UI toolkit does not require rewriting app services or
  presentation models.
- The uConsole Linux app can be installed through a standard `.deb` package, not
  only by copying a build artifact by hand.

## 12. Drift Checks for Future Agents

Before implementing any uConsole/AIO2 slice, check these questions:

- Is this change treating uConsole as a product target or merely as a display
  size?
- Is this change putting AIO2 hardware knowledge into UI or core code?
- Is this change deepening `MinimalLinuxAppFacade` instead of extracting service
  composition?
- Is this change copying compact UI layout when only data/actions should be
  shared?
- Is this change making `linux_rpi` own another Linux device family?
- Is this change creating a second copy of app services?
- Is this change treating MCU feature parity as the ceiling for Linux?
- Is this change pushing Linux-only scale or background-job assumptions into
  shared MCU-compatible modules?
- Is this change letting LVGL, Qt, GTK, Slint, or a web runtime leak into service
  composition or presentation-model contracts?

If the answer to any of these is yes, the implementation is drifting away from
this specification.

## 13. Open Engineer Decisions

These points are intentionally not decided by this document:

- Whether the target directory should be `apps/linux_uconsole` or whether
  `apps/linux_unoq` should be redefined to cover uConsole/AIO2.
- Which long-term UI stack should back the production uConsole desktop-class
  shell after the LVGL bring-up shell exposes enough requirements.
- Which AIO2 capabilities are mandatory for the first buildable slice.
- Which feature provides the first presentation-model slice.
- Which Linux-native feature module should be introduced first.
- What minimum capability tier each future feature requires.

The current recommendation is:

- use `apps/linux_uconsole` unless UNO Q and uConsole/AIO2 are explicitly
  declared to be the same target;
- keep LVGL as the buildable bring-up/fallback shell, without forcing compact UI
  structure;
- evaluate GTK 4 first for the long-term uConsole product UI once rich
  desktop workflows become concrete;
- evaluate Slint if GTK's deployment or binding cost is too high;
- use Chat or Contacts as the first presentation-model slice because they expose
  service/UI coupling quickly without requiring real hardware;
- introduce Linux-native extensions after the service/presentation boundary is
  visible, with search/indexing, data/package management, or diagnostics as
  likely early candidates.
