# Native call path waiting

The Linux Reticulum network owner can reserve an outgoing call when the peer's
identity hash is known even when its public keys or telephony path have not arrived.
Previously it sent a path request and immediately returned NotReady, requiring
another manual dial after discovery. The public MeshWorker entry point now
admits the same waiting operation instead of issuing discovery and returning
NotReady before it reaches the network owner. A target without either public
keys or an identity hash still requires identity discovery first.

The link owner generates the real ephemeral keys, canonical LinkRequest ID and
pending call session without sending a handshake. If keys are missing, the
prepared session contains only the expected identity; it cannot enter Handshake
until a supplied public key hashes to that identity. The call owner starts the
normal shared outgoing runtime using that ID. Cancellation, modal ownership,
sleep inhibition, audio focus and deferred network configuration consequently
use the same lifecycle as routed calls. PCM capture does not start during path
waiting. The network owner holds one bounded pending call and rejects a second.

Waiting lasts at most 30 seconds. The network owner sends a telephony path
request and retries after 20 seconds following successful submission, or after
one second following failure, subject to the ingress path-request cooldown.
The signed path must belong to a currently live interface generation. Before
transmission, peer lookup must succeed, public keys learned through the existing
verified announce projection must derive the
same telephony destination, and the peer must not be ignored. Permission loss,
transport reset, cancellation or expiry closes the original call rather than
turning a later announce into another dial.

Once the route is available, the link owner rebuilds the request using its
prepared keys and the selected direct or multi-hop transport header. It verifies
that the canonical ID still equals the ID already held by the UI. Only then
does it enter Handshake and submit the packet on the selected endpoint. The
15-second handshake clock starts at this transmission, independently of the
earlier path wait. Failed transmission closes the call; it does not cause
automatic redial after rejection or an established-call failure.

`linux_pending_call_path_*` runs the production network, call and link owners
against a real loopback TCP socket with a controlled clock and fake PCM. The
resolve/multihop cases delay the signed path for more than 20 seconds, verify a
second path request but no handshake before resolution, and verify the original
ID and selected transport header afterward. Other cases cover cancel, timeout,
ignoring the peer, network clear and transmit-permission revocation. Unknown-key
cases additionally reject invalid signatures, inconsistent identity/address
pairs, missing identity information and wrong key binding. A late
announce must not send another handshake. These touched protocol sources are
instrumented with ASan/UBSan in native builds. Existing call and production
MeshWorker TCP fixtures continue to cover authenticated signalling, Codec2 media,
durable text delivery, active-call configuration deferral and shutdown.

Initial known-key validation on 22 October passed all 169 native tests and the rebuilt 19/19
Cardputer Zero suite. After strengthening the reset/revocation cases to
reconnect before injecting a late announce, all seven scenarios and the two
existing call/worker fixtures passed again with sanitizers. The seven scenarios
and existing call/production-worker TCP fixtures also passed as real ARM
executables on CM0, using generated loopback peers and fake PCM. The standard
armhf deb passed Lintian without diagnostics, installed with clean `dpkg -V`
and original-logo checks, and exited a focused fullscreen Wayland window through
Alt+F4 with code 0 in 0.502 seconds. The ESP stack guard passed. No real radio
peer, acoustic capture or physical display-power test was performed.

The subsequent missing-key extension passed all 178 native tests and the 19/19
Cardputer Zero suite. Its sixteen path/key scenarios passed on CM0, including
both public MeshWorker admission cases (known keys/missing path and known
identity/missing keys), plus the existing ARM call and production-worker TCP
fixtures. Public entry originally still returned NotReady before the waiting
operation reached the network owner; the explicit worker cases guard against
reintroducing that mismatch. Wrong-key and absent-key link transmission attempts
are rejected before any packet is sent. A forged announce does not populate the
peer directory or advance the outgoing session, while a subsequent valid one
resumes the original ID.

The Agenda UI fixture now persists an empty network snapshot before startup.
It had received unrelated default-gateway peer announcements and retained late
node events at teardown under LeakSanitizer. Network isolation removes that
external dependency; sanitizer checks remain enabled. The resulting standard
deb passed Lintian, `dpkg -V` and original-logo validation on CM0. Its real
focused/fullscreen Alt+F4 smoke check returned code 0 in 0.586 seconds. Physical
audio and external-peer interoperability remain outside this evidence.

This is software protocol evidence with generated identities and local peers.
It does not establish physical microphone response or interoperability with an
independent external implementation. AutoInterface, LXMF propagation and full
native TMS import/export remain separate work.
