# Cardputer Zero Migration Path

This note captures the recommended way to adapt `Cardputer Zero` into the
current `Trail Mate` repository without damaging the existing ESP/PlatformIO
mainline.

## Why not bolt Linux onto the current root build

The current root build is still split between:

- root `CMakeLists.txt` / `src/main.cpp` for ESP-IDF
- root `platformio.ini` for Arduino/PlatformIO targets
- `modules/*` that are only partially platform-neutral today

Some modules that should eventually become shared still hard-reference ESP or
Arduino pieces. For example, [modules/ui_shared/library.json](../../../../modules/ui_shared/library.json)
still includes `../../platform/esp/arduino_common/include` and depends on
`Preferences`.

That means treating `Cardputer Zero` as "just another board target" right now
would create the wrong dependency direction: Linux code would end up tunneling
through ESP-specific layers.

## Why `M5Stack_Linux_Libs` changes the device plan

For the real Cardputer Zero hardware shell, the most valuable external input is
`M5Stack_Linux_Libs`, not `M5CardputerZero-UserDemo`.

- `M5Stack_Linux_Libs` gives us the reusable device-facing SDK:
  LVGL integration, fbdev, evdev, and Linux bus helpers.
- `M5CardputerZero-UserDemo` is useful only as a source of concrete board
  hints such as framebuffer naming and likely input-device paths.
- Therefore the correct move is to build our device shell on top of the SDK,
  while keeping our app architecture and migration discipline independent from
  the demo app.

## Best adaptation strategy

The recommended sequence is:

1. Keep `apps/linux_sim` as the simulator and developer-tooling shell.
2. Keep `apps/linux_rpi` as the real Pi OS device shell, and let its preferred
   device-facing path use `M5Stack_Linux_Libs`.
3. Extract shared logic from the current repository into `platform/linux/common`
   in vertical slices.
4. Only move code into truly shared `modules/*` after the slice is Linux-safe
   beyond the Linux bring-up pair.

## Extraction order

Use this order when porting real Trail Mate features into the Linux shell:

1. runtime and input contracts
2. presentation-independent app state
3. render/layout helpers
4. storage abstractions and file-backed settings
5. Linux-safe chat, GPS, and team slices
6. hardware integrations for Pi OS on the real device

## Working rule

Do not start by compiling the whole current app stack under Linux. Start by
building two thin Linux shells and one neutral shared slice:

- `apps/linux_sim` owns desktop process lifecycle and simulator presentation
- `apps/linux_rpi` owns Pi OS device lifecycle and the SDK-backed hardware path
- `platform/linux/common` owns only the Linux-safe code both shells need
- Linux file/storage adapters should enter the shared slice only when they are
  no longer device-specific

Then pull shared logic into it deliberately.

That keeps the Linux adaptation incremental, testable, and reversible.
