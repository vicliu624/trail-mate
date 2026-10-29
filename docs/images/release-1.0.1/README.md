# 1.0.1 Web illustrations

The seven English tool illustrations use production Web bundles from source
`f3e66efdf1bcc7800dac91f6488a4636f7fa36fb`, including Animal Island UI 2.2.1,
naive-icons 1.6.0, shared navigation, readable language selection and the native
control theme bridge. The homepage was recaptured from the public site after
the stable `v1.0.1` release and its
[Pages deployment](https://github.com/vicliu624/trail-mate/actions/runs/38994670042),
whose UI source is `c63ea60d33439a0d1862f524504f01d3f661b884`.
The [capture receipt](web-capture-receipt.json) records bundle and image SHA-256,
viewports, URLs and data sources, including the homepage's separate revision and
capture time. The [public-site readback](public-site-verification.json) records
verified scripts/styles and navigation checks. Generated SVG filenames can differ
with source line endings; their normalized contents were also verified.

## Capture scope

- Homepage: actual site HTML, CSS, components and native firmware preview.
- Device workspace and percentage controls: real browser UI and Web Serial
  client, with an isolated synthetic USB device and real author signatures.
- Geocaching maintenance: real UI, Worker, Reticulum and native loopback service,
  using a synthetic author. Create, update, archive, receipt and device
  confirmation are exercised without publishing to the public network.
- Track workspace and elevation: a synthetic Alpine KML with coordinates,
  elevations and times; this does not represent a physical recorded walk.
- Country and administrative search: actual read-only public directory requests
  for Austria and Vorarlberg, including pagination and boundary selection.

Connecting simulated USB is not physical Pager/T-Deck acceptance. Native preview
hardware values are simulated. These limitations apply to screenshots as well as
browser regression results.

## Reproduce

Build all four site bundles first and set `TRAIL_MATE_DOCS_OUTPUT` to this
directory. Set `TRAIL_MATE_TEST_BROWSER_CHANNEL` when using a locally installed
browser. Run the following from the repository root:

```text
node tests/site/device-page.browser.mjs --release-docs
node tests/site/geocaching-network-maintenance.browser.mjs --release-docs
node tests/site/release-web-captures.mjs
```

The final command requires clean committed site source and checks that every
capture is newer than the bundles. The native loopback test uses the pinned
Python interoperability environment. Do not add hardware-mode arguments when
creating these documentation fixtures.
