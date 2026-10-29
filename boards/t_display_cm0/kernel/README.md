# RM69A10 backlight interface

**Experimental: do not deploy this patch as a working brightness driver.**
The 2026-10-21 physical test still produced a black/abnormal display and VC4
DSI transfer timeouts after video enable. Callback tests and submitted-value
readback did not establish physical display correctness. The test deployment
was rolled back to the original DKMS module and exact boot images. Runtime
DSI timing/transport must be resolved before another production deployment.

The supplied CM0 panel driver writes a fixed DCS `0x51` value and does not
register a Linux backlight class device. `0001-rm69a10-backlight.patch` adds that
interface to the source snapshot under the separate T-Display-CM0 checkout:

```text
deploy/desktop_alt/desktop_raspberry/trixie_support/panel_rm69a10/
```

The patch registers an 8-bit raw backlight, preserves the vendor's default
brightness of 254, and starts panel initialization dark. DRM manages blanking
through the panel's backlight association. Brightness transfers use the panel's
existing one-byte DCS `0x51` format and are serialized against lifecycle state.
Unprepared/disabled/removed panels do not receive brightness transfers.
Preparation and display enable are separate states: preparation initializes a
dark panel, enable sends sleep-out/display-on, and DRM then enables the
associated backlight. Disable sends display-off/sleep-in but preserves the
prepared state, so a following enable works without another preparation.
Short DSI brightness writes are errors and do not update the reported value.
The Raspberry Pi VC4 host reports a successful transfer as zero, rather than
the byte count. A zero completion is accepted only for a host with compatible
`brcm,bcm2835-dsi0` or `brcm,bcm2835-dsi1`; other zero/short writes remain errors.
This follows the Raspberry Pi
[VC4 transfer implementation](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/gpu/drm/vc4/vc4_dsi.c).
`actual_brightness` reports the last successfully submitted value while the
panel is prepared, or zero when inactive; it is not an optical measurement.

## Build

Set `CM0_VENDOR_ROOT` to the supplied device checkout and `TRAIL_MATE_ROOT` to
this checkout. Work on a reviewable copy of the vendor driver directory.

```sh
source_dir="$CM0_VENDOR_ROOT/deploy/desktop_alt/desktop_raspberry/trixie_support/panel_rm69a10"
driver=$(mktemp -d)
cp -a "$source_dir/." "$driver/"
patch_file="$TRAIL_MATE_ROOT/boards/t_display_cm0/kernel/0001-rm69a10-backlight.patch"
patch --dry-run -d "$driver" -p1 < "$patch_file"
patch -d "$driver" -p1 < "$patch_file"
make -C "/lib/modules/$(uname -r)/build" M="$driver" -j2 modules
```

Use matching headers and the vendor `compat.h`/Makefile. The older source under
`src/cmo_drivers/panel_rm69a10_amoled` differs from this snapshot; check patch
applicability rather than applying it blindly to that version.

The patched module was compiled on the CM0 against its installed headers:

```text
6.18.50+rpt-rpi-v7 SMP mod_unload modversions ARMv7 p2v8
```

The resulting development artifact is `build/cm0-kernel/panel_rm69a10.ko` in the
Trail Mate workspace. Its SHA-256 at verification was:

```text
a47a2fe6ec7a193fb88d923c7583844e4096a1dccd7551aeb2791638117f3a9d
```

This is the corrected lifecycle build. The earlier build's disable callback
cleared the prepared state while its enable callback did not restore it; that
could prevent brightness restoration after blanking. The callback regression
below applies the actual patch to a temporary vendor-source copy, compiles the
patched C callback bodies with a fake DSI host, and runs ASan/UBSan checks for
prepare/enable/disable/unprepare, re-enable without prepare, initialization and
wake errors, brightness-write errors and short transfers. It passed before
the corrected module was rebuilt on CM0.

```sh
python3 "$TRAIL_MATE_ROOT/boards/t_display_cm0/kernel/tests/test_panel_backlight.py" \
  "$source_dir/panel_rm69a10.c" "$patch_file"
```

Pass an unpatched source file to this test. The supplied source is never
modified; the patch is applied only inside a temporary directory.

The original lifecycle build was installed with permission on 2026-10-21. It
booted but rejected VC4's zero completion as `-EIO`; brightness readback stayed
zero. The exact original DKMS driver and four boot images were restored and
verified after rollback/reboot. The regression now reproduces the VC4 return
convention and verifies its narrow compatibility handling, alongside negative
errors and positive short writes. The hash above identifies the subsequent
VC4-compatible build. The application Debian package ships the patch and
documentation as reference material and does not replace the kernel module.

## Deployment review and rollback

The current device is running an active Labwc desktop on this panel. Use a
planned reboot for a module replacement rather than unloading its active DRM
panel. Installing the application deb alone does not activate this driver.

The original module resolves to `updates/dkms/panel_rm69a10.ko.xz` under the
running kernel's module directory. An exact backup was made before preparing
deployment; its SHA-256 is
`5657f317c1455e38d4666e6d6bd80dc8bc77b49cd34fa882690910720cf7df3b`.
The boot image uses `auto_initramfs=1`. Both the current kernel's initrd and
`/boot/firmware/initramfs7` contain the original panel module, so replacing only
the root-filesystem module cannot establish which module boots.

The concrete deployment sequence is:

1. Save verified copies of the current kernel's initrd and every firmware
   `initramfs*` image affected by the platform's update hooks. Record their
   paths, hashes and the existing module selection before any replacement.
2. Install the corrected module in `updates/trailmate` for the exact running
   kernel. Keep the original DKMS module intact. Add a panel-only depmod
   override and regenerate module dependencies; verify `modinfo -n` selects
   the corrected artifact and its vermagic matches the running kernel.
3. Regenerate that kernel's initrd with the platform tool and verify the
   selected module is present in the boot image. Preserve all backups until
   the reboot and restoration tests have passed.
4. With permission to interrupt the desktop, reboot and reconnect. Confirm the
   backlight class, bounded brightness changes, blank/unblank and the app's
   ordinary-session brightness path. Readback proves submitted driver state;
   optical brightness still requires observation of the physical display.
5. If validation fails, restore the saved initrd/firmware images, remove only
   the newly installed panel override and module, regenerate dependencies and
   reboot onto the untouched DKMS driver.

The VC4-compatible module was subsequently installed into `updates/trailmate`
for the exact running kernel. The initrd and firmware `initramfs7` were rebuilt;
their contents were extracted and the module hash and dependency selection
verified before reboot. Root-owned deployment logs, exact boot-image backups
and a rollback script are retained at `/root/trailmate-panel-vc4-deployment` on
the test device. The original DKMS module remains intact.

That second deployment reported `actual_brightness=254`, but the kernel also
logged `DSI transfer failed whilst in HS mode` and `failed to enable backlight:
-110`. The user confirmed a black/abnormal physical display. This contradicts
any claim that readback alone proves the panel works. The authorized rollback
restores the original module and all four saved boot images before reboot;
the experimental override is not intended to remain installed.

The second rollback was verified on the device: the original DKMS module hash
and all four saved boot-image hashes matched, the experimental override was
absent, and Labwc was running. The user confirmed the physical desktop had
returned to normal. No experimental backlight driver is currently accepted as
working; further runtime transport/timing diagnosis starts from the restored
vendor baseline.

A temporary diagnostic module was then used under the untouched vendor driver,
without changing boot images or driver binding. Three HS writes returned
`0, 0, -110`; three low-power writes returned `0, 0, 0`. Each diagnostic run
unloaded its module and cycled the output through the vendor driver to restore
the original panel initialization. This separates runtime host/mode behavior
from the replacement panel lifecycle, but does not establish optical brightness
or robust runtime operation. The diagnostic module is not installed at boot.

## Application integration

The shared Linux adapter discovers `/sys/class/backlight` and uses the native
`max_brightness`, `brightness`, and optional `actual_brightness` attributes.
Exactly one valid device may be selected automatically. With multiple devices,
set an absolute `backlight_device` in the application profile; the adapter does
not choose an arbitrary display. `backlight_root` overrides the discovery root.

Writes run on a worker. When ordinary sysfs permissions prevent a write, actual
class nodes can use the standard logind `SetBrightness` method for the active
session. The app does not invoke sudo, change device permissions, or emulate
hardware brightness with a display overlay. A missing interface stays
unavailable. The settings/quick-control value uses a normalized 0-255 scale;
the worker converts it to the driver's native range and reads back the result.

Relevant contracts: [kernel backlight API](https://docs.kernel.org/gpu/backlight.html)
and [logind session interface](https://www.freedesktop.org/software/systemd/man/257/org.freedesktop.login1.html).
