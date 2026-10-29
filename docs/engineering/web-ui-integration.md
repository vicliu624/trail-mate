# Web UI integration

The website uses Animal Island UI **2.2.1** and naive-icons **1.6.0**, pinned in
`site/package.json` and the lockfile. The versions were checked against npm on
2026-10-17. React remains 18.3.1; these packages share the same React instance.

## Component choices

- Import `animal-island-ui/style` once per application entry. Homepage, Device,
  Tracks and Geocaching each build an independent bundle and stylesheet.
- Use the library Button, Card, Title, Background and Select where the runtime
  owns a React view. Device's general text, password and number fields use Input.
  Native input attributes, validation descriptions and focus IDs must reach the
  actual input. An Input adds a wrapper; tests must locate the semantic form
  control or its named ancestor rather than assuming its immediate parent.
- Preserve native file and range controls for browser APIs. Geocaching's
  dynamically populated selectors also retain their DOM contract. The shared
  `site/island-controls.css` gives these controls theme, disabled and focus states
  without overriding the library's internal input classes.
- Configuration and identity dialogs retain the native dialog top layer, focus
  restoration and cancellation restrictions during sensitive operations. A
  visual migration must preserve those behaviors before replacing this layer.
- Keep Leaflet map rendering, route colors and geographic overlays separate from
  the component library. Application controls around the map use the same warm
  text, mint primary and yellow focus tokens as other pages.

## Theme and layout

Use the package's public `--animal-*-color` tokens. Existing short names in
`site/styles.css` are aliases to these tokens, so custom editors and library
components respond to the same theme values. Apply layout classes to wrappers;
do not recolor or add a second border to an Input's internal element. A Card is
a container, a Title is a section ribbon, and primary Buttons own their pressed
depth. Decorative effects must not obscure focus, text or navigation.

Long headings must wrap without losing the Title ribbon. Test narrow layouts
and RTL with actual translated copy. Unpublished drafts, dirty configuration,
keyboard focus and map state must survive language changes.

Disposable track maps use `createTrackMap`, with Leaflet's public
`zoomAnimation: false` option. Leaflet 1.9.4 retains a zoom completion timer
after `remove()`, which can access a destroyed map when a view is closed during
zoom. The shared-package browser regression triggers that sequence and lets the
250-ms callback window expire. Dragging, pan animation, zoom controls and fitting
still work. Re-evaluate this option after adopting a stable Leaflet release with
the [upstream lifecycle fix](https://github.com/Leaflet/Leaflet/issues/9575).

## Verification

Build all four site entry points after a dependency change. Run the locale and
navigation suites, the device configuration browser flow, and the live language
switch regression. Review desktop and narrow screenshots of the actual bundles;
verify controls in connected, disabled, invalid and modal states. A successful
build does not establish usable styling or physical USB compatibility.

References: [official component demo](https://guokaigdg.github.io/animal-island-ui/#/),
[design system](https://github.com/guokaigdg/animal-island-ui/tree/main/docs/design-system),
and the installed package's TypeScript declarations. The installed API is the
authority when an older usage guide differs.
