# GNSS configuration and status presentation

The baseline CM0 has no GNSS expansion. An enabled GPS preference alone does
not establish a configured source or received data. The native diagnostics now
append `NotConfigured` without changing existing diagnostic enum values. This
reason applies when no serial/file source, explicit state fixture, demo source
or automatic serial selection is configured. An explicit but unavailable source
or enabled automatic selection retains the transport-not-ready diagnosis.

The shared GPS status projection and P4 dashboard distinguish these states:

| Observation | Status | Dashboard explanation |
| --- | --- | --- |
| User disabled GPS | GPS OFF | GPS is turned off |
| No configured source | GPS N/A | GNSS source not configured |
| Power disabled | GPS POWER | GPS power is off |
| Configured source not ready, no traffic or stalled traffic | NO RX | Waiting for GNSS receiver |
| Receiver ready without a position fix | NO FIX | Waiting for better sky view and satellite count |
| Receiver ready with a valid fix | FIX | Active position stream and satellite count |

The projection does not publish old fixes, altitude, movement, satellites or
GPS-synchronization facts while the receiver is disabled, unpowered, unconfigured
or unready. The dashboard clears its old satellite positions in those states.
The explanation is derived from shared status facts, with no board name or
Linux device path in the UI. It does not infer physical absence merely from a
missing configuration. The OS/RTC system clock remains independent of GPS fix
and synchronization presentation.

`linux_gps_status_readiness` exercises the real native runtime and production
projection with isolated environment sources, including missing explicit and
automatic selectors, valid/no-fix fixtures, disable and power transitions.
`gps_status_projection` compiles the same production projection with injected
ports and deliberately stale valid coordinates, satellites and fix flags. It
verifies that unavailable states cannot display a green FIX or stale telemetry,
including a receiver whose stream has stopped. Both focused regressions passed.

The change does not install the absent GNSS expansion, alter its power wiring,
or introduce new probes on the CM0. Package, full-suite and installed P4 evidence
are recorded in the [integration ledger](t-display-cm0-hardware-integration.md)
after verification. The complete native suite passed 133/133 and Cardputer Zero
passed 14/14. Both ARM regressions passed on CM0. The exact standard deb passed
Lintian without diagnostics, installation/integrity checks, original-logo
comparison and 1232 x 568 P4 rendering. Visual inspection of the installed
baseline confirmed `GPS N/A` and `GNSS source not configured`. The installed
map regression still rendered TMAP data, and Alt+F4 from the active fullscreen
map exited with code 0 in 0.583 seconds. The preceding deb is retained for
rollback; hashes are recorded in the ledger.
