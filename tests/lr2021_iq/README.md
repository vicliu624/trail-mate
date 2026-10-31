# Pinned LR2021 RX engine contract

The reusable CMake helper in `cmake/TrailMateLr2021Iq.cmake` compiles only the
MIT-licensed RX engine and bounded single-producer/single-consumer queue from
[lr2021-sdr commit 0a44fc69beb34274d6e86f29c1c064b3d720c48d](https://github.com/SAMS0N1TE/lr2021-sdr/tree/0a44fc69beb34274d6e86f29c1c064b3d720c48d).
The archive SHA256 is checked. Its upstream CMake project, TX code, host servers
and optional external drivers are not added to Trail Mate's build graph.
The source's MIT notice remains available through the target license property.

Trail Mate builds a generated RX source/header overlay in the build directory;
the fetched upstream checkout stays unchanged. Both input files must match
their pinned SHA256 hashes before the overlay is generated. The adaptation adds
an optional nonblocking `lr2021_iq_session.cancelled` callback and a result flag.
The producer checks cancellation every outer FIFO service iteration and before
tail publication, even when the FIFO is empty. Explicit cancellation discards
the pending tail and follows normal standby/reset/configuration recovery; it
does not turn an empty cancelled stream into a timeout or hide FIFO/recovery
errors. Previously validated blocks remain counted. Null callbacks preserve
upstream behavior. All linked consumers must use the generated public header
because the result/session structs now include these additional fields.

For `ESP_PLATFORM`, continuous streaming requires a checked session with
caller-owned staging storage. The engine rejects missing storage and legacy
unbuffered stream entry points before taking the HAL lock or touching hardware.
Finite diagnostic capture is unchanged. Host legacy stream APIs retain their
fallback. The unchecked host path reuses that staging area instead of reserving
a second 256-byte automatic buffer. No shared fetched source is modified.

`iq_stream_esp_storage` compiles the exact generated C source with
`ESP_PLATFORM=1`, reuses the cancellation/recovery cases, and verifies that
missing caller storage is rejected without hardware access. GNU builds enforce
`-Wframe-larger-than=512` as an error on this contract target. This checks the
host compiler's ESP conditional path, not the final RISC-V task stack budget.
On 2026-10-30, all seven IQ tests passed; the same host compiler rejected the
unadapted source's 1792-byte stream frame under that 512-byte guard.

`iq_stream_cancel` covers empty FIFO cancellation, pending-tail discard,
cancellation after a published block, FIFO faults at cancellation, and reset or
configuration recovery failure. It also reruns the unchanged upstream radio
tests against the adapted target. These are fake-HAL tests; the new callback
does not itself solve RTOS producer scheduling or connect FM reception.

```sh
cmake -S tests/lr2021_iq -B .codex-build/lr2021-iq/build -G Ninja
cmake --build .codex-build/lr2021-iq/build
ctest --test-dir .codex-build/lr2021-iq/build --output-on-failure
```

These run the pinned upstream radio transaction and queue tests against the
same RX library target. They cover firmware/profile preflight, timeouts, FIFO
mapping/overflow errors, rejected blocks, reset/configuration rollback and
restore errors, plus queue overflow, index wrap and 10,000 concurrent blocks.
They exercise fake HAL boundaries and actual queue synchronization on the host.
They do not verify a Trail Mate hardware HAL, physical ROM compatibility,
exclusive ownership against the mesh worker or FIFO scheduling on either board.

The P4 adapter test extracts its command, snapshot, reset and restore methods
directly from production source. It checks two-frame register reads, big-endian
address/data packing, restoration of RadioLib command width even on failures,
transport/status rejection, actual firmware/configuration snapshot, task-pause
and modem/TX preflight fields, reset without RX, restoration of prior RX versus
standby, and propagation of antenna/configuration/reset errors. RadioLib,
board power and hardware remain fake boundaries in this host test.

The P4 extraction also includes the production `iqFifoRead` method used by the
experimental FM stream adapter. Tests check its one-frame `0x0001` transaction,
1/64/256-byte payloads, singleton-owned scratch, argument rejection before I/O,
strict status-word rejection without publishing payload, and balanced bus/CS
cleanup on transfer failure and an already failed HAL. These fake-boundary
checks do not qualify FIFO service timing or continuous RTOS scheduling.

`p4_iq_wait` tests the production `P4IqWait` header against timer/semaphore
boundaries. Its one-shot timer blocks the dedicated producer at microsecond
resolution; the normal interval is independent of the 100 Hz RTOS tick.
Generation checks reject stale tokens. A timeout leaves process-lifetime
callback storage intact and refuses another arm until the late callback
completes, avoiding a dangling task handle or per-iteration allocation.
Tests cover create/start failure, normal expiry, timeout/late completion,
spurious and pre-arm tokens, and 10,000 repeated waits. The ESP stream test also
checks cancellation immediately after a service wait, before another FIFO read.
On 2026-10-30, IQ tests passed 8/8, native FM/TV/media tests passed 14/14 and the
TFT LR2021 firmware build succeeded. These are control-flow and compilation
results; actual dispatch latency and watchdog/FIFO margin still need hardware
measurement.

The Linux adapter test also extracts production command/snapshot/reset/restore
methods. It verifies actual ROM and applied settings, single-owner LoRa guards,
big-endian register traffic, command-width recovery, configured TCXO voltage,
full configuration restore and propagated failures. The Linux driver now links
this RX engine and implements the optional packet-radio finite capture port;
its final application symbols are checked separately from these fake-boundary
tests. The Linux worker and conditional Probe entry now use the foreground
coordinator and a physical receive-session release barrier. Its real-thread host
test checks handoff waiting, cleanup before Ready, signed IQ statistics, partial
data and restore failures, non-simulated capability gating and exit joining.
Separate LVGL fixtures cover both 1232x568 and 320x170, including long errors,
scroll access, footer separation, shortcut routing and post-recovery controls.

The runtime adapter must implement every required bounded/status-checked HAL
hook before exposing raw IQ. It must use persistent internal-memory staging
and queue storage. The session API accepts caller-provided staging, but its
current implementation still declares a 1280-byte automatic fallback and a
256-byte legacy read buffer. Supplying staging alone does not prove those
automatic allocations disappear; audit compiler stack usage before using the
continuous API on ESP. Producer callbacks only copy to
the queue. A rejected/full block stops capture; a restart gets a new generation.
The consumer must reset DSP on generation/index discontinuities. Recovery must
restore modem, calibration, RF switches, IRQs and prior reception before mesh
operation resumes.

The native P4 LR2021 driver now has a finite `captureIq` adapter using the real
singleton transport and mutex. It requires an acquired foreground owner with
AppTasks radio work already paused, online LoRa mode, enabled radio power and
no TX. Caller-owned non-overlapping 256-word buffers hold output and SRAM backup.
The portable engine validates ROM, captures 256 IQ pairs, resets, restores and
verifies SRAM, then reapplies the prior LoRa configuration and RX/standby state.
No output bytes are published on error; failed recovery takes the driver offline
and asserts reset. A zero nominal rate remains unknown and must not be passed
to a decoder as an assumed sample clock.

On the native P4 LR2021 target, Protocol Probe exposes **IQ Snapshot** (keyboard
`I`). This stops any probe session first and captures the currently configured
mesh PHY in a worker, not the LVGL event callback. Starting a scan or setting a
profile is blocked while capture/recovery is active. Page exit joins the worker
before another page can acquire the radio. The worker releases/restores mesh
operation before publishing Ready; failed capture, incomplete data or an offline
radio after mesh restoration publishes no sample statistics. Results include
pair count, qualified nominal rate (or explicitly unknown), RMS, DC and clipped
pair count. These are finite diagnostic statistics, not protocol evidence.
RMS is `sqrt(mean(I*I + Q*Q)) / 32768`; DC components are also normalised to
32768. These values are not calibrated RF power or signal-to-noise estimates.
The native TFT firmware link is checked for the UI start function, driver
capture method and portable `lr2021_iq_capture`, so an unused standalone capture
object is not accepted as evidence that the entry reaches the firmware backend.

The worker host test compiles the production runtime with fake task/radio
boundaries. It checks single-owner admission, cleanup-before-Ready, sample
statistics, failure/incomplete-data invalidation, pre-existing owner rejection,
task allocation failure and exit joining. Its Windows task fixture is sequential;
Linux CI and firmware use real standard-library mutexes. The separate LVGL IQ
page test supplies a fixture runtime and checks button/shortcut wiring, busy
guards, error text, unknown sample rate, content bounds and exit joining.

This finite diagnostic is not a continuous capture producer or FM receiver.
Hardware behavior remains unverified. The bounded FIFO/queue consumer path still
needs a board streaming HAL, task scheduling/ownership integration and measured
service deadlines. Other targets do not expose this entry without a real runtime.
