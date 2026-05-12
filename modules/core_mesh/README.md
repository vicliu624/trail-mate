# core_mesh

`core_mesh` is the Phase 4 home for LoRa/Mesh domain language, ports, protocol
strategies, and use cases.

The boundary is deliberately narrow:

- platform radio drivers implement ports such as `IPacketRadio`
- platform storage implements repository ports such as `ILocalIdentityStore`
  and `IPeerKeyStore`
- protocol strategies build and parse Meshtastic/MeshCore packets
- use cases coordinate identity, direct send, receive, events, and session state

This module must not include Arduino, RadioLib, Preferences, SQLite, board,
BLE, UI, or platform SDK headers.
