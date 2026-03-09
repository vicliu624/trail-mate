# App Shells

This directory contains platform-specific application shells.

An app shell is responsible for:

- startup and entrypoints
- feature composition
- selecting platform adapters
- build/runtime configuration

During the migration period, the root-level `platformio.ini` and current root build flow remain authoritative for the Arduino/PlatformIO targets.
The directories here define the target structure we are converging toward.
