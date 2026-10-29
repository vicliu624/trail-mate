# Native call audio integration

The Linux media implementation reuses the shared Reticulum call runtime, call
overlay, Codec2 payload format and audio focus policy. Production device-local
services now compose the TCP network and call owners, including when physical
LoRa is disabled. Call capability is conditional on a connected gateway,
transmit permission, non-anonymous identity and native media support.

`NativeCallAudioBinding` owns the runtime hooks and a single audio worker.
The worker opens PCM devices, runs `CallAudioSession` and closes PCM before
releasing call focus. Startup readiness requires successful device opening;
cancellation and timeout invalidate late completions. Runtime generation checks
prevent an old media or focus callback from changing a replacement call.

`CallAudioSession` supports Codec2 3200, 1600, 1200 and 700C with fixed bounds on
packet assembly and jitter storage. Microphone mute, speaker mute and half-duplex
PTT transitions discard pending audio at their respective boundaries. Capture
and playback stalls fail the session. The shared echo guard gates capture after
render activity; it is not adaptive acoustic echo cancellation.

Device selection uses `TRAIL_MATE_CALL_INPUT_DEVICE` and
`TRAIL_MATE_CALL_OUTPUT_DEVICE`, then the existing generic audio device settings.
No CM0 ALSA device identifier is embedded in the media owner. An explicitly
selected device that cannot open reports failure rather than silently selecting
another device.

The P4 application loop services the existing shared call overlay and restores
its interrupted application during shutdown. The Linux application service owns
the binding lifetime. Builds without native media use an unsupported stub.

The focused host tests cover shared runtime compatibility, queued-audio privacy,
cancelled startup and focus admission, all four real Codec2 modes, bounded jitter,
mute/PTT, device routing, ownership and the shared UI lifecycle. PCM and transport
are injected or use SDL dummy devices. These tests do not establish physical
microphone/speaker performance, an interoperable peer call or RF transport.

The native secure link owner now has a distinct `lxst.telephony` destination.
It reuses the existing X25519 handshake, authenticated responder proof, encrypted
identity proof and encrypted packet transport. Call hooks are opt-in. Incoming
handshake validation can queue signalling work before identity admission;
`sendCallSignal` permits this control phase while `sendCall` and received media
remain gated on identity admission. Hooks run on the mesh worker and must queue
work rather than reenter the link owner. They must outlive its reset/destruction.
Text delivery and resource handling remain restricted to delivery links.

The link fixture uses two actual native identities and two production link
owners. It exercises forged proof/token rejection, pre-identity media rejection,
control before identity, independent delivery/call destinations, admission
denial, encrypted close, late data, send failure and handshake timeout. The
changed link implementation is instrumented with address and undefined-behavior
sanitizers in this host fixture.

On 22 October 2026 the complete native suite passed 154/154 and the Cardputer
Zero suite passed 19/19. Earlier verification included two lifecycle fixes. The native audio worker now
cleans SDL thread-local storage after its PCM owners close; the voice and Agenda
fixtures had reproduced a 128-byte leak before this correction. The compact
calculator fixture now destroys its shell and joins application workers before
removing SQLite files, avoiding a WAL cleanup race. No leak suppressions or
disabled checks were added.

Visual review of the CM0 Wayland fixture also exposed a call-duration clock
origin mismatch. The overlay now uses `sys::millis_now()`, matching the shared
runtime timestamp, for elapsed time. LVGL ticks remain responsible for UI input
guards. The UI regression deliberately uses a different LVGL epoch and checks
that a newly active call displays less than ten seconds; it failed before the
clock correction.

Nine corresponding ARM fixtures passed on the CM0. The shared overlay fixture
also passed under the device's real Wayland session at 1232 x 568 with injected
signalling/PCM, including the duration assertion and restoration to Chat.

The standard armhf Debian package was rebuilt and installed on CM0. Its SHA-256
is `05b517d44bee7dce825ac58d672084511b9e3216c885838e0fb48b4de6ccc18e`.
Lintian produced no diagnostics, `dpkg -V` passed, and the installed icon matched
the original `images/logo.png`. The installed P4 homepage and active packaged
map both passed compositor-confirmed fullscreen/focus and Alt+F4 exit with code
zero in 0.649 and 0.639 seconds respectively. The prior package is retained
as `trailmate-cm0-before-wifi-profiles.deb` in the device validation cache.

`LinuxReticulumCalls` now owns the shared LXST state machine, peer-directory
policy, runtime UI actions and media packet normalization on one protocol worker.
Link callbacks only mark pending state or copy into a fixed packet ring; they do
not reenter the link owner. User acceptance performs media preparation on the
protocol worker. The overlay remains on the UI thread. Ignored callers and
directory errors fail admission; authenticated unknown callers can ring. Dialling
requires a public identity key bound to the selected delivery destination.

The combined fixture runs two independent processes with actual persisted native
identities, production encrypted links, protocol owners, shared call runtimes and
Codec2 media owners. PCM devices are injected; local datagrams and the production
TCP bearer through a byte relay carry the protocol.
Scenarios cover conversation with mute/PTT, rejection, ignored peers, media open
failure, transport loss, active reset, an occupied runtime, a packet burst that
fills the queue, and replacement of the runtime by a different session. Runtime
replacement reproduced an orphaned old transport; the owner now retires it before
processing queued controls without closing the replacement runtime. Shutdown
assertions require closed PCM and released audio focus.

All nine combined scenarios passed 15 consecutive host runs with address,
undefined-behavior and leak checking enabled. The final combined fixture and
both production call/delivery secure-link fixtures also passed on CM0 ARM.
One initial host compile hit a GCC internal compiler error; the unchanged
sanitizer configuration succeeded on retry, and no checks were disabled.

An interrupted call may discard the final in-flight audio packet when an
authenticated close arrives in the same poll. The fixture proves bidirectional
audio before interruption and terminal cleanup rather than equal final counters
on both ends. Normal conversation still checks multiple packets in each direction.

The production worker now instantiates the network/call owner and uses actual
gateway readiness for `supports_reticulum_audio_call`. Existing gateway host,
port and policy settings persist through application save/reload. A production
application-service fixture with LoRa disabled verifies signed peer discovery,
durable text acknowledgement and a bidirectional Codec2 call over actual TCP.
The corresponding ARM fixture passed on CM0. An asynchronous dial failure
publishes a revisioned runtime fact without taking media/modal leases or
interrupting an active call; shared UI displays its notice once. This UI
regression passed under the CM0 Wayland session.

The full network snapshot now persists through the native service API using
shared TMS records; the production fixture also passes with a saved explicit
endpoint and an empty legacy gateway host. See
[network configuration](linux-reticulum-network-configuration.md).
The shared multi-endpoint settings UI and factory defaults now use the native
configuration port. The protocol worker queues edits without advancing a live
call's context, then coalesces them after its closing phase. The production TCP
fixture verifies continued bidirectional media during two consecutive edits.
Factory Reset now uses the native settings transaction and restart guard;
see [factory reset](linux-settings-factory-reset.md).
Calls with a known identity now reserve a cancellable outgoing session while waiting for
a signed telephony path, including bounded retry and multi-hop route binding;
see [call path waiting](linux-pending-call-path.md). Missing keys can be learned
from the verified path announcement before sending the handshake. Targets
without identity information still require identity discovery. Complete native TMS import/export, AutoInterface
discovery and LXMF propagation client flows remain incomplete;
see [native network transport](linux-reticulum-network-transport.md).
These fixtures do not prove an interoperable external peer call or physical
microphone/speaker performance.
