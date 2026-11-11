Native Linux ESP-NOW Team pairing

The Linux app uses LinuxEspNowPairing and the shared TeamPairingCoordinator.
The privileged Python helper uses the existing brcmfmac vendor DCMD actframe
and cfg80211 management RX paths. ESP32 firmware and the Team wire protocol
remain unchanged. No monitor mode, replacement Wi-Fi firmware, or kernel
patch is required on the two qualified CYW43439 devices.

Install the helper from the repository root:
    sudo sh scripts/install_linux_espnow_helper.sh

Requirements: systemd, NetworkManager, Python 3, iw, ip, rfkill, the Linux
nl80211 UAPI header at /usr/include/linux/nl80211.h, and a local pi account.
The application remains unprivileged. The control socket is root:pi 0660;
the helper also checks SO_PEERCRED. Only bounded ESP-NOW application frames
are admitted through IPC; the client cannot submit arbitrary firmware commands.

During pairing, the helper records the active wlan0 connection UUID in a
root-only durable recovery record, verifies the independent timer is enabled
and active, relinquishes NetworkManager control, and selects channel 1 with
firmware readback. RX registration MUST follow this interface transition.
The ordinary iw channel setter is unsupported on the qualified devices.
The original firmware MPC/PM policy is also saved before disabling radio
sleep for the connectionless receive window, and restored with readback.
Leaving ordinary station power management active made silent-receiver
enrollment timing-dependent during the hardware UI qualification.

The helper's active exchange lasts at most 125 seconds. The independent
recovery lease expires at 150 seconds and its timer checks every 15 seconds.
The helper unit also has a 165-second systemd runtime limit. Client EOF or
normal completion expires the lease immediately and requests recovery in a
separate unit. SIGKILL cannot bypass the independent timer. An interrupted
armed session is recovered after boot using boot-ID comparison rather than
the previous boot's monotonic deadline. Failed activation retains the record
and retries; it is removed only after the saved connection and IPv4 route
have been verified. Do not disable the recovery timer while a lease is armed.

The app persists Team ID, key ID, PSK, team name and membership in a private
atomic team.snapshot under its settings root and restores TeamController keys
at startup. Linux Team message crypto matches ESP: SHA256(PSK || label) and
ChaCha20-Poly1305 with a 12-byte nonce and appended 16-byte tag. OpenSSL is
required for authenticated message encryption/decryption; absent support
fails closed.

Validation on 2026-11-10:
- CM0: ARMv7 Raspbian, kernel6.18.50-trailmate-irq-v7.
- M5Stack Cardputer Zero: AArch64 Debian, kernel6.18.39+rpt-rpi-v8.
- Both: CYW43439, original brcmfmac firmware7.95.75/FWID01-bb9b5b66.
- Channel8 and disconnected channel1 bidirectional broadcast/unicast RX.
- Shared coordinator real Beacon/Join/Key exchanges with both role choices.
- Independent recovery after a worker timeout and after killing the actual
  root helper with SIGKILL; normal Wi-Fi returned without SSH intervention.
- Hardware-only UI acceptance uses the product ShellSession, Team page
  Create/Join actions, LinuxAppServices and real radio helpers on the physical
  CPUs. Its Canvas host does not take over the device's normal framebuffer.
- An unmodified nearby ESP32 running TrailMate was manually placed in its
  leader pairing window. Both Linux devices joined leader 8F519A94 through
  the real Team UI/runtime. Both derived the same Team ID/key ID/PSK material
  digest, and the operator confirmed two new members on the ESP32.
- Fresh Linux application processes restored the ESP32-issued Team material
  from private snapshots without another pairing exchange.

The explicit hardware target is trailmate_linux_team_espnow_hardware_test.
It is intentionally NOT registered as an automatic CTest test. Usage:
    trailmate_linux_team_espnow_hardware_test leader|member NEW_TEST_SETTINGS_ROOT
    trailmate_linux_team_espnow_hardware_test restore EXISTING_TEST_SETTINGS_ROOT
Use isolated roots: tests must not overwrite the user's identity or Team.
The lightweight team_espnow_pairing_probe.cpp exercises the same coordinator
without the UI. Neither tool substitutes synthetic peers for radio delivery.

The ESP32 was operated locally by the user; no ESP32 network, serial connection,
firmware upload, or source change was required. The PlutoSDR IIO endpoint was
inspected read-only; it was not used as the proof of packet delivery.

The initial radio/UI acceptance used isolated settings roots and separate test
executables. The normal Linux application executables were subsequently built
and installed on both devices with rollback copies; existing identities and
settings were retained. Both installed Team pages opened with Create/Join
enabled. This was a local development deployment, not a published Debian release.
Ordinary Wi-Fi is paused while pairing on channel 1; concurrent station Wi-Fi
is not a requirement of this implementation.
See docs/engineering/linux-espnow-team.md for the acceptance record and limits.
