# Native chat durability and LXMF identity

The native SQLite chat store implements the existing `IChatStore` durability
ports. `ChatMessageLedger` remains the authoritative application message owner;
`ChatService` retains its existing deferred incoming queue and two-phase
`IIncomingDeliveryCommitPort` callback. A protocol owner must wait for that
callback before acknowledging application acceptance. No second message ledger
or protocol-owned copy of the chat history is introduced.

`appendDurably` and `appendIncomingDurably` return success only when the write
transaction commits, or when an identical accepted incoming identity is already
durable. Statement, unread-update, receipt-write and commit errors return false.
SQLite uses WAL with `synchronous=FULL`. `INSERT OR IGNORE` alone does not prove
acceptance: an ignored legacy insert must match the existing message rather
than an unrelated constraint or conflicting ID.

Incoming LXMF messages persist their full 32-byte hash, source verification
state and RX origin. Message data, unread count and receipt identity commit in
one transaction. Legacy message-ID indexes apply only when no LXMF hash is
present. Distinct full hashes therefore survive a collision in a projected
32-bit message ID. History queries reload the full hash and verification facts.

The migration preserves the old message/unread schema and adds the new columns,
indexes and receipt table transactionally. Existing messages without a full
LXMF hash retain their legacy identity rules; a hash cannot be reconstructed
from the displayed text or projected message ID.

The `chat_reticulum_received_hashes` table is owned by the same SQLite store.
It retains only message hashes and receipt timestamps after history deletion,
so a retransmission cannot resurrect deliberately removed history. It does not
retain the removed text. Retrying an uncertain failed sync must not add a
second message or unread increment. This retention currently has no expiry;
explicit identity-reset policy remains separate from clearing visible history.

The native regressions use real SQLite databases, an old-schema migration,
restart/load checks, full-hash collisions, history removal, and aborting SQL
triggers at each stage of the transaction. A delegated SQLite VFS injects an
actual WAL `xSync` failure, then verifies recovery/retry. The real `ChatService`
and native store also run together: storage failure defers delivery without a
commit callback, recovery commits once, and a duplicate is acknowledged without
incrementing unread state. Store code runs under ASan/UBSan on the host.

The native plain link-text receiver now uses this boundary; see
[Native LXMF link-text reception](linux-lxmf-incoming.md). These storage
guarantees do not implement resource reception or outgoing delivery.

Verification for this stage: 92/92 native tests and 14/14 compact tests passed.
The expanded migration/ChatService integration and VFS fault-injection targets
also passed under ASan/UBSan. Assertion-enabled armhf storage, I/O-failure and
mesh-worker executables passed under QEMU. The standard Debian package was
rebuilt and its extracted application passed identity/store bootstrap and
1232 x 568 rendering; Logo, README and experimental kernel-reference files
matched the repository inputs. No RF peer or native LXMF message-transport
completion is established by these checks.
