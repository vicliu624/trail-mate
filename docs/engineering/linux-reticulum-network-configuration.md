# Linux Reticulum network configuration

The Linux application service now owns an explicit, durable
`ReticulumNetworkConfig` in addition to the older `AppConfig` gateway fields.
`setReticulumNetworkConfig` admits and saves a complete snapshot before
queuing it for `LinuxMeshWorker`. `getReticulumNetworkConfig` reads the cached
snapshot without storage I/O. Both methods belong to the UI/service owner;
the protocol worker receives a copy through its existing mailbox.

`LinuxReticulumNetworkStore` writes one SQLite blob under the `linux_reticulum`
namespace. The blob uses the shared TMS codec and network record dialect,
including interface IDs/order/enabled state, three TCP endpoints, AutoInterface
descriptors, IFAC credentials and propagation preferences. This is an internal
network projection. It is not a complete device TMS export and does not replace
the existing Linux `AppConfig` blob or implement other platform TMS extensions.
Core fields in its envelope are not applied when loading the network projection.

The shared `ReticulumNetworkRecords` is a non-owning codec under `core_sys`.
It uses caller-owned staged configuration and scalar presence masks, performs
no platform I/O, and retains the existing record names and schema. ESP's TMS
extension delegates through its original entry points. Domain admission lives
in `validateNetworkConfig` under `core_chat`; ESP and Linux use the same rules.
Neither layer imports Linux paths, board identifiers, sockets or ESP storage.
The extraction adds no large automatic configuration object to an ESP stack.

The Linux reader distinguishes missing, invalid and unavailable storage. Only a
missing network projection permits the legacy single-gateway fallback. An
invalid projection reports an error and installs an explicit empty interface
list; it never silently substitutes a legacy or public endpoint. Unavailable
storage fails service initialization. Decode uses a temporary candidate and
publishes it only after the complete bounded document passes validation.

SQLite writes replace the whole network projection atomically. A failed write
leaves both the previous durable configuration and live worker configuration
unchanged. Inactive slots are cleared before serialization and activation so
shrinking the list does not retain old credentials. An explicit empty list
survives restart and continues to suppress the legacy fallback.

The record regression round-trips three TCP entries alongside LoRa and Auto
descriptors, IFAC credentials with escaping/UTF-8, a disabled entry and an
explicit propagation node. Each of its 61 network records is independently
removed and duplicated; admission rejects every mutation without changing the
previous output. The persistence regression reopens real application services,
injects an actual SQLite trigger rejection and checks corrupt-record isolation.
The production TCP fixture boots application services with the old gateway host
empty and an explicit persisted endpoint, then verifies signed discovery,
durable text acknowledgements and bidirectional Codec2 using injected PCM.

The P4 multi-endpoint editor now uses the platform configuration port on Linux.
Its TCP ordinal, host, port, removal and restore-default actions use the same
allocation-free domain policy as ESP. The compact Linux composition uses this
port too. ESP stages edits in owned PSRAM; Linux stages them in native storage.
Neither shared page code nor the presentation source casts to a Linux service.

An application/UI-thread binding connects the platform port to the owning Linux
service. Shutdown detaches callbacks before service destruction, and an older
owner cannot detach a replacement binding. The port reads cached desired state;
successful edits persist before that state changes. The worker owns actual
transport activation. TCP edits no longer request a redundant whole-mesh
reconfiguration from shared UI. ESP's adapter observes configuration generation,
while the native service explicitly queues the updated network snapshot.

With no explicit saved snapshot, production Linux services use the shared
factory-default builder. A custom legacy gateway stays the sole TCP default;
otherwise the existing three domain-based factory entries are retained.
LoRa-only policy suppresses IP defaults. Explicit removed/disabled entries and
an empty saved list continue to take precedence over defaults. This reuses the
repository's factory list; it is not new availability evidence for public nodes.

The protocol worker coalesces network edits in a bounded mailbox without
changing the active generation during an incoming, outgoing, active or closing
call. It applies only the final snapshot after the call owner becomes idle.
Media, encrypted links and delivery receipts retain their existing context
until then. The production TCP regression queues an empty list followed by a
replacement during bidirectional Codec2, verifies continued media and readiness,
and waits for activation after hangup. Configuration and presentation are
independent snapshots; tests check each sample's internal consistency across
the generation transition rather than assuming two reads are simultaneous.

Factory Reset now clears the explicit network projection alongside application
settings and prevents stale-process writes through teardown; see
[settings factory reset](linux-settings-factory-reset.md).
Complete native TMS import/export remains unfinished. AutoInterface and LXMF
propagation descriptors can be retained but their native bearers/client flows
are not implemented by this storage change. Their presence in a saved snapshot
does not establish runtime support.

On 22 October 2026 the full native suite passed 154/154 and Cardputer Zero passed
19/19. HostLink/TMS passed 126/126 and the earlier independent gateway suite passed
11/11. The four focused ARM fixtures passed on CM0. The SQLite failure fixture
initially failed to create its trigger during full-suite execution; its helper
connection now uses the same five-second busy timeout as production connections.
It then passed twenty consecutive runs and the complete native suite. The ESP
stack guard passed. These checks compile the production ESP TMS extension in
host fixtures; they are not a full ESP firmware build.

The final settings, production TCP/call and shared call-UI fixtures each passed
five consecutive native runs. The actual ESP defaults/edit/remove/
restore implementation and its source contract also passed after the policy
extraction. The call-UI fixture now saves an explicit empty network list when
asserting unavailable transport: disabling physical LoRa alone no longer
guarantees that state once factory TCP bootstrapping is enabled.

On the installed CM0 package, compositor-injected pointer and keyboard input
selected TCP entry 2 in the shared P4 Settings page and saved
`CM0-MixedCase.example.test:5252`. The SQLite network projection contained the
exact mixed-case host and port at interface index 3. A fresh process reopened
that entry; the host was reviewed in the P4 page and the reopened port modal
displayed `5252`. Reopening/cancelling the fields left the durable projection
unchanged. These use an isolated settings
root and a copied profile with physical radio disabled. They establish the
installed UI/storage path, not physical touch or key-matrix performance.

The standard armhf deb was installed on CM0, SHA-256
`05b517d44bee7dce825ac58d672084511b9e3216c885838e0fb48b4de6ccc18e`.
Lintian produced no diagnostics. `dpkg -V`, the original logo hash and the
installed Crypto license comparison passed. The installed P4 homepage and
active packaged map passed compositor-confirmed fullscreen/focus and Alt+F4
exit with code zero in 0.649 and 0.639 seconds. The preceding package is retained
as `trailmate-cm0-before-wifi-profiles.deb` in the validation cache.
