# Security Policy

## Reporting a Vulnerability

Please report suspected security vulnerabilities privately before opening a
public issue or pull request that discloses the details.

Contact the maintainer at **[vicliu@outlook.com](mailto:vicliu@outlook.com)**.
Use a subject such as `Trail Mate security report`.

Please include, where possible:

- Affected hardware target and firmware version or commit
- Protocol or interface involved
- Reproduction conditions and a minimal reproducer
- Expected and observed behavior, and the potential impact
- Logs or packet samples with sensitive information removed
- Whether the issue is remotely reachable and what access is required

Do not include private keys, passwords, personal location history, or other
people's data. If a reproducer requires sensitive material, describe that
requirement first so a suitable sharing method can be agreed upon.

## Scope

Security-sensitive areas in this repository include:

- Meshtastic, MeshCore, and Reticulum/LXMF packet handling
- LoRa, Wi-Fi, Bluetooth, serial, USB, and HostLink input
- Identity, key handling, and message and team authentication
- File parsing, local persistence, and removable storage
- Firmware updates and configuration paths
- Resource exhaustion and memory safety on constrained devices

Reports involving an upstream dependency are welcome when they affect Trail
Mate. Please identify the dependency and any existing upstream advisory.

## Supported Versions

Security maintenance focuses on the current development branch and the next
maintained release. Older releases and experimental hardware ports do not
have a separate guaranteed security backport schedule. Include your exact
version and target so the affected scope can be assessed.

## Coordinated Disclosure

The maintainer will review reports and coordinate reproduction, fixes, and
public disclosure with the reporter where possible. Please allow time for
that review before publishing exploit details. Response and fix timing depend
on maintainer availability, impact, and access to affected hardware.
