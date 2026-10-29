# Linux Bluetooth scope

Phone Bluetooth support is intentionally disabled and deferred for this delivery.
This applies to T-Display-CM0 and Cardputer Zero. The Linux service composition
ignores legacy BLE enablement requests, and the application entries do not attach
a pairing presenter. Production builds use the unavailable transport without GIO;
Debian packages do not require BlueZ for the application.

Existing transport, protocol and pairing sources remain as deferred development
material. Earlier integration records describe historical experiments, not current
release capabilities. No pairing or Bluetooth interoperability work is required
to finish the current device adaptation. Operating-system Bluetooth configuration
is independent of this application scope and is left as previously configured.
