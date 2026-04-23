# Theme Slot Inventory

## 1. Purpose

This file is the runtime-facing inventory for the `theme` layer only.

It exists to answer three questions clearly:

- which `theme` IDs are part of the public contract
- which of those IDs are already live in the current runtime
- where firmware maintainers should look when they need to verify or extend a slot

This file does **not** define layout-replacement contracts.
For `page_archetype.*`, `region.*`, `binding.*`, and `action.*`, use:

- [presentation_contract_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/presentation_contract_inventory.md)

Public/builtin boundary reminder:

- public selection IDs: `official-*` and third-party pack IDs
- public builtin baseline: `builtin-ascii`
- builtin fallback-only IDs: `builtin-legacy-warm`, `builtin-default`, `builtin-directory-stacked`

When examples in this repo refer to public inheritance, they should point at
`official-*` or `builtin-ascii`, not at builtin official fallback IDs.

## 2. Source of Truth

Current live runtime source files:

- [theme_slots.h](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/include/ui/theme/theme_slots.h)
- [theme_slots.cpp](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/src/ui/theme/theme_slots.cpp)
- [theme_registry.cpp](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/src/ui/theme/theme_registry.cpp)

Rule:

- if Markdown and runtime disagree, prefer the concrete slot IDs exposed by `theme_slots.h`

## 3. Coverage Notes

Current live external theme runtime consumes:

- `tokens.ini` for `color.*`
- `components.ini` for the component batch listed below
- `assets.ini` for the semantic asset batch listed below

The wider spec still reserves `font.*`, `metric.*`, and `motion.*`, but those are not yet fully
runtime-backed as external theme slots in the same way as `color.*`.

The current live component batch now covers:

- shared chrome and shared widgets
- menu / boot / watch-face shared surfaces
- chat scaffold and bubble surfaces
- map / instrument / service archetype scaffold surfaces
- tracker / team / GNSS / SSTV page-private visual extraction surfaces
- GPS / map page-private control, popup, marker, and readout surfaces

## 4. Live Token Slots

### 4.1 Color Tokens

| Slot ID | Required | Current Sources | Notes |
| --- | --- | --- | --- |
| `color.bg.page` | yes | `modules/ui_shared/include/ui/ui_theme.h`, `modules/ui_shared/src/ui/watch_face.cpp`, `modules/ui_shared/src/ui/ui_boot.cpp` | Page root background |
| `color.bg.surface.primary` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Primary panel background |
| `color.bg.surface.secondary` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Secondary panel background |
| `color.bg.chrome.topbar` | yes | `modules/ui_shared/src/ui/widgets/top_bar.cpp` | Top bar background |
| `color.bg.overlay.scrim` | yes | `modules/ui_shared/src/ui/widgets/busy_overlay.cpp`, `modules/ui_shared/src/ui/screens/usb/usb_page_runtime.cpp` | Overlay scrim background |
| `color.border.default` | yes | `modules/ui_shared/include/ui/ui_theme.h`, `modules/ui_shared/src/ui/components/two_pane_styles.cpp` | Default border |
| `color.border.strong` | no | `modules/ui_shared/src/ui/menu/dashboard/dashboard_style.cpp` | Strong border/accent border |
| `color.separator.default` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Default separator |
| `color.text.primary` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Primary text |
| `color.text.muted` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Muted text |
| `color.text.inverse` | no | `modules/ui_shared/src/ui/widgets/ble_pairing_popup.cpp` | Inverse text |
| `color.accent.primary` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Primary accent |
| `color.accent.strong` | no | `modules/ui_shared/src/ui/menu/dashboard/dashboard_style.cpp` | Strong accent |
| `color.state.ok` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Success / OK state |
| `color.state.info` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Informational state |
| `color.state.warn` | yes | `modules/ui_shared/src/ui/menu/dashboard/dashboard_style.cpp`, `modules/ui_shared/src/ui/widgets/ble_pairing_popup.cpp` | Warning state |
| `color.state.error` | yes | `modules/ui_shared/include/ui/ui_theme.h` | Error state |
| `color.map.background` | yes | `modules/ui_shared/include/ui/ui_theme.h`, `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Map carrier/background |
| `color.map.route` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_route_overlay.cpp` | GPS route overlay color |
| `color.map.track` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_tracker_overlay.cpp` | GPS track overlay color |
| `color.map.marker.border` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | Marker/chip border used on map overlays |
| `color.map.signal_label.bg` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | Team signal label background on map |
| `color.map.node_marker` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | Remote-node / fallback map marker color |
| `color.map.self_marker` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Self marker color on map |
| `color.map.link` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Link line color between self and remote node |
| `color.map.readout.id` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Node ID readout color |
| `color.map.readout.lon` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Longitude readout color |
| `color.map.readout.lat` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Latitude readout color |
| `color.map.readout.distance` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Distance readout color |
| `color.map.readout.protocol` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Protocol readout color |
| `color.map.readout.rssi` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | RSSI readout color |
| `color.map.readout.snr` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | SNR readout color |
| `color.map.readout.seen` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Last-seen readout color |
| `color.map.readout.zoom` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Zoom readout color |
| `color.team.member.0` | no | `modules/ui_shared/include/ui/screens/team/team_state.h`, `modules/ui_shared/src/ui/screens/team/team_page_components.cpp` | Team member semantic color 0 |
| `color.team.member.1` | no | `modules/ui_shared/include/ui/screens/team/team_state.h`, `modules/ui_shared/src/ui/screens/team/team_page_components.cpp` | Team member semantic color 1 |
| `color.team.member.2` | no | `modules/ui_shared/include/ui/screens/team/team_state.h`, `modules/ui_shared/src/ui/screens/team/team_page_components.cpp` | Team member semantic color 2 |
| `color.team.member.3` | no | `modules/ui_shared/include/ui/screens/team/team_state.h`, `modules/ui_shared/src/ui/screens/team/team_page_components.cpp` | Team member semantic color 3 |
| `color.gnss.system.gps` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GPS constellation color |
| `color.gnss.system.gln` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GLONASS constellation color |
| `color.gnss.system.gal` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | Galileo constellation color |
| `color.gnss.system.bd` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | BeiDou constellation color |
| `color.gnss.snr.good` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | Good SNR color |
| `color.gnss.snr.fair` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | Fair SNR color |
| `color.gnss.snr.weak` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | Weak SNR color |
| `color.gnss.snr.not_used` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | Not-used satellite color |
| `color.gnss.snr.in_view` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | In-view-only satellite color |
| `color.sstv.meter.mid` | no | `modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp` | SSTV mid-meter color |

### 4.2 Reserved Theme Tokens Not Yet Fully Runtime-Backed

These remain part of the top-level specification surface, but are not yet consumed by the
external theme runtime with the same completeness as `color.*`:

- `font.*`
- `metric.*`
- `motion.*`

See:

- [theme_system_spec.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_system_spec.md)
- [theme_delivery_plan.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_delivery_plan.md)

## 5. Live Component Slots

| Slot ID | Required | Current Sources | Notes |
| --- | --- | --- | --- |
| `component.top_bar.container` | yes | `modules/ui_shared/src/ui/widgets/top_bar.cpp` | Shared top bar container |
| `component.top_bar.back_button` | yes | `modules/ui_shared/src/ui/widgets/top_bar.cpp` | Shared back button |
| `component.top_bar.title_label` | yes | `modules/ui_shared/src/ui/widgets/top_bar.cpp` | Shared title label |
| `component.top_bar.right_label` | yes | `modules/ui_shared/src/ui/widgets/top_bar.cpp` | Shared right-side label |
| `component.directory_browser.selector_panel` | yes | `modules/ui_shared/src/ui/components/two_pane_styles.cpp` | Directory-browser selector panel |
| `component.directory_browser.content_panel` | yes | `modules/ui_shared/src/ui/components/two_pane_styles.cpp` | Directory-browser content panel |
| `component.directory_browser.selector_button` | yes | `modules/ui_shared/src/ui/components/two_pane_styles.cpp` | Directory-browser selector button |
| `component.directory_browser.list_item` | yes | `modules/ui_shared/src/ui/components/two_pane_styles.cpp` | Directory-browser list item |
| `component.info_card.base` | no | `modules/ui_shared/src/ui/components/info_card.cpp` | Info card base |
| `component.info_card.header` | no | `modules/ui_shared/src/ui/components/info_card.cpp` | Info card header |
| `component.busy_overlay.panel` | yes | `modules/ui_shared/src/ui/widgets/busy_overlay.cpp` | Busy overlay panel |
| `component.busy_overlay.progress_bar` | yes | `modules/ui_shared/src/ui/widgets/busy_overlay.cpp` | Busy overlay progress bar |
| `component.notification.banner` | yes | `modules/ui_shared/src/ui/widgets/system_notification.cpp` | Notification banner |
| `component.modal.scrim` | yes | `modules/ui_shared/src/ui/widgets/busy_overlay.cpp`, `modules/ui_shared/src/ui/widgets/ble_pairing_popup.cpp`, `modules/ui_shared/src/ui/screens/usb/usb_page_runtime.cpp`, `modules/ui_shared/src/ui/screens/team/team_page_components.cpp`, `modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp` | Shared modal scrim |
| `component.modal.window` | yes | `modules/ui_shared/src/ui/widgets/busy_overlay.cpp`, `modules/ui_shared/src/ui/widgets/ble_pairing_popup.cpp`, `modules/ui_shared/src/ui/screens/usb/usb_page_runtime.cpp`, `modules/ui_shared/src/ui/screens/team/team_page_components.cpp`, `modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp` | Shared modal window/panel |
| `component.menu.app_button` | yes | `modules/ui_shared/src/ui/menu/menu_layout.cpp` | Menu app button |
| `component.menu.app_label` | yes | `modules/ui_shared/src/ui/menu/menu_layout.cpp` | Menu app label |
| `component.menu.bottom_chip.node` | yes | `modules/ui_shared/src/ui/menu/menu_layout.cpp` | Menu bottom node chip |
| `component.menu.bottom_chip.ram` | yes | `modules/ui_shared/src/ui/menu/menu_layout.cpp` | Menu bottom RAM chip |
| `component.menu.bottom_chip.psram` | yes | `modules/ui_shared/src/ui/menu/menu_layout.cpp` | Menu bottom PSRAM chip |
| `component.menu_dashboard.app_grid` | yes | `modules/ui_shared/src/ui/presentation/menu_dashboard_layout.cpp` | Menu dashboard grid scaffold |
| `component.menu_dashboard.bottom_chips` | yes | `modules/ui_shared/src/ui/presentation/menu_dashboard_layout.cpp` | Menu dashboard bottom chips region |
| `component.boot_splash.root` | yes | `modules/ui_shared/src/ui/presentation/boot_splash_layout.cpp`, `modules/ui_shared/src/ui/ui_boot.cpp` | Boot splash root |
| `component.boot_splash.log` | yes | `modules/ui_shared/src/ui/presentation/boot_splash_layout.cpp`, `modules/ui_shared/src/ui/ui_boot.cpp` | Boot splash log area |
| `component.watch_face.root` | yes | `modules/ui_shared/src/ui/presentation/watch_face_layout.cpp`, `modules/ui_shared/src/ui/watch_face.cpp` | Watch-face root |
| `component.watch_face.primary_region` | yes | `modules/ui_shared/src/ui/presentation/watch_face_layout.cpp`, `modules/ui_shared/src/ui/watch_face.cpp` | Watch-face primary region |
| `component.watch_face.node_id` | yes | `modules/ui_shared/src/ui/watch_face.cpp` | Watch-face node ID |
| `component.watch_face.battery` | yes | `modules/ui_shared/src/ui/watch_face.cpp` | Watch-face battery |
| `component.watch_face.clock.hour` | yes | `modules/ui_shared/src/ui/watch_face.cpp` | Watch-face hour |
| `component.watch_face.clock.minute` | yes | `modules/ui_shared/src/ui/watch_face.cpp` | Watch-face minute |
| `component.watch_face.clock.separator` | yes | `modules/ui_shared/src/ui/watch_face.cpp` | Watch-face separator |
| `component.watch_face.date` | yes | `modules/ui_shared/src/ui/watch_face.cpp` | Watch-face date |
| `component.chat_conversation.root` | yes | `modules/ui_shared/src/ui/presentation/chat_thread_layout.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_conversation_layout.cpp` | Chat conversation root |
| `component.chat_conversation.thread_region` | yes | `modules/ui_shared/src/ui/presentation/chat_thread_layout.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_conversation_layout.cpp` | Chat conversation thread region |
| `component.chat_conversation.action_bar` | yes | `modules/ui_shared/src/ui/presentation/chat_thread_layout.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_conversation_layout.cpp` | Chat conversation action bar |
| `component.chat.bubble.self` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_styles.cpp` | Self bubble surface |
| `component.chat.bubble.peer` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_styles.cpp` | Peer bubble surface |
| `component.chat.bubble.text` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_styles.cpp` | Bubble text style |
| `component.chat.bubble.time` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_styles.cpp` | Bubble time/meta style |
| `component.chat.bubble.status` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_styles.cpp` | Bubble delivery/error status style |
| `component.chat_compose.root` | yes | `modules/ui_shared/src/ui/presentation/chat_compose_scaffold.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_compose_layout.cpp` | Chat compose root |
| `component.chat_compose.content_region` | yes | `modules/ui_shared/src/ui/presentation/chat_compose_scaffold.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_compose_layout.cpp` | Chat compose content region |
| `component.chat_compose.editor` | yes | `modules/ui_shared/src/ui/screens/chat/chat_compose_styles.cpp` | Chat compose editor |
| `component.chat_compose.action_bar` | yes | `modules/ui_shared/src/ui/presentation/chat_compose_scaffold.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_compose_layout.cpp` | Chat compose action bar |
| `component.map_focus.root` | yes | `modules/ui_shared/src/ui/presentation/map_focus_layout.cpp` | Map-focus root scaffold |
| `component.map_focus.overlay.primary` | yes | `modules/ui_shared/src/ui/presentation/map_focus_layout.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_layout.cpp` | Map-focus primary overlay |
| `component.map_focus.overlay.secondary` | yes | `modules/ui_shared/src/ui/presentation/map_focus_layout.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_layout.cpp` | Map-focus secondary overlay |
| `component.map.info_panel` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Node-info floating info panel |
| `component.map.control_button` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | Shared GPS/map control button surface |
| `component.map.layer_popup_window` | no | `modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp` | Node-info layer popup window |
| `component.gps.indicator_label` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS pan indicator label |
| `component.gps.loading_box` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS loading overlay box |
| `component.gps.loading_label` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS loading overlay label |
| `component.gps.toast_box` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS toast box |
| `component.gps.toast_label` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS toast label |
| `component.gps.zoom_window` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS zoom popup window |
| `component.gps.zoom_title_bar` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS zoom popup title bar |
| `component.gps.zoom_title_label` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS zoom popup title label |
| `component.gps.zoom_value_label` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS zoom value readout / touch-selection highlight |
| `component.gps.zoom_roller` | no | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp` | GPS zoom roller |
| `component.instrument_panel.root` | yes | `modules/ui_shared/src/ui/presentation/instrument_panel_layout.cpp` | Instrument-panel root scaffold |
| `component.instrument_panel.body` | yes | `modules/ui_shared/src/ui/presentation/instrument_panel_layout.cpp` | Instrument-panel body |
| `component.instrument_panel.canvas_region` | yes | `modules/ui_shared/src/ui/presentation/instrument_panel_layout.cpp` | Instrument-panel canvas region |
| `component.instrument_panel.legend_region` | yes | `modules/ui_shared/src/ui/presentation/instrument_panel_layout.cpp` | Instrument-panel legend region |
| `component.service_panel.root` | yes | `modules/ui_shared/src/ui/presentation/service_panel_layout.cpp` | Service-panel root scaffold |
| `component.service_panel.body` | yes | `modules/ui_shared/src/ui/presentation/service_panel_layout.cpp` | Service-panel body |
| `component.service_panel.primary_panel` | yes | `modules/ui_shared/src/ui/presentation/service_panel_layout.cpp` | Service-panel primary panel |
| `component.service_panel.footer_actions` | yes | `modules/ui_shared/src/ui/presentation/service_panel_layout.cpp` | Service-panel footer actions |
| `component.team.member_chip` | no | `modules/ui_shared/src/ui/screens/team/team_page_components.cpp` | Team member chip |
| `component.gnss.sky_panel` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GNSS sky plot panel |
| `component.gnss.status_panel` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GNSS status panel |
| `component.gnss.status_header` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GNSS status header |
| `component.gnss.table_header` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GNSS table header |
| `component.gnss.table_row` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GNSS table row |
| `component.gnss.status_toggle` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GNSS status toggle button |
| `component.gnss.satellite_use_tag` | no | `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp` | GNSS satellite USE tag |
| `component.sstv.image_panel` | no | `modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp` | SSTV image panel |
| `component.sstv.progress_bar` | no | `modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp` | SSTV progress bar |
| `component.sstv.meter_box` | no | `modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp` | SSTV audio meter box |
| `component.action_button.primary` | yes | `modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_compose_styles.cpp` | Shared primary action button |
| `component.action_button.secondary` | yes | `modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp`, `modules/ui_shared/src/ui/screens/team/team_page_styles.cpp`, `modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp` | Shared secondary action button |

## 6. Live Semantic Asset Slots

| Slot ID | Required | Current Sources | Notes |
| --- | --- | --- | --- |
| `asset.boot.logo` | yes | `modules/ui_shared/src/ui/ui_boot.cpp` | Boot logo |
| `asset.notification.alert` | yes | `modules/ui_shared/src/ui/widgets/system_notification.cpp` | Notification icon |
| `asset.status.route` | yes | `modules/ui_shared/src/ui/ui_status.cpp` | Route status icon |
| `asset.status.tracker` | yes | `modules/ui_shared/src/ui/ui_status.cpp` | Tracker status icon |
| `asset.status.gps` | yes | `modules/ui_shared/src/ui/ui_status.cpp` | GPS status icon |
| `asset.status.wifi` | yes | `modules/ui_shared/src/ui/ui_status.cpp` | Wi-Fi status icon |
| `asset.status.team` | no | `modules/ui_shared/src/ui/ui_status.cpp` | Team status icon |
| `asset.status.message` | yes | `modules/ui_shared/src/ui/ui_status.cpp` | Message status icon |
| `asset.status.ble` | yes | `modules/ui_shared/src/ui/ui_status.cpp` | BLE status icon |
| `asset.menu.app.chat` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Chat app icon |
| `asset.menu.app.map` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Map app icon |
| `asset.menu.app.sky_plot` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Sky plot app icon |
| `asset.menu.app.contacts` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Contacts app icon |
| `asset.menu.app.energy_sweep` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Energy sweep app icon |
| `asset.menu.app.team` | no | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Team app icon |
| `asset.menu.app.tracker` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Tracker app icon |
| `asset.menu.app.pc_link` | no | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | PC link app icon |
| `asset.menu.app.sstv` | no | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | SSTV app icon |
| `asset.menu.app.settings` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Settings app icon |
| `asset.menu.app.extensions` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Extensions app icon |
| `asset.menu.app.usb` | no | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | USB app icon |
| `asset.menu.app.walkie_talkie` | yes | `modules/ui_shared/src/ui/app_catalog_builder.cpp` | Walkie-talkie app icon |
| `asset.map.self_marker` | yes | `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | GPS self-position marker icon |
| `asset.team_location.area_cleared` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_components.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | Team location marker icon |
| `asset.team_location.base_camp` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_components.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | Team location marker icon |
| `asset.team_location.good_find` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_components.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | Team location marker icon |
| `asset.team_location.rally` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_components.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | Team location marker icon |
| `asset.team_location.sos` | yes | `modules/ui_shared/src/ui/screens/chat/chat_conversation_components.cpp`, `modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp`, `platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp` | Team location marker icon |

## 7. Extraction Status Notes

The current tracker / team / GNSS / SSTV / GPS / map extraction passes have already moved these
page-private visuals into runtime-backed theme surface:

- tracker mode buttons, list rows, action buttons, and modal surface
- team member semantic colors, member chips, list items, and modal surface
- GNSS constellation colors, SNR colors, sky/status/table surfaces, and satellite-use tags
- SSTV image panel, progress bar, audio-meter box, and meter mid-range color
- GPS route/track overlays, control buttons, loading/toast/zoom popup surfaces, team map-marker chrome, and the self-position marker
- node-info map markers, link/readout semantic colors, floating info panel, and layer popup surface

That means those pages are no longer relying on hardcoded warm-color styling in their page code
for the extracted surfaces above. Remaining extraction work outside this file should now focus on
other page families or on unfinished `font.* / metric.* / motion.*` runtime support.
