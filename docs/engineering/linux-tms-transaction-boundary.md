# Native TMS transaction boundary

Complete native TMS document staging, snapshot export, fenced transactional
import, the frontend service handoff and HostLink paged snapshot export are
implemented. HostLink upload, validation and commit are connected to the existing
web configuration editor. Device release validation is tracked separately. The existing native
Reticulum `network_tms_v1` value is an internal network projection and must not
be exposed as a complete portable TMSET7 document.

`SettingsReadView` now provides strict signed, unsigned, byte, boolean, text and
blob reads through one borrowed SQLite read transaction. Export can read the
AppConfig blob and independently owned settings without mixing versions from
separate database connections. The view must not escape its callback, and the
callback must not call the public settings APIs, which use the same store mutex.
Binary data remains bounded by the caller's buffer, including empty blobs and
embedded NUL bytes. Failed reads clear outputs and distinguish Missing, Invalid
and Unavailable rather than introducing defaults for damaged values.

`write_settings_transaction` accepts borrowed namespace batches and applies
them under one `BEGIN IMMEDIATE` transaction. All namespace/key/kind validation
and reset-fence checks precede writes. A failure in any namespace rolls back all
earlier updates. The existing one-namespace `write_settings_batch` delegates to
this same engine, preserving Wi-Fi's storage boundary. Mutation payloads are
already encoded native storage values; this primitive does not validate a TMS
document or AppConfig schema.

The new fixture uses synthetic AppConfig/network payloads to verify the storage
boundary, not to claim full document compatibility. It commits updates to a
second SQLite connection between snapshot reads and checks that the first view
still returns a coherent older set across namespaces. It injects an abort in
the last namespace, checks fresh-process persistence, typed unsigned/blob reads,
byte overflow, physical SQLite type mismatch, bounded blob rejection, deletion,
invalid namespace input and all-or-nothing reset-fence rejection. Existing
strict-read, Wi-Fi and factory-reset fixtures remain relevant regression gates.

Validation on 22 October passed the complete native suite (179/179), including
the instrumented settings transaction fixture, and the rebuilt Cardputer Zero
suite (19/19). The transaction fixture and existing ten-profile Wi-Fi regression
also passed as real ARM executables on CM0. Their data and reset operations used
isolated temporary roots. The device application package remains the previous
verified build; no incomplete TMS capability is advertised by this change.

The complete importer stages and validates the shared core document
and required extension records before constructing these batches. AppConfig,
independent UI/time/locale/audio settings, ten Wi-Fi profiles and the Reticulum
network snapshot commit together. Unsupported platform records have the
explicit compatibility policy below; disabled Bluetooth stays disabled at runtime.
The generic transaction API does not quiesce application services; the dedicated
import commit adds its own write fence. Import must still arrange owners and a
restart/handoff so delayed old-process saves cannot overwrite the imported set.
Validation failure must leave both persistent and runtime state unchanged.

No import/export UI, HostLink capability, automatic working-file authority or
document roundtrip is supplied by this storage primitive. The later document,
frontend and HostLink layers below supply those integration stages.

## Shared base settings records

`app/tms_settings_records.h` now owns the storage-independent base Settings
record state, consume/validate logic and record emission. It covers display,
audio, haptics, locale/IME, timezone, alerts, auto reply, debug/gauge values,
radio preference and ten Wi-Fi profiles. The ESP adapter inherits this bounded
state and delegates base parsing/validation, while retaining NVS application,
network and compatibility-extension ownership. Linux can use the same record
grammar without including ESP runtime code. The existing optional-radio record
policy remains explicit through its presence bit.

Each active Wi-Fi profile requires both an SSID record and a password record;
an explicitly empty password still represents an open network. Previously an
OR presence check admitted either record alone. Auto-reply text decoded from
base64 also rejects embedded NUL bytes, preventing truncation in string-backed
storage. Timezone data remains a bounded binary field with embedded NUL support.
The adapter reconstructs its state directly in the existing PSRAM allocation
rather than assigning a large temporary object on the task stack.

Validation passed the 180/180 native suite, including the instrumented shared
record test, all 127/127 HostLink/TMS tests, the real CM0 ARM record fixture, and
the ESP stack guard. A host source syntax check with the P4 conditional enabled
passed its state-size assertion; this is not a full ESP firmware build. The
record fixture deliberately tests only the core plus base extension and must
not be mistaken for complete TMSET7 compatibility: network, BLE compatibility,
optional platform records and native persistence/runtime handoff were assembled
in the subsequent stages below, including runtime handoff. The
installed device deb remains unchanged during this stage.

## Network projection and snapshot ownership

`LinuxReticulumNetworkStore::encode` and `decode` provide the existing internal
network projection grammar without accessing SQLite. The ordinary `load` and
`save` entry points delegate to these functions. A complete exporter can read
the network blob through its existing `SettingsReadView` and decode it without
opening another snapshot or recursively taking the settings mutex. An importer
can prepare the network blob before entering its cross-namespace transaction.
Both codec operations leave caller output unchanged on failure; decode rejects
null, empty, oversized and embedded-NUL input before parsing. This remains an
internal projection, not a complete portable TMS document.

## Complete native document staging

`LinuxTmsDocument` assembles core AppConfig, shared base settings, Wi-Fi,
Reticulum network and protocol-compatibility records in one caller-owned heap
object. Parsing has no SQLite or service side effects. It accepts the current
TMSET7 schema and the requested Working/Backup kind only, requires every
mandatory extension block and rejects malformed or unknown records. Export
validates its entire emitted document before replacing the caller's output.
This establishes document validity, not persistent or live configuration
replacement.

The protocol-compatibility record codec now lives in
`app/tms_compatibility_records.h`; the existing ESP storage adapter delegates
to it. Linux shares the protobuf, chunk, duplicate and scalar validation without
pulling in NVS or device identity. It retains disabled-protocol configuration as
data and does not initialize Bluetooth. The last protobuf chunk is bounded by
the actual remaining destination buffer rather than the nominal chunk width.

Optional C6 records are validated as a complete block, retained and re-emitted
without operating a companion device. Cellular records are explicitly rejected
by the current strict unknown-record policy, rather than silently discarded;
Linux has no owner for that block. Older TMS schema migration is not provided by
this native document adapter.

The native document fixture covers complete roundtrips, open-network passwords,
compatibility module values, required-record rejection, optional C6 roundtrips,
duplicate/unknown records, document kind, input bounds, embedded NUL and failed
export isolation. Its document implementation is instrumented with ASan/UBSan
on native builds. The existing ESP complete-document fixture remains the
independent adapter used for interoperation validation. Public UI/HostLink
capabilities are supplied only by the bound frontend and protocol coordinators
below; this document layer does not start a service or release an application package.

## Native snapshot and import transaction

`export_linux_tms_snapshot` reads AppConfig, independent settings, the shared
Wi-Fi snapshot and internal network projection through one `SettingsReadView`.
It uses strict typed reads; missing values receive native defaults, while
invalid/unavailable values abort export without replacing the caller output.
The existing V3 AppConfig object layout is shared with application startup via
`linux_app_config_blob.h`; V2 migration remains a startup responsibility.

`LinuxTmsImport::prepare` validates the entire input without writes, then
constructs mutations for the app, network, settings and power namespaces. It
preserves compatibility/C6 records in a canonical working document under
`linux_app_facade/tms_compatibility_v1`; subsequent exports overlay current
native settings rather than treating the preserved document as a second
authority. Wi-Fi replacement clears inactive slots and obsolete password aliases
and updates the primary pair in the same transaction. An absent optional radio
record retains the saved preference. The persisted AppConfig always keeps the
disabled Linux phone protocol off.

`commit_settings_import` commits the complete mutation set and installs an
independent process-local import write fence while still holding the store
mutex. A failed commit installs no fence. After success, old-process writes to
the settings-owned namespaces are rejected, including mixed batches containing
an unrelated namespace. Unrelated application data can still be written. The
factory-reset state remains separate; a reset cannot replace a committed import
before restart. Fresh exec starts with normal write admission.

This transaction does not stop services or request restart by itself. The
application owner must exclude new frontend work/calls and arrange service
teardown and restart after success. The commit fence serializes concurrent old
settings writes. HostLink capability advertisement requires the corresponding
bound frontend and, for import, an active application lifecycle owner.

The focused native validation batch passed the complete TMS storage fixture
and the existing transaction, ten-profile Wi-Fi, factory-reset and strict-read
fixtures (5/5). The TMS fixture injects failure in the final power namespace,
checks that every earlier write rolls back and admission remains open, then
commits and verifies all fields and compatibility records after fresh exec.
It also checks stale writes and mixed-batch rejection after the import fence.
These are host validations; no new ARM package was installed for this stage.

## Frontend service handoff

`LinuxAppServices::exportSettingsDocument` flushes pending AppConfig edits before
reading the complete database snapshot. A failed flush aborts export. Its
`importSettingsDocument` counterpart requires an active application lifecycle
owner, validates the document, rejects busy voice work, and acquires the shared
call runtime's configuration-change admission lock. The lock is acquired only
when call signalling/media/closing work is idle and no outgoing admission hook
is in flight. It rejects new incoming and outgoing calls while held.

A failed transaction releases that call lock and leaves the running services,
pending edits and restart intent unchanged. Success retains call exclusion,
clears old pending AppConfig persistence, freezes both tick and explicit
`updateCoreServices` frontend paths, unbinds network edits and requests the
existing application restart. Old asynchronous settings writes are rejected by
the transaction fence. The existing composition then tears down workers and
media before exec; no imported hardware configuration is activated in the old
process. Alt+F4 retains precedence over restart, with the committed settings
applied at the next launch. This service API is frontend-thread-only; HostLink
must marshal requests rather than invoking it on its worker.

The native handoff fixture passed alongside the storage and shared call-runtime
compatibility fixtures (3/3). It blocks an outgoing admission inside its external
hook and verifies that configuration exclusion cannot be acquired while the
public call state still appears idle. It also checks busy incoming calls,
closing-call exclusion, rejected new calls during replacement, pending-edit
export, missing lifecycle ownership, rollback recovery, committed restart intent,
both frontend update paths, shutdown stale-save isolation and exit precedence.
The fixture requests the real restart intent without executing the test process
again; existing application lifecycle fixtures own exec mechanics. These are
host checks, not device UI or physical audio validation.

## HostLink snapshot export

The optional `HostlinkConfigSnapshot` coordinator implements the existing
ConfigSnapshot operation (17) with the official `[offset, count, expected_hash]`
request and `[offset, total, hash, bytes]` response. The chunk limit remains 320
bytes. The worker requests a frontend export through a bounded mailbox and
returns Busy until the normal service update produces the document. This
flushes pending frontend AppConfig edits without calling frontend services on
the worker or retaining frontend pointers in worker callbacks.

One immutable document is held for the owning HostLink session. All subsequent
pages require its SHA-256 hash, so local edits during download cannot mix
document versions. Invalid ranges, mismatched hashes and foreign sessions are
rejected. Disconnect/session close releases the snapshot and cancels pending
mailbox work; binding generations prevent canceled work from publishing into a
replacement request. The frontend releases its binding before service teardown.

Capabilities advertise operation 17 and schema 7 only when a frontend owner and
SHA-256 support are available. This snapshot coordinator exposes no mutation
operations; the separate transfer coordinator below owns ConfigCommit and
uses the frontend import/restart boundary. The existing coordinator-less
management API retains its previous capabilities for other consumers.

The native focused batch passed snapshot export, existing management dispatch,
HostLink socket lifecycle and TMS frontend lifecycle fixtures (4/4). The snapshot
fixture uses production management envelopes and frontend service updates to
download and hash a complete TMSET7 document, verifies pending-edit flush and
immutability during concurrent frontend edits, and checks capability precision,
bad ranges, wrong hashes, foreign-session access/close, explicit close, queued
read cancellation and frontend read failure. No device package was replaced.

## Durable configuration upload and commit

`HostlinkConfigTransfer` implements configuration kind 1 through the existing
TransferBegin/Chunk/Status/Finish/Abort operations and ConfigValidate/Commit.
It persists metadata and the bounded candidate in a single typed SQLite blob,
using the existing TMU1 metadata layout, CRC and 32 KiB document limit. Each
acknowledged chunk is durable. Repeated matching chunks are idempotent; changed
or overlapping chunks, foreign sessions, wrong hashes and malformed documents
are rejected. A fresh worker/process resumes the saved offset. Candidate bytes
are retained on validation failure or disconnect until explicit abort.

Begin captures a frontend-flushed base snapshot. Commit supplies that same base
hash and marshals an immutable command to the frontend. After call/voice
admission, `commit_settings_import_if` checks the current full document hash
inside the same `BEGIN IMMEDIATE` transaction that replaces configuration. Local
edits before or during upload therefore produce Conflict without replacing the
current configuration. Its borrowed view uses only that transaction's connection;
it cannot recursively call the public store.

The committed phase-2 transfer receipt is the fifth namespace batch in the
configuration transaction. Configuration and receipt either both commit or both
roll back. A lost reply or process restart can replay the same ConfigCommit and
read the durable receipt without applying the document again. The restart flag
reflects the current process intent. The frontend may restart before delivering
an ACK; transport loss remains unconfirmed until receipt/readback verification.
An explicit abort cancels queued work, but returns Busy for an applying commit
and Conflict for an already committed transfer.

The factory-reset transaction also clears this transfer namespace, and its
write fence prevents old-process transfer state from recreating removed private
candidate data. Import uses its own independent fence as before.

The formal Linux application starts its HostLink worker after binding frontend
services and stops it during service teardown. The default listener remains
loopback-only, with the existing configurable bind address and port fallback.
This lifecycle entry is shared by native Linux device compositions; no board
name or interface is embedded in configuration services.

Capabilities advertise ConfigValidate/Commit and the configuration transfer
operations only with SHA-256, a bound frontend and a lifecycle owner. Track
uploads retain their existing coordinator and routing. The web editor retries
only explicit Busy responses for snapshot/commit, with a bounded, abortable wait;
disconnects, timeouts, conflicts and storage failures are never blindly replayed.

The focused native batch passed 9/9, including fresh-exec partial-upload resume,
queued abort, changed local base rejection, injected receipt-write rollback,
restart intent and fresh-exec receipt replay. The production web reader/editor/
uploader/save code also completed a native binary-management roundtrip. The
related web tests passed 17/17, covering Busy waits, cancellation and no transport
replay. Full Native/Cardputer builds and ARM package/device gates follow this
feature-level validation; no device deployment is implied by these host checks.

The formal-application fixture additionally launches `trailmate-linux` with a
temporary profile/root and dummy SDL devices, uses real TCP framing to read and
upload a document, observes the actual application exec/reconnect, verifies the
new configuration and replays the durable receipt with restart no longer pending.
It then checks orderly timed exit. It captures no screenshots and does not
exercise physical display, keyboard or audio hardware.

The complete Native rebuild passed its 186 existing fixtures; the added formal
application fixture subsequently passed after correcting its test profile and
canonical request encoding. Cardputer Zero rebuilt and passed 19/19. Eight ARM
fixtures passed on CM0, followed by the final entry build's lifecycle/transfer
fixtures. The installed `/usr/bin/trailmate-linux` also passed the formal TCP
application fixture against the packaged CM0 profile, including actual exec and
post-restart receipt replay. Test roots were temporary and SDL video/audio dummy;
the stock display setup and `screen_power=none` were preserved.

The TMS-stage standard `trailmate-cm0_1.0.0_armhf.deb` passed lintian, installation,
`dpkg -V` and original-logo hash verification. Its SHA-256 is
`1f87bc2f9cdcd4cea27f1dca0ec392e3a927c3fee1e2ade8270f36cb9070aa23`;
the installed executable is
`e4a1ce693b3aa9ddf861ca6e98fd4a53d03127c531a590b34b6c661f7954be1b`.
Later feature deliveries retain this functionality; the hardware integration
ledger records the currently installed package rather than this historical hash.
