# Linux SDL text input

The SDL host separates navigation/held keys from committed text. While the
application reports a focused editable text area, SDL text input is enabled and
`SDL_TEXTINPUT` delivers the keyboard layout's UTF-8 text. Printable raw keycodes
are suppressed in that mode, preventing duplicate lowercase characters beside
the committed uppercase, punctuation or non-ASCII text. Outside text editing,
raw keys continue through the existing navigation and application key observer.
The observer retains held-key ownership for walkie PTT.

The SDL host knows no board or page identifiers. Application composition reuses
the existing `LvglTextInput` adapter used by Cardputer Zero. That adapter validates
UTF-8, tracks focused text areas, honors password fields and handles field
deletion. Window focus loss disables text input and ignores subsequent queued
text commits. SDL composition/preedit events feed the adapter's temporary overlay;
they do not modify persistent editor contents. Character-based cursor/selection
positions are validated and converted to UTF-8 byte positions. Commit and focus
loss clear the overlay. Password composition uses the adapter's masked display.
Extended composition is enabled on SDL 2.0.22 and newer, and its allocated buffer
is freed even when editing is disabled. See the upstream
[editing event](https://wiki.libsdl.org/SDL2/SDL_TextEditingEvent) and
[extended editing event](https://wiki.libsdl.org/SDL2/SDL_TextEditingExtEvent)
contracts.

The window regression covers text commits with uppercase, punctuation and a
multibyte character, suppression of the duplicate raw key, ignored commits while
editing is disabled or window focus is lost, PTT key cancellation, four display
rotations, and Alt+F4. It also covers ordinary/extended composition, selection
forwarding, separation from committed text, and cleanup on commit/focus loss.
It locates pointer/keypad devices by LVGL type, without
depending on input-device registration order. The existing LVGL text adapter
tests separately cover text-area mutation and UTF-8 behavior. Physical key-matrix
and touchscreen behavior still require device interaction; synthetic SDL events
do not establish physical input validation.

## Package verification (2026-10-21)

The SDL window regression and existing LVGL text-input test passed, as did the
platform UI boundary check. The armhf package was built with the target Raspbian
C++ ABI overlay. Its SHA-256 is
`d2bcf067ae399bb937b7a1e96550a97e68c73c564de950f1e35a51ec74d3e311`.
Installation and `dpkg -V` succeeded on the CM0. The installed P4 interface reached
UI-ready and rendered at 1232 by 568, with the original logo. A focused fullscreen
Alt+F4 check exited with status zero in 0.883 seconds.

The composition follow-up passed both the SDL window regression and the reused
LVGL text adapter test, plus platform boundaries. Its deb SHA-256 is
`9c0ae421f0053256b2e895282ed7535c935eeb877f730db6d4b26612f3d7de55`.
Installation and package integrity verification passed on CM0, with UI-ready and
the 1232 by 568 P4 frame confirmed. Focused fullscreen Alt+F4 exited with status
zero in 0.750 seconds. The actual desktop IME/candidate interaction and physical
keyboard/touch remain outside this automated verification.

A subsequent installed form test verified exact mixed-case/punctuation input,
save and fresh-process reload through the P4 Contacts UI. It exposed and fixed a
separate configuration ownership issue; see
[Linux configuration persistence](linux-config-persistence.md).
