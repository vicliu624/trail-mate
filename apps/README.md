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

- `apps/linux_sim_shell` is the final simulator and developer-tooling app shell.
- `apps/linux_uconsole_gtk` is the final GTK/uConsole app shell.
- `removed root linux_sim` and
  `removed root linux_uconsole` are transitional historical roots
  that may be reached only through declared compatibility adapters.
- `removed root linux_rpi` remains a historical bring-up root
  until a final Linux device app shell owns the product path.
