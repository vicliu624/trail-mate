# Linux phone protocol and worker runtime

The Linux application service now owns `LinuxPhoneRuntime` when its composition
opts into BLE support. The generic LVGL desktop and default Cardputer compositions
opt in; other compositions keep their explicit support policy. Requested enablement
is persisted separately from readiness. `isBleEnabled()` projects successful
GATT and advertising registration rather than the requested flag.

The runtime reuses the existing MeshtasticPhoneSession and MeshCorePhoneCore.
`LinuxPhoneFacade` maps their application port to LinuxAppServices, shared config
snapshot helpers, ChatService, ContactService and the mesh adapter's optional
MeshCore BLE backend. It has no dependency on the legacy UI facade or LVGL.
Text sends use ChatService, including its normal store and failure handling.
Missing backends return failure rather than synthetic keys or radio success.

## Ownership

Application configuration, contacts, shared phone sessions and chat observations
remain on the application thread. One worker owns BlueZ discovery, registration,
D-Bus callbacks and notification delivery. It does not read a mutable AppConfig
or execute domain methods directly.

`PhoneDispatchQueue` permits at most 16 pending worker requests. Application ticks
process at most eight entries per pump. A request that has not started within two
seconds is cancelled and cannot mutate state on a later tick. Once an operation
has started, its caller retains captured storage until completion. Exceptions
return failure. Shutdown cancels queued work before joining the worker, so a
callback cannot wait for a UI tick that will never occur.

Incoming text and data are copied from ChatService observations. The phone never
polls the radio input queue a second time or removes data belonging to another
consumer. The phone's data fan-out has 16 slots and drops the newest observation
when full. Disconnect resets its sessions, cached frames and subscription state.

Notifications remain owned by the shared session adapter until transport accepts
the complete frame. A token identifies each pending frame/counter generation.
After delivery the worker retries acknowledgement without resending the frame if
application ticks temporarily pause. Stale acknowledgements cannot remove a newer
frame. A failed synchronous notification can still be recovered by a normal
FromRadio read. FromRadioSync is explicitly optional and defaults off, matching
the ESP production transport.

## Composition and capability

LinuxPhoneRuntime derives its controller MAC from the dynamically selected
controller address, never from a node number. Controller selection supports a
full address or one unambiguous compatible controller. The service options own
the selector, advertisement name and optional private bus address.

The active application protocol must match the phone protocol. Reticulum is the
product default; requesting BLE does not advertise a misleading Meshtastic
service in that mode. MeshCore additionally requires an actual backend public
identity. The native mesh worker does not yet expose IMeshCoreBleBackend, so that
phone service remains unavailable until the backend port is completed. Transport
registration tests for NUS do not override that gate.

Application startup creates the opted-in runtime, normal service ticks update its
desired state and pump domain requests, and shutdown destroys it before radio and
storage teardown. Changing options invalidates published readiness immediately;
registration results from older configuration generations are ignored.

Shutdown flushes already-pending configuration changes before returning. This
also preserves an explicit BLE-off change made immediately before exiting; it
does not create a save request when none exists. The CM0 cross-package dependency
list now includes GIO's `libglib2.0-0t64` owner and BlueZ. On the validation device
those are GLib 2.84.4 and BlueZ 5.82.

## Verification and remaining work

The host application-owned integration test passed a real private D-Bus exchange
through BluezPhoneGatt, its worker, the dispatch queue, shared Meshtastic session
and actual Linux services. It covered config handshake, changing owner names,
draining responses, durable reopen, protocol-requested Device1 disconnect,
disabling and shutdown connection cleanup. Reopen verified that BLE stayed off.
The protocol test also covered per-protocol node filtering and notification retry
ownership. Queue deadline, cancellation, owning-thread and exception checks passed,
including AddressSanitizer and UndefinedBehaviorSanitizer for the queue helper.
The seven new phone/BlueZ implementation and integration-test translation units
also passed an instrumented host run with AddressSanitizer, UndefinedBehaviorSanitizer
and leak detection. Existing application-service/core archive objects were linked
into that run without rebuilding those older objects with instrumentation.
The final native application and full regression targets were rebuilt and all
107 CTest cases passed after the lifecycle/clock follow-up. CM0 passed the armhf protocol, application-owned
private-bus runtime, dispatch and GATT tests. Tests isolate settings, cache and
SD roots and select an absent radio endpoint, so these do not perform RF peer
validation. Both real BlueZ transport registration probes also passed and
ActiveInstances returned to zero.

This is not yet a claim of complete phone interoperability. Pairing-agent and
Bluetooth/module configuration policy, native MeshCore backend operations and
phone-to-radio end-to-end coverage remain. The native clock port now performs
the system call and propagates authorization failure; unprivileged clock policy
remains a deployment prerequisite. The SDL and compact entrypoints now own
[graceful application re-execution](linux-application-lifecycle.md).

The current CM0 alpha deb has SHA-256
`defe319b4abe2e123455654731520bcedb794e5703797e15dcfe979b18d81845`.
It was installed, passed package-file verification, rendered the P4 UX at
1232 x 568 and exited through compositor-confirmed fullscreen/focused Alt+F4
with code 0 in 0.649 seconds. The prior package was retained as a verified
rollback artifact. This package carries the runtime binding and the explicitly
incomplete phone-feature status, not a complete phone interoperability claim.
