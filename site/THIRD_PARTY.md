# Website credits

- Geocaching country search uses public-domain Natural Earth 1:50m boundaries.
  Ocean/sea search uses Flanders Marine Institute (2018), IHO Sea Areas v3,
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), via Alvin Bryan's
  simplified web conversion. [Dataset sources, changes and notices](./geocaching/data/README.md).

- **Animal Island UI 1.10.0**, by **guokaigdg**:
  <https://guokaigdg.github.io/animal-island-ui/>.
  Licensed under **Creative Commons Attribution–NonCommercial 4.0 International**:
  <https://creativecommons.org/licenses/by-nc/4.0/>.
  The site uses Select, Title, Icon, Background and BackTop; the Geocaching page
  also uses Button, Card and Divider. These use custom layout,
  colors, sizing and localized labels. The shopping-bag icon is extracted from
  this library for device purchase links.
- React and React DOM are distributed under the MIT license; their license
  notices are preserved in the generated component bundle.
- Noto Sans SC font assets are bundled through Animal Island UI and are licensed
  under the SIL Open Font License 1.1: <https://openfontlicense.org/>.
- Sample map data is attributed to OpenStreetMap contributors in the preview.
  Tile and synthetic route details are in
  `../tools/web_simulator/data/ATTRIBUTION.md`.
- Device SVG artwork was supplied for this project. LILYGO, Seeed Studio and
  device names remain their respective owners' marks.
- The Geocaching worker bundles **@reticulum/core 0.9.0** and
  **@reticulum/lxmf 0.9.0**, by **Henri Bergius and contributors**, under
  [EUPL 1.2](./geocaching/licenses/EUPL-1.2.txt).
  [Upstream source](https://github.com/bergie/reticulum-js) is also included in
  the generated worker's source map. The packages are unmodified; Trail Mate's
  adapter sets resource limits and injects compression on outbound reply links.
- The Geocaching map uses **Leaflet 1.9.4**, by **Vladimir Agafonkin and
  contributors**, under BSD-2-Clause. The map displays OpenStreetMap attribution;
  tiles are loaded only for the current interactive viewport using browser cache
  semantics. No offline tile download or prefetch is provided.
  [Tile usage policy](https://operations.osmfoundation.org/policies/tiles/).
- The Geocaching worker uses **seek-bzip 2.0.0** (C. Scott Ananian, Eli Skeggs,
  Kevin Kwok and contributors, MIT) and **buffer 6.0.3** (Feross Aboukhadijeh and
  contributors, MIT), plus buffer's base64-js/ieee754 dependencies. Their notices
  are copied into the generated `geocaching/dist/THIRD-PARTY.txt` at build time.
