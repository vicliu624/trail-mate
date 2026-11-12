# HostLink ROM download mode

The USB-only `EnterBootloader` management operation allows an explicitly
requested firmware update to enter the ESP32-S3/S2 ROM loader without a second
physical BOOT interaction. Unsupported adapters omit operation 10 from their
capabilities and return Unsupported if it is requested.

Run the command against the **application** port, with Python and PySerial
available:

```sh
node tools/hostlink/enter_download_mode.mjs --python /path/to/python APPLICATION_PORT
```

The command reuses the production HostLink frame and MessagePack codecs. It
performs HELLO, checks management capabilities, submits operation 10 with the
explicit `[true]` body, and validates both correlation IDs and the response.
It closes the application serial transport before looking for the ROM port.
The JSON result reports `accepted`, `applicationPort`, `downloadPort`, and
`serialClosed`. Port matching prefers the normalized MAC serial number, then
the physical USB location. A changed USB interface suffix is allowed; a known
different MAC is rejected. Failure to observe the ROM port after acknowledgment
requires inspecting the current device state rather than blindly repeating.

The firmware reuses the existing storage worker and Arduino service pump; it
does not allocate another task or increase an existing task stack. The portable
owner stores one pending request's correlation IDs and transition state. It
waits for storage stop acknowledgment, claims the SD runtime's exclusive owner,
and queues a response carrying a completion cookie. Only a fully written and
flushed matching response triggers the SDK ROM restart after a short grace
period. Rejected, disconnected, timed-out, and discarded transitions restore
the previous storage service. Pending transitions block other management
mutations and legacy writes. Existing active storage publication/install owners
reject a competing transition.

The command does not flash an image, erase NVS/SD, change Team membership, or
export configuration or identity material. Use the normal board-specific
application flashing and reset procedure after the ROM port appears. Once a
transition succeeds the device remains in ROM mode until that procedure runs.

## Verification

`tests/hostlink_download_mode` exercises the actual portable owner and Arduino
binding with bounded platform fakes, including S3 and unsupported builds. It
covers explicit confirmation, borrowed request lifetime, concurrent requests,
storage stop/claim/restore, stale and discarded completion cookies, disconnect,
timeout, tick wrap, and an unexpectedly returning restart call.

`node tools/hostlink/test_enter_download_mode.mjs` checks the production codecs,
capability gate, correlated business response, rejected responses, and USB port
identity. On a LilyGo Pager SX1262, the command was additionally observed to
move the running application to its matching ROM port; application-only flashing
and a full SDK reset returned the same hardware to its application port.
