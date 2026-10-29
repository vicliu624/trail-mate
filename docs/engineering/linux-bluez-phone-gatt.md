# Linux BlueZ phone GATT transport

Status: implemented transport with host and CM0 validation. The source now binds
it to `LinuxAppServices` and shared phone sessions through the
[Linux phone worker runtime](linux-phone-runtime.md). Requested enablement does
not imply readiness. Pairing/configuration policy and the native MeshCore backend
remain incomplete; the current alpha package contains this runtime binding.

`BluezPhoneGatt` exports either the Meshtastic service or the MeshCore Nordic UART
service using the UUIDs in the shared phone BLE header. It contains no protocol
parser, node database, configuration implementation, or radio adapter. The owner
supplies read/write/session/subscription hooks and retains queue ownership when
an operation or notification fails.

The public API uses C++ data only. GIO types stay inside the implementation. All
methods, event pumping and hooks belong to a single worker thread. Controller
selection uses a full address or one unambiguous eligible controller, with no
fixed controller index. The transport does not enable the adapter or modify
boot configuration.

## Registration and lifetime

The application exports ObjectManager, GattService1, GattCharacteristic1 and
LEAdvertisement1 objects on a dedicated D-Bus connection. It resolves and pins
the unique `org.bluez` owner. Registration calls use asynchronous GIO completion
while pumping the owning context, allowing BlueZ to query the exported directory
and properties before replying. Application readiness requires successful GATT
and advertising registration, with a three-second deadline on each call.

Failure closes the dedicated connection, releasing registrations without closing
another component's shared connection. Stop is idempotent. BlueZ owner loss,
adapter removal/power-off, advertisement release and bus closure invalidate
readiness. Restart/retry policy belongs to the future runtime owner.

These contracts follow the official BlueZ
[GATT manager](https://github.com/bluez/bluez/blob/master/doc/org.bluez.GattManager.rst),
[characteristic](https://github.com/bluez/bluez/blob/master/doc/org.bluez.GattCharacteristic.rst)
and [advertisement](https://github.com/bluez/bluez/blob/master/doc/org.bluez.LEAdvertisement.rst)
interfaces.

## Access and framing

Only the pinned BlueZ owner can invoke live characteristic methods or release
the advertisement. Directory and property inspection contain metadata and empty
Value properties. Payload notifications are addressed to BlueZ's unique bus name,
rather than broadcast to other local D-Bus clients.

The default characteristic flags require encrypted reads, writes and notification
subscriptions. An explicit unencrypted option supports the protocol's no-PIN
mode. Pairing-agent policy and application configuration are not implemented by
this transport and must be supplied by its future runtime owner.

Meshtastic exposes ToRadio, FromRadio, FromNum and LogRadio. FromRadioSync is
explicitly optional and defaults off to match the ESP production transport;
MeshCore exposes NUS RX and TX. Long reads use the
[cached framing helper](linux-phone-gatt-session.md). Writes must be complete,
bounded frames; callback rejection is returned as a D-Bus error. Data is never
truncated to report success. The four-byte FromNum counter may notify before a
peer is claimed. Other notifications require a claimed peer and a complete frame
that fits the negotiated MTU.

Read/write requests identify a single owning device on the selected controller.
Other peers are rejected until that owner disconnects. Device disconnect clears
cached reads and subscription state and invokes the protocol reset hook.

Standard StartNotify does not identify the remote device. Consequently business
notifications also require exactly one known connected device on the selected
controller. Initial ObjectManager data, PropertiesChanged and InterfacesAdded/
InterfacesRemoved maintain that set. A second connected device suppresses business
notifications until the connection is unique again; the caller must retain its
frame. This is intentionally conservative and can also suppress notifications
when another Bluetooth peripheral is connected to the same controller. It is
not simultaneous multi-phone support.

## Verification

The private D-Bus fixture uses controller index nine and performs real reentrant
ObjectManager/property queries during registration. It covers both protocol
schemas, long-read continuity, cross-peer isolation, write backpressure, another
bus client's denied live access, directed notifications, empty public Value
properties, MTU rejection, multiple connected devices, device objects added while
already connected, disconnect recovery, registration failures and BlueZ owner
loss. A separate observer verifies that private Value signals are not broadcast.

The host full Linux suite passed 102/102 after the main implementation. The final
InterfacesAdded branch subsequently passed the targeted GATT regression. The
armhf build and framing test passed, and CM0 passed the private-bus fixture and
both real system-bus registration probes. Both Meshtastic and MeshCore services
and advertisements were accepted by the actual BlueZ daemon. Each probe closed
its dedicated connection; the controller remained powered and ActiveInstances
returned to zero.

The final fixture also passed AddressSanitizer, UndefinedBehaviorSanitizer and
LeakSanitizer with leak detection enabled, plus `-Wall -Wextra -Werror`. The leak
check found two missing unrefs in the test's parsed GVariant fixtures; those
ownership errors were corrected before the clean run. A no-GIO syntax build
passed as well.

The `--system` and `--system-meshcore` test modes advertise a temporary validation
service for two seconds. They have no protocol handlers and reject business
operations. They do not establish phone interoperability, pairing, message
delivery, or application integration. No phone BLE readiness claim follows from
these registration tests.
