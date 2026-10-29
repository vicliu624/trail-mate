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
| Team, Sky Plot, USB, Walkie, SSTV, Extensions | W/S | Previous/next control |
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

Extensions exposes Make TMAP directly in its bottom action bar. Help can be
opened from the creator outside text editors, without losing the entered plan.
