# UX Profiles

`docs/ux_profiles/` records Device UX profile baselines.

UX profile documents describe product UX decisions. They do not define board
facts, build entrypoints, HAL details, or renderer implementation internals.

Core rule:

```text
Board describes.
Target chooses.
UX Pack presents.
Renderer draws.
```

Each profile records:

- Screen class
- Input model
- Feature set
- Screen set
- Map mode
- Chat mode
- Team action mode
- GPS mode
- Modal/picker strategy
- Renderer family
- Deferred decisions

Phase 8 profiles:

- `deck_full.md`
- `pager_compact.md`
- `watch_quick.md`
- `cardputer_wide.md`
- `tab5_touch.md`
- `tiny_node_status.md`
- `uconsole_desktop.md`
