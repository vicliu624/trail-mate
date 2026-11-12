# Deferred storage startup reservation

This fixture compiles the production Arduino `bootstrapAppContext` and the real
`StorageMaintenanceOwner` with its production state machine. Host FreeRTOS shims
record queue/task allocation and drain queued commands at the same blocking
boundaries. BLE and radio services account for bounded test memory allocations.

The cold-start case models 72 KiB of available internal memory, followed by
24 KiB of BLE and 16 KiB of radio allocations. Creating the existing 8 KiB
storage worker only afterwards fails the Arduino runtime's 40 KiB remaining
memory floor. The original bootstrap reproduces that failure and leaves initial
hydration pending. Reserving the same blocked worker before those allocations
allows recovery to start later without another task, queue or enlarged stack.

The fixture also verifies that preparation does not arm recovery or perform
storage reads, that the display gate remains effective after arming, that a
failed early reservation can still use the original retry path, and that a
failed AppContext initialization does not reserve resources.

```sh
cmake -S tests/esp_storage_startup -B .codex-build/esp-storage-startup -DCMAKE_BUILD_TYPE=Debug
cmake --build .codex-build/esp-storage-startup --parallel 2
ctest --test-dir .codex-build/esp-storage-startup --output-on-failure
```

This is a startup-order and ownership test. It does not measure a particular
device's live heap or claim to read its SD card, and it does not establish that
every `Loading...` report has the same cause. Pager hardware acceptance remains
separate from this reproducible admission failure.
