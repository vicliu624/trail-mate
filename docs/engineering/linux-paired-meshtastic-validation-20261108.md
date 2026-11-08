# Cardputer Zero and T-Display-CM0 Meshtastic validation — 2026-11-08

This is the earlier paired-Meshtastic checkpoint. The later
[three-protocol, official-firmware and storage qualification](linux-paired-mesh-validation-20261108.md)
supersedes its package versions and outstanding items.

## Scope and devices

The user authorized simultaneous physical testing of both Linux devices, placed
in the same room approximately 1–3 m apart. Both use Meshtastic CN, LongFast,
slot 35, 478.875 MHz, 250 kHz bandwidth, SF11, CR 4/5, sync word 0x2B,
preamble 16 and the default primary channel key (hash 0x08). MQTT was not used
for these exchanges. Node identities and private keys were retained; database
backups remain on the devices with mode 0600 and were not downloaded.

| Device | Radio | Node identity | Installed package |
| --- | --- | --- | --- |
| Cardputer Zero | Cap LoRa-1262 / SX1262 | `!4BE5DDEF` / CZ | `trailmate-cardputer-zero 1.0.1+cardputer.20261108.11` (arm64) |
| T-Display-CM0 | LR2021 | `!F48DA2AE` / TM | `trailmate-cm0 1.0.1+mesh.20261108.3` (armhf) |

The checkout was refreshed against `origin/main` at
`8bc50ffc22aab1ea2c88472c9dcc3050e61590d4` before the paired work. Existing
Cardputer adaptation changes were preserved. The packages include the relevant
working changes and the driver fixes described below. The CM0 build used the
existing native Raspbian armhf chroot to retain its C++ ABI and 1seg support;
the installed 1seg backend was not replaced.

The user confirmed that the Cap is the standard version and explicitly asked
to continue using 478.875 MHz, which they regularly use successfully at home.
Its published 868–923 MHz specification was not treated as a blocker.
The successful CN exchanges below are actual device evidence; they do not
establish RF performance at other distances or in other environments.

## Driver faults and repairs

### CM0: receive restart during continuous RX

The original CM0 package (`1.0.1+git4732374d`) reported a hardware RX count
without delivering the received NodeInfo to the protocol adapter. Diagnostic
builds captured the first failure: after RX_DONE (`0x40170`), RadioLib's receive
restart returned `-706` at the RX-path configuration command. It was invoked
while continuous RX was still active. Later hardware-error IRQs caused radio
reinitialization approximately every 0.4 seconds.

`Lr2021Radio::pollReceive` now enters standby before restarting RX on valid
packets and all damaged/timeout/read recovery paths. A successfully read packet
is delivered once even when restarting RX fails; the existing error handling
marks the driver offline for recovery. Optional diagnostics record the operation,
return code, IRQ flags and transport health without dumping packet data.

The regression extracts the production receive method and uses a fake that
rejects a restart while already receiving. It checks damaged-packet rejection,
fatal-error handling, standby ordering and one-time valid delivery when standby
or RX restart fails. The test passed, and subsequent physical packets were
decoded without the reinitialization loop.

### Zero: TX IRQ mask retained in receive mode

`Sx126xRadio::transmit` selected TX_DONE/TIMEOUT reporting before transmission,
then returned to RX with SetRx alone. The chip retained the TX IRQ mask, so
software could not observe RX_DONE after a send, including the automatic startup
NodeInfo. This explained the persistent zero receive count despite successful
transmission.

All TX exit paths now restore RX_DONE, CRC, header-error and timeout reporting,
clear IRQs, and restart receive. A successful physical TX increments its counter,
but a failed receive restart is reported to the caller. The regression compiles
the production TX method against a command-state fake and checks both LoRa and
FSK after success, hardware timeout, host timeout, failed start and failed write,
as well as failed RX restart. All 12 cases passed in 8.01 seconds.

## Physical results

Times below are UTC on 2026-11-08; add eight hours for CST.

| Evidence | Result |
| --- | --- |
| Zero NodeInfo `15F94CDA`, transmitted 10:03:00.331 | CM0 decoded `!4BE5DDEF / CZ / Cardputer Zero` at 10:03:01.374 and 10:03:03.072; the latter had RSSI −55.5 dBm. |
| Zero `.11` startup after a TX at 10:10:52.311 | Received a relayed copy at 10:10:55.369, confirming receive reporting remained active after TX. |
| CM0 NodeInfo after Zero `.11` installation | Zero decoded `!F48DA2AE / TM / T-Display-CM0` at 10:12:42.411. |
| CM0 private text `cm0 retry RF1813`, message `26B4F95C` | Zero stored the incoming plaintext and transmitted a Routing ACK at 10:16:16.264. CM0 persisted status `Delivered` (enum value 4). |
| Zero private reply `zero to cm0 rf1813`, message `044E2BCC` | CM0 stored the incoming plaintext with LoRa origin. Zero persisted status `Delivered` (enum value 4). |
| Zero keyboard workflow | Opened the received conversation, pressed S to compose, typed the reply, pressed Tab to focus Send, and pressed Enter. A live framebuffer capture shows both messages and Delivered. |

The first CM0 private attempt did not decrypt. Zero requested fresh NodeInfo;
public-key fingerprints were then checked on both devices and matched each
other's local public key. The subsequent private text and reply succeeded.
That initial unsatisfied send remains in history; it was not relabeled as
delivered or deleted. No private-key material was printed or transferred.

The CM0 Nearby view visibly displayed Cardputer Zero. Both incoming messages
and both delivery statuses were checked in the devices' own SQLite databases,
in addition to radio logs and live UI captures. The test environment contains
another relaying node, so these results demonstrate physical LoRa interoperability
and receipt, without claiming every packet took a single direct hop.

## Startup configuration and artifacts

Both canonical CM0 profiles had `runtime_mode=mesh` followed by an appended
`runtime_mode=local`. The duplicate local overrides were removed after device
backups. The existing KMS/1seg system service was left inactive as found;
the current desktop session uses the canonical capture profile through the
paired-test user service. Verbose SPI tracing was disabled when this normal
profile was restarted. Zero was installed with dpkg and reopened through its
existing APPLaunch entry.

After the normal-profile restart, CM0 retained its identity and conversation
history. Another private message (`524A0CF9`, actual text `tart RF1813`) was
stored by Zero and acknowledged at 10:20:59.283; CM0 again persisted Delivered.
The automated UI input began before the cold compose screen was fully ready,
so this test's text was shorter than the intended draft; the record above uses
the actual transmitted and received text. Final read-only audits at 10:22:33–36
reported CM0 PID 19332, Zero PID 264170, tracing disabled on both, and radio
counters CM0 RX 8 / TX 4 since restart and Zero RX 23 / TX 8. CRC, header,
timeout, invalid-length and read-error counters were all zero in these sessions.

| Package | SHA-256 |
| --- | --- |
| `trailmate-cm0_1.0.1+mesh.20261108.3_armhf.deb` | `530253e271aab5f3dfbe3db4881b9e9d0f0c73297433145afd83403c11761cd2` |
| `trailmate-cardputer-zero_1.0.1+cardputer.20261108.11_arm64.deb` | `3678b6395f2ddd2a01a33bc81952c7fdc3ccd28809f4c923db04d426a0fc87d5` |

All started builds completed with exit code 0. GitNexus upstream analysis
reported MEDIUM impact for the LR2021 shared error checker (12 direct methods)
and LOW for the receive/TX methods and test symbols. The graph did not identify
their virtual callers; physical verification was therefore essential. No commit
was created. The ESP stack hygiene check and focused whitespace checks passed.

The previously accepted GPS criterion remains satellite reception and display;
a position fix is not required for this deployment. Earlier map creation,
framebuffer and keyboard evidence is retained in
[the Cardputer deployment record](cardputer-zero-device-validation-20261108.md).
