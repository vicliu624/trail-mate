# uConsole desktop feature parity

This document records the uConsole desktop feature audit against the ESP32
application catalog. The desktop shell reuses the existing Trail Mate domain,
protocol, radio, GPS, tracker, map, and package runtimes. It does not introduce
or depend on `meshtasticd`; Meshtastic traffic continues through Trail Mate's
native implementation.

## Interaction model

- Target geometry: 1280 x 720 landscape with keyboard and pointer.
- A 168 px navigation rail that can be collapsed with `\`, task-focused desktop
  workspaces, and a persistent 30 px keyboard/status bar.
- T-Deck visual language is retained through the embedded cream, amber, brown,
  gold-border, and blue/green status palette. uConsole expresses density with
  dividers, aligned data rows, continuous lists, and explicit action bars;
  generic rounded dashboard cards are not a default component.
- `F1`/`H` opens a Pager/T-Deck-style shortcut reference; direct workspace
  letters and `[`/`]` navigation keep every feature reachable without touch.
  These commands do not consume ordinary editable-text input or modified
  accelerator combinations.
- Related low-frequency ESP screens are combined where a desktop workbench is
  more efficient. Energy Sweep, SSTV, and Walkie share **Radio tools** while
  keeping their independent runtimes and controls.

## Feature matrix

| ESP32 capability | uConsole desktop surface | Backend reuse | Status |
| --- | --- | --- | --- |
| Dashboard | No separate dashboard; Map is the launch workspace | Global status plus task workspaces | Intentional removal; the previous Overview duplicated incomplete projections without an operation |
| Chat | Chat workspace | Native Trail Mate Meshtastic/MeshCore/Reticulum services | Implemented |
| Contacts and nearby nodes | Contacts & nodes | Chat workspace model and node actions | Implemented |
| Map | Map workspace | Linux map cache, tile fetcher, contour store | Implemented |
| Automatic map download | Global background map service and Data & maps status | Existing asynchronous tile cache/fetch runtime | Implemented; desktop-specific behavior retained |
| GPS and Sky Plot | GPS & sky plot | GPS runtime and GNSS sky-plot presenter | Implemented; the workspace retains GNSS status, satellite rows, and sky plot rather than a summary-only metric |
| Team | Team operations | Dashboard team snapshot and chat model | Implemented |
| Tracker | Tracker | Linux GPX/CSV/binary tracker runtime | Implemented; saved settings now drive runtime |
| Energy Sweep | Radio tools | Existing LoRa runtime | Implemented; the Run sweep action samples the configured band and restores the active mesh radio configuration |
| SSTV | Radio tools | Existing SSTV runtime | Implemented; Start/Stop and frame/progress are exposed, and capability state identifies simulated backends |
| Walkie | Radio tools | Existing Walkie runtime | Implemented; Start/Stop, PTT, monitor, levels, and capability state remain direct controls |
| Extensions | Extensions | Existing pack repository runtime | Implemented; packages are a continuous install/uninstall list, not dashboard tiles |
| Settings | Settings | Existing app config and service apply paths | Implemented |
| Network | Settings and global status | Existing MQTT/network configuration | Implemented as desktop-integrated settings |
| USB disk | Data & maps / native Linux filesystem | Linux storage paths | Desktop equivalent; uConsole retains local SQLite/cache roots and cache operations because ESP USB mass-storage mode is not applicable |
| Poweroff | Native window/session lifecycle | GTK application shutdown | Desktop equivalent |
| Hardware diagnostics | Hardware | uConsole hardware probe | Desktop extension |
| Packet diagnostics | Logs | Linux packet log runtime | Desktop extension |

## Capability honesty

Desktop UI availability does not imply physical hardware availability. Radio
tools display `Available`, `Simulated`, `Degraded`, `Unsupported`, or `Error`
from the shared capability contract. This is especially important for the
current Linux Walkie and SSTV development backends, which must not be presented
as real audio/radio hardware.

## Surface acceptance rules

- Radio tools presents a compact capability/status table, a dominant sweep
  workspace, then SSTV and Walkie operation sections. Its actions must be
  usable independently; a capability summary never replaces the action.
- Data presents an inventory table, map-cache operations, then local-root
  detail. Counts are data rows, not a row of dashboard cards.
- Extensions is an installable-package list with one clear action per package.
- Logs is a continuous chronological diagnostic stream. Its timestamp,
  source/direction, parsed segments, and raw bytes are deliberately separated
  by typography and dividers rather than by a card around each record.
- Map remains canvas-first: the viewport fills the workspace until a user opens
  an overlay drawer. The shared navigation rail can be collapsed to prioritize
  map use further.

## Verification boundary

The shared uConsole shell and UX pack are covered by the Windows CMake smoke
suite. Modified GTK translation units are syntax-checked in a Linux compiler
environment. The 1280 x 720 LVGL shell is also built and exercised through the
real SDL3 presenter, including renderer readback screenshots for Overview,
Chat, Map, GPS, Radio tools, and Extensions.

See the [uConsole SDL visual review](uconsole-sdl-visual-review.md) for the
screenshots, acceptance checklist, and reproduction command. SDL validates the
shared composition and interaction model; final GTK pixel metrics still require
a GTK4-capable target because GTK remains a separate widget renderer.
