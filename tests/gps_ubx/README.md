# Arduino GNSS UART contract

This fixture compiles the production Arduino `GPS` class with the real pinned
TinyGPSPlus 1.0.3 parser and a host-only UART receiver. It checks the first UBX
payload byte, fragmented input, full checksum consumption, unrelated traffic,
live NMEA parsing during reply waits, and continued legacy sentence setup after
an individual NAK. The unmodified 29ea91ea implementation fails all six legacy
cases, including rejection of a correctly encoded ACK.

Pager selects the MIA-M10Q configuration protocol at the board boundary. The M10
cases inspect complete checksum-valid CFG-VALSET frames for power mode, satellite
systems, full/reduced NMEA output, output disable and NAK handling. Only the
current RAM configuration layer is written; no receiver BBR/flash layer is used.
The frame keys and restart delay follow the
[u-blox M10 SPG 5.10 interface description](https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf).
Satellite-system selection preserves the receiver's supported signal settings.

```sh
cmake -S tests/gps_ubx -B .codex-build/gps-ubx -DCMAKE_BUILD_TYPE=Debug
cmake --build .codex-build/gps-ubx --parallel 2
ctest --test-dir .codex-build/gps-ubx --output-on-failure
```

`TINYGPSPLUS_SOURCE_DIR` can point to the installed PlatformIO dependency.
Otherwise CMake downloads the fixed upstream commit and checks the archive hash.
The tests prove UART/configuration behavior, not RF reception or a physical fix.
