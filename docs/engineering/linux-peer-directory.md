# Native peer directory ownership

## Shared data, native concurrency

`LinuxPeerDirectory` implements the existing `IMeshPeerDirectory` port around
one `MeshPeerDirectoryCore`. Its format, protocol partitions, identity keys,
fact-merging rules and local flags remain owned by that core. Native callers
serialize memory operations through a short recursive mutex; no blob I/O runs
under that mutex. Read-only visitor reentry is allowed, while mutation or flush
from a visitor returns Busy instead of invalidating its scan or deadlocking.

One storage thread loads the existing blob, encodes persistence snapshots and
saves them. Startup `begin()` waits for initial loading. Runtime `record()`
acknowledges admission to the in-memory owner and schedules persistence; it
does not assert a durable observation. Administrative mutations retain an
explicit persistence barrier before returning, and `flush()` waits for a fresh
attempt covering its target revision. These administrative/startup barriers
can wait for storage and are not frame-loop query operations.

The storage thread allocates its encoded byte buffer outside the mutex, copies
a coherent core revision under the mutex and performs the physical write after
unlocking. It acknowledges a revision only after the port accepts that write.
Concurrent newer observations stay dirty and are encoded again. Save failure
retains dirty memory and retries with a bounded pause; a later explicit barrier
does not reuse an earlier failed attempt as its result. Shutdown rejects new
mutations and attempts the newest admitted snapshot, including updates that
arrived during an older in-flight write. A failed volume cannot create an
endless shutdown retry loop.

Core policy mutations apply in memory before persistence. An I/O error is
reported by the barrier; it does not roll back that in-memory user decision.
Callers must distinguish accepted policy from durable storage rather than
assuming an error restores an older policy. This preserves the existing core
merge model without introducing another policy table or optimistic UI owner.

## File effects

The native composition still supplies the existing resolved storage path and
logical `mesh/peers.bin` file.
The resolved path is pinned before its storage thread starts, so shutdown I/O
does not depend on the lifetime of a global path-configuration object.
Linux publication uses an exclusively created,
descriptor-relative temporary file, synchronizes file contents, atomically
renames it over the target and synchronizes the containing directory. It does
not unlink the previous book before publication. Failures retain the previous
target whenever publication has not occurred. A rename followed by failed
directory synchronization reports uncertainty as failure and leaves the core
dirty for retry.

Corrupt and truncated existing snapshots refuse loading and do not accept
ordinary mutations. Missing storage is distinct from a valid loaded book; the
core's canonical codec still validates record shape and CRC. This is not a new
peer-file format or an automatic repair/migration policy.

## Verification

The native regression blocks the actual persistence port while reading and
admitting a newer observation, checks that the newer revision is eventually
saved, verifies explicit barriers, retries a failed revision, preserves local
flags/aliases on runtime updates and rejects visitor mutation. It also stops
with an older snapshot blocked and confirms that the later accepted update is
drained. Reopen and corrupt-blob retention are covered. Source/core units run
with ASan, UBSan and leak checks on native Linux.

Reticulum ingress now uses this port from its protocol worker. The UI contact
service reads the same protected memory owner through its existing contract;
node events do not introduce a second directory or perform background writes
against an unprotected UI-owned core.
