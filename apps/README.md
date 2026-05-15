# App Shells

This directory contains platform-specific application shells.

An app shell is responsible for:

- startup and entrypoints
- feature composition
- selecting platform adapters
- build/runtime configuration

During the migration period, the root-level `platformio.ini` and current root build flow remain authoritative for the Arduino/PlatformIO targets.
The directories here define the target structure we are converging toward.

For Linux bring-up:

- `legacy/app_implementations/linux_sim` is the simulator and developer-tooling shell
- `legacy/app_implementations/linux_rpi` is the real Pi OS device shell for Cardputer Zero
- `legacy/app_implementations/linux_uconsole` is the desktop-class Linux handheld shell for
  uConsole/AIO2-class targets
