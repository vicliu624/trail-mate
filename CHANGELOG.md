# Changelog

All notable changes to this project will be documented in this file.

This project follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Older releases used the pre-1.0 alpha version line. Version 1.0.0 is the first
stable firmware release. Hardware support and validation remain target-specific.

## [Unreleased]

## [1.0.1] - 2026-10-17

This patch enables the browser-management USB path in Pager and T-Deck release
images, improves connection diagnostics, and updates the website UI and privacy
positioning. Install the new board-specific firmware to gain USB management;
updating the website alone cannot change an installed 1.0.0 image.

### Fixed

- Pager SX1262, Pager LR1121 and T-Deck release builds now include USB CDC and HostLink
  management, using the same connection-driven service lifecycle as Wio L2.
  Release checks require these components while continuing to exclude DFU.
  Browser connection errors identify the failed stage and retain serial read
  diagnostics instead of replacing them with a generic disconnect.
- The website now exposes Device management, Tracks and Geocaching from every
  page, with shared navigation, current-page highlighting, language-preserving
  links, responsive layouts and a keyboard-accessible Resources menu. The home
  page includes task-oriented tool cards. Tool connection controls remain in a
  separate action row, and existing unsaved-edit protections remain active.
- Release-facing Web illustrations are recaptured in English from the current
  production bundles. Their provenance identifies simulated USB/sample data
  separately from read-only directory queries and physical-device acceptance.
- Closing a shared-track map during zoom no longer leaves Leaflet's completion
  callback accessing a destroyed view. Track maps share a public-option
  workaround, with a browser regression that reproduces the former failure.

### Changed

- Animal Island UI is updated from 2.0.0 to 2.2.1 and naive-icons from 1.2.0 to
  1.6.0. Device text inputs use the library Input, native file/range and dynamic
  editor controls receive shared theme/focus states, and long section titles
  wrap on narrow screens.
- Homepage positioning now explains the privacy intent behind device-held keys,
  local records, pseudonymous authorship and independently operated services:
  reducing the personal information a platform needs to hold. The bilingual
  promotional set is reduced to four A4 posters per language around those core
  benefits, decentralized communication and offline maps.

## [1.0.0] - 2026-10-17

### 1.0.0 — first stable firmware release

Trail Mate 1.0.0 brings offline route planning, maps, messaging and everyday tools
together on the handheld. Its browser tools add device configuration, Geocaching
maintenance and signed trail sharing. Your device holds the author identity;
independently operated Reticulum directories help others find what you choose
to publish, without requiring a website account.

### Added

- Offline foot-route planning and saved navigation: choose a current or searched origin and a destination on the device, calculate from installed TMAP 0.2.0 routing data, save an indexed `.tmroute` plan, preview it and follow it in Map without an Internet connection. Map provides progress, remaining distance, geometric turns, deviation, replanning and arrival; saved plans survive stopping or a failed replan.
- A shared adaptive launcher and widget screen for Pager, T-Deck and Wio L2, with Mesh, GNSS, activity, Agenda, Music and Quick controls. Compass appears only on boards with a magnetometer. Touch swipes move between the launcher and widgets; Pager uses V, with contextual keyboard/encoder controls and Help. P4 presents paged widgets in its larger right-hand dashboard.
- SD-card MP3 playback from `/Music`, including subfolders; song/artist/album/folder browsing, user playlists, album art, available ID3 fields, embedded/sidecar lyrics, repeat-all, repeat-one and shuffle. Active sessions reopen in Now Playing; the Music widget provides quick playback controls.
- Quick controls for Wi-Fi, LoRa, GPS, volume and brightness, routed through the existing service and audio-focus coordinators. Voice and intercom pause music when taking the audio output.
- USB browser configuration management: complete verified snapshots, classified settings and search, readable enumerations and units, validation, change review, save/readback, and configuration backup/restore.
- Navigation diagnostics over framed HostLink USB logs, with a bounded capture helper and an external-only 384-byte formatting scratch owner.
- Interactive route previews with map dragging, zoom buttons and Fit, equivalent keyboard controls, retained view across layer/image changes, and indexed SD detail for generated routes.
- Pager route-preview shortcuts use W/A/S/D to pan, Q/E to zoom and F to fit, with explicit Preview Help instructions. G downloads images and V toggles the profile to avoid movement/zoom conflicts; W/S selects images while the image strip is shown.
- Device author identity in the browser, including proof of possession and device-side signatures. Website registration and login are unnecessary; author operations are independent of the selected chat/RF protocol.
- Browser Geocaching maintenance: device drafts, signed publication through Reticulum using the computer's network, updates under the same cache ID, archive states, directory proofs and device confirmation.
- Browser trail workspace: local drafts and device-record import; KML/KMZ and supported GPX/CSV/BIN input; geometry and waypoint maintenance; elevation and time views; attachments; portable draft and signed-package export.
- Shared-trail browsing by map extent, queries after map movement, direct object-ID lookup, copyable IDs and version links, verified details and downloads. The public administrative catalog supports country/region and one administrative level globally, with 251 parents and 4,596 units; it adds no city/county hierarchy or firmware dataset.
- Device-authored trail publishing, updates and archive states with stable object IDs, parent-version chains, signed directory receipts and bounded USB synchronization.
- Eleven browser interface languages for configuration, identity and content maintenance.
- Navigation serial diagnostics identify UI validation, distinct task-admission failures, PSRAM/internal free and largest blocks, SD/USB ownership, selected map packages, search/export progress and terminal outcomes. Request IDs correlate backend events; bounded progress logging avoids per-expansion output and omits endpoint names, exact coordinates and configuration fields.

### Existing capabilities included in 1.0.0

- Offline navigation: GNSS position; north-up OSM, terrain and satellite maps; contour overlays; TMAP packages; offline POI search; saved waypoints; route overlays; and map-based node inspection.
- GNSS sky plot, constellation/satellite information, signal quality and fix diagnostics.
- Local track recording and removable-media storage; KML route following, elevation previews, waypoint images and navigation status.
- Meshtastic, MeshCore and Reticulum/LXMF messaging, discovery and contacts, persistent conversations and message history, with target-specific networking support.
- Reticulum settings, supported LoRa/AutoInterface/TCP interfaces, gateway discovery and fallback, editable SD configuration and IFAC for supported manually configured TCP interfaces.
- On-device Nomad/Micron browsing, links, forms, constrained rendering, cached state and compatibility diagnostics.
- Team formation and ESP-NOW key exchange, leader/member roles, LoRa team chat, controlled position and status sharing, waypoints and team-map awareness on supported devices.
- Hardware-dependent FSK/Codec2 half-duplex voice and SSTV reception with image preview.
- Agenda countdowns and recurring reminders, saved waypoints and event map previews on supported targets.
- USB HostLink exchange of messages, team/GPS events and diagnostics; host integrations, removable-media access and local screenshots where supported.
- Display/input, sleep/power, GNSS and network settings, graceful shutdown and runtime status.
- Trail Mate Center workflows for offline map preparation, USB dashboards, messaging, logs/events, replay and export, plus its separate propagation workbench. Center remains a companion project rather than embedded firmware.
- ESP32, nRF52 and Linux targets with device-specific displays and inputs, firmware packaging, selected web-flasher images, nRF52 UF2 assets and debug symbols.
- GitHub Pages product guide, browser installation, native firmware previews and downloadable locale/resource packs.

### Changed

- Product documentation introduces device-owned identity, decentralized Geocaching and trail sharing alongside navigation and communication.
- Music content labels use the installed content font for CJK titles, artists, albums, lyrics and playlists. Compass widgets are filtered by magnetometer capability, without starting absent hardware or leaving empty pages.
- L2 uses a verified 0 dB ES8311 DAC stage instead of the library's +17.5 dB boost. JPEG covers convert LVGL's BGR output correctly; register-I/O and color-bar regressions cover both fixes.
- Music avoids waiting on UI state locks and publishes cached progress at 10 Hz. A producer fills a bounded PSRAM PCM queue and an independent output worker drains it; L2 keeps audio work alongside its core-1 UI. Pause, seek, track changes and audio-focus preemption discard stale queued audio. SD reads are batched, transitions silence the amplifier, and starvation diagnostics support further device testing.
- SD working configuration uses strict TMSET7, including supported Wi-Fi, Reticulum/LXMF, BLE and cellular settings. Earlier schemas are migration inputs; complete validated transactions replace the file atomically, while malformed documents remain available for repair.
- Configuration controls show meanings, ranges and units instead of unexplained numbers. Brightness/audio use percentages and satellite masks have understandable choices.
- Browser publication guides signing, network publication and device confirmation in one operation while preserving unconfirmed work.
- Successive publications of a trail draft retain the same object ID and parent-version chain.
- After durable device confirmation, browser trail synchronization also releases the exact staged attachment mapping, index and resources using their retained upload IDs, lengths and hashes, so successive package publications do not consume temporary slots indefinitely.
- Offline map reading and search consume the independent TMAP SDK 0.2.0, locked to commit `d74b084acb61653149dec1ac161df58a92278cff`. PlatformIO and CMake share the release pin and CMake verifies the published source archive hash. Native archive production streams through an exclusive SD spool and releases search storage before export; following uses fixed external owners and a 128-segment screen snapshot. Production-worker and LVGL tests use Center's real 20.779 km London route. See `docs/engineering/native-navigation.md` for measured payload/read volumes and physical-device verification limits.
- Firmware Tracker and Geocaching reuse one browsable item card with previous/next navigation and on-demand details. Main cards show distance and an approximate administrative area through a shared map-search component instead of raw coordinates.
- Tracker cards prefer KML route names over internal revision filenames, use readable fallbacks for unnamed shared routes, remove the redundant filename status, and place paging and localized Actions in the card footer.
- Tracker, Contacts, Chat, Settings, Extensions and Network use a shared bottom bar and upward filter menu, recovering the permanent sidebar width for content. Contacts/Chat air status is available on demand under Info. Existing filter widgets and page callbacks are reused; menu layout, input dismissal and transient surfaces share one component.
- Map controls use the same bar and popup components for separate upward Menu and Team menus. Menu contains a direct OSM/Terrain/Satellite submenu, Search, and stable Navigation, Tracks and Contours management entries. Team appears only while joined to a valid team.
- Map and Tracker dialogs share a frame composed from the page TopBar and a compact touch close button, with a fixed header and scrolling body. Keyboard devices can close current frame-based dialogs with X/x. Tracker card actions use flat rows, a light focus state and content-sized height; the redundant Cancel row is removed.
- Tracker's Tracks and Nav menus expose View, New and Current instead of separate root creation commands. Stop recording remains directly available only while recording.
- Named POI label budgets increase by approximately 40 percent while keeping the existing placement buffers and overlap checks. Actual visible density depends on available data and screen space.
- Supported workflows use standalone device controls, browser USB and Center; Meshtastic/MeshCore Bluetooth companion-app connections are not advertised as supported.
- Privacy descriptions distinguish private local records from voluntarily public coordinates, routes, time and author-linked metadata. Independent services avoid requiring one central account operator, but recipients can retain and analyse public data.

### Fixed

- Pager encoder focus and keyboard routing across app pages and widget controls, focus preservation during refresh and track changes, repeated Enter/click handling, nested Preview Help, and launcher Help consuming the advertised V shortcut. Help now uses one titled, closable style with visible switching hints.
- Music output status handling on Pager, L2 codec gain and playback buffering, late widget metadata, CJK content fonts and RGB565 cover colours. A bounded 256 KiB PSRAM PCM queue and a separate output worker give playback more headroom during UI work; remaining physical playback testing is target-specific.

### Memory and reliability

- Native ESP-IDF P4 and C6 builds embed the selected firmware version explicitly. Packaging checks the application descriptor in the actual binary, rejecting a release-named file whose embedded version differs.
- Route calculation accepts existing `/routes` and `/routes/navigation` directories instead of interpreting SdFat's exclusive mkdir failure as unavailable storage. Regular files at either path remain errors; the production-worker regression now uses the same existing-directory semantics.
- Native route SD writes extend gaps in bounded 256-byte steps before seeking, matching exFAT's rejection of seeks beyond EOF. This supports reverse leg-spool output and aligned archive pages without a route-sized RAM buffer. Navigation prefers current integrated map packages over older standalone graph directories and logs failed map/route I/O operations with offsets.
- Saved-route display retries transient SD contention during LVGL open/read/seek/tell instead of reporting damaged files. Dense viewport limits preserve the open archive and recover after zoom; actual read failures still report operation/offset diagnostics.
- Compact launcher scrolling clips at the fixed top/bottom bars instead of a centered two-row viewport. Dashboard slides cancel residual parent scroll animations and stop launcher scroll chaining, so native scrolling cannot retain a separate horizontal offset after a feed transition.
- Compact launcher icons fit the complete source image inside their flex layout box. This prevents top/bottom clipping while stationary when a larger source image is assigned a shorter layout height before scaling.
- Wio L2 LVGL and HostLink frame allocation prefer PSRAM, retaining the internal-heap improvement accepted on the real device.
- Read-only bzip2 tables use read-only storage. Protocol operations use bounded transfers, borrowed views and explicit scratch ownership instead of large ESP task-stack objects.
- Configuration, identity and publication operations retain device-owned authorization and persistent confirmation state. Real-hardware evidence is distinguished from simulated or native checks.
- Browser Geocaching draft saves yield and retry when the shared SD filesystem is busy opening, sizing, seeking or reading the staged edit, preserving the last complete input slice instead of treating contention as a storage failure.
- Reconnecting after a failed Geocaching draft save can resume the exact staged edit under a new USB session after checking the device author generation, draft ownership and version. Active saves remain protected; recovery reuses existing scratch storage.
- Geocaching signature input processing also yields on transient SD open/size/seek/read contention and resumes from the last completely hashed slice. A native regression reproduces the original failure and verifies all four busy stages without adding a persistent input buffer.
- Signature validity checks distinguish a busy identity lock from an actual identity change. A busy status/chunk read preserves the signed output for retry; identity changes, storage errors and expiry still invalidate the task. The regression reproduces loss of an already signed result before the correction.

### Validation and device support

- Wio L2 offline navigation, route following, the current menus and touch interaction were accepted on hardware. Native and browser checks cover the shared UI and publication flows separately; the detailed receipts are in the [release record](docs/releases/1.0.0.md).
- The P4 TFT test unit has working display, touch, brightness and volume controls. Its external keyboard still reports no response from the accessory's I2C expander and remains under diagnosis. P4 AMOLED and other bring-up ports retain their own device-validation status.
- Music, widgets, navigation, voice, Compass and network interfaces follow the board's actual capabilities. A stable version number does not give every target the same peripherals or feature set.
- Real Geocaching and trail publish/update/archive flows were verified with the retained device identity and original objects. The acceptance record distinguishes those checks from simulated authors, native renders and the historical browser-run locale assertion.

## [0.1.53-alpha] - 2026-10-10

### Added

- TMAP offline map packages for OSM, terrain and satellite imagery, with separate POI labels and generated map font resources.
- Offline POI search from the Map search window, with names, administrative hierarchy and distance; selecting a result moves the map to the place and displays a target marker.

### Changed

- Map packages can cover different regions and zoom ranges together, replacing large collections of individual tile files with a small number of indexed files.
- Native map pixels are read by indexed offsets without PNG decoding, while POI display data and search indexes are stored in the OSM package instead of separate JSON files.
- Shared pixel ownership, a smaller PSRAM worker buffer, retained map-file handles and reuse of POIs across style changes reduce copying, decoding and repeated file access. Prefetching keeps nearby map data ready for panning.


## [0.1.52-alpha] - 2026-10-01

### Fixed

- Keep Geocaching rows stable during unrelated operations and query progress, and skip completed tasks during publication recovery.
- Allow removed local caches to be downloaded again while protecting modified GPX files and safely resuming history cleanup.
- Preserve current Geocaching indexes during checkpoint rebuilds and upgrade legacy local indexes during idle maintenance after writable recovery.
- Upgrade legacy Downloaded metadata once without reading GPX; subsequent pages use current indexes only.

## [0.1.51-alpha] - 2026-10-01

### Added

- Decentralized Geocaching over Reticulum, with signed publications, independent directories, offline caches and a live web map.
- IFAC support for manually configured Reticulum TCP interfaces through SD configuration.

### Fixed

- Allow owners to archive public caches from their details; hide archived caches and reject old downloads.
- Refresh local map markers after saved-cache changes and keep details valid after saving.
- Fix Reticulum resource interoperability and publication reply timing; keep the live map current.

### Changed

- Reticulum network settings now have their own category, with automatic gateway discovery, persistent fallback and failover.

## [0.1.50-alpha] - 2026-09-23

### Added

- Added SD-backed Agenda with event countdowns, recurring reminders, and location map previews on T-LoRa Pager, T-Deck, and Wio Tracker L2.
- Added local waypoint saving and selection for Agenda events.

### Changed

- Removed redundant PNG map tile pixel copies to reduce memory traffic and transient memory usage.

## [0.1.49-alpha] - 2026-09-21

### Fixed

- Fixed Settings Back navigation and encoder focus in setting dialogs.

### Changed

- Stacked setting names above values on narrow screens such as T-Deck and Wio Tracker L2.

## [0.1.48-alpha] - 2026-09-20

### Changed

- Improved Wio Tracker L2 SD read/write performance with SDMMC.
- Reduced Wio Tracker L2 static RAM usage.

## [0.1.47-alpha] - 2026-09-18

### Fixed

- Fixed GPS runtime initialization on Wio Tracker L2. The shared ESP GPS runtime previously required a motion-sensor board to be present, causing GNSS initialization to be skipped entirely on Wio Tracker L2. GPS can now initialize and run normally on targets without motion hardware.

## [0.1.46-alpha] - 2026-09-18

### Changed

- Updated the Wio Tracker L2 screen-saver interaction for its touch-only input
  model. Wio Tracker L2 now uses an upward swipe to resume the previously active
  page instead of showing the keyboard-specific `Press SPACE to resume` action.
  The shared screen-power runtime now exposes an explicit resume action so
  keyboard and touch targets can use different physical interactions while
  preserving the same wake/resume state-machine behavior.

- Changed the default ESP Arduino fallback node-name prefix from the
  hardware-vendor-specific `lilygo` prefix to `TrailMate`, so unconfigured
  devices use a product-level identity instead of identifying themselves as a
  LilyGo device.

### Fixed

- Fixed the Wio Tracker L2 LVGL display transfer contract. The board adapter had
  interpreted the shared `x, y, width, height` transfer arguments as
  `x1, y1, x2, y2`, causing the first full-screen frame to be rejected before
  reaching the NV3031B display.

- Fixed Wio Tracker L2 RGB565 rendering by replacing the incompatible
  `pushImage()` framebuffer path with an explicit LovyanGFX address-window and
  pixel-transfer path using the required RGB565 byte order. This restores the
  correct colors and full 320x240 layout without partial-screen or black-area
  artifacts.

- Fixed the Calculator app being absent from the Wio Tracker L2 application
  menu by enabling it for the Wio Tracker L2 target.

- Added the missing localized `Swipe up to resume` strings across the locale
  packs so locale validation succeeds and the Wio Tracker L2 screen-saver hint
  can be translated consistently.

## [0.1.45-alpha] - 2026-09-16

### Added

- Added full-touch Wio Tracker L2 device support, including its board profile,
  display and input integration, SD storage, and build configuration.
- Added offline map annotations for the shared ESP32 Arduino map backend used by
  T-LoRa Pager, Arduino T-Deck builds, and Wio Tracker L2. Compatible Center map
  packs provide independently rendered road names, place names, and facility POIs
  over text-free raster tiles. Pager LR1121 has been validated on hardware;
  ESP-IDF builds, including Tab5 and T-Display P4, are not enabled by this change.
- Added support for version 3 map annotation indexes with bounded road geometry,
  while retaining legacy POI index compatibility. Map-specific font subsets use
  the existing SD content-font package mechanism.
- Added an eleven-language project website with native-device previews.

### Changed

- Map annotations now prioritize readable names with separate road, place, and
  facility layout budgets, stable selection, UTF-8-safe name shortening, and
  counts for clustered unnamed facilities. Text backgrounds are transparent,
  without white label plates or halos.
- Annotation loading reuses the existing map worker and SD access boundary.
  Bounded candidate and display buffers allocate on demand in PSRAM, and failed
  updates retain a valid frame when it still corresponds to the current view.
- Removed gaps and contrasting backgrounds from two-pane UI layouts.

### Fixed

- Fixed annotation reads failing with `err=-1` while raster tiles still displayed
  by explicitly aligning shared worker scratch and typed annotation payloads.
- Deferred annotation layout when required glyphs are unavailable, preventing
  missing-glyph placeholders from being used as valid text metrics.
- Stabilized T-Deck Pro e-paper refresh scheduling.
- Stabilized T-Display P4 power sequencing and keyboard backlight behavior.

## [0.1.44-alpha] - 2026-08-31

### Fixed

- Fixed T-LoRa Pager Walkie FSK interoperability between LR1121 and SX1262 radios by
  using each chip family's TX/RX completion and timeout IRQ masks. Completed and timed-out
  asynchronous transmissions now run the RadioLib transmit cleanup path, preventing the
  voice queue from remaining permanently in flight and allowing a compatible receiver to
  read and buffer incoming Codec2 packets.

## [0.1.43-alpha] - 2026-08-28

### Fixed

- Fixed ESP32 timezone changes so they apply without restarting the device and are
  synchronized to the SD working configuration before a later boot can restore an older
  `config.tms` value (#86).

## [0.1.42-alpha] - 2026-08-26

### Added

- Added a TI-inspired scientific Calculator app for T-LoRa Pager, T-Deck, and
  T-Display-P4. It provides basic arithmetic, pi/ANS, powers, roots,
  reciprocals, logarithms, percentages, degree/radian trigonometry and inverse
  trigonometric functions, with touch and physical-keyboard operation plus
  device-specific layouts.

### Fixed

- Restored the T-Display-P4 `Alt` double-press screenshot shortcut after the
  keyboard transport moved into its dedicated adapter.

## [0.1.41-alpha] - 2026-08-24

### Added

- Added an SD-editable `/trailmate/config.tms` working configuration. Valid settings on the
  card take priority at startup; when it is absent, existing NVS settings are used and can
  be materialized to the card.
- Added a show/hide control to masked on-device Settings fields, so PSKs and other long
  secrets can be verified before saving while masking remains the default (#82).

### Changed

- Made the SD working configuration the portable, primary source for all supported
  settings, including protocol and network options, radio channels, device preferences,
  Wi-Fi profiles, and cellular settings. Existing installations retain their saved
  settings during the first upgrade to this release.
- USB Disk now gives the host exclusive SD-card access and temporarily pauses competing
  application, radio, Wi-Fi, GPS, and display activity without changing the user's saved
  settings.

### Fixed

- Fixed USB Disk transfers that could report absent media or drop during access by holding
  SD ownership stable for the whole host session and preventing competing shared-SPI
  activity (#83).

## [0.1.40-alpha] - 2026-08-14
### Added
- Added bounded VMP voice messages for T-LoRa Pager chat. The compose microphone records up to five seconds of Codec2 audio; LR1121 uses the active Sub-GHz settings only for private control/receiver-ready negotiation, then sends the compact voice train over its 2.4 GHz radio. Private voice is end-to-end encrypted, while broadcast voice is intentionally public. Received VMP objects are never flooded, relayed, retransmitted, or injected into a radio mesh.
- Added carrier and storage safeguards for VMP voice: SX1262 Pager can record/send only through an enabled MT MQTT uplink and never enters VMP RF, 2.4 GHz, or LXMF paths; received voice is stored in a bounded PSRAM/SD attachment inbox with integrity checks and backup recovery, appears in the conversation after durable storage succeeds, and can be tapped to play.

- Replaced generic Energy Sweep with Protocol Probe, which uses protocol-specific evidence to discover and verify complete LoRa profiles instead of treating RSSI or a quiet spectrum as a usable channel.
- Added the T-Deck Pro-only 240×320 monochrome text UI and A7682E cellular workflow.

### Changed
- Reduced T-LoRa Pager /T-Deck internal static RAM from 140,592 B to 106,544 B (33.25 KiB reclaimed) by making external locale/font resources lazy and PSRAM-only, moving non-real-time UI/page state and cold Reticulum configuration scratch storage to strict PSRAM allocation, and removing permanent static worker-stack baselines. Wi-Fi/DNS state, DMA/I²S audio buffers, and other real-time synchronization state remain in internal RAM by design.

- Split T-Deck and T-LoRa Pager USB maintenance profiles: release builds remove application USB CDC, DFU Runtime, and full TinyUSB DFU (use BOOT + RESET for flashing), while `*_debug` builds retain CDC logs and full DFU. Reticulum Call audio queues now allocate only when media starts and prefer PSRAM, reclaiming 4,436 B of always-resident internal RAM without moving Wi-Fi/DNS state.

### Fixed
- Fixed ESP Team and normal Wi-Fi from each attempting to create the same default Wi-Fi STA netif. Both startup paths now adopt the existing ESP-IDF netif, and active ESP-NOW pairing reserves the radio so Settings cannot start normal Wi-Fi until pairing ends.
- Fixed saved Wi-Fi profile cycling: up to ten SSID/password pairs are retained and the Wi-Fi runtime now schedules the next saved profile after a failed connection instead of depending on an external network client to retry.

## [0.1.39-alpha] - 2026-07-31
### Fixed
- Stabilized the ESP Meshtastic MQTT session so connection, subscription, and keepalive progress are tracked explicitly through CONNACK, SUBACK, and PINGRESP deadlines; reconnects use bounded exponential backoff, control-plane replies cannot be starved by ordinary Wi-Fi traffic budgets, and inbound QoS 1 publishes are acknowledged with PUBACK. Transient publish failures now retain the in-flight uplink for retry, while a full bounded uplink queue rejects low-priority NodeInfo, position, telemetry, and traceroute reports before they can displace text, control, or encrypted traffic.

## [0.1.38-alpha] - 2026-07-14
### Breaking Changes
- **BLE connections to Meshtastic and MeshCore apps are no longer supported.** In `0.1.38-alpha`, Meshtastic and MeshCore companion apps can no longer connect to Trail Mate over BLE. If you need to keep using either app over BLE, **do not upgrade to this version**. This does not remove Trail Mate's on-device Meshtastic or MeshCore protocol modes.

### Added
- Added route-photo completion and preview for KML files exported by 2bulu.
- Significantly expanded Reticulum support with LXMF message sending and Reticulum calls, together with an initial subset of Nomad Network page-browsing capabilities.
- Added direct on-device configuration for MQTT and Meshtastic/MeshCore channel settings, without depending on a companion app for setup.
- Added configurable automatic replies for private Meshtastic, MeshCore, and Reticulum text messages: while the display is asleep, Trail Mate can immediately reply through the originating conversation using the user-defined message. Broadcast/group traffic, unverified Reticulum sources, and self-originated messages are excluded.


### Changed
- Reworked the built-in Emoji picker into a curated 324-item, nine-category catalogue for outdoor, radio, survival, rescue, people, and animal communication. The picker now uses fixed reusable controls with category selection and pagination instead of creating every emoji button at once; T-LoRa Pager uses text-only category cards plus wider keycap-style `B`/`N` paging and `C`/`Q` actions. The full-screen picker is also attached to the active screen so an Alt double-press screenshot captures the visible picker.

### Project Direction
- Trail Mate is moving toward fully phone-independent operation so devices can work on their own, and `0.1.38-alpha` is another step in that direction. Feedback on this direction, and on workflows that still require a phone, is welcome.

## [0.1.37-alpha] - 2026-07-04

### Changed
- Reworked the Settings Date/Time dialog into a bounded six-column datetime picker for year, month, day, hour, minute, and second, replacing free text input with scroll-only selection, explicit keyboard focus traversal, and automatic day-range clamping.

### Fixed
- Fixed Shutdown confirmation dialog CJK rendering by routing translated labels through the shared localized font fallback instead of forcing the ASCII Montserrat font.

## [0.1.36-alpha] - 2026-07-04

### Changed
- Restricted offline map tile layers to PNG tiles and removed the stale JPEG tile path so map source cache and documentation follow one supported tile format.
- Updated Pager shutdown and battery handling with an explicit power-off confirmation flow, translated shutdown notices, and more stable board ADC sampling for battery reporting.
- Made shared two-pane side filter panels vertically scrollable by default so Settings, Extensions, Contacts, Chat, and similar left-filter layouts remain reachable on small screens.
- Updated the promotional poster to show both supported Pager and T-Deck devices with the current Trail Mate UI instead of misleading generated hardware or screens.

### Fixed
- Fixed Pager power-off failure handling so USB-connected or PMU-unavailable shutdown attempts show an explicit failure notice instead of falling back to BOOT-only deep sleep.
- Fixed a Pager shutdown build regression caused by the board library depending on the UI i18n header.
- Fixed system notification font fallback and UTF-8 truncation so translated notifications and runtime notices can render CJK text without missing glyphs or split UTF-8 characters.
- Fixed the Pager BOOT long-press interaction so deep sleep starts as soon as the hold threshold is reached instead of waiting for button release.

## [0.1.35-alpha] - 2026-07-03
### Added
- Added a Meshtastic memory model specification covering stable BLE `FromRadio` slots, bounded queues, priority backpressure, MQTT downlink ownership, and hot-path allocation rules across nRF52, ESP32, and Linux.


### Changed
- Reworked Meshtastic BLE/MQTT bridge ownership around fixed published slots, fixed retransmit/pending tables, and shared backpressure rules so high-traffic BLE, MQTT, NodeInfo, and status flows no longer depend on unbounded dynamic containers.
- Updated GitHub Pages device copy for the 0.1.35-alpha nRF52 stabilization lane while keeping firmware download metadata generated by the Release/Pages workflows.

### Fixed
- Stabilized nRF52 Meshtastic BLE connection and reconnect behavior by aligning `FromRadio` notify/read liveness, stale-consume recovery, disconnect handling, and phone-visible session state with the app protocol flow.
- Restored nRF52 MQTT proxy and downlink behavior under the new memory model, keeping MQTT incoming/outgoing traffic, node metadata projection, and LoRa downlink policy functional under bounded pressure.
- Fixed T-Echo Lite GPS memory-corruption exposure by moving GPS/input runtime ownership out of runtime heap and removing transient peer-key vector allocation from the hot PKI/NodeInfo persistence path.
- Fixed resource-pack font loading so package update/reinstall paths refresh installed Chinese font resources correctly after pack operations.

## [0.1.34-alpha] - 2026-06-29
### Fixed
- Fixed the T-LoRa Pager LR1121 MeshCore node advert receive path by allowing RadioLib payload-present CRC mismatch reads to continue into the shared protocol parser instead of being dropped at the ESP radio task boundary.
- Restored the T-Echo Lite pager notification tone path with the shared ADPCM notification asset so message receive alerts can produce the expected audio feedback.
- Added LoRa receive state/IRQ diagnostics and first-failing-step logging for Pager LoRa radio configuration, making LR1121/SX1262 radio setup and receive anomalies visible in field logs.


### Changed
- Updated the GitHub Pages LR1121 install copy for the 0.1.34-alpha publishing lane while keeping firmware download metadata generated by the Release/Pages workflows.

## [0.1.33-alpha] - 2026-06-28
### Fixed
- Fixed Meshtastic Android/iOS peer identity projection for MQTT map-report nodes by decoding `MAP_REPORT_APP` through the shared node metadata path, preserving public-key state correctly, and preventing map reports from being retransmitted as normal LoRa downlink packets.
- Fixed route/track file loading so GPX and KML files are parsed through bounded streaming readers, including KML `<coordinates>` token parsing and active route overlay loading from `route_path`.

## [0.1.32-alpha] - 2026-06-27
### Added
- Added verified UF2 manual-flash release artifacts for both nRF52840 manual targets, `gat562_mesh_evb_pro` and `t-echo-lite`, checking the UF2 family ID `0xADA52840` while keeping `.hex` and `.zip` outputs as fallback/diagnostic assets alongside the existing ESP32 firmware targets.
- Added the T-Echo-Lite-KeyShield target to the project site device list so pre-release users can identify the matching manual firmware package from the latest GitHub Release.

### Fixed
- Fixed the T-LoRa Pager LR1121 MeshCore discovery/contact path from `459e8f52`, aligning MeshCore path descriptor handling and Android app contact/adverts projection with the shared protocol rules.
- Stabilized nRF52 local persistence for the pre-release by limiting node storage pressure, recovering UI settings writes when volatile cache files fill LittleFS, and adding a Settings action that clears local node/contact/message cache while preserving device and radio settings.
- Tightened the nRF52 Meshtastic BLE path for iOS by aligning fromRadio notification timing, restart state, and stale-consume recovery with the app protocol sequence.

## [0.1.31-alpha] - 2026-06-27
### Fixed
- Fixed [#64](https://github.com/vicliu624/trail-mate/issues/64) and [#41](https://github.com/vicliu624/trail-mate/issues/41).

## [0.1.30-alpha] - 2026-06-24
### Added
- Added support for the T-Echo Lite keyboard kit, including the nRF52 build target, e-paper mono UI adaptation, physical keypad input with EN/123/CN modes, T9-style Chinese candidate input, device I/O settings, 10px Fusion Pixel font support, and TrailMate logo sleep page
- Added nRF52 MeshCore discover entry points and protocol-aware chat send eligibility, so MC/MT channels and contacts can be browsed across protocol modes while sends stay limited to the active protocol
- Added issue #33 support for firmware-provided Emoji input/display candidates and issue #61 support for firmware-provided Symbol candidates, replacing the earlier extension-pack-only route with built-in candidate sets for constrained devices
- Added issue #57 Pager-only main-menu keyboard backlight control, including a bottom Help hint and Help-modal shortcut documentation for adjusting keyboard backlight from the Pager main menu
- Added the European Cyrillic Extended language bundle with Russian locale coverage, Cyrillic external font coverage, and a Russian Cyrillic keyboard IME; the package remains installable through Extensions with locale, font, and IME metadata in the Pages pack catalog
- Added Walkie monitor mode controls and top-bar modulation/listen indicators, including LoRa/FSK modulation badges and a persistent monitor state surfaced outside the Walkie page


### Changed
- Moved the protocol runtime closer to the 0.1.30 architecture target by routing MeshCore/Meshtastic UI actions, discover, text send, node actions, position sharing, traceroute/reply handling, and protocol effects through shared runtime/facade boundaries
- Reworked ESP shared SPI and storage ownership around a single SdFat/SdFs runtime, retaining the legacy `/chat/index.bin` plus `/chat/*.log` chat layout while removing the new `/chat_messages.bin` bypass and Arduino `SD.h`/`FS.h` storage backend paths
- Retired the unused T-LoRa Pager NFC runtime, build dependencies, board facts, power path, and shared-SPI CS participation, because NFC has no active product feature and was verified to interfere with reliable high-speed SD startup on the shared SPI bus
- Refined localization, IME, and language-pack handling for the 0.1.30 UI surface, including external CJK font-pack loading, manual language-load busy overlays, settings/team copy, and keyboard layout descriptor decoupling
- Updated Team, Contacts, and Nearby presentation to prefer short names and last-seen age semantics instead of online/offline labels where the protocol does not provide a reliable online state
- Reworked Pager Walkie layout and input focus behavior so monitor controls no longer trap the rotary encoder as a volume-only control
- Updated GitHub Pages release copy for the 0.1.30-alpha publishing lane, keeping firmware download metadata generated by the Release/Pages workflows instead of hand-editing asset URLs
- Reduced internal DRAM pressure by storing built-in Symbol/Emoji binfont data in flash rodata and lowering the localized font binding pool after verifying the active fallback chain

### Fixed
- Fixed issue #58 by allowing failed MT/MC chat bubbles to be tapped for retry, reusing the existing failed-message resend path without adding extra chat-screen controls
- Fixed issue #59 by burning down stale ESP map/SD/UI bypasses, bounding shared SPI display waits, moving blocking SD work behind runtime/storage boundaries, and validating the repaired T-LoRa Pager SD startup on real hardware
- Fixed issue #60 by implementing MeshCore V2 multibyte path handling so multibyte MeshCore paths no longer break route encoding/decoding on supported runtimes
- Fixed Symbol and Emoji candidate-page header overlap by replacing the flex-wrap candidate grid with explicit row/column placement and giving the full header width to the operation hint
- Fixed ESP map tile regressions by isolating tile IO from the UI loop, hardening tile identity and decoder cache handling, decoding satellite JPEG tiles from memory, and improving tile backpressure recovery without reintroducing UI stalls
- Fixed startup and SD persistence regressions so boot UI appears before slow storage work, CJK language packs load from the SD pack path, chat/contact/node/team persistence selects the SD backend when SD is available, and SD diagnostics report mount/read failures with actionable error codes
- Fixed send-result feedback regressions by routing chat delivery notices through the event-driven feedback runtime, including ESP and nRF environments
- Fixed random stale GPS/location handling by rejecting stale fixes before they can be shown as current map positions

### Removed
- Removed legacy presentation adapter include surfaces, Linux fallback UI paths, obsolete ESP GPS map page implementation, stale map tile cache lifetime code, and the legacy chat delivery bridge after their runtime replacements became active
- Removed Arduino SD / `FS.h` / `File` audio and storage bypasses from active ESP storage paths, keeping SD access on `SdRuntimeFile` / `SdRuntimeDir` and the shared SPI lock


## [0.1.29-alpha] - 2026-06-11
### Added
- Added shared ESP32 SD diagnostics for issue #54: Debug Logs now mirror startup/runtime notes to `/trailmate/debug/debug.log`, and ESP flash coredumps are exported to `/trailmate/coredumps/core-*.elf` on the next boot after a crash
- Added a Cardputer Zero Linux Wayland APPLaunch Debian package route for version `0.1.29-alpha`, including private app libdir layout, desktop `X-Zero-Display=wayland` metadata, Wayland runtime dependencies, icon install rules, CI package build, Docker `linux/arm64` package validation, the `linux-cardputer-zero-deb` preset, and an explicit fbdev/evdev debug fallback binary


### Changed
- Added M5Stack to the project acknowledgements for the Cardputer Zero hardware support that helped validate the Linux portable-device adaptation path
- Verified issue #53 against the current Team persistence path: Team membership remains persisted across reboot/crash recovery unless the user explicitly leaves or clears the team

## [0.1.28-alpha] - 2026-06-02

### Changed
- Updated GitHub Pages and release audit material for the #43-#51 repair pass, keeping release assets generated by the Pages/Release workflows instead of hand-editing download metadata
- Clarified the Web Flasher interface for the two T-LoRa Pager hardware variants so SX1262 and LR1121 users choose separate release targets and radio-specific install cards

### Fixed
- Fixed the MeshCore recovery path for issue #43 by splitting the broad failure into protocol-owned fixes for discovery, public group send, passive advert parsing, send diagnostics, and delivery status propagation; complete multi-channel MeshCore companion management remains outside this bug-fix slice
- Fixed Meshtastic Custom channel configuration for issue #44 so phone-side primary channel rename, short `AQ==` PSK handling, and manual LoRa settings update Meshtastic state instead of MeshCore aliases
- Fixed first-entry Chat and Contacts navigation for issue #46, including reachable Team filters, protocol-labelled MC/MT contact rows, bounded pager list labels, stable timestamps, and MeshCore incoming bubbles that no longer render as Me
- Fixed the Pager tracker/map SD crash path for issue #47 by serializing tracker SD transactions with the shared SPI lock and recorder mutex, and by correcting the SD-ready timeout conversion
- Fixed Pager map view persistence for issue #48 so zoom, center, pan, and follow mode survive leaving and re-entering the Map screen
- Fixed MeshCore Discover actions for issue #49 by sending the compatible zero-hop local `DISCOVER_REQ`, surfacing detailed send failures, restarting and synchronizing shared RX state after direct TX, keeping a scan RX guard window, and deferring adverts that could collide with discover responses
- Fixed MeshCore public group send for issue #50 by falling back to the MeshCore public PSK when no group channel key is configured, while still reporting malformed configured keys explicitly
- Fixed MeshCore Nearby naming for issue #51 by parsing signed advert appdata for name, node type, and location, and by giving discover responses stable MC placeholders until real advert or node-info names arrive

## [0.1.27-alpha] - 2026-05-30
### Added
- Added the T-LoRa Pager LR1121 build and release path for issue #40, including the CI build target, web flasher target, RF switch setup, and TCXO bring-up


### Changed
- Updated GitHub Pages device/install metadata so the LR1121 Pager appears as its own release target, while CI continues to validate `tlora_pager_lr1121` alongside the existing targets

### Fixed
- Fixed Team mode on T-LoRa Pager for issue #35 by finishing the Team runtime refactor and restoring the Team chat receive/send path
- Fixed daylight-saving time handling for issue #38 so displayed local time follows DST rules correctly
- Fixed spotty T-Deck battery reporting for issue #39 by smoothing the battery estimate path
- Fixed 64 GB SD card support for issue #42 by adding SDXC/exFAT-capable storage and routing map, Team, GPS, Settings, contact, node, route, tracker, and SSTV persistence through the shared SD runtime

## [0.1.26-alpha] - 2026-05-19
### Added
- Treated the cross-platform UI/runtime ownership work from this branch as one architecture-refactor feature, consolidating presentation models, UX packs, composition roots, runtime ownership boundaries, legacy-source burn-down guardrails, target metadata, and uConsole/GAT562 build ownership behind the post-refactor product architecture
- Added SD-card-only settings backup and restore under Settings > Advanced for issue #32, covering app/radio/chat/channel/GPS/map/privacy/APRS/UI settings without falling back to internal NVS when no SD card is present


### Changed
- Chat notification settings for issue #34 are now split into message alert enablement and contact alert scope, so users can separately control whether messages make sound and whether contact discovery alerts are disabled, contacts-only, or all alerts
- Locale bundles are updated to package version 1.1.1 for the 0.1.26-alpha UI surface, adding localized strings for the new chat alert controls and SD settings backup/restore prompts, status text, and confirmation modal

### Fixed
- Fixed Meshtastic channel sync behavior and added the MeshCore CN preset at 495.200 MHz, including channel response handling that avoids leaving the phone app stuck at 100% while preserving the device-side secondary-channel state

## [0.1.25-alpha] - 2026-05-11
### Added
- Added GPS receiver bring-up controls under Settings, including receiver baud, probe window, receiver profile, RXM/GNSS/NMEA init policy, and a non-blocking diagnostics view that reports transport, traffic, fix, satellite, and polling state
- Added shared GPS diagnostics and receiver-init runtime surfaces across ESP Arduino, ESP-IDF, Linux, and UI bindings so Settings and board runtimes can observe the same GPS health model
- Added Linux uConsole chat workspace smoke coverage and richer node detail views, including nearby-node conversations, radio metadata, position details, and a compact node-centered map preview
- Added a repository text-encoding integrity specification to make valid UTF-8 preservation an explicit engineering requirement for source, docs, packaging metadata, and generated text assets
- Added GPS Settings and ordinary T-Deck GPS UART/LoRa TX noise guides under `docs/devices/`, covering recommended receiver profiles, diagnostics interpretation, self-check procedures, TX-power correlation evidence, and hardware mitigation steps


### Changed
- T-Deck GPS startup now follows a runtime-owned GPS path: board boot no longer performs early GPS initialization, Auto baud preserves the 0.1.23-compatible 38400 baseline, and manual baud selections still run UART probes for diagnostics
- T-Deck default GPS Auto profile now restores the 0.1.23-style active RXM/GNSS/NMEA configuration path, while explicit passive or legacy receiver profiles can still skip those configuration writes
- GPS task logging now reports rolling UART traffic, post-config stream recovery, and health summaries without printing raw mutex pointer addresses that confuse PlatformIO's ESP32 exception decoder
- uConsole map overlays now expose richer node telemetry, MQTT visibility behavior, selectable node bubbles, last-seen ages, radio stats, altitude, and node-centered snapshots that can be reused by chat node detail panels

### Fixed
- Fixed the T-Deck GPS regression where current builds could report the GPS UART ready but never resume a steady NMEA stream under the default receiver-init policy
- Fixed GPS NMEA configuration coverage by enabling GSA alongside GGA/RMC/GSV for the internal receiver stream so skyplot and satellite diagnostics have the sentences they need
- Fixed GPS diagnostics blind spots by sampling initial NMEA sentences, capturing raw non-NMEA bytes before a valid stream appears, tracking recent character counts, and surfacing stalled-traffic states in UI diagnostics
- Fixed T-Deck GPS noise handling by separating UART read counts from parser progress and filtering non-NMEA bursts before they can be mistaken for valid receiver traffic
- Fixed Linux/uConsole node and map workspace gaps where nearby LoRa nodes, MQTT-hidden counts, packet log context, and selected node metadata were not consistently reflected in the desktop shell

## [0.1.24-alpha] - 2026-05-09
### Added
- Added a formal locale-pack release specification covering package versioning, `min_firmware_version`, archive layout, catalog generation, review/release gating, rollback behavior, and pre-release validation
- Added installable Arabic locale-pack assets with Noto Naskh Arabic font generation support and RTL metadata, kept in review status until native-language and input review are complete
- Added locale-pack structural validation tooling for key completeness, duplicate keys, empty translations, placeholder consistency, release English fallback detection, dependency checks, and fake IME rejection


### Changed
- Language packs are now versioned as 1.1.0 packages for the 0.1.24-alpha release, with `translation_status` metadata carried into `site/data/packs.json` so review packs can be cataloged without being selectable as runtime UI languages
- Simplified Chinese remains the only release locale with the real built-in Pinyin IME; Traditional Chinese (Taiwan), Arabic, Russian, European Latin, Japanese, and Korean packs are cataloged as display-only review bundles
- The GitHub Pages language-pack section now surfaces review/release status and package metadata so users can see language-pack maturity and update availability more clearly
- Localization and IME specifications now explicitly distinguish locale, region, script, font coverage, release quality, and real input backends, including the rule that Taiwan Traditional Chinese must not default to Pinyin

### Fixed
- Filled missing and English-fallback entries across non-English locale `strings.tsv` files while keeping non-native-reviewed packages gated as `review` instead of pretending they are release quality
- Removed fake IME manifests and locale `ime_pack` dependencies for scripts without implemented runtime input engines, preventing Settings from exposing unusable input methods
- Runtime locale discovery now skips non-release `translation_status` values, preserving English fallback and preventing incomplete review packs from entering the normal language picker

## [0.1.23-alpha] - 2026-05-01
### Added
- Cardputer Zero and Linux simulator foundations, including shared Linux runtime wiring, shell boot paths, simulator pages, and an adaptation specification for the final device shape
- An installable Russian locale bundle with a Cyrillic external font pack, package catalog metadata, and attribution for @polarikus' contributed translations
- A GPS runtime specification under `docs/specs/`, defining user enable intent, receiver mode, satellite mask, power policy, internal NMEA, external NMEA export, transport readiness, and observation state as separate concepts


### Changed
- GPS configuration semantics are now unified across ESP Arduino, ESP-IDF, Linux, nRF52/GAT562, phone bridge, and UI settings: `gps_enabled` is the enable flag, while `gps_mode`, `gps_sat_mask`, `gps_strategy`, and external NMEA export keep their own meanings
- Board-level GPS startup now treats UART transport readiness separately from UBX receiver probing on T-Deck, T-Deck Pro, and T-LoRa Pager, with each board owning the correct UART teardown path
- The Linux simulator/dev-container path now mounts and exposes the repository root more consistently, making shared runtime work easier to exercise from the Linux shell

### Fixed
- Fixed the T-Watch S3 release build for watch Morse input by passing board-owned PDM microphone pins through build-time definitions
- Fixed the GPS no-lock regression where internal receiver NMEA could be disabled by external export/privacy-style settings, leaving maps and skyplot without parsed GPS data even when GPS was enabled
- Fixed startup cases where T-Deck GPS was reported as failed because a short UBX identity probe timed out, preventing the runtime from continuously reading NMEA while the receiver warmed up
- Fixed T-Deck display startup and brightness handling, reducing early backlight/display initialization regressions on that board family
- Fixed Energy Sweep sampling so it uses instant RSSI and only owns the LoRa lock while scanning, avoiding stale measurements and unnecessary radio contention outside active sweeps

## [0.1.22-alpha] - 2026-04-22
### Added
- A shared map viewport component plus UI/UX specs for firmware visual style, Node Info page behavior, and map-layer popup semantics, so map-backed screens now share one explicit contract instead of parallel page-local implementations
- Formal localization and locale-pack specifications in Chinese, covering runtime responsibilities, install layout, package/version boundaries, and the evolution rules between firmware and language packs


### Changed
- The Node Info page was rebuilt around the shared map viewport, with bottom-center layer switching, node-centered zoom semantics, draggable map browsing, right-aligned link readouts, and a content-bounded translucent backdrop for readability over live map tiles
- Shared SPI bus ownership is now modeled explicitly as shared_spi_lock, with naming, guard usage, and supporting guidance aligned across display, map tiles, track recording, SSTV, and USB mass-storage paths
- Shared localization/runtime surfaces were refined further across settings, contacts, GPS, and package/runtime plumbing, and the Simplified Chinese language pack was updated to cover the new map-layer and UI copy

### Fixed
- Fixed Node Info regressions where tiles could fail to render, map dragging/layer switching diverged from the main map page, and right-side readouts became hard to read or inconsistent over map imagery
- Fixed shared-SPI contention during USB mass-storage activity by locking SD access and pausing radio tasks while the USB MSC backend owns the storage path, reducing display corruption and tear-like artifacts on ESP boards
- Fixed supporting protocol/runtime helper paths so node detail, map-layer UI, and localization-driven surfaces behave more predictably across mixed protocol and device states

## [0.1.21-alpha] - 2026-04-21

### Changed
- Extensions app surfaces now participate fully in the shared localization pipeline, covering the menu label/description, top bar title, package list/detail headings, and install/uninstall status copy across the supported language packs
- Pager home footer layout now uses a split alignment model, keeping node ID anchored on the left while RAM and PSRAM usage stay grouped on the right, with the app description lifted above the footer to preserve readability

### Fixed
- Fixed localized menu refresh paths that were still using raw text updates, which left the Extensions icon label and focused description untranslated after language changes
- Fixed untranslated and inconsistent copy throughout the Extensions runtime, including compatibility badges, package action states, Wi-Fi status text, and package install/uninstall progress notifications
- Fixed pager footer overlap where the bottom bar could cover icon descriptions, especially on the focused-card layout

## [0.1.20-alpha] - 2026-04-21
### Added
- On-device firmware update controls under Settings > Advanced, including update check/install actions, progress overlays, and release metadata sourced from GitHub Pages
- Pages/release packaging now publishes OTA-ready binaries, Web Flasher images, and language-pack assets together so the project site can drive both browser flashing and in-device updates
- Flash-backed language-pack storage on ESP, allowing installed locale/font packages to load from internal flash without depending on SD card presence


### Changed
- Startup flow now surfaces UI earlier, shows boot progress/log feedback, and keeps long-running locale/package operations behind a shared busy overlay that blocks input and suppresses sleep while work is active
- Firmware version reporting now prefers the injected Trail Mate version from the build pipeline instead of exposing the raw ESP-IDF app version, and CI validates OTA slot size before publishing artifacts
- Chinese locale/font handling was reworked around tiered packs, IME availability controls, and flash-first discovery so startup and runtime font loads are faster and more predictable

### Fixed
- Fixed low-memory Wi-Fi bring-up for settings/package/update flows by adding heap diagnostics, BLE pause-and-retry fallback, and lighter initialization settings when needed
- Fixed incorrect esp-idf:v4.4.7 ... version strings in the UI, and kept update/version surfaces aligned with the actual firmware release being built
- Fixed several UX regressions around locale install/load and multilingual input, including missing loading feedback, sleep during blocking tasks, and Chinese IME/candidate layout issues

## [0.1.19-alpha] - 2026-04-21
### Added
- Installable multilingual language bundles delivered through the new Extensions flow, with initial external locale/font packages for European Latin Extended, Simplified Chinese, Traditional Chinese, Japanese, and Korean
- A Pages-side pack catalog and build pipeline that publishes SD-installable locale bundles from `packs/` into `site/data/packs.json` and packaged zip assets
- Shared Wi-Fi runtime support for the ESP targets, plus Extensions app entry points and top-bar/status exposure needed for on-device package downloads


### Changed
- Localization is now pack-driven instead of being limited to a small built-in language set, with runtime locale/font/IME registration, SD discovery, and memory-profile-aware gating for constrained devices
- GitHub Pages now presents Trail Mate as a multilingual firmware project and surfaces the installable language-pack catalog alongside the existing firmware download path
- Meshtastic direct-message handling and send-status flow were unified around upstream PKI-style behavior, while outdated settings-side PKI toggle/plumbing was removed and Chat/Contacts compose handling now stays on one path

### Fixed
- Fixed false `No response` send results after valid Meshtastic acknowledgements or response packets had already arrived, so UI send state now follows the actual protocol outcome more closely
- Fixed blind spots in Meshtastic diagnostics by logging decode, key verification, routing, and drop reasons more explicitly, making packet handling failures and dedup behavior easier to trace
- Fixed Extensions/package-download reliability under tight internal-RAM conditions by steering large TLS allocations toward PSRAM and tightening Wi-Fi/runtime state reporting in the UI

## [0.1.18-alpha] - 2026-04-20
### Added
- A shared bottom air-status footer for Chat and Contacts, showing the active protocol, frequency, bandwidth, spreading factor, and coding rate without overloading the page header
- A System > Factory Reset action that clears persisted app/settings namespaces and local team state before rebooting the device
- Shared ESP settings-store diagnostics and storage-key alias handling, so persisted reads and writes are now traceable through one platform-level path on both Arduino and ESP-IDF


### Changed
- ESP configuration persistence is now unified more cleanly around the shared settings-store and AppConfig flow, replacing scattered direct Preferences access for UI/system values such as language, BLE, timezone, and screen timeout
- Menu and page terminology were tightened to shorter labels such as PC Link and USB Disk, while Contacts now keeps its top bar focused on the page title and moves air-interface details to the new footer
- Shared UI boot paths now reload the persisted display language before shell/menu construction, keeping startup presentation aligned with the saved language choice

### Fixed
- Fixed display-language persistence on ESP so the saved language now survives reboot and is applied correctly during startup instead of falling back to English
- Fixed multiple Chinese-rendering issues across Settings, Chat, Sky Plot, and the top bar by replacing ambiguous full-width punctuation where needed, applying localized fonts more carefully, and forcing ASCII rendering for battery/status chrome text
- Fixed stale configuration fallbacks and dead state around legacy settings migration, region-reset cleanup, and needs_restart, reducing mismatch risk between runtime state, reset actions, and persisted storage

## [0.1.17-alpha] - 2026-04-19
### Added
- Runtime English and Chinese localization across the shared UI shell, covering menus, dashboard surfaces, chat and contacts flows, settings labels, and reusable localization helpers for future screens
- A GitHub Pages project site under `site/`, with firmware-aligned branding, feature walkthrough sections backed by real screenshots, and repository-linked architecture/device entry points
- Browser-based Web Flasher support for the ESP32-S3 targets, including merged release images, release-driven manifests, and Pages-side asset preparation scripts so supported boards can install directly from the project site


### Changed
- The ESP `LXMF` runtime was refactored further toward Reticulum alignment, separating runtime state more clearly, tightening protocol wiring, and documenting the device-mode/runtime direction in dedicated architecture notes
- Pages and release automation now prepare web-ready firmware assets alongside the existing PlatformIO outputs, so release artifacts can drive both download-based flashing and the future project-site install path
- The project homepage visual system now follows the firmware's warm amber palette instead of a generic web palette, keeping the web experience closer to the embedded product identity

### Fixed
- Meshtastic send and acknowledgement recovery on T-Deck were hardened, reducing stuck outbound-message states and improving recovery after failed or delayed ack paths
- Multiple shared UI screens were adjusted to behave correctly under localization-aware labels and updated layout rules, reducing text overflow and presentation mismatches across devices
- GitHub Pages asset preparation now preserves generated-site structure more safely while refreshing manifests and showcase assets, avoiding accidental cleanup of placeholder files needed by the repo layout

## [0.1.16-alpha] - 2026-04-17
### Added
- Native `LXMF` protocol mode on ESP over Reticulum/RNode-compatible wire layers, including device-side identity, announce/discovery handling, opportunistic text and app-data delivery, and local contact/chat integration
- `RNode Bridge` USB CDC KISS modem mode for external Reticulum hosts, keeping host-controlled modem bridging separate from native on-device `LXMF` messaging
- Dedicated RNode radio profile defaults/persistence and a new Reticulum/LXMF device-mode architecture document for the ESP shared shell path


### Changed
- Settings, Contacts, Chat, and the PC link page now understand `LXMF` and `RNode Bridge`, expose protocol-aware labels/capability gating, and stop assuming every protocol behaves like Meshtastic or MeshCore
- Team and HostLink app-data flows now propagate `want_response` separately from delivery-ack semantics, so business-level response intent is preserved across device-side Team sends and host-triggered app-data routing
- Team and protocol runtime wiring now use dedicated RNode/LXMF mesh settings where appropriate, including bridge-aware hostlink runtime switching and RNode-backed protocol factory selection

### Fixed
- Team app-data broadcast behavior now falls back to per-member unicast when a backend lacks native broadcast app-data support, improving cross-protocol team delivery on constrained transports
- Team position events now also publish node-position updates, keeping map/contact location state aligned with Team traffic instead of leaving position changes trapped inside Team-only events
- Reticulum token encryption/decryption now uses dynamic storage instead of fixed-size buffers, reducing MTU-related overflow risk in the shared wire helpers

## [0.1.15-alpha] - 2026-04-16
### Added
- Reusable T-Deck info_card component for two-pane detail rows, now shared by Settings and chat/contact list items on touch-first layouts


### Changed
- Device capability gating now uses stable runtime capability checks such as gps_supported() as the single source of truth, so Map and Sky Plot menu visibility no longer depends on transient GPS readiness or settings-side position strategy
- T-Deck two-pane pages now share consistent filter-button sizing and second-column behavior across Settings, Chat, and Contacts, with chat list presentation logic moved out of behavior code and into reusable layout/component layers
- Chat conversation headers on T-Deck now compact long labels and keep info-card headers on a single line for more stable list-card presentation

### Fixed
- Fixed the Map app visibility regression triggered by settings-side position strategy, so GPS-capable devices no longer lose the Map icon during startup or configuration changes
- Fixed redundant second-column Back affordances and aligned chat-list selection plumbing across touch and watch-specific implementations, reducing cross-device UI inconsistencies
- Fixed CI-facing build regressions for lilygo_twatch_s3 and gat562_mesh_evb_pro; local verification now passes the workflow's four PlatformIO targets and the clang-format check

## [0.1.14-alpha] - 2026-04-13
### Added
- GAT562 Mesh EVB Pro relay-edition firmware artifacts are now built and published in CI, adding dedicated release outputs for the nRF52 relay target
- README (EN/CN) now explicitly documents the Trail-mate relay-edition role of GAT562 Mesh EVB Pro, alongside the actual repository targets and current build flows


### Changed
- Build and target documentation now distinguishes long-term hardware direction from the targets that are actually wired in the repository today, across PlatformIO, ESP-IDF, and the nRF52 relay path
- GAT562 development and diagnostics are aligned further with the device's much tighter RAM budget, with clearer relay-oriented positioning in docs and CI outputs

### Fixed
- Meshtastic BLE receive handling on nRF52/GAT562 was hardened to prevent screen-saver time and GPS state resets when NodeInfo or Text traffic arrived, with safer payload bounds and richer diagnostics
- Meshtastic NodeInfo persistence now survives reboot correctly by fixing node-store serialization size and by persisting received identity fields instead of dropping them on the floor
- Position data embedded in NodeInfo is now persisted like standalone position updates, so node locations no longer disappear after restart
- nRF52 flash/free-space reporting and related settings-info diagnostics are corrected for GAT562, avoiding misleading `0/243k`-style memory displays

## [0.1.13-alpha] - 2026-03-06
### Added
- Meshtastic BLE pairing popup with passkey display for iOS secure-pairing flow
- BLE-side MQTT client proxy bridge for `ToRadio/FromRadio.mqttClientProxyMessage`, including topic forwarding into the Meshtastic adapter
- Native T-Deck I2C keyboard path with text input, backspace, enter, and keyboard backlight control


### Changed
- Meshtastic BLE behavior aligned further with iOS app expectations, including pairing UX, config sync, queue/status delivery, MQTT proxy forwarding, and additional admin-tag coverage
- T-Deck screen-saver interaction now uses keyboard-first entry with `Press SPACE to enter main menu`, while preserving Pager touch-wake behavior
- App self node-id derivation and node/contact event routing are aligned more closely with Meshtastic-style canonical IDs, with SD-backed chat storage preferred when available

### Fixed
- `OK to MQTT` and `ignore MQTT` channel settings now persist and read back correctly in the iOS app
- Direct-message acknowledgements and BLE-side status propagation now clear stale `Waiting to be acknowledged` states more reliably
- Incoming chat messages now wrap to conversation bubble width instead of overflowing off-screen
- T-Deck keyboard support is restored in the dedicated environment, and Pager screen wake/menu-entry regression is resolved
- Contact deletion/store synchronization improved, including `remove_by_nodenum` admin handling and persisted node/contact updates

## [0.1.12-alpha] - 2026-02-22
### Added
- MeshCore identity module with Ed25519 + ECDH key material handling and persistent peer-key storage path
- Independent `team::proto` payloads for Team Position and Team Waypoint, decoupled from Meshtastic envelope path
- Contacts "Discover" mode with inline actions (`Scan Local`, `Send ID Local`, `Send ID Broadcast`, `Cancel`) and MeshCore-only guard


### Changed
- MeshCore adapter behavior aligned further with native protocol semantics, including app-data unicast `dest`, discover/advert/control handling, PATH/trace/multipart plumbing, and route-quality updates
- Chat/Contacts protocol presentation is now protocol-aware to reduce cross-protocol reply confusion while keeping mixed history visible for review
- README (EN/CN) updated to explicitly state Meshtastic + MeshCore dual-protocol compatibility

### Fixed
- Discovery filter-mode focus/highlight consistency in Contacts list navigation
- MeshCore peer-key load/persistence wiring and related decrypt-failure diagnostics for interop debugging

## [0.1.11-alpha] - 2026-02-21
### Added
- Energy Sweep app in main menu (Spectrum icon) with Sub-GHz RSSI bars, cursor metrics, best-channel recommendation, and STOP/SCAN + AUTO controls


### Changed
- Energy Sweep sweep band now follows current radio region settings (Meshtastic region or MeshCore region preset), so sweep bounds automatically match configured regional limits

### Planned
- Settings screens (radio, channels, device)
- Flash-backed chat storage and message persistence
- Meshtastic encryption (AES/PSK) and broader protocol compatibility
- Real LoRa field tests, performance, and power tuning


## [0.1.10-alpha] - 2026-02-17
### Added
- Offline map layer menu with three base map sources (OSM/Terrain/Satellite) and contour overlay toggle
- Map screenshots and SD card structure guide for map tiles
- MeshCore region preset table and preset-driven parameter mapping (freq/bw/sf/cr)
- LILYGO T-Watch S3 target support (board definition, variant, driver integration, device docs)
- Watch-side UI modules and Morse input engine scaffolding


### Changed
- Settings now show protocol-specific items for Meshtastic vs MeshCore, including MeshCore-only network/channel tuning items
- Mesh configuration model split into `meshtastic_config` and `meshcore_config`, with independent persistence/apply paths
- Meshtastic radio apply path now supports preset/manual parameters, channel slot, frequency offset, override frequency, and region-aware clamping
- MeshCore adapter upgraded from simplified compatibility logic to REQ/RESPONSE/PATH + direct routing behavior with repeat/multi-ACK controls
- CI adds clang-format-14 format check and versioned firmware artifact naming for build outputs

### Fixed
- Region changes now cascade into related radio constraints (e.g. TX power limits and frequency bounds) and re-apply immediately
- Hidden Relay toggle in settings when real forwarding path is not wired to avoid false switch semantics
- GPS motion power policy corrected: outside team mode, prolonged idle can suspend GPS and motion resumes it
- Walkie service config wiring/build guards aligned with updated mesh config fields

## [0.1.9-alpha] - 2026-02-12
### Added
- SSTV audio receive and on-device image decoding
- SSTV UI page and decode result screenshots

### Fixed
- Chat compose input handling (IME/backspace)


## [0.1.8-alpha] - 2026-02-08
### Added
- GNSS Sky Plot page with satellite sky view, status table, and SNR/constellation legends
- GNSS satellite parsing (GSV/GSA) and snapshot API for UI consumption

### Changed
- Sky Plot UI layout refinements and status table sorting
- Sky Plot top bar now shows USE/HDOP/FIX summary

### Fixed
- Sky Plot backspace behavior now follows TopBar back action

## [0.1.7-alpha] - 2026-02-08
### Added
- APRS/HostLink expansions (RX metadata TLV, APRS config keys, iGate data coverage)
- ESP-NOW team pairing flow with key distribution and status rebroadcast
- GPS jitter filtering and improved tracker recording
- Node Info page and status bar assets

### Changed
- Settings categories regrouped (Chat/GPS) without behavior changes
- Team UI/UX updates and simplified pairing flow
- Map tile loading prioritization and tracker dialog layout refinements

### Fixed
- LoRa TX power handling on SX1262 (14 dBm no longer silent)
- NodeStore/NVS diagnostics and persistence robustness

## [0.1.6-alpha] - 2026-02-06
### Added
- Walkie talkie feature (FSK + Codec2)

## [0.1.5-alpha] - 2026-02-05
### Added
- Data Exchange (PC Link) mode with USB CDC-ACM transport and HostLink protocol (codec/service/bridge)
- HostLink protocol spec doc with frame/command/event details and app payload coverage
- HostLink EV_APP_DATA forwarding for Team apps and non-team app payloads (plaintext)
- HostLink EV_GPS + CMD_GET_GPS support
- Team track sampler + TEAM_TRACK_APP protocol for batched team track points
- GPS route overlay (KML) and route focus support
- GPS altitude display at bottom center
- Tracker UI overhaul with record/route modes, list pagination, and action panel
- Menu icon asset for PC Link (RF)


### Changed
- App context wiring to forward events into HostLink bridge
- Settings wiring to AppConfig + GPS/Map/Chat/Network/Privacy hooks
- GPS service abstraction + tracker recorder improvements
- USB product name set to "TRAIL MATE"

### Fixed
- HostLink now forwards NODEINFO_APP via EV_APP_DATA (with fallback decoding guidance)

## [0.1.4-alpha] - 2026-02-02
### Added
- T-Deck board support and new build env
- GPS tracker/track recorder with Tracker screen and assets
- Team NFC support, team chat protocol, and team UI store
- Toast widget and LVGL lifetime notes


### Changed
- UI lifecycle refactor with compose flow
- GPS page layout/styles and map overlay refactor
- UI controller and app screen wiring adjustments

### Fixed
- Meshtastic node id handling error
- UI glitches across chat/contacts/gps/team

## [0.1.3-alpha] - 2026-01-23
### Added
- Team management stack (domain/protocol/service) and Team screen
- Team docs/assets
- Send-status UI and message preview truncation


### Changed
- Mesh persistence and PKI handling (node store/backoff)
- Chat storage improvements across RAM/Flash/log stores
- Diagnostics and timestamp utilities

### Fixed
- Messaging reliability across reboots
- UI timezone offset application

## [0.1.2-alpha] - 2026-01-21
### Added
- Meshtastic routing ACK decoding with reason mapping, ack timeouts, and richer RX/TX diagnostics
- PKI peer key persistence and per-node PKI backoff after NO_CHANNEL/UNKNOWN_PUBKEY
- Time helpers for chat timestamps


### Changed
- Chat app switching now calls exit callbacks and clears active app on menu return
- Contacts compose flow supports broadcast channels and avoids UI tree mismatch when chat container exists
- UI timestamp display applies stored timezone offset

### Fixed
- Chat screen cleanup safety on repeated enter/exit to prevent black-screen stacking

## [0.1.1-alpha] - 2026-01-20
### Added
- Mesh protocol factory with explicit Meshtastic/MeshCore selection
- Settings actions: reset mesh params, reset node DB, clear message DB
- Meshtastic region utilities and docs (region table, frequency helpers, protocol docs)


### Changed
- Settings: protocol/region changes now restart immediately
- Config sync: protocol/region/preset and PSK wired to AppConfig/applyConfig
- UI assets and README screenshots refreshed

### Fixed
- CN region frequency calculation alignment for LongFast

## [0.1.0-alpha] - 2026-01-18
### Added
- Offline GPS mapping with fixed north-up orientation, zoom levels, and breadcrumb trails
- LoRa text chat compatible with Meshtastic (simplified codec + dedup)
- Chat architecture layers (domain/usecase/ports/infra), RAM store, and mock adapter
- Chat UI: channel list + conversation screens, integrated into the main UI
- App context/config, event bus, ring buffer, and task scaffolding
- English/Chinese README plus supporting design docs

### Notes
- This is an initial, pre-release build; several features are intentionally incomplete.
