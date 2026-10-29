# Linux HostLink binary transport

The native TCP port now consumes the same `HL` frames as the formal firmware,
using the shared decoder, CRC, frame router, HelloAck builder and status encoder.
It no longer sends text greetings, marks arbitrary bytes as a completed Hello,
or echoes command bytes. Only a complete CRC-valid Hello enters Ready. HelloAck
retains the request sequence and reports the shared protocol version, 512-byte
payload bound, product firmware version and generic Linux model label.

The current capabilities are `CapStatus` and the limited `CapManagement` surface
documented in [author management](linux-hostlink-author-management.md). Initial and periodic status
frames contain cached battery/charging data, link state and last error. The
adapter omits radio/configuration fields that it cannot yet supply rather than
encoding placeholder values. Commands before Hello receive the shared NotInMode
Ack. Commands without a native handler receive Unsupported with their original
sequence. Configuration mutation, time setting and RF transmission are not
advertised by this transport stage.

## Stream ownership

One worker owns the connection's decoder and output queue. TCP read boundaries
have no frame meaning: fragmented, coalesced and maximum-length frames pass
through the shared streaming decoder. CRC-invalid input is discarded. The
existing five-second Hello timeout closes peers that never complete a valid
handshake. Disconnect/reconnect clears decoder and output state before reuse.

The output queue has eight fixed slots, each sized to the shared maximum encoded
frame. It performs no per-frame allocation. A short write retains the exact byte
offset; EINTR retries, would-block preserves the queue, and connection failure
ends the session. Tx counts advance only after whole frames are sent. Queue
exhaustion fails the connection rather than overwriting a pending response.
Periodic status is queued only when existing output has drained. Stop
cancellation is checked during writes, and the existing worker owner joins on
application shutdown/process exit.

## Verification scope

The deterministic queue fixture checks partial writes, interrupted writes,
would-block/resume, fixed capacity, failed writes and invalid writer counts.
The real loopback socket fixture checks absence of text/echo, pre-Hello command
rejection, corrupt CRC recovery, fragmented Hello, exact capability bits,
initial/periodic status TLVs, coalesced commands, 512-byte payloads, correlated
Unsupported responses, reconnect after a partial frame and Hello expiry after
garbage input. It runs alongside the existing failure/exit/concurrent-lifecycle
regressions. These are protocol tests, not proof of a browser's complete
management workflow or USB interoperability.

The fixture passed natively and on CM0 as armhf code. The complete native suite
passed 116/116; Cardputer Zero Wayland/framebuffer builds and 14/14 tests passed.
Platform boundary and ESP stack hygiene checks passed.

The standard CM0 development deb SHA-256 is
`7469fc15db58d0d189caa7be0ec6cd9d0c3be624f6f84ee452a004e5beb76781`.
It passed Lintian without diagnostics, installation and `dpkg -V`. The existing
logo and 1232 by 568 P4 render were confirmed. Compositor-confirmed fullscreen
Alt+F4 exited with code zero in 0.681 seconds. No kernel, display driver, boot
configuration or network-interface configuration was changed.

## Remaining management integration

The formal envelope and device-author read/proof operations are now implemented.
Owned staged upload transactions are now implemented separately in
[the upload adapter](linux-hostlink-uploads.md). Broader mutation authorization
and owned-track signing/history/network publication remain separate work.
Public package installation/status is now supported by
[the package coordinator](linux-hostlink-package-install.md). Configuration and transmit
capabilities remain absent. The existing signed-package image reader verifies an
already installed package; it does not replace authenticated import/installation.
