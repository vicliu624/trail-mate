# Arduino Team position persistence contract

This target compiles the production Arduino Team SD store and snapshot codec.
Only the SD file boundary and uptime provider use native test implementations;
the test does not duplicate the position encoder or reader.

The contract covers the first and latest received positions, complete 28-byte
V2 slots, filesystems that reject sparse seeks, and the 512-slot ring
wrapping. It also injects failed seeks, short record reads/writes, failed flushes
and corrupt headers, and verifies that a 24-byte tail is not read or padded.
Reads never request more than one 28-byte slot. Failed
reads do not rewrite history, and a failed open cannot truncate an existing
ring. The fixture uses an isolated temporary directory and no device keys.

The map bridge target links the same Arduino SD store to the production shared
`TeamMapOverlaySource`. It checks member lookup and map coordinates from the
V2 file, refresh after a second member arrives, reopening the map
data source, read-only history access, and separation between team identities.
The roster and clock are controlled fixtures; the position reader and map
projection are the production implementations, without Linux's memory adapter.

V2 uses `posring-v2.log` with a PSR2 header and full 28-byte records. Old
`posring.log` files are retained untouched and are not read or migrated. The
position cache is independent of Team membership, keys and chat history.

These native storage/map checks do not establish that a particular ESP firmware
boots on hardware; Pager startup and radio/map acceptance remain separate.

```sh
cmake -S tests/team_position_store -B .codex-build/team-position-store -DCMAKE_BUILD_TYPE=Debug
cmake --build .codex-build/team-position-store --parallel 2
ctest --test-dir .codex-build/team-position-store --output-on-failure
```
