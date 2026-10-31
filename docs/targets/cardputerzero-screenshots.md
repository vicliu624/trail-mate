# Cardputer Zero Screenshot Evidence

## Current review after CM0 sync

The `../images/cardputerzero/current-review/` directory contains fresh 320 x 170
captures from `fee017b3` plus the local navigation, Help and page fixes. These supersede the
older ten-page captures for review of the current page set. The capture uses
mesh runtime mode, disables simulated radio/GNSS/world data and the hardware
LoRa endpoint, and uses an isolated
settings/SD directory under the local build cache. No attached device session
is represented by these images.

Pages: Dashboard, Chat, Contacts, Map, Sky Plot, Team, Tracker, Calculator,
Geocaching, Protocol Probe, Walkie Talkie, Extensions, Settings, and Agenda.
The `menu-open/` subdirectory records expanded-menu requests separately. The
`help/` subdirectory captures the real contextual Help overlay for each app.

Current host observations: Calculator and Agenda render their real pages;
Geocaching reports that directory transport is not connected. Protocol Probe
renders its actual page, reports `RADIO UNAVAILABLE` and disables Start and Set
without a radio. Walkie Talkie is linked to the native media service and renders
the actual backend error when GPIO/radio access is absent. These error states
do not establish physical RF or audio acceptance. All thirteen additional
app-level Help regressions passed on 2026-10-29. They exercise keyboard opening,
scrolling, closing, focus restoration and the clickable header Help entry. The
Walkie case also checks that the bottom level meters fit within 320 x 170 in
the real hardware-error state. The 17 checks recorded before these additional
regressions remain baseline evidence.

This document records the current Cardputer Zero page screenshots captured from
the shared LVGL Cardputer compact UI path. These images are evidence from the
real Trail Mate shell route; they are not mockups, generated drawings, or a new
Linux-panel design direction.

The current product manifest has fourteen routes, including Agenda. The checked-in image matrix
below covers ten earlier pages. Calculator, Geocaching and Protocol Probe are
present in the source and maintenance tests; their PNGs are not checked in here.
Agenda is also registered and tested; the historical capture selectors and image
matrix do not establish an Agenda screenshot.
Do not regenerate all screenshots for routine backend or documentation changes.

## Capture Path

The screenshots are produced by:

```text
apps/linux_cardputer_zero/tools/cardputer_zero_screenshot_capture.cpp
  -> ShellSession
  -> CanvasLvglHost
  -> shared LVGL shell
  -> cardputer_compact UX pack
  -> make_cardputer_zero_profile()
```

The app shell sets the Cardputer Zero compact UX pack, and the LVGL host renders
to a 320 x 170 canvas before encoding PNG output.

## Capture Command

From WSL at the repository root:

```bash
cd builds/linux_cmake
cmake --build --preset linux-cardputer-zero-debug-build

cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate
rm -f docs/images/cardputerzero/screenshots/*.png
for target in dashboard chat contacts map sky_plot team tracker calculator geocaching energy_sweep walkie extensions settings; do
  timeout 40s env TRAIL_MATE_LORA_DISABLE=1 \
    builds/linux_cmake/build/linux-cardputer-zero-debug/apps/linux_cardputer_zero/trailmate_linux_cardputer_zero_screenshot_capture \
    /mnt/c/Users/VicLi/Documents/Projects/trail-mate/docs/images/cardputerzero/screenshots \
    "$target" || exit $?
done
```

From PowerShell, pass each target explicitly so `$target` is not expanded before
the WSL shell receives it:

```powershell
$targets = @(
  'dashboard',
  'chat',
  'contacts',
  'map',
  'sky_plot',
  'team',
  'tracker',
  'calculator',
  'geocaching',
  'energy_sweep', # Capture selector for Protocol Probe.
  'walkie',
  'extensions',
  'settings'
)
wsl bash -lc 'cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate && rm -f docs/images/cardputerzero/screenshots/*.png'
foreach ($target in $targets) {
  wsl bash -lc "cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate && timeout 40s env TRAIL_MATE_LORA_DISABLE=1 builds/linux_cmake/build/linux-cardputer-zero-debug/apps/linux_cardputer_zero/trailmate_linux_cardputer_zero_screenshot_capture /mnt/c/Users/VicLi/Documents/Projects/trail-mate/docs/images/cardputerzero/screenshots $target"
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

`TRAIL_MATE_LORA_DISABLE=1` is used so screenshot capture does not depend on a
live SX1262 endpoint. It does not change the UI route being rendered.

## Screenshot Matrix

### Navigation review captures

The capture helper accepts an optional comma-separated action sequence after the
page id. Use `menu` to send the production `M` shortcut, `tab` to move keyboard
focus, `enter` to activate it, and `escape` to send Escape. For example, capture
`settings` with `menu` to review the category menu, then with
`menu,down,enter` to review category activation. Use separate output directories
for the closed and open menu states because each uses the same page filename.

Page registration and a default-state screenshot alone do not prove action
reachability. Navigation review must include the bottom Menu entry, its expanded
items, keyboard activation, and closing the menu without leaving the page.
Historical screenshots below must not be used to infer that the current runtime
still presents persistent side-by-side navigation: the shared controller moves
the filter widgets into a bottom-bar menu during initialization.

Keyboard menus use Up/Down to move focus and scroll the selected entry into
view, Enter to activate, and Escape to dismiss. This is handled by the shared
menu itself so Linux keypad input has the same access to entries as encoder
input. `menu-selected/` records Settings category selection, Tracker's Tracks
submenu, and Extensions' Uninstalled selection.

| Page | Evidence | Notes |
| --- | --- | --- |
| Dashboard | `docs/images/cardputerzero/screenshots/dashboard.png` | Shared Cardputer compact menu/dashboard frame. |
| Chat | `docs/images/cardputerzero/screenshots/chat.png` | Real shared chat page reached from the compact menu. |
| Contacts | `docs/images/cardputerzero/screenshots/contacts.png` | Real shared contacts page reached from the compact menu. |
| Map | `docs/images/cardputerzero/screenshots/map.png` | Real shared map page reached from the compact menu. It exposes compact zoom, center, base-layer, and contour controls and must not be seeded with product-path mock coordinates. |
| Sky Plot | `docs/images/cardputerzero/screenshots/sky-plot.png` | Real shared GNSS sky-plot page reached from the compact menu with the Cardputer Zero dense geometry. |
| Team | `docs/images/cardputerzero/screenshots/team.png` | Real shared team page reached from the compact menu. |
| Tracker | `docs/images/cardputerzero/screenshots/tracker.png` | Real shared tracker page reached from the compact menu. |
| Calculator | No checked-in PNG | Registered compact route; covered by calculator engine/page tests. |
| Geocaching | No checked-in PNG | Registered compact route; covered by compact manifest and business wiring tests. |
| Protocol Probe | No checked-in PNG | Registered compact route; `energy_sweep` is the capture selector. Its port/session behavior has separate fixtures. |
| Walkie Talkie | `docs/images/cardputerzero/screenshots/walkie.png` | Real shared walkie-talkie page reached from the compact menu. |
| Extensions | `docs/images/cardputerzero/screenshots/extensions.png` | Real shared extensions page reached from the compact menu. |
| Settings | `docs/images/cardputerzero/screenshots/settings.png` | Real shared settings page reached from the compact menu. |

Excluded Cardputer Zero product entries: PC Link, SSTV, and SD
Storage / USB Disk. SD Storage is the card-access/USB
mass-storage feature and is distinct from Extensions, which remains part of the
Cardputer Zero product menu.

The default page images were refreshed from the current source on 2026-10-29.
All generated PNGs use the 320 x 170 logical canvas.

The `menu-open/` and `menu-closed/` subdirectories contain keyboard-driven
captures for Chat, Contacts, Tracker, Extensions, and Settings. Closed captures
send `menu,escape`, proving the page remains visible after dismissal. The
Extensions keyboard handler now accepts `M` like the shared menu controller.

| Page | Expanded menu | Dismissed menu |
| --- | --- | --- |
| Chat | [Open](../images/cardputerzero/screenshots/menu-open/chat.png) | [Closed](../images/cardputerzero/screenshots/menu-closed/chat.png) |
| Contacts | [Open](../images/cardputerzero/screenshots/menu-open/contacts.png) | [Closed](../images/cardputerzero/screenshots/menu-closed/contacts.png) |
| Tracker | [Open](../images/cardputerzero/screenshots/menu-open/tracker.png) | [Closed](../images/cardputerzero/screenshots/menu-closed/tracker.png) |
| Extensions | [Open](../images/cardputerzero/screenshots/menu-open/extensions.png) | [Closed](../images/cardputerzero/screenshots/menu-closed/extensions.png) |
| Settings | [Open](../images/cardputerzero/screenshots/menu-open/settings.png) | [Closed](../images/cardputerzero/screenshots/menu-closed/settings.png) |

## Visual Observations

The current evidence proves that each compact route can be reached and rendered
from the Cardputer Zero shared LVGL path. It also exposes page-adaptation work
that should stay visible:

- Dashboard now captures the Pager-derived compact menu after the boot splash
  gate instead of the splash overlay.
- Contacts now uses the dense fixed protocol/name/status row layout on the 320 x
  170 viewport; the first-contact text overlap shown in the initial screenshot
  evidence has been removed.
- Chat, Settings, Tracker, and Team now consume the first dense page-profile
  slice for top bars, side filters, list rows, controls, value columns, and the
  air status footer. Chat conversation bubbles still need message-populated
  screenshot validation.
- Map and Sky Plot now render distinct projections. Map uses the runtime map
  workspace source/sink and exposes compact operation controls for zoom,
  center-on-self, base-layer cycling, and contour toggling; Sky Plot uses the
  GNSS sky-plot runtime page.
- Sky Plot now uses Cardputer Zero dense geometry so the sky panel bottom,
  constellation legend, status overlay, and Status toggle fit within the 320 x
  170 screenshot without lower-edge clipping.
- Map tile imagery is absent in this host capture because no tile cache is
  available in the capture environment. The screenshot should therefore show
  the live no-fix/no-tile state and controls, not product-path mock map data.
- Chat, Team, Tracker, and Settings render as reachable shared pages, but still
  need hardware-session interaction checks before being treated as final
  Cardputer Zero page closure.

## Images

### Dashboard

![Cardputer Zero Dashboard](../images/cardputerzero/screenshots/dashboard.png)

### Chat

![Cardputer Zero Chat](../images/cardputerzero/screenshots/chat.png)

### Contacts

![Cardputer Zero Contacts](../images/cardputerzero/screenshots/contacts.png)

### Map

![Cardputer Zero Map](../images/cardputerzero/screenshots/map.png)

### Sky Plot

![Cardputer Zero Sky Plot](../images/cardputerzero/screenshots/sky-plot.png)

### Team

![Cardputer Zero Team](../images/cardputerzero/screenshots/team.png)

### Tracker

![Cardputer Zero Tracker](../images/cardputerzero/screenshots/tracker.png)

### Walkie Talkie

![Cardputer Zero Walkie Talkie](../images/cardputerzero/screenshots/walkie.png)

### Extensions

![Cardputer Zero Extensions](../images/cardputerzero/screenshots/extensions.png)

### Settings

![Cardputer Zero Settings](../images/cardputerzero/screenshots/settings.png)

## Current Caveats

- On 2026-10-29 the host build and all six Cardputer Zero smoke tests passed.
  The shared bottom-bar regression also passed with a 320 x 170 canvas and the
  existing 480/240-pixel-wide scenarios, including Up/Down focus navigation.
- Walkie Talkie's short-screen spacing now keeps both level meters within the
  viewport. Sky Plot uses a compact satellite/HDOP/fix summary on short screens
  so the fix state is not clipped by the clock.

- These screenshots validate the shared LVGL Cardputer compact UI path, not the
  physical Cardputer Zero framebuffer handoff.
- Real hardware evdev keyboard mapping still needs device-session validation.
- Fcitx5 text commit and notifyd D-Bus behavior still need live Cardputer Zero
  session validation.
- Map tile imagery is only visible when the host-side tile cache is populated or
  the Linux online tile cache can fetch it. Cardputer Zero product runtime must
  not use mock/default coordinates to make Map look populated.
- The current compact shell exposes distinct Map and Sky Plot route ids. Sky
  Plot should show the GNSS sky-plot page rather than the map workspace.
- The capture helper exits each single-page capture process immediately after
  encoding the PNG to avoid current LVGL teardown instability in the evidence
  tool. Product runtime does not use that exit path.
