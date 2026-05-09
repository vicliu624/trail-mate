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
- A future uConsole shell should depend on presentation models/actions, not on
  the compact handheld page implementation.
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

### Layer E: AIO2 Platform Adapters

Likely future locations:

- `platform/linux/aio2/`
- or `platform/linux/uconsole/` if the adapters are inseparable from the
  uConsole target

Responsibilities:

- expose AIO2 hardware capabilities through platform contracts
- report honest capability status
- keep driver/device concerns below the app service and presentation layers

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

## 8. uConsole UI Product Rules

The uConsole shell should feel closer to desktop software than to compact
firmware UI.

Expected characteristics:

- persistent navigation or command surface
- multi-pane layouts where useful
- keyboard-first interaction paths
- dense but readable information hierarchy
- stable side panels for status, team, device, or capability details
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
- decorative dashboard cards that reduce operational density
- forcing every page into the current compact 12-entry app menu model

## 9. Migration Program

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

## 10. Acceptance Checks

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

## 11. Drift Checks for Future Agents

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

If the answer to any of these is yes, the implementation is drifting away from
this specification.

## 12. Open Engineer Decisions

These points are intentionally not decided by this document:

- Whether the target directory should be `apps/linux_uconsole` or whether
  `apps/linux_unoq` should be redefined to cover uConsole/AIO2.
- Whether the first desktop-class shell should use LVGL, a native Linux UI
  toolkit, or another rendering approach.
- Which AIO2 capabilities are mandatory for the first buildable slice.
- Which feature provides the first presentation-model slice.
- Which Linux-native feature module should be introduced first.
- What minimum capability tier each future feature requires.

The current recommendation is:

- use `apps/linux_uconsole` unless UNO Q and uConsole/AIO2 are explicitly
  declared to be the same target;
- start with LVGL only if it accelerates the first shell without forcing compact
  UI structure;
- use Chat or Contacts as the first presentation-model slice because they expose
  service/UI coupling quickly without requiring real hardware;
- introduce Linux-native extensions after the service/presentation boundary is
  visible, with search/indexing, data/package management, or diagnostics as
  likely early candidates.
