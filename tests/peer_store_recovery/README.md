# Arduino peer journal recovery

This fixture compiles the production `SdProtocolPeerRepository`, fixed-slot
journal engine/cursor, peer/contact codecs, and peer merge implementation.
Only the SD filesystem boundary, FreeRTOS mutexes, clock, and unused chat-store
collaborator are native fakes. ASan and UBSan are enabled.

The seed matches the observed Pager incident: a 16-byte JNL2 header and 146
current 29-byte contact slots, for 4250 bytes. A one-shot metadata Busy returns
zero from `size()`; the previous writer performed `seek(0)`, replaced the header
with CNT2, and returned success. The regression requires a failed append to
preserve every original byte, followed by a successful ordinary retry.

The recovery case overwrites the header with a CRC-valid current contact
record. The repository must stream valid records on the original slot grid,
replay the newest record at offset zero last, publish a valid replacement,
finish hydration, and retain the entire original as `contacts.delta.corrupt`.
The damaged first payload slot cannot be reconstructed; all retained records
still require normal codec CRC validation. Unknown data is not converted into
an empty Ready directory. No historical Team position format is involved.

Additional cases verify ordinary startup, transient journal-open failure,
an interruption between publication renames, a failed forensic rename followed
by restart, fresh-process hydration after recovery, and ordinary appends to the
repaired journal. An unrelated Team credential sentinel remains byte-identical.

```sh
cmake -S tests/peer_store_recovery -B .codex-build/peer-store-recovery -DCMAKE_BUILD_TYPE=Debug
cmake --build .codex-build/peer-store-recovery --parallel 2
ctest --test-dir .codex-build/peer-store-recovery --output-on-failure
```
