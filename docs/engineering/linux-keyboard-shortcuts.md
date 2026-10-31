# Linux keyboard shortcuts

CM0 and Cardputer Zero share the existing Pager page-navigation adapter rather
than carrying a separate shortcut map. The CM0 SDL key observer and Cardputer
Wayland/framebuffer LVGL input bridge both call it. Page-specific semantic
shortcuts remain owned by the existing shared pages.

| Context | Keys | Action |
| --- | --- | --- |
| Launcher | WASD / arrows, Enter | Select and open an application |
| Launcher | H | Show launcher help |
| Application | Tab, Enter, Esc | Select, activate/edit, back/close |
| Application outside text editors | H | Open/close contextual help and restore focus |
| Page header | Click/tap H Help (H ? on narrow screens) | Open the same contextual help without stealing the current control's focus |
| Team, Sky Plot, USB, Walkie, SSTV, Extensions | Up/Down or W/S | Previous/next control outside editors |
| Chat, Settings, Tracker and open page menus | Up/Down | Previous/next control or menu item |
| Chat list | M or F; / or S | Filters; search conversations |
| Chat list with a selection | Enter/Space; C; I; P; D | Actions; open; information; Reticulum ping; delete confirmation |
| Protocol Probe | Up/Down; Enter; S | Select observed profile; apply confirmation; start/stop |
| Protocol Probe with a real IQ backend | I; Up/Down | Capture a finite IQ snapshot; scroll its diagnostics |
| Cardputer Zero Walkie | T | Toggle talk/listen; opening Help or leaving the page stops transmission |
| CM0 Walkie outside Help/overlays | Space | Hold to talk; release to listen |
| Calculator | Enter or =; Left/Right; Backspace | Calculate; move expression cursor; delete character |
| Calculator | s/c/t; Shift+S/C/T; l/G; I | Trigonometry; inverse trigonometry; logarithms; reciprocal |
| Calculator | F/M; P/A; Q/R | Second functions/angle mode; pi/previous answer; square/root |
| Help | Up/Down or W/S; H or Esc | Scroll; close and restore previous focus |
| Extensions | M / m | Open or close the page menu; Esc also closes it |
| Extensions | T / t | Open Make TMAP directly, including while the menu is open |
| Map | WASD / arrows | Pan |
| Map | Q/E, C | Zoom out/in, center on current position |
| Map | F, M, L | POI search, menu, cycle base layer |
| Map | H or F1 | Map's own help |
| Music player | Space, A/D, W/S | Play/pause, previous/next, volume |
| TMAP creator | Tab or W/S, Enter | Select and activate wizard controls |
| TMAP creator | Esc | Previous step, or close source/progress page |
| CM0 desktop | Alt+F4 | Quit the application |

W/S is not remapped globally: Music, Calculator and Protocol Probe retain their
own semantics. A focused textarea or IME keeps literal H/W/S and ordinary text
editing. H help never intercepts those letters while editing text. Native page
help remains authoritative for Map, Contacts, Network and chat history.

IQ Snapshot is exposed only when a real, non-simulated backend reports support
and its SPI/GPIO endpoints exist. Its worker waits for the previous Probe
session's physical release, restores the radio before publishing results and
joins before page exit. Unknown sample rate stays unqualified. IQ values are
diagnostics, not protocol evidence. The 320x170 view uses a bounded scrolling
region so long errors cannot cover the footer; `S` returns to probing.

Extensions exposes Make TMAP directly in its bottom action bar. Help can be
opened from the creator outside text editors, without losing the entered plan.

For host review, the CM0 executable accepts `--open-app ID --page-help` together
with `--quit-after-ms` and `--screenshot`. It resolves the app from the production
catalog, opens its real page, enables the review keyboard and verifies that the
keyboard router opens Help. `--page-help-touch` instead disables the keyboard
device and clicks the actual header Help entry. Both modes check modal/focus
ownership; Map may reuse its existing group. Normal launches retain the device
profile's keyboard auto-detection. These review tests disable simulated radio,
GNSS and world data. The Walkie Help review also checks that Space does not
enter the PTT route while Help owns the input group.
