# Linux phone GATT framing

`PhoneGattSession` is a worker-owned framing helper for the Linux phone
GATT service. It does not itself register a BlueZ application, advertise, enable the
application's BLE capability, or interpret phone protocol messages.

The protocol owner supplies complete frames and retains responsibility for queue
ownership and retry policy. Meshtastic limits are 510 bytes for reads and 504 for
writes; MeshCore uses 172 bytes. The helper also caps configured limits at the
512-byte ATT attribute ceiling.

A read at offset zero obtains one complete frame. Further offsets read slices of
that cached frame without consuming another queued message. Each response is at
most MTU minus one bytes. An offset equal to the frame length returns an empty
response; an offset beyond it fails. A new offset-zero read obtains the next
frame. A reader must enforce its own queue contract when returning failure.

The first successful operation claims a peer object path. Other peers are
rejected until the owner disconnects. Disconnect clears the cached frame.
Prepared writes, nonzero write offsets, empty writes, and oversized writes are
rejected before protocol dispatch. Callback failures and exceptions report
failure. The owner must not acknowledge unsupported protocol actions.

Notification eligibility requires an owned peer and enough space for the whole
frame in MTU minus three bytes. No notification truncation is performed. The
future exporter must own subscription state and keep undelivered frames in its
protocol queue; this helper alone does not send a notification.

`linux_phone_gatt_session_test.cpp` covers cached long reads, boundary offsets,
connection isolation, disconnect recovery, write rejection, callback failures,
and notification capacity at default and larger MTUs. These are transport tests,
not phone interoperability or radio tests.
