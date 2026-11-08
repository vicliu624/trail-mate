# MC/MT node discovery reliability

The NodeInfo and discovery audit on 2026-11-08 found independent failures between
radio observations, encryption-key storage and the Contacts/Nearby projection.

## Corrected behavior

- Linux MC and MT adapters schedule a self announcement after startup, identity,
  name, protocol or transport configuration changes. The initial delay is five
  seconds, successful announcements recur every three hours, and failed automatic
  attempts wait thirty seconds before retrying. TX-disabled configurations never
  announce. MC receive guards defer automatic announcements.
- An MC scan sends a local signed identity advert before its anonymous discovery
  request. The responder can then decrypt a subsequent direct message from the
  scanner. A correlated full-key discovery response is persisted and becomes
  available for encryption immediately after the scan guard. This does not grant
  manual key verification. Unsigned responses cannot replace a cached key for the
  same node ID, and partial or unsolicited responses do not populate that cache.
- The Linux MC adapter and worker expose that identity/key readiness through
  their PKI capability and query APIs as well as through the direct-send cache.
  These APIs no longer report MC keys as unavailable solely because MT is inactive.
- MC NodeInfo and full-key observations converge on one public-key directory
  identity, in either arrival order. Refreshing a legacy duplicate preserves its
  display metadata, user alias and local verification state. Different full keys
  remain distinct even if their projected node IDs collide.
- Directory persistence stores/restores the MC projected node-ID hint. Older
  public-key records without the hint recover it from their complete identity
  key, so a reboot does not leave a valid peer unreachable by its node ID.
- Shared MC discovery and ESP MC advert/key-cache paths do not reject a peer just
  because its first public-key byte matches the local routing hash. The shared
  runtime compares the complete projected node ID; the ESP adapter compares the
  complete public key where it has access to both keys.
- ESP and nRF52 MC NodeInfo observations use the local receive time for Nearby
  freshness. An old sender clock must not immediately expire a newly heard peer;
  advert and position timestamps retain their separate protocol uses.
- Linux MT NodeInfo reply suppression starts only after a successful reply.
  Failed or suppressed attempts do not extend the suppression window.

The shared MT decoder continues to support on-air `User` payloads, wrapped
`NodeInfo` payloads and `MapReport` metadata. The Contacts refresh timer reads the
directory again while Nearby is open. Saved contacts and ignored peers retain
their existing separate views.

## Regression coverage

`apps/linux_lvgl/tests/linux_node_discovery_test.cpp` exercises automatic
announcement/retry scheduling, TX disable/enable, failed MT replies and repeated
requests, full-key MC discovery, both MC wire profiles, Nearby projection,
directory promotion and legacy duplicate repair. Its paired radio fixtures verify
that MT/MC direct text is actually decrypted by the recipient after discovery.
Discovery-key persistence is checked without marking the key manually verified.

The test is registered in the Linux LVGL and Cardputer Zero compositions when
OpenSSL is available. Build and run it using the selected composition's existing
build directory, for example:

```sh
cmake --build <linux-build> --target trailmate_linux_node_discovery_test
ctest --test-dir <linux-build> --output-on-failure -R '^linux_node_discovery$'
```

The existing peer-directory contracts, MC runtime contracts, MT NodeInfo/User/
MapReport tests and MC discovery/direct/NodeInfo/service/repeater adapter tests
provide additional coverage. ESP changes also require the stack hygiene check.
Host radio fixtures do not qualify physical RF reception, antenna performance or
firmware running on a device; those require a separate hardware validation run.

The 2026-11-08 host run passed seven Linux integration tests (node discovery,
worker, MC NodeInfo, discovery, direct messaging, services and repeater adapter),
the three shared contract executables (peer directory, MC runtime and MT node
payloads), the ESP stack guard and `git diff --check`. ESP/nRF52 firmware builds
and physical RF interoperability were not part of that host validation.
