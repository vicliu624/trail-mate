# PlatformIO nRF52 Build Entrypoint

Authoritative nRF52 build entrypoint.

Build authority:

```text
nRF52 -> PlatformIO
```

Current transitional path may be:

```text
removed root esp_pio
root platformio.ini
target-specific PlatformIO files
```

Final wrapper direction:

```text
builds/pio_nrf52
  -> invokes apps/nrf52_node app shell
```

This directory is a Phase 8.2 migration stub. It does not yet contain
`platformio.ini`, environment definitions, library path rewrites, or generated
build state.

Rules:

- thin wrapper only
- Build Entrypoint invokes
- App Shell composes
- do not assemble Chat/Map/GPS runtime here
- do not choose UX pack here
- do not define board facts here
- keep current PlatformIO flow stable until wrapper parity is proven
