# Target Profiles

`docs/targets/` records product target selections.

Target profiles are documentation baselines in Phase 8.3. They do not generate
build configuration, create runtime objects, select board pins, or implement UX
packs.

Core rule:

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

Target profile documents may describe:

- target name
- board
- platform family
- build entrypoint
- app shell
- UX profile placeholder
- status

They must not:

- define board facts
- own build host files
- create runtime object graphs
- implement screen internals
- replace existing capabilities YAML files

Phase 8.3 target profile baselines:

- `esp32_lvgl_targets.md`
- `nrf52_node_targets.md`
- `linux_targets.md`
