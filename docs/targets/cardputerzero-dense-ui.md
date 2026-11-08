# Cardputer Zero Dense UI Specification

Cardputer Zero uses the shared LVGL business pages through the
`cardputer_compact` UX pack. The target-specific problem is density: a 320 x
170 keyboard device cannot spend the same pixels on chrome, rows, buttons, and
status text as larger Pager-style pages.

This specification defines how Cardputer Zero receives denser UI metrics without
rewriting pages or changing other targets.

## Boundary

Cardputer Zero dense UI is a page-layout profile, not a new page ownership
model.

- The target remains `cardputerzero`.
- The UX pack remains `cardputer_compact`.
- Business pages remain in `modules/ui_shared`.
- The Linux app shell only selects the Cardputer Zero product route and shared
  shell; it must not own page layout fixes.
- Compatibility layers must not absorb Cardputer Zero page behavior.
- `Legacy*` adapters are not valid implementation owners for this target; they
  may remain only as older-target compatibility or historical burn-down
  surfaces outside the Cardputer Zero product path.

Pages must consume `ui::page_profile::current()` and related resolvers. Pages
must not branch directly on `TRAIL_MATE_CARDPUTER_ZERO_LINUX` unless they are
inside the profile-selection boundary itself.

## Dense Metrics

`ui::page_profile::make_cardputer_zero_profile()` owns the first dense metrics
slice:

| Metric | Cardputer Zero |
| --- | --- |
| dense profile flag | true |
| top bar height | 22 px |
| side/filter panel width | 78 px |
| side/filter button height | 24 px |
| list/form row height | 24 px |
| control button height | 24 px |
| route map zoom buttons | 24 x 20 px |
| route map Fit button | 36 x 20 px |
| route map control group | 90 x 20 px, 3 px gaps |
| content/list gaps | 1-2 px |
| modal margin | 6 px |
| modal pad | 5 px |
| title/body font | 12 px Montserrat |
| caption/tiny font | 10 px Montserrat |
| air status footer | single dense row where possible |

Pager, T-Deck, and Tab5 profile values are not changed by this profile.

## Component Rules

Shared components should consume the profile before individual pages are tuned:

- top bars use profile height and profile title font
- side navigation uses profile width and button height
- list rows use profile row height
- form rows use profile row height
- footer/status strips use profile dense heights and fonts
- labels that share a row must use fixed meta columns plus single-line ellipsis
- page-specific dense behavior should branch on profile semantics such as
  `ui::page_profile::is_dense()`, not on target macros
- Sky Plot uses a dense compact geometry on 320 x 170: its sky panel, legends,
  status overlay, and status toggle must all stay inside the visible viewport,
  and the status toggle must not cover the lower constellation legend.

## Contacts Row Rule

Contacts rows on dense targets use three columns:

```text
[MT] | Alice Sean... | 25m
[MC] | Bravo         | Offline
```

- protocol tag is a fixed-width column
- display name is the only flexible column
- distance/status is a fixed-width right-aligned column
- display name must be single-line ellipsis
- status must not be hidden by a long display name

Non-dense targets preserve the existing Contacts row projection until they are
explicitly migrated.

## Validation

Each dense UI change must pass:

```bash
python scripts/check_platform_ui_boundaries.py
git diff --check
cd builds/linux_cmake
cmake --build --preset linux-cardputer-zero-debug-build
ctest --preset linux-cardputer-zero-debug-test
```

After visible page changes, regenerate Cardputer Zero screenshots:

```bash
cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate
rm -f docs/images/cardputerzero/screenshots/*.png
for target in dashboard chat contacts map sky_plot team tracker walkie extensions settings; do
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

The screenshot document records which pages are visually improved and which
still need work.

## First Slice Status

Implemented first:

- Cardputer Zero page profile selection through
  `TRAIL_MATE_CARDPUTER_ZERO_LINUX`.
- `PageLayoutProfile::dense` and `ui::page_profile::is_dense()` as the shared
  semantic switch for 320 x 170 page density.
- Shared top bar, footer/status strip, and two-pane style consumption of dense
  metrics.
- Contacts dense three-column row layout for protocol, name, and status.
- Chat list side-panel width, conversation bubble spacing, reply bar sizing,
  and bubble meta fonts now consume dense metrics.
- Settings dense rows now use a bounded value column and ellipsized label
  column.
- Team and Tracker now use dense spacing/font/row helpers for their first 320 x
  170 pass.
- Sky Plot now uses a Cardputer Zero dense geometry for the sky panel, legends,
  status overlay table, and Status toggle so the bottom of the page is not
  clipped on the 320 x 170 viewport.
- Cardputer Zero page-profile smoke test.

Deferred:

- Chat conversation bubble density with populated message fixtures.
- Tracker and Team deeper semantic re-layout where bottom action bars still
  waste vertical space.
- Deeper Map no-fix/no-position empty-state polish now that Map and Sky Plot
  have distinct product page identities.
