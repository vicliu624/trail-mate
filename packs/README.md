# Packs Source Layout

This directory stores repository-side source bundles for Trail Mate packs.

The source layout is grouped by bundle family for maintainability:

- `locale-bundles/`
  locale-oriented bundles that provide `locales/`, `fonts/`, and optional `ime/`
- `ui-bundles/`
  UI-oriented bundles that provide `themes/` and `presentations/`

Examples:

- `locale-bundles/zh-Hans`
- `locale-bundles/europe-latin-ext`
- `ui-bundles/official-themes`
- `ui-bundles/official-presentations`

Important distinction:

- this repository layout is the source-of-truth layout for authors and tooling
- it is not the runtime install layout used on the device

Runtime payloads still unpack under:

- `/trailmate/packs/fonts/...`
- `/trailmate/packs/locales/...`
- `/trailmate/packs/ime/...`
- `/trailmate/packs/themes/...`
- `/trailmate/packs/presentations/...`

`python scripts/build_pack_repository.py --pack-root packs --site-root site`
recursively discovers bundle roots by finding `package.ini` files under this directory.
