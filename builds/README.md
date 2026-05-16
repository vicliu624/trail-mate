# Build Entrypoints

`builds/` contains build system entrypoints.

It does not contain product app shells.

Core rule:

```text
Build Entrypoint invokes.
App Shell composes.
```

The Phase 8.2 skeleton records authoritative build hosts without moving current
build files:

```text
builds/
  esp_idf/
  pio_nrf52/
  linux_cmake/
```

Build entrypoints are thin wrappers. They may call into app shells and adapt
build-host conventions, but they must not assemble Chat, Map, Team, or GPS
runtime object graphs, choose UX packs, define board facts, or implement product
behavior.

Current transitional paths remain valid until wrapper migration is proven:

- `removed root esp_idf`
- `removed root esp_pio`
- Linux app-local CMake paths such as `removed root linux_sim` and `removed root linux_rpi`

This directory is intentionally documentation-only in Phase 8.2.
