# Linux compositor output power

`OutputPowerRuntime` isolates Wayland compositor power commands from UI code.
It is a Linux platform adapter, not a panel driver or brightness implementation.
The LVGL Linux composition connects it to the shared screen-timeout lifecycle
when a profile explicitly selects `screen_power=wayland`. The default `none`
does not create a power worker or advertise app-managed automatic sleep.

The owner runs `wlopm` with direct argument vectors on a worker. Queries and
commands have bounded execution through the existing process runner. UI callers
read cached snapshots or submit a request; neither operation executes a process.
The default selection requires exactly one advertised compositor output. A
composition may provide an explicit output name for a multiple-display setup.
There is no CM0 connector name or board identifier in the adapter.

Every accepted request captures the selected output name. The worker discovers
the outputs again before executing it. A missing output, changed selection or
ambiguous automatic selection rejects the operation; a request is never carried
forward to a newly discovered, differently named display. Compositor output
names are the identity available through this interface: replacement hardware
that reuses the same name cannot be distinguished by this adapter.

The snapshot reports observed power, availability, selection requirements and
failure. Command success alone never establishes applied state. Invalid query
output, command failure and mismatching readback do not produce an optimistic
successful sleep/wake result. Failures remain visible until another valid request
completes. A later query may restore availability without clearing the failure.

Before turning an initially-on output off, the worker records restoration
ownership, including when the command might partially apply and return failure.
A failed sleep attempts immediate restoration. Shutdown joins the worker and
attempts to enable each output this owner still holds off. Initially-off outputs
are not enabled by destruction. These are bounded best-effort compositor calls;
they do not prove physical panel recovery or survive process termination by
SIGKILL, power loss, or a compositor crash.

`linux_output_power` uses an injected compositor with real worker scheduling.
It covers sleep/wake, thread ownership, destruction restoration, an initially-off
display, multiple outputs, explicit selection, a partially applied error,
successful commands with unchanged readback, malformed replies and a queued
request whose original display is replaced before execution. These tests do not
send commands to the developer's desktop.

Validation on 22 October passed the complete native suite (155/155), a separate
build of the actual adapter, process runner and fixture under ASan/UBSan, and
execution of the cross-compiled fixture on CM0. The device fixture uses the fake
compositor too; it proves ARM worker behavior without changing display power.
The Cardputer Zero build and its existing 19/19 regressions also passed after
adding the adapter to the shared Linux library. No Cardputer physical display
capability is inferred from those tests, and its composition does not enable
this adapter.
Those adapter-only results preceded the screen-service integration described
below; they are not end-to-end application evidence.

On the CM0 stock image, a separate bounded real-session test dynamically found
the sole active output, armed an independent delayed recovery process, requested
power off for approximately half a second, and restored power. `wlopm` reported
`on`, `off`, then `on`, and the recovery process exited successfully. Only numeric
and output-state evidence was retained in the device validation cache as
`stock-output-power-cycle.json`. Physical screen recovery still requires the
user's observation; command readback alone is insufficient. No driver, kernel,
startup image, or automatic idle configuration was changed.

## Application lifecycle

`screen_runtime.cpp` serializes shared state-machine access. Background callers
may update activity or nested sleep inhibition, but never invoke UI hooks.
`service_screen_runtime` runs on the UI thread, advances timeouts only with a
healthy selected power port, submits power changes, and applies screen saver
show/hide/present hooks outside the state mutex. Reentrant hooks can query the
screen ports. Missing or failed output control returns the logical UI to Awake;
a pending wake is retried when the output becomes available again.

The service enqueues a wake even if current readback still says On when a
previous Off command is in flight. Otherwise a fast input could return the UI to
Awake while the delayed command subsequently turns its display off. The hardware
worker retains restoration ownership independently of the logical UI state.

The SDL host offers a generic input observer before key, pointer and editor
delivery. `ScreenInputRouter` consumes initial wake input and its release,
quarantines held-key repeats, blocks editor commits during preview, and accepts
SPACE/ENTER or a subsequent upward swipe to resume. It uses transformed logical
coordinates and a gesture threshold derived from display height. The saver is
an overlay: resume does not reopen the main menu or replace the current page.
Focus loss clears input quarantine along with the host's held input.

The LVGL entry point initializes the shared saver hooks after startup, services
the screen runtime in the normal UI loop and releases it before deleting UI
objects. Existing call, busy-overlay, SSTV and walkie sleep inhibitors keep their
shared port API and use the same nested state-machine counter. Compact Linux
compositions retain their existing screen policy unless explicitly configured.

Profile keys are `screen_power=none|wayland` and optional `screen_power_output`.
The latter must name an advertised output and is invalid with backend `none`.
CM0 still selects `none`: the user could not observe the physical result of the
stock-driver power-cycle probe. No automatic physical power cycling is enabled
by the package until that capability is accepted. Brightness remains a separate
backlight capability.

`linux_screen_runtime` uses a deterministic monotonic clock, real SDL dummy
input processing and the production power worker with an injected compositor.
It checks timeout persistence and activity deadlines, saver hooks on the UI
thread, editor/wake isolation, keyboard repeats, swipe resume, concurrent nested
inhibition, fast wake during an in-flight Off, preview timeout, failed power
readback, output loss/reappearance and shutdown restoration. Native builds
instrument the touched screen service, power worker, process runner and SDL
host under ASan/UBSan. This test does not change physical display power.
