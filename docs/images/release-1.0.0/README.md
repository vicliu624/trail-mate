# Release documentation screenshot sources

These files are copies of actual interface captures, not generated concept art.
Their captions distinguish sample data from hardware acceptance. Final release
review should refresh captures if the corresponding UI changes.

All release-facing Web captures were refreshed in **English on October 17,
2026**, from the rebuilt production sources on `main` at
`e6b692fc5df25ca4ae25f625d0343708db958b1d`. They replace the earlier Chinese
fixture captures and October 14 review build. The [capture receipt](web-capture-receipt.json)
records the exact bundle/image SHA-256 digests, language, viewport and source
revision. Firmware tag `v1.0.0` is unchanged. The receipt's source revision
identifies the Web code, rather than the later documentation commit containing
the new image files.

Build all four bundles from `site` before capturing: `npm run build:components`,
`npm run build:device`, `npm run build:tracks`, `npm run build:geocaching`.
Prepare the homepage showcase assets with `scripts/prepare_pages_site.py` and
current release metadata. The capture scripts assert English navigation and
loaded logos. On machines with Edge, set
`TRAIL_MATE_TEST_BROWSER_CHANNEL=msedge`; CI uses Chromium.

After building, run the device and Geocaching `--release-docs` captures first,
then `node tests/site/release-web-captures.mjs`. The final command records all
eight images and rejects images older than the bundles, so reusing a stale
capture cannot silently produce a fresh receipt. It requires committed Web
sources and records the actual current commit. The public regional queries
also require the configured gateway and directory to be reachable.

| File | Source | Evidence boundary |
| --- | --- | --- |
| `web-home-tools.png` | `node tests/site/release-web-captures.mjs` | Current English homepage, shared navigation and task cards; actual v1.0.0 release metadata. |
| `web-device-management.png` | `node tests/site/device-page.browser.mjs --release-docs` | Current English production UI, generated USB author and simulated configuration values; cancelled navigation retains edits. |
| `web-config-percent.png` | Same command, brightness section | English percentage controls; simulated USB values. |
| `web-geocaching-maintenance.png` | `node tests/site/geocaching-network-maintenance.browser.mjs --release-docs` | English production UI/Worker/native loopback directory; synthetic USB author and real signatures, create/update/archive/proof/device confirmation. No public write or physical USB. |
| `web-track-workspace.png`, `web-track-elevation.png` | `node tests/site/release-web-captures.mjs` | Current English production UI and sample Alpine KML elevations/timestamps; cancelled navigation retains the route edit. No real hiking or hardware assertion. |
| `web-track-country-search.png` | Same command; real public WSS/Reticulum directory | Country pagination, verified Austria boundary and empty regional query. Local built assets are served under the site's origin in an isolated browser for the gateway Origin allowlist; directory responses remain real. No USB author or public write. |
| `web-track-administrative-search.png` | Same build/public directory | Austria-to-Vorarlberg selection, verified first-level boundary and empty regional query. No deeper selection, USB operation, public write or deployment assertion. |
| `center-map-cache.png` | Trail Mate Center repository `assets/QQ20260515-103200.png` | Existing desktop screenshot; separate Center release, not a firmware acceptance capture. |
| `center-propagation.png` | Trail Mate Center repository `assets/QQ_1778812190407.png` | Existing desktop screenshot; no completed radio simulation claimed. |
| `firmware-geocaching-card-native.png` | Production Geocaching LVGL page rendered by `modules/ui_shared/tests/geocaching/test_page.cpp`, 320×240, 2026-10-14 | Reused card with distance and a synthetic approximate administrative area; not a geographic-accuracy measurement, camera capture or real-device publication acceptance. |
| `firmware-tracker-card-native.png` | Actual Wio LVGL native preview from successful Pages/native run `38714841112`, artifact `11961803314`, UI source `84a3ecf6`, 320×240, 2026-10-15 | Full-width Routes card and outlined bottom Menu. Sample KML and simulated GPS; zero distance is the demo fix matching the sample start. No administrative dataset is loaded, so the region is explicitly unknown. Not a device camera capture or real hiking record. |
| `firmware-tracker-actions-native.png` | Same Wio native module, source and run | Flat Route Actions rows, content-sized dialog and compact close; not a physical input capture. |
| `firmware-map-menu-native.png` | Same Wio native module, source and run | Current Map taskbar and stable menu entries, using sample map/GPS data. Team is absent because the preview has no joined team. |
| `firmware-map-navigation-native.png` | Same Wio native module, source and run | Current Navigation management dialog; no loaded route or physical route-following claim. |
| `firmware-map-help-native.png` | Same Wio native module, source and run | Fixed Help header, separately scrolling body and compact touch close. Keyboard/pointer behavior is covered by the cited native tests, not a device camera capture. |

Firmware illustrations already live under `docs/images/`, including map,
messaging, team and route-preview captures. The final 1.0.0 screenshot set still
includes native Geocaching/Tracker card captures. Physical card interaction and
TMAP geographic accuracy still require hardware acceptance; native captures do
not establish those results.
