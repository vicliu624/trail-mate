# Arduino Team position persistence contract

This target compiles the production Arduino Team SD store and snapshot codec.
Only the SD file boundary and uptime provider use native test implementations;
the test does not duplicate the position encoder or reader.

The contract covers the first and latest received positions, complete 28-byte
slots, legacy V1 tails with 24 valid field bytes, a legacy cursor advanced past
the available data, filesystems that reject sparse seeks, and the 512-slot ring
wrapping. The fixture uses an isolated temporary directory and no device keys.

```sh
cmake -S tests/team_position_store -B .codex-build/team-position-store -DCMAKE_BUILD_TYPE=Debug
cmake --build .codex-build/team-position-store --parallel 2
ctest --test-dir .codex-build/team-position-store --output-on-failure
```
