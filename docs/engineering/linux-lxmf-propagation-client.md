# Native LXMF propagation client integration

This feature is in progress. Shared planning/state, native propagation RPC
transport, worker-owned node discovery/selection and inbound mailbox
synchronization and outbound propagation are connected. Native regression passed
196/196, compact regression passed 19/19, and the standard ARM deb was installed
on CM0 after seven ARM fixtures passed. Package validation, original-logo
comparison, formal configuration/exec/receipt-replay checks and AutoInterface
descriptor replacement passed on the installed executable. The formal incoming
propagation-store/acknowledgement/restart loop also passed on the host and on
the installed CM0 executable, using isolated roots and a genuine official node.

`core_chat` now owns propagation peer selection, bounded entry/transient state,
service planning and synchronization transitions. ESP headers retain compatibility
entry points; the existing ESP `PropagationClient` delegates its sync methods to
the same shared functions. Stamp generation and device policy remain in their
platform owners. Native code no longer needs an ESP include to use these state
contracts.

The sync boundary separates listing, download, local persistence and remote
acknowledgement. A downloaded transient enters the acknowledgement set only
after durable local acceptance. Rejection or persistence expiry leaves it out of
the remote deletion request. Request correlation, retry intervals and clock
correction/rollover retain the existing behavior. These are state contracts,
not evidence of a working native propagation connection.

The native link owner supports propagation-aspect links, identification,
correlated packet requests and resource-backed requests/responses. The pinned
RNS 1.5.4 interoperability fixture exercises explicit nil, compressed responses,
incompressible 40 KB requests/responses, incorrect correlation rejection,
concurrent requests and resource-proof retries after response consumption.
This fixture validates RNS transport, not an LXMF mailbox client.

Authenticated, path-admitted propagation announcements from both IP and LoRa
feed one worker-owned bounded node directory. Selection honors the configured
automatic/manual choice, active-node state, transmission/privacy settings and
the directory's current ignored flags. Freshness uses monotonic elapsed time,
independently of the remote timebase and local UTC corrections. Radio-context
rebuilds clear this state. Native IP propagation operations reuse existing
interface-generation bindings rather than introducing a second route owner.

The native link owner also exposes original-message decryption and text
validation for mailbox downloads. It checks the recipient destination, derives
the token key using the local identity hash, authenticates the encrypted token,
resolves the original author's delivery identity and verifies the LXMF
signature. Direct and propagated text share this materialisation path; only
direct sessions may supply an authenticated-session identity fallback. The
propagation decoder does not queue application delivery or generate any storage
acknowledgement. The worker-owned controller registers a pending durable commit
before calling the application's existing incoming-text port, then consumes the
same application-store completion tokens as direct delivery. It sends remote
`/get` haves only for committed transients. Rejection, timeout, context changes
and disabled transmission cancel the transaction without claiming acceptance.
Only requested transients are admitted; duplicate response entries share one
application acceptance. Manual node and unresolved original-author identities
use the existing path-request owners.

The controller pins each transaction to one bearer. IP operations keep the
existing interface-generation binding; integrated-radio operations use the
radio's guarded sender. Each mailbox request advertises a 63 KB download budget
and stages at most 32 messages. The server may retain messages that do not fit
this batch. Node selection and sync transactions run on the serialized mesh
worker, independently of UI rendering.

The outgoing envelope builder shares signing/packing with direct text. It binds
the recipient's verified public key to the delivery destination, generates a
fresh ephemeral X25519 key and token IV per envelope, and derives the token key
with the recipient identity hash. Failed encoding preserves caller output;
ephemeral private keys and derived secrets are released or cleared after use.
The official RNS/LXMF outgoing fixture independently decrypts a native 40 KB
envelope and validates the original author, message hash and transient ID.

`LinuxReticulumStamp` provides bounded incremental expansion and nonce search
with heap scratch and cloned OpenSSL digest state. ESP retains its mbedTLS and
PSRAM owner. Both platforms use the shared workblock-round encoding and target
comparison in `lxmf_propagation_stamp.h`; the comparison includes equality at the
official numerical threshold.

Native propagation links expose a separate bounded resource-upload port. Its
receipt is `TransportAccepted`, not recipient delivery or proof of durable node
storage. Valid resource proofs complete the receipt; cancellation, resource
expiry and lost sessions yield failure. Reset invalidates the receipt owner.
The existing RNS 1.5.4 fixture now also checks a byte-exact incompressible 40 KB
upload and connection cancellation, with no chat message status outcomes.
Network upload operations use the same interface-generation bindings as RPC.

`LinuxReticulumUpload` connects these encoders and the upload port as one
incremental outgoing task. It honors the selected node's transfer limit and
announced stamp cost. The worker serializes upload and mailbox transactions,
checks current configuration/recipient policy and tracks the upload's message
ID in its public pending snapshot. Shutdown and configuration changes retire
the original command. Successful transport completes the chat attempt as Sent,
never Delivered.

The production ESP delivery planner now lives in `core_chat`, with compatibility
entrypoints retained for ESP. Linux uses the same Direct/Propagated/Automatic
decision. Automatic failures may retry the retained original command through
direct delivery once; forced propagation never silently falls back. The native
upload fixture covers bounded admission, Busy retries, cancellation, changed
stamp cost and encrypted payload materialisation. The ESP dispatch/runtime-state
contracts continue to cover the shared planner's existing public interface.

The formal application fixture verifies incoming propagation persistence. The
official protocol fixture separately tests the native upload controller and
network owner; outgoing application-store status remains covered by component
contracts rather than a complete formal-application outbound fixture.

The formal persistence fixture exposed a dashboard latency defect on CM0: Recent
Activity cached only a prefix while passing the complete 40 KB message to LVGL.
The unequal full-source/cache comparison forced repeated expensive DOT layout,
preventing the main loop from servicing its exit deadline. A 2 KB control exited
normally, and debugger samples located the long case in glyph lookup. The card
now compares and renders the same bounded UTF-8 prefix with an ellipsis. SQLite
still retains all 40 KB. The original scenario passed on the candidate and then
on the exact installed deb, including restart and timed exit; four focused menu
refresh regressions also passed. No display-driver or boot changes were needed.

With the pinned reference environment, run the full application fixture as:

```sh
.venv-lxmf-reference/bin/python apps/linux_lvgl/tests/linux_propagation_application_test.py <build>/trailmate-linux
```

The CM0 check uses isolated reference source modules and existing system
cryptography in its validation directory; it does not install system Python
packages. All application settings/cache/SD/state roots are temporary. It imports
the TCP/node configuration through public HostLink/TMS, observes actual SQLite
message/author records, confirms the official node retained the rejected
envelope, restarts the application and checks unchanged stored content plus a
successful timed exit. No screenshot or physical RF peer is involved.

The protocol reference is the [official LXMF router](https://github.com/markqvist/LXMF/blob/master/LXMF/LXMRouter.py).
The current shared sync fixture checks request binding, known/missing transient
selection, rejected/successful persistence, acknowledgement ordering, timeout,
clock correction and timestamp rollover. The existing ESP runtime-state contract
continues to exercise its original public facade over the shared implementation.

`linux_reticulum_propagation_sync_test.cpp` checks delayed successful persistence,
rejection, timeout, disabled configuration, duplicate message entries and
unsolicited-transient rejection. The official-node fixture is separate:

```sh
python3 -m venv .venv-lxmf-reference
.venv-lxmf-reference/bin/pip install rns==1.5.7 lxmf==1.2.1
cmake --build <build> --target trailmate_linux_reticulum_propagation_reference_test
.venv-lxmf-reference/bin/python tests/reticulum_gateway/propagation_mailbox_reference.py <build>/trailmate_linux_reticulum_propagation_reference_test
```

This fixture uses the official router's signed announcements, `/get` handler,
encrypted original LXMF messages and message store. It verifies a 40 KB original
message is delivered under the author's identity, delays the persistence callback,
then checks the node removed that message and retained an envelope with an
invalid original signature. It uses a test persistence callback rather than the
formal application's SQLite store. Integrated-radio on-air interoperability and
the final ARM package remain separate validation gates.

The same fixture then sends a native 40 KB encrypted message through the upload
controller. The official node verifies its advertised-cost propagation stamp
and stores the upload. The fixture decrypts that stored message with the
recipient identity and verifies the original native author's signature.
