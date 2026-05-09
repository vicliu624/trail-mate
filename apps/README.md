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

- `apps/linux_sim` is the simulator and developer-tooling shell
- `apps/linux_rpi` is the real Pi OS device shell for Cardputer Zero
