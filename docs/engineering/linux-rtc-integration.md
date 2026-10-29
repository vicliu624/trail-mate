# Linux hardware RTC integration

Linux RTCs are discovered through their standard sysfs class, without assuming
`rtc0`, a chip model or an I2C bus number. The monitor chooses an explicit
`TRAIL_MATE_RTC_NAME` match, the unique kernel `hctosys` clock, or a single RTC.
Ambiguous selection fails. `since_epoch` must be readable and valid; a registered
device with an oscillator/time error is not reported ready. Reads run on a
monitor worker. `platform::ui::device::rtc_ready()` reads its cached, freshness-
checked snapshot instead of returning a constant. Operating-system clock and
timezone APIs remain independent of physical RTC presence.

The shared P4 interface and business services continue using the operating-system
clock. A fitted RTC seeds this clock through the normal kernel `hctosys` path,
so Agenda and message timestamps work before network time becomes available.
There is no second application clock or Linux chip-specific logic in shared UI.
The adapter does not write time, clear voltage-loss flags, configure alarms or
grant application clock-setting capabilities.

## CM0 RX8130 startup binding

The user confirmed that RX8130 and AHT20 are fitted. A directed read on the CM0
I2C controller found RX8130 at 0x32 with valid UTC time. The current boot
`i2c-rtc` overlay does not expose an `rx8130` parameter, despite the config line
requesting it. Both vendor and mainline drivers are installed. Explicit loading
of the existing mainline `rtc_ds1307` driver and registering `rx8130` created the
standard RTC class; kernel logs confirmed `hctosys`, and hardware/system UTC agreed.
No kernel binary or display driver was replaced.

The CM0 deb includes an opt-in `trailmate-cm0-rtc.service`. Its board-owned command
selects the controller by device-tree identity, not the dynamic bus number. The
generic registration helper loads an existing kernel driver, validates the client
identity and requires a readable RTC class. An existing healthy client is reused;
its time is not rewritten by the helper. Kernel registration can set system UTC
from the RTC through `hctosys`. This is intentional clock restoration, not an
application-set-time test. A clock with invalid contents requires administrator
initialization; the helper never substitutes a fabricated date.

On a CM0 with this RTC fitted, activate the packaged unit:

```sh
sudo systemctl enable --now trailmate-cm0-rtc.service
systemctl status trailmate-cm0-rtc.service
```

The service runs before `sysinit.target` and `time-set.target`, after local
filesystems and module loading. It is not enabled automatically for every CM0,
because optional hardware differs between devices. Package upgrades reload unit
metadata without re-registering a healthy clock. Removal disables the unit but
does not unbind a clock already owned by the kernel during the current boot.
Other Linux devices can provide their own controller/client configuration using
the same helper; the C++ application does not contain CM0 wiring constants.

## Verification scope

Native tests cover missing/invalid clocks, explicit selection, multiple clocks,
kernel-source preference, removal and cache lifecycle. Registration fixtures
cover adapter renumbering, duplicate adapter matches, wrong client identity,
idempotency and invalid clock contents. Real-device boot persistence and final
packaged execution must be recorded in the CM0 hardware ledger. Reboot testing
does not prove coin-cell retention after removing all main power. Alarm/wakeup
behavior is not established by this timekeeping integration.

AHT20 is fitted, but the user explicitly excluded temperature/humidity collection
on 21 October 2026. Earlier directed reads at 0x38 failed on the two inspected
controllers; no further AHT20 diagnosis or integration is required for this task.
This scope decision does not affect the completed RX8130 timekeeping integration.
