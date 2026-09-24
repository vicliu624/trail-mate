# Map gesture release regression

This test compiles the production gesture callback and its helpers extracted
from `map_viewport.cpp`, then sends pointer samples through real LVGL 9.4.
It does not simulate release by directly calling the map callback. The fixture
only substitutes the surrounding runtime fields; no SD or board is involved.

```sh
cmake -S tests/map_gesture -B .codex-build/map-gesture \
  -DLVGL_DIR=/path/to/lvgl-9.4 -DCMAKE_BUILD_TYPE=Debug
cmake --build .codex-build/map-gesture --parallel
ctest --test-dir .codex-build/map-gesture --output-on-failure
```

It checks a tap and 100 consecutive drags, including dragging outside the
surface. Each drag must emit exactly one DragEnd or Cancel and clear both
pressed and dragging state. Assertions are enabled even in release builds.

Before the fix the pinned dependency reproduces on the first drag:
`release lost: cycle=0 pressed=1 dragging=1 ended=0`. Calling
`lv_indev_stop_processing` from the map's object PRESSING handler leaves a flag
which suppresses delivery of a subsequent RELEASED to that object. Limiting
consumption to the current event with `lv_event_stop_bubbling` passes the same
test. Physical touch-driver and complete viewport/loader integration are
separately verified on the board with the MAPD diagnostics.
