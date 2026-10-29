# Foreground LoRa receive sessions

The shared Protocol Probe page uses `platform/ui/lora_receive_session.h` rather
than performing radio acquisition, retuning, transmission or receive polling
inside LVGL callbacks. The existing synchronous LoRa port remains the backend
for device probes and other callers with their own execution owner.

## Contract and ownership

A receive request returns a nonzero generation token. This acknowledges the
request, not radio readiness. Consumers wait for a matching `Ready` snapshot.
Each retune immediately invalidates the preceding receive queue. Packets from
an old in-flight operation are discarded; an old generation cannot read, send
or release a newer session.

Linux owns one worker. It acquires/releases the existing foreground radio port,
configures the candidate, polls packets and executes a single outstanding TX.
All device operations run without holding the UI mailbox mutex. The receive
queue has sixteen fixed slots and drops the oldest on overflow. TX admission
copies caller bytes; caller changes and later retunes cannot modify an in-flight
transmit buffer. Configuration failure, TX failure and observed backend loss
publish failure and clear queued RX before releasing the lease.

Cancellation invalidates the visible session immediately. A hardware operation
already dispatched remains bounded by its backend and may finish; its obsolete
result is not published. Cleanup executes on the worker. The worker is constructed
after the concrete radio owner and joins before that owner's static destruction.
The metadata-only snapshot does not construct the worker or acquire hardware.

Non-Linux targets use an inline synchronous compatibility adapter over their
existing LoRa port. They gain no worker, queue, additional task or large automatic
buffer. The shared page consumes the same completion contract on both paths.

## Probe timing

Candidate dwell begins when configuration becomes ready, excluding reset/retune
time. Active MeshCore/Meshtastic verification waits for TX completion before its
response window begins. A queued TX is never treated as physical send success
or protocol evidence. Existing frame classifiers and correlated response checks
retain responsibility for confirmed evidence. Generation mismatch or worker
failure stops the scan and releases only the page's own token.

## Validation and delivery boundary

The worker fixture blocks acquisition, RX, TX and configuration while exercising
UI requests, cache reads, cancellation and retuning. It checks thread ownership,
stale RX suppression, owned TX bytes, bounded queue behavior, configuration/TX
failure and runtime radio loss. A separate fixture compiles the non-Linux inline
adapter. The actual shared page is tested through its public snapshot/control
API with a fake clock and receiver; synchronous radio functions throw if called.
It verifies that long setup/TX delays do not consume dwell/response windows,
and that receiver failure stops/releases the scan. The focused synchronous-port
fixture also covers rejected-retune admission and recovery.

All four focused fixtures passed natively and as armhf executables on CM0.
The worker fixture additionally passed ASan/UBSan. Native and ARM applications
and both compact Wayland/framebuffer targets rebuilt successfully. The package
passed lintian in an isolated Perl subprocess after an initial checker crash;
the archive was unchanged between checks. The standard deb was installed on
CM0, passed `dpkg -V` and original-logo verification, and completed an isolated
application startup/timed-exit check without screenshots or physical radio I/O.
Its SHA-256 is
`1595c01698e2ef3186399e42ab2fc657fd469fb602b4bc69d077f4e4dbce34c8`.
The preceding `de4fa2d...` package is retained for rollback. The earlier complete
203-test Linux and 19-test compact gates were not repeated for this focused RF
update; they remain baseline evidence, not a claim of a new full-suite run.

These checks do not establish physical packet reception or peer interoperability; the
user explicitly deferred peer-device radio validation. The standalone GTK RF
tools retain their own synchronous LoRa calls and are outside this P4 page path.
