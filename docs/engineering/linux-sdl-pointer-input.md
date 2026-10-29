# Linux SDL pointer event delivery

The SDL host previously retained only the latest pointer position and pressed
state. A press and release arriving before the next LVGL sample therefore
looked like a release alone, losing quick clicks and taps. This was reproduced
both by a regression test and by a focused fullscreen application on the CM0:
an injected Wayland click on Chat left the application on the main menu.

The host now queues pointer samples and drains them through LVGL's
`continue_reading` mechanism. Press and release edges retain their positions;
only consecutive motion samples are coalesced. The queue is bounded to 64
samples. Overflow cancels the overloaded gesture with a release rather than
leaving the pointer held. Focus cancellation and rotation discard old samples.
This is a desktop input concern; shared pages and board profiles are unchanged.

The window regression first failed when a press/release pair produced no click.
After the fix, same-pump mouse clicks and finger taps pass under all four
rotations, alongside existing text, focus, PTT and window-exit checks. The platform
boundary check also passes. The updated armhf deb SHA-256 is
`03cf1ecd5742bcb28618b5a89cd4422f9ba422598f99a2dcf15e91a2de38eb59`.
It was installed and integrity-checked on CM0, preserving the original logo and
1232 by 568 P4 rendering. Repeating the same Wayland click now opens Messages;
the subsequent Menu click opens its Direct/Broadcast/Info popup. This establishes
the installed application/compositor/pointer path, not physical touchscreen
electrical or coordinate validation.
The final focused fullscreen Alt+F4 check exited with status zero in 0.749
seconds, and readback confirmed no application process remained after the
bounded navigation probes. Subsequent installed-page text entry, save and reload
verification is recorded in [Linux configuration persistence](linux-config-persistence.md);
the navigation probes above alone did not establish that behavior.
