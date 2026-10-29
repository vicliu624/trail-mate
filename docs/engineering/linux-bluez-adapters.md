# Linux BlueZ adapter discovery

Status: discovery used by the Linux phone runtime; discovery alone is not phone readiness.

`queryBluezAdapters` reads the system BlueZ object manager on a worker. GIO types
remain inside the Linux adapter library; the public result owns plain C++ data.
The probe has a two-second cancellable connection deadline and a two-second
object-manager call deadline. It does not power adapters, register a GATT
application, advertise, pair or change operating-system configuration.

Adapters come from `org.bluez.Adapter1` objects. Person/device objects beneath
an adapter are not mistaken for additional controllers. Phone-service eligibility
requires both `org.bluez.GattManager1` and `org.bluez.LEAdvertisingManager1` on the
same object. Their contracts are documented by BlueZ's
[Adapter API](https://github.com/bluez/bluez/blob/master/doc/org.bluez.Adapter.rst),
[GATT manager API](https://github.com/bluez/bluez/blob/master/doc/org.bluez.GattManager.rst)
and [advertising manager API](https://github.com/bluez/bluez/blob/master/doc/org.bluez.LEAdvertisingManager.rst).

Selection uses a normalized full controller address or one unambiguous eligible
adapter. It never assumes `hci0` or chooses the first entry among multiple eligible
controllers. Missing services/controllers, invalid selectors, ambiguous matches,
unsupported adapters and powered-off adapters remain distinct outcomes. A
failed query cannot reuse an adapter from stale data. A build without GIO returns
an unavailable result rather than pretending that discovery succeeded.

The standalone CMake library is reused by the [BlueZ phone GATT transport](linux-bluez-phone-gatt.md).
It is now connected to `LinuxAppServices` through the
[phone worker runtime](linux-phone-runtime.md). Shared `core_phone` sessions retain
Meshtastic/MeshCore protocol and configuration semantics; discovering a controller
does not enable a false phone-ready projection. Readiness requires the selected
protocol backend and successful GATT/advertising registration. Pairing policy and
native MeshCore backend completion remain separate work.

Verification includes a pure selection regression and an actual private D-Bus
fixture serving a BlueZ object manager. The fixture covers nonzero controller
indices, multiple controllers, address selection, power state, service loss,
unrelated device objects and a malformed bus address. A Unix peer that accepts
connections without completing D-Bus authentication verifies cancellation at
approximately two seconds. The host full suite passed 100/100 before that final
deadline check was added; the updated targeted tests then passed. Assertion-enabled
armhf selection and private D-Bus tests, including the stalled-authentication
deadline, also passed on CM0. This does not require or enable its physical HCI
controller.

The user-authorized CM0 boot change subsequently enabled the existing controller.
Its firmware, powered state, GATT/advertising managers and real system-bus query
were verified. The discovery code selected that controller as Ready without
assuming its object path. The reviewed boot experiment and backup are recorded
in the CM0 hardware ledger; no boot change is performed by this library or by
the Debian package. The subsequently implemented transport passed GATT and
advertisement registration on CM0 for both phone protocols. Shared phone-session
and application binding subsequently passed the application-owned Meshtastic
private-bus tests on host and CM0. Pairing policy and native MeshCore backend
completion are still required for full phone functionality.

The bounded connection helper now creates a dedicated D-Bus connection for its
caller, so a future GATT transport can close its own registration connection
without terminating another component's shared bus connection. The updated
private-bus/deadline tests and real CM0 system-bus query passed after this change.
Phone UUIDs were consolidated into the platform-shared header while preserving
both embedded include entry points. All nine string UUIDs match their original
ESP/nRF52 values, and the nRF52 little-endian arrays are unchanged; header syntax
and ESP stack-hygiene checks passed.
