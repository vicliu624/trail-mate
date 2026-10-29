# 🗺️ Trail Mate

<p align="center">
  <a href="https://github.com/vicliu624/trail-mate/stargazers"><img src="https://img.shields.io/github/stars/vicliu624/trail-mate?style=flat-square&amp;label=stars&amp;color=4285f4" alt="GitHub stars" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-orange?style=flat-square" alt="License: AGPL-3.0" /></a>
  <a href="https://github.com/vicliu624/trail-mate/releases"><img src="https://img.shields.io/github/v/release/vicliu624/trail-mate?sort=semver&amp;style=flat-square&amp;label=version&amp;color=4285f4" alt="Latest stable release" /></a>
  <a href="https://github.com/vicliu624/trail-mate/releases"><img src="https://img.shields.io/github/downloads/vicliu624/trail-mate/total?style=flat-square&amp;label=downloads&amp;color=65b821" alt="Total GitHub release asset downloads" /></a>
  <br />
  <a href="https://github.com/vicliu624/trail-mate/actions/workflows/ci.yml?query=branch%3Amain"><img src="https://img.shields.io/github/actions/workflow/status/vicliu624/trail-mate/ci.yml?branch=main&amp;style=flat-square&amp;label=CI" alt="CI status on main" /></a>
  <a href="#-current-device-support--development-status"><img src="https://img.shields.io/badge/platforms-ESP32%20%7C%20nRF52%20%7C%20Linux-00897b?style=flat-square" alt="Platforms: ESP32, nRF52, and Linux" /></a>
  <a href="https://discord.gg/shkueG4zfc"><img src="https://img.shields.io/badge/Discord-join%20community-5865F2?style=flat-square&amp;logo=discord&amp;logoColor=white" alt="Join the Trail Mate Discord community" /></a>
</p>

![trail mate page](docs/images/ChatGPTImage.png)

> Your identity belongs to you. Keep private records local. Choose what to share.

**Trail Mate is a privacy-focused handheld for decentralized communication and offline navigation.** It reduces the personal information a platform needs to hold: no real-name, phone or email account is required for the Web tools, and device-held keys prove authorship. Contacts, conversations and unshared tracks remain on hardware and storage you control.

Communicate over compatible Meshtastic, MeshCore or Reticulum/LXMF networks, carry offline maps and record your journey locally. Choose independent gateways and directories, and publish only the trails or Geocaches you select. A service can help you communicate without becoming the owner of your identity and complete private history. Features depend on the board; the project includes ESP32, nRF52 and Linux implementations.

[English](README.md) | [Documentation](https://github.com/vicliu624/trail-mate/wiki) | [Releases](https://github.com/vicliu624/trail-mate/releases) | [Join Discord](https://discord.gg/shkueG4zfc)

**1.0.1 fixes USB browser management for Pager and T-Deck.** Install the matching
new firmware; a website update cannot add the missing USB service to 1.0.0.
Get the board-specific files and
[release notes](https://github.com/vicliu624/trail-mate/releases/tag/v1.0.1), or
use the [browser installer](https://vicliu624.github.io/trail-mate/). Check the
device support table below before choosing an image.

## The device is your account

Connect a supported Trail Mate device over USB to prove your author identity in
the browser. There is no website account or password to register. This avoids
requiring a platform profile with your real name, phone number or email. The device
holds its identity and signs publications; the browser provides convenient
editing, maps, file handling, and access to Reticulum through the computer's
network. These author operations do not require changing the device's selected
chat protocol or routing Internet traffic through its LoRa radio.

| Part of the system | What it does |
| --- | --- |
| **Firmware** | Owns identity, records local tracks, reads offline maps, communicates in the field, and signs authorized maintenance operations. |
| **Web** | Edits device configuration through Web Serial, maintains Geocaching publications, and imports, edits, browses and shares KML trails. |
| **Trail Mate Center** | Prepares offline map regions, provides a desktop dashboard, and handles HostLink data, replay, export and propagation tools. |
| **Reticulum services** | Independently operated gateways and directories carry requests and index signed shared content. They can run on different nodes. |

Browsing shared content does not require the author's device. Creating, updating
or archiving under your device identity requires its authorization. Browser USB
access requires a browser that implements Web Serial and a secure origin, such
as the HTTPS site or localhost. Browser support differs; the page reports
availability before asking you to choose a serial port.

### Ownership and privacy

Your identity and private local records do not have to live in one platform's
account database. You decide which caches and tracks to publish, keep portable
files, and can use independently operated directories. Author signatures allow
receivers to check who created a publication without trusting the directory to
invent that identity. A single operator is not required to control the entire
system or collect every user's complete activity history.

**Public sharing is still public.** A directory or recipient can store and
analyse the routes, coordinates, timestamps, author identity and metadata you
publish. Repeated use of one public author identity can link those publications.
Gateways can observe connection metadata, and external map providers can see
map requests. Archiving changes the current published state; it cannot erase
copies already received by others. Decentralization gives users choices and
reduces dependence on one operator; it does not make published location data
secret or prevent every form of behavioural analysis.

The [1.0.1 delivery record](docs/releases/1.0.1.md) keeps the build and acceptance
evidence separate from device-support claims. Use [Releases](https://github.com/vicliu624/trail-mate/releases)
for installable images and check the board table before choosing a target.

---

## ⚙️ Engineering Snapshot

- **Platforms:** ESP32 and nRF52 firmware, Linux device applications, and a Linux simulator
- **Networking:** Meshtastic · MeshCore · Reticulum/LXMF, with support depending on the target
- **Hardware:** keyboard handhelds, touch navigation terminals, and low-power message devices
- **Toolchains:** PlatformIO / Arduino · ESP-IDF · CMake for Linux
- **Validation:** multi-target CI builds, native regression tests, LVGL integration tests, architecture boundary checks, stack and runtime-budget checks, and OTA-size validation
- **Releases:** automated firmware packaging, web-flasher images for selected targets, nRF52 UF2 images, and tagged-release debug symbols
- **Core capabilities:** GNSS · offline maps · LoRa messaging · local storage · on-device team awareness
- **License:** [AGPL-3.0](LICENSE)

Capabilities vary with memory, peripherals, input hardware, and port maturity. The simplified nRF52 targets currently provide Meshtastic / MeshCore messaging rather than the full feature set. See the device table below for current status.

Engineering evidence: [CI and release workflow](.github/workflows/ci.yml) · [shared protocol tests](modules/core_chat/tests) · [repository architecture](docs/specification/REPOSITORY_LAYOUT_ARCHITECTURE_SPEC.md) · [version history](CHANGELOG.md).

## 🚀 Try Trail Mate

### Plan and follow a route offline

Prepare a region in Trail Mate Center, then put its TMAP files and resources on
the SD card. Include the area's routing graph and POI search data as well as
the map styles you want to see. OSM, terrain and satellite views, numeric terrain
data and contour overlays each use the data included in those packages.

On a supported PSRAM-capable device, open **Tracker → Menu → Nav → New**.
Start from your current GNSS position or a place you find in the offline search,
choose a destination, and calculate a walking route on the device. Save it as a
`.tmroute` plan for another day, or open its preview and start following it in Map.
Calculating and following the plan do not need an Internet connection.

Map shows progress, remaining distance, geometric turn hints, deviation and
arrival. Manual or automatic replanning can calculate a new path from your
current position; cancellation or a failed attempt keeps the saved plan intact.
Live guidance is presented while Map is open. The graph must cover the requested
trip, and route calculation has an explicit memory/search budget. A raster-only
map cannot supply a path by itself.

You can also bring your own KML routes, record local tracks, inspect elevation
profiles and route images, search for places, save waypoints, and see team
members or radio contacts on the map. The [offline navigation guide](https://github.com/vicliu624/trail-mate/wiki/24.-Offline-Navigation-and-Map-Controls)
explains installation and controls. Wio L2 navigation, presentation and touch
interaction have been accepted on hardware; other targets retain their own
validation status.

### Widgets and music for everyday use

Pager, T-Deck and Wio L2 share an adaptive app grid. The widget screen beside it
shows one widget at a time: mesh status, GNSS, recent activity, Agenda, Music,
Quick controls, and Compass when the device has a magnetometer. P4 keeps the
same idea in a larger layout, with pages in its right-hand dashboard.

On T-Deck and Wio L2, swipe right from the main menu to open the widget screen and
left to return; swipe up/down to change widgets. **On Pager, press V to switch
between the main menu and widgets.** W/A/S/D and the wheel select app icons;
inside widgets, W/S or the wheel changes widgets, A/D selects a control and
Enter or a wheel press activates it. H opens contextual Help. The footer keeps
the V shortcut visible, and V also works with launcher Help open. P4's right-hand
dashboard uses horizontal paging for additional widgets.

Put MP3 files under **`/Music`** on the SD card; subfolders are scanned too.
Music opens Now Playing when a session is active, or the library when idle.
Browse songs, artists, albums and folders, create your own playlists, and use
repeat-all, repeat-one or shuffle. Track information includes the available ID3
fields, album art and embedded lyrics; matching `.lrc` files can sit beside a
song. Chinese titles and lyrics use the same installed content font as Chat.

The Music widget offers transport controls, and Quick controls provides Wi-Fi,
LoRa, GPS, volume and brightness through the existing device services. Only the
visible widget retains its view resources. Playback uses a bounded PSRAM PCM
buffer and pauses when voice or intercom takes the audio output. Audio hardware
and playback quality still depend on the target; the support table applies.

![Music on Pager](docs/images/music/pager-now-playing.png)

![Quick controls on Pager](docs/images/music/pager-quick-controls.png)

*Actual native LVGL previews with sample metadata, artwork and simulated service
state. The [Music and widgets guide](https://github.com/vicliu624/trail-mate/wiki/25.-Music,-Widgets-and-Daily-Controls)
includes file placement and keyboard controls.*

### Browser device configuration

The [website](https://vicliu624.github.io/trail-mate/?lang=en) provides direct
**Device management**, **Tracks** and **Geocaching** links on every page. The
highlighted link identifies the current tool. Tool links and the home logo keep
your selected interface language; **Flash** opens firmware installation.
GitHub and Wiki are in **Resources**. The same tool links remain visible on
narrow screens, with task cards on the homepage.

![English website navigation and browser tools](docs/images/release-1.0.1/web-home-tools.png)

*Current homepage and tool navigation, captured in English from the rebuilt
Web sources. Device connection and publication still require the appropriate
hardware, permissions and reachable directory.*

Open the [Device page](https://vicliu624.github.io/trail-mate/device/), connect
the device and choose its USB port. The page verifies the author and configuration
snapshot before allowing edits. Settings are grouped by purpose and searchable;
enumerations, percentages, durations, frequencies, masks and collections use
appropriate controls with explanations rather than unexplained numeric fields.

Review the changed values before saving. The page reads back the device result
and supports configuration backup/restore and explicit identity maintenance.
Identity backups are sensitive: normal author operations use device-side signing
and do not require exposing the private key in the browser.

Keep the device's SD card installed for configuration exchange and stored-content
maintenance. Device author proof itself is independent of the card; a successful
identity check does not imply that its file storage is available.

![Web configuration percentage control](docs/images/release-1.0.1/web-config-percent.png)

![English device management workspace](docs/images/release-1.0.1/web-device-management.png)

*Current English browser configuration controls with simulated-device data. The screenshot
illustrates readable percentage input, not a physical-device acceptance result.*

### Decentralized Geocaching

Create and publish signed caches over Reticulum, browse independently operated directories, and download cache details for offline maps. The [live web map](https://vicliu624.github.io/trail-mate/geocaching/) queries the network directly, so new publications do not require a website update. Publishers keep their own identities; the protocol supports independent directory operators rather than requiring one central service.

The browser also maintains your own caches. Connect USB, open your device drafts,
edit the cache, review the publication and authorize its signature. The browser
publishes through the computer's Reticulum connection, verifies the directory
receipt and confirms the result on the device. Updates keep the cache identity;
archive operations publish a new state. Pending confirmation remains visible
and can be continued instead of silently starting a second publication.

![Web Geocaching maintenance](docs/images/release-1.0.1/web-geocaching-maintenance.png)

*Actual Web maintenance page from a native Reticulum directory check with a
simulated USB author. Sample cache data is shown; this is not a real-Wio result.*

The firmware browses caches through a reused single card. The card emphasizes
distance and an approximate administrative area; details retain coordinates and
the full cache description. Only the current item is hydrated.

![Firmware Geocaching card](docs/images/release-1.0.0/firmware-geocaching-card-native.png)

*Production LVGL page rendered with synthetic cache and administrative-area data;
this capture demonstrates the interface, not geographic accuracy on hardware.*

### Decentralized KML trail sharing

Firmware Tracker uses the same card interaction for recordings and KML routes.
Previous/next navigation reuses the widgets; the current file's start position
feeds the shared approximate-area and distance presentation. Route geometry,
elevation and images are opened through the existing preview when needed.

The card uses the full content width. **Menu** in the bottom bar selects
**Tracks**, **Routes** or **Nav** on supported navigation targets. Contacts,
Chat, Settings, Extensions and Network use the same bottom-menu components;
Contacts and Chat expose their radio status through **Menu → Info**.
Tracks and Nav provide **View**, **New** and **Current** submenus instead of
separate creation commands in the root menu. An active recording retains a direct
**Stop recording** action.

![Firmware Tracker card](docs/images/release-1.0.0/firmware-tracker-card-native.png)

*Actual Wio native LVGL preview from CI with sample KML and simulated GPS. The
preview has no TMAP administrative data, so its region remains unknown. This is
not a physical-device capture.*

![Tracker route actions](docs/images/release-1.0.0/firmware-tracker-actions-native.png)

*Current production LVGL native render: flat action rows, a light focus state,
content-sized height and a compact title-bar close button. X/x closes the current
frame-based Map/Tracker dialogs on keyboard devices; touch uses the same close
control. Other legacy dialogs have not all been migrated.*

The [Tracks workspace](https://vicliu624.github.io/trail-mate/tracks/) combines
**your own trail library**, **shared-trail discovery**, and **trail details and
maintenance**. Import KML/KMZ or supported device recordings, keep drafts in the
browser, and use the connected device as the publication author.

- Browse independently hosted directories and query the visible map extent.
  Pan or zoom the map to explore another area; automatic search can follow map
  movement. Administrative filtering uses the catalog supplied by the directory.
  The public directory supplies a global catalog with 251 country/region parents
  and 4,596 administrative units. Selection stops after two levels. Region names
  use available source translations for the 11 Web languages and otherwise fall
  back to the base name; boundaries are approximate discovery geometry.
- Open a trail by its publication ID, copy that ID or a version link, and inspect
  verified author/version information before downloading it. A publication ID
  identifies the shared object; a KML Placemark ID identifies an item inside a
  file and is not a global directory address.
- Maintain geometry, segments, waypoints, supported KML metadata and attachments;
  inspect elevation and timed records; and keep portable local backups. Source
  files are retained rather than quietly replaced by an unrelated format.
- Publish, update and archive using device signatures. Successive updates keep
  the object ID and link to the previous signed version. Archiving preserves the
  published history rather than pretending to delete every remote copy.
- Save shared content for offline use and install supported signed packages on
  a connected device. Hardware storage and the supported package capabilities
  determine which resources can be used on that device.

![Shared-trail country search through the public Reticulum directory](docs/images/release-1.0.1/web-track-country-search.png)

*Current English Web page querying the public WSS/Reticulum track directory.
The selected Austria boundary is verified; this query returned no shared trails.
This capture uses no USB author and does not claim a completed publication test.*

![Shared-trail first-level administrative search](docs/images/release-1.0.1/web-track-administrative-search.png)

*The same built Web page selects Austria and then Vorarlberg through the public
directory, verifies the supplied boundary and queries that administrative area.
The global catalog stops after two levels. This capture uses no USB author;
the capture itself does not establish firmware or publication acceptance.*

![Trail elevation and timed-record maintenance](docs/images/release-1.0.1/web-track-elevation.png)

![English track workspace with a sample recorded route](docs/images/release-1.0.1/web-track-workspace.png)

*Current English browser workspace with sample KML data, showing elevation
inspection and the time filter for recorded points. The example supplies its
own elevations and timestamps; it does not imply automatic terrain data.*

The [release record](docs/releases/1.0.0.md) records deployment and acceptance
separately from these browser captures.

Configure network access under **Settings > Reticulum** without changing the chat protocol. Manually configured TCP interfaces support IFAC through the SD configuration; RNode/LoRa and AutoInterface IFAC are still pending. See the [Reticulum settings guide](https://github.com/vicliu624/trail-mate/wiki/4.1.1-Reticulum-Settings) and [Geocaching developer guide](https://github.com/vicliu624/trail-mate/wiki/20.-Geocaching-Developer-Guide).

1. **Choose a device** from the support table below. T-LoRa-Pager SX1262 and T-Deck are the primary validation targets.
2. **Install a matching image** from [Releases](https://github.com/vicliu624/trail-mate/releases), or use the [website and web flasher](https://vicliu624.github.io/trail-mate/) for supported targets. For source builds, see [Build Methods](#build-methods).
3. **Prepare for offline use** with the [Wiki](https://github.com/vicliu624/trail-mate/wiki): configure the selected protocol and radio region, and prepare maps and local storage for your device.

Review the [responsible-use and safety guidance](#responsible-use) before field use.

---

## 🧩 Current Device Support & Development Status

The table below records the **real build targets currently present in the repository and their maturity**.

| Device / Target | Build Target | Stack | Current Status |
| --- | --- | --- | --- |
| **LILYGO T-LoRa-Pager (SX1262)** | `tlora_pager_sx1262` | PlatformIO / Arduino | Current default environment and still the most complete day-to-day validation target |
| **LILYGO T-Deck** | `tdeck` | PlatformIO / Arduino | Primary validation target; keyboard, chat, maps, and shared UI paths are actively used |
| **Seeed Wio Tracker L2 Pro (touchscreen)** | `wio_tracker_l2` | PlatformIO / Arduino (ESP32-S3) | Offline navigation, current menus and touch interaction accepted on hardware; CI and native LVGL coverage. ES8311 audio is supported; see the [target contract](docs/targets/wio_tracker_l2.md). |
| **GAT562 Mesh EVB Pro** | `gat562_mesh_evb_pro` | PlatformIO / Arduino (nRF52) | Simplified nRF52 firmware target with monochrome UI, Meshtastic / MeshCore LoRa paths, and persistent on-device radio settings |
| **LILYGO T-Echo-Lite-KeyShield** | `t-echo-lite` | PlatformIO / Arduino (nRF52) | Simplified nRF52 firmware target with 192x176 e-paper UI, 4x5 physical keypad input, Meshtastic / MeshCore LoRa paths, and local device settings |
| **LILYGO T-LoRa-Pager (LR1121)** | `tlora_pager_lr1121` | PlatformIO / Arduino | Supported Pager RF variant with LR1121 RF switch and TCXO bring-up |
| **LILYGO T-Deck Pro** | `tdeck_pro_a7682e` / `tdeck_pro_pcm512a` | PlatformIO / Arduino | Separate environments exist, but this line is still in active bring-up / adaptation work |
| **LILYGO T-Watch S3** | `lilygo_twatch_s3` | PlatformIO / Arduino | Experimental target used more for system and UI validation than for full feature coverage |
| **M5Stack Tab5** | `TRAIL_MATE_IDF_TARGET=tab5` | ESP-IDF | Main large-screen IDF bring-up target; the shared shell runs and hardware-specific work is still being filled in |
| **LILYGO T-Display P4 TFT** | `TRAIL_MATE_IDF_TARGET=t_display_p4_tft` | ESP-IDF | TFT / HI8561 port with shared UI and dashboard; display, touch and sliders verified on the test unit. External keyboard input remains under hardware diagnosis. |
| **LILYGO T-Display P4 AMOLED** | `TRAIL_MATE_IDF_TARGET=t_display_p4_amoled` | ESP-IDF | Explicit AMOLED / RM69A10 + GT9895 bring-up target for the shared IDF shell |

Linux development also includes the [Cardputer Zero device app](apps/linux_cardputer_zero/README.md) and [Linux simulator](apps/linux_sim_shell/README.md). These have their own build and validation paths; embedded CI coverage does not establish Linux hardware readiness.

### How To Choose A Target Today

- If you want the most stable daily development path right now, start with **`tlora_pager_sx1262`** or **`tdeck`**
- If you are debugging a resource-constrained simplified nRF52 target, start with **`gat562_mesh_evb_pro`** or **`t-echo-lite`**
- If you are working on the newer large-screen ESP-IDF path, start with **`tab5`**
- **`tdeck_pro_*`**, **`lilygo_twatch_s3`**, **`t_display_p4_tft`**, and **`t_display_p4_amoled`** are better treated as bring-up, layout, or device-adaptation targets than as the highest-maturity feature-validation path
- A build target establishes that the device is present in the repository; the table's status column records page and capability maturity. Features may be enabled or hidden dynamically according to capabilities, RAM budget, and input hardware
- [GitHub Actions](.github/workflows/ci.yml) builds **`wio_tracker_l2`**, **`tlora_pager_sx1262`**, **`tlora_pager_lr1121`**, **`tdeck`**, **`lilygo_twatch_s3`**, **`gat562_mesh_evb_pro`**, and the nRF52 wrapper target **`t-echo-lite`**, plus the ESP-IDF **P4 TFT**, **P4 AMOLED**, and **C6 companion** targets; GAT562 and T-Echo-Lite release artifacts include UF2 files for manual flashing, verified with the nRF52840 UF2 family ID `0xADA52840`

---

## 🏗️ Shared Architecture & Protocol Interoperability

Trail Mate carries shared messaging, storage, navigation, and UI/business capabilities across device families. The repository separates reusable [modules](modules), [platform adapters](platform), [board definitions](boards), and [application shells](apps), with responsibilities documented in the [architecture specification](docs/specification/REPOSITORY_LAYOUT_ARCHITECTURE_SPEC.md).

Meshtastic, MeshCore, and Reticulum/LXMF are selectable network paths within that product model. A device runs one explicit product protocol path at a time; this does not imply automatic bridging between the three networks or complete upstream feature parity. On-device TAK means Trail Mate's own team-awareness capabilities; ATAK, WinTAK, and CoT interoperability are outside the current claim.

Maintaining this shared product involves protocol compatibility, different radio and display drivers, constrained RAM and task stacks, storage ownership, UI behavior, and release images across targets. The [CI workflow](.github/workflows/ci.yml) makes these concerns visible through build matrices and targeted regression and contract checks.

## 🔒 Security & Reliability

Trail Mate handles input from radio, network, Bluetooth, serial, USB, and removable storage. Protocol parsing, identity and key handling, persistence, and shared hardware access are security-sensitive boundaries.

The repository includes tests for [control-frame authentication and corruption rejection](modules/core_chat/tests/test_vmp_control_auth.cpp), [media-frame integrity](modules/core_chat/tests/test_vmp_media_frames.cpp), [duplicate and malformed shard rejection](modules/core_chat/tests/test_vmp_receive_block.cpp), and [voice-inbox persistence](modules/core_chat/tests/test_vmp_voice_inbox.cpp). CI also checks storage transactions, bounded map delivery, ESP stack hygiene, Wi-Fi access policy, and Reticulum runtime budgets. These checks provide specific regression evidence, not a claim of a completed security audit.

See [SECURITY.md](SECURITY.md) to report a suspected vulnerability privately.

---

## 📋 Why Trail Mate Exists

![logo](docs/images/logo_big.png)

On a walk, the useful things are straightforward: see where you are, find a
route, keep a record and stay in touch with your group. Trail Mate puts those
tools on a device with its own controls, local maps and a radio. Installed map
data remains useful when Internet access is unavailable; communication uses
the protocol and links you configure.

**The person carrying the device should keep control of its identity and data.**
The device holds its author key. Maps, recordings, messages and contacts have
local storage. Public discovery, direct contacts, Team position and personal
tracks have separate sharing rules, so joining a network does not have to mean
handing one service your complete activity history.

When a larger screen helps, use the browser or Center to edit settings, prepare
maps or work on a trail. The device can authorize a specific publication while
the computer handles its network connection. Independent directories index
signed content, and recipients can verify the author's signature. Publishing
still makes that chosen content public; the privacy section above explains what
others can retain.

Portable files, device-held identity and independently operated services give
people alternatives when a provider changes or disappears. That is the reason
for bringing offline navigation, Meshtastic, MeshCore, Reticulum and team tools
together: keep a useful field companion in the user's hands.

> **Connection is a capability people can hold. Trail Mate keeps that capability in the hands of each user.**

## 🧭 Product Position

Trail Mate is organized around four defining capabilities:

* **Anonymous operation**: device identity exists independently of cloud accounts and phone numbers. The system reduces unnecessary public discovery, identity linkage, and location exposure. Anonymous operation has explicit technical limits, and the user still applies security judgment to the surrounding environment.
* **Decentralized communication**: Meshtastic, MeshCore, and Reticulum provide three selectable product network paths. Identity and contacts can exist independently of any single platform, messages can travel through the network selected by the user, and the loss of one path cannot claim ownership of the whole system. The device runs one explicit protocol path at a time, keeping network membership and behavior understandable.
* **Offline operation**: input, viewing, configuration, maps, contacts, messages, and tracks can be handled on the device. Phone and desktop tools provide optional extensions while control remains at the edge.
* **TAK capability**: the device provides member, position, waypoint, track, status, and team-message awareness. Trail Mate's current TAK claim covers its own on-device team-awareness capability. ATAK, WinTAK, and CoT interoperability sit outside the present claim.

The system models public discovery position, contact position, Team position, and local tracks as four distinct data relationships. Users should be able to confirm the shared content, its recipients, and the network path used.

<a id="responsible-use"></a>

## ⚖️ Responsible Use, Legal Compliance, and Scope

Trail Mate is an assistance system for hiking, camping, trail running, and
other outdoor activities. Its purpose is to help people share positions,
coordinate with a group, maintain safety contact, and seek assistance when
normal connectivity is limited or unavailable.

Trail Mate is not intended to serve anyone who harms, controls, surveils,
harasses, or stalks others, or who engages in any other unlawful activity.
Use this device and its software only for lawful, legitimate purposes that
respect the rights, safety, and privacy of others.

The product boundary is deliberate: Trail Mate focuses on helping people stay
in contact and obtain assistance during outdoor activities. It is not a
general-purpose radio experimentation platform or a complete communication
protocol terminal. Features unrelated to outdoor activity, safety
coordination, or assistance—and especially features aimed primarily at radio
operation, network administration, or other non-outdoor uses—are outside the
scope of this project.

### Legal compliance

Before using Trail Mate, understand and comply with every applicable law,
regulation, and local requirement where it is used. This includes, but is not
limited to:

* Radio-spectrum rules, including permitted frequencies, transmit power,
  transmission duration, and duty-cycle limits
* Radio-equipment approval, licensing, registration, or amateur-radio operator
  requirements
* Rules set by countries, regions, parks, protected areas, campsites, and
  event organizers
* Privacy, personal-data, location-data, and communication-content protections
* Restrictions governing emergency, public-safety, and other protected
  communications

Requirements vary substantially by country and region. A feature being
technically capable of operating does not mean it is lawful to use in a given
location. Each user is responsible for ensuring that their device
configuration, frequency plan, transmit power, and operating practice comply
with local requirements.

Do not use Trail Mate, derivative devices, software, or networks for illegal
activity. This includes unauthorized interception, tracking, or location of
people; privacy violations; harassment; fraud; regulatory evasion;
interference with lawful radio communications; or assisting anyone in harming
others.

### Meshtastic and MeshCore interoperability

Trail Mate integrates selected Meshtastic and MeshCore protocol capabilities
so that outdoor users can establish the necessary interoperability with people
running their official firmware. The intended use is outdoor coordination,
safety contact, and seeking or offering assistance when conventional network
coverage is absent or unreliable.

Trail Mate does not aim to implement every Meshtastic or MeshCore feature, nor
to replace their official firmware, clients, or ecosystems. It implements only
the capabilities that fit Trail Mate's purpose of outdoor assistance,
coordination, and safety contact.

Features without a direct relationship to outdoor activities and safety
communication—particularly those focused on radio experimentation, network
administration, general-purpose communication extensions, or other
non-outdoor uses—are not planned for implementation. This is a product-scope
decision, not a judgment on Meshtastic, MeshCore, their communities, or their
respective feature directions.

Meshtastic and MeshCore names, software, protocols, and related materials
belong to their respective owners and communities. Compatibility or
interoperability with those projects does not imply their endorsement,
approval, or official support of Trail Mate.

### Safety notice

Trail Mate can support outdoor activities, but it cannot replace thorough trip
planning, route preparation, weather assessment, navigation skills, first-aid
knowledge, group coordination, or professional rescue equipment.

In remote, high-risk, extreme-weather, or otherwise unreliable communication
environments, do not treat Trail Mate as your only life-safety measure. Choose
appropriate additional equipment for the activity, such as maps and a compass,
backup power, first-aid supplies, a whistle, satellite communications, or a
personal locator beacon, and share your itinerary with a reliable contact
before departure.

## 🔒 Frozen Embedded-Firmware Boundary

The major feature set of Trail Mate's current embedded firmware is now defined. Its future scope consists of the existing feature domains, the three Meshtastic/MeshCore/Reticulum product protocols, and the four core capabilities of anonymous operation, decentralization, offline operation, and TAK. This boundary applies specifically to the embedded firmware product surface. Trail Mate as a whole also includes the Linux version and other edge-device forms.

Ongoing work focuses on bug fixes, reliability and security, protocol interoperability correctness, resource efficiency, carrying the existing product on suitable hardware, test tooling, and documentation. New board ports carry the established capabilities onto suitable devices.

---

## ✨ Core Features

### 🧭 Main Menu Overview

![main menu](docs/images/menu-widgets/pager-menu.png)

The main menu provides quick access to maps, chat, Tracker, Music and utilities.
Pager, T-Deck and Wio L2 use the same grid with a column count that adapts to the
available width. Pager uses W/A/S/D or the wheel to select an app, V to switch
between apps and widgets, and H for Help. Touch devices expose the same pages
through their controls and gestures.

*Current native Pager preview with simulated service state. The footer keeps
the V shortcut visible; this image demonstrates the interface rather than a
physical-device test.*


### 🧭 GPS Map (Performance-First)

| Layer Menu | OSM Base Map |
| --- | --- |
| ![map menu](docs/images/release-1.0.0/firmware-map-menu-native.png) | ![map osm](docs/images/map_osm.png) |

| Terrain Base Map | Satellite Base Map |
| --- | --- |
| ![map terrain](docs/images/map_terrain.png) | ![map satellite](docs/images/map_satellite.png) |

* Fixed **North-Up** map orientation (no rotation)
* Fully **offline** map rendering from SD-hosted TMAP packages
* Three switchable base layers: **OSM / Terrain / Satellite**
* Optional contour overlay for terrain shape awareness
* Real-time position marker for the current GPS fix
* Discrete zoom levels optimized for embedded systems
* Simple breadcrumb trails for path awareness
* Fast in-page layer switching via map layer menu (no page restart)

The current bottom bar keeps zoom at the left and **Pos**, **Help**, conditional
**Team**, and **Menu** at the right. Menu opens upward: **Map Layer** selects OSM,
Terrain or Satellite directly; **Search** opens the existing place picker;
**Navigation**, **Tracks** and **Contours** open dedicated management dialogs.
Load/unload and contour visibility are managed inside those dialogs. Team has a
separate quick menu and appears only while joined to a valid team.

| Navigation management | Touch-accessible Help |
| --- | --- |
| ![Navigation manager](docs/images/release-1.0.0/firmware-map-navigation-native.png) | ![Map Help](docs/images/release-1.0.0/firmware-map-help-native.png) |

*The Menu, Navigation and Help images are production LVGL native renders
with sample map/GPS data from the candidate before the saved-route replan/Finish
controls were added. Follow the workflow below for the current controls. The
base-layer images illustrate existing map display. Native renders are not physical-device
acceptance captures. Each layer displays only the coverage and zoom levels
contained in its installed packages.*

On supported PSRAM-capable ESP targets, create a foot route through **Tracker →
Menu → Nav → New**. Use the current GNSS fix as the origin, or select both endpoints
with the existing POI search. Calculate from an installed routing graph, then save
and preview a self-contained `.tmroute` file. Load the saved plan and open Map
for indexed viewport rendering, progress/remaining distance and geometric turn
hints. Map's Navigation dialog offers manual/automatic replanning, cancel/retry,
reload and a fixed Stop/Finish action. A replacement is opened before activation;
a failed or cancelled replan preserves the previous saved route. Confirmed
arrival ends the active session. Unloading does not delete its file.

Following runs while Map is open and resumes the saved active path on reentry.
Geometry cues have no street-name or intersection semantics. Routing coverage
depends on the installed package's graph, independently of its raster coverage.
The released TMAP SDK **0.2.0** is pinned by commit and source hash. See the
[native navigation implementation and resource evidence](docs/engineering/native-navigation.md)
for the Center London **20.779 km** replay, fixed reader/screen budgets, failure
behavior and the distinction between host measurements and physical-device
acceptance. A few KiB of free RAM cannot run the complete planner.

#### Node inspection from the map

![map node information](docs/images/nodeinfo-1.png)

Select a discovered node directly on the map to inspect its protocol, last-seen state, zoom level, coordinates, and surrounding offline-map context without leaving the situational view.

Recommended TMAP package layout on the SD card:

```text
/maps/tmap/osm/<region>-osm-z<min>-<max>.tmap
/maps/tmap/terrain/<region>-terrain-z<min>-<max>.tmap
/maps/tmap/satellite/<region>-satellite-z<min>-<max>.tmap
/routes/navigation/*.tmroute              # saved calculated plans
/trailmate/packs/fonts/<resource-id>/     # required generated map fonts
```

Copy the packages and their generated font resources from Center. POIs, names and
search indexes are carried by the OSM TMAP; detailed Center packages can also
contain their routing graph and digital elevation. The current TMAP workflow does not
require the former per-tile PNG/JSON export. The reader also discovers flat
`/maps/tmap/*.tmap` packages and supported routing locations described in the
guide. See [TMAP maps and terrain](https://github.com/vicliu624/trail-mate/wiki/6.2-TMAP-Maps-and-Terrain)
for export, installation and font details.

### 🛰️ GNSS Sky Plot

![skyplot](docs/images/SkyPlot.png)

* Real-time sky plot of visible satellites (azimuth/elevation)
* SNR status and constellation coloring (GPS/GLONASS/Galileo/BeiDou)
* Clear indication of satellites used in the current fix
* Summary of USE/HDOP/FIX for fast diagnostics

### 📡 Protocol Probe (LoRa Profile Discovery)

Protocol Probe finds complete LoRa air profiles carrying real Trail Mate
protocol traffic. It is not an RSSI spectrum analyser and never calls a quiet
frequency a usable protocol channel.

* Starts with the active profile and finite protocol-derived candidates instead of a generic 25 kHz frequency grid
* MeshCore uses Discover and a valid response/ACK for active confirmation
* Meshtastic passively discovers a target node, then uses a keyed unicast `want_ack` check when available
* Reticulum records only self-consistent public Announce or control Path Request traffic; it remains passive and does not claim confirmation in this temporary tuning flow
* Lists only `OBSERVED` and `CONFIRMED` profiles, with evidence counts but no business-packet labels
* Lets the user apply a selected observed/confirmed profile only after explicit confirmation

See [the Protocol Probe specification](docs/EnergySweep/function.md) for the
evidence model and protocol-specific boundaries.

### 📡 Decentralized Messaging (Meshtastic / MeshCore / Reticulum)

| Conversation | Reticulum Peer |
| --- | --- |
| ![messages](docs/images/chat_conversion.png) | ![Reticulum peer details](docs/images/reticulum-1.jpg) |

Messages view shows recent conversations and history for quick review.

* Three selectable product protocols: Meshtastic, MeshCore, and Reticulum
* Reticulum mode uses an on-device Reticulum/LXMF runtime and can carry traffic over configured LoRa, AutoInterface, or TCP gateway interfaces
* Reticulum peer details expose the display name, LXMF address, and identity hash needed to verify and manage decentralized identities
* Chinese text support
* Compatible with the **Meshtastic public mesh** (LongFast/PSK)
* Compatible with **MeshCore networks** (native MeshCore packet path)
* Standalone operation with on-device controls and optional USB tools; Meshtastic / MeshCore companion-app connections are outside the supported product workflow
* Designed for high latency, low bandwidth, and packet-loss environments
* Contacts, conversations and messages persist on-device. Reticulum networking uses the SD working configuration; the firmware manages its binary peer directory.

For the Reticulum SD-card format, file locations, contact import, and on-device operations, see the [Reticulum Mode User Guide](https://github.com/vicliu624/trail-mate/wiki/3.5-Configuration-Guide).

### ☁️ Mesh MQTT (Meshtastic / MeshCore)

![Mesh MQTT settings](docs/images/mt_mqtt-1.png)

Trail Mate can bridge both Meshtastic and MeshCore traffic through a configurable MQTT broker when Internet or gateway connectivity is available. Each protocol has independent enable, uplink, downlink, host, port, root-topic, and credential settings. Meshtastic ships with its ecosystem defaults; MeshCore ships disabled with the public `test.mosquitto.org:1883` test broker and `meshcore` root as editable starter values. The public broker is intended for evaluation only—field and production deployments should use a private or self-hosted broker.

### 🌐 Nomad Network

| Micron Page Rendering | Engine & Link Semantics |
| --- | --- |
| ![Nomad Network rendered Micron page](docs/images/nomad-1.png) | ![Nomad Network Micron links](docs/images/nomad-2.png) |

| Offline Form State | Compatibility Diagnostics |
| --- | --- |
| ![Nomad Network offline form](docs/images/nomad-3.png) | ![Nomad Network compatibility diagnostics](docs/images/nomad-4.png) |

Nomad Network renders Reticulum Micron pages directly on the device. The embedded browser supports navigable links, constrained layout primitives, forms, cached/offline-ready state, and visible compatibility diagnostics for unsupported or unknown Micron constructs. It keeps small information services reachable over Reticulum without depending on a conventional web browser or the public Internet.

### 📷 SSTV Receiver (Slow-Scan TV)

![sstv page](docs/images/sstv_page.jpg)

![sstv result](docs/images/sstv_image_result.jpg)

* Receive SSTV audio and decode to images on-device
* Real-time decode progress and image preview
* Designed for low-power, embedded decoding

### 👥 Contacts

![contacts](docs/images/contacts.png)

Contacts is a persistent on-device communication directory. It combines discovered and user-maintained contacts, recent activity, and protocol identities, with quick access to direct messaging, calls, ping, or team actions when supported by the active protocol and hardware.

On keyboard-equipped interfaces, press `S` to search contacts. Save a discovered
peer from Nearby or use Contacts > Add. A Reticulum destination entered without
verified identity keys remains `Address pending` until a valid announce or path
response supplies them. The firmware owns `/mesh/peers.bin`; current firmware
does not import contacts from a hand-edited TSV/JSON file. See the
[Reticulum guide](https://github.com/vicliu624/trail-mate/wiki/4.1-Reticulum,-LXMF-and-RNode-Bridge)
for the supported workflow.

### 💻 Data Exchange (PC Link)

![data exchange](docs/images/data_exchange.png)

PC Link connects the device to a host computer and exposes a structured HostLink stream
for real-time APRS/iGate integration, diagnostics, and data capture.

* Live forwarding of LoRa messages, team updates, and GPS snapshots
* APRS-oriented metadata for external gateways and dashboards
* USB CDC-ACM transport with deterministic framing

### 🖥️ Trail Mate Center

[Trail Mate Center](https://github.com/vicliu624/trail-mate-center) is the desktop-side control center for Trail Mate and Meshtastic-compatible field devices connected through USB HostLink. Its Avalonia interface combines live maps, nodes, teams, event streams, messaging, device configuration, replay, protocol inspection, and data export in one application.

It also prepares offline OSM, terrain, satellite, and contour-map regions for removable media; can build a buffered cache around imported KML routes; and provides a propagation workbench for coverage, interference, relay placement, calibration, and result export. Trail Mate Center is maintained as a separate desktop repository and does not contain the embedded firmware.

| Offline map preparation | Desktop propagation workbench |
| --- | --- |
| ![Center map cache workflow](docs/images/release-1.0.0/center-map-cache.png) | ![Center propagation workflow](docs/images/release-1.0.0/center-propagation.png) |

*Existing actual screenshots from the Center repository. These illustrate the
desktop workflow; Center has its own release cycle and is not assigned the
firmware's 1.0.0 version. Screenshot provenance is recorded with the assets.*

### 🤝 TAK / Team Mode (ESP-NOW Pairing + LoRa Ops)

![team join](docs/images/team_join.png)

![team map](docs/images/team_map.png)

Team mode is designed for small groups that are physically together.
Pairing happens over ESP-NOW at close range to exchange a team key,
then all team operations run over LoRa.

* Create a team (leader) or join a nearby team (member)
* Secure key distribution and team ID establishment during pairing
* Team chat (text/location) and shared status updates
* Member list with leader/member roles and counts
* Team waypoint / assembly-point sharing
* Team map view with latest member positions and track snapshots

### 🧭 Track Recording & Route Following

| Step 1 — Elevation & Route Overview | Step 2 — Waypoint Image Preview |
| --- | --- |
| ![route elevation preview](docs/images/route-1.jpg) | ![route waypoint image preview](docs/images/route-2.jpg) |

* Track recording and storage (record/route modes)
* Track list browsing and route focus
* KML route overlay support
* Two-stage route preview: inspect the full elevation profile first, then page through georeferenced remote images and their positions along the route
* Route preview progress, saved-image counts, distance, and load state remain visible on the embedded display
* GPX tracks exportable via USB Mass Storage

### 🎙️ Walkie Talkie

![walkie talkie](docs/images/walkie_talkie.png)

* FSK + Codec2 voice walkie talkie
* Half-duplex PTT (press to talk / release to listen)
* Jitter buffering and fixed playback cadence for stability

---
## 💡 Non-Negotiable Design Principles

* **Edge ownership**: identity, contacts, messages, configuration, maps, and tracks belong first to the person holding the device.
* **Explicit data relationships**: protocols, contact relationships, and position purposes remain distinct and visible to the user.
* **Honest uncertainty**: the interface directly presents link failure, stale position, and unknown state, and displays success only after obtaining evidence of success.
* **Predictable degradation**: when a network, phone, cloud service, or hardware capability is absent, the independently useful parts of the system remain available.
* **Long-term reliability on constrained hardware**: resource efficiency, determinism, and maintainability take priority over feature accumulation and visual spectacle.

---

## 📱 Hardware-Carriage Strategy

Trail Mate's hardware work focuses on devices suited to carrying the established product capabilities. This section describes the **hardware-selection direction**. The [build-target table above](#-current-device-support--development-status) records current implementation maturity. New hardware continues to carry the defined product domains.

Current priority device categories include:

- **Keyboard-first LoRa handhelds** such as `T-LoRa-Pager`, `T-Deck`, `T-Deck Pro`, and `Cardputer`-style devices, where users can enter free-form text directly without depending on a phone
- **Large-screen touch navigation terminals** such as `M5Stack Tab5` and `T-Display P4`, which are better suited to maps, team situational awareness, and HostLink / companion-computer workflows
- **Ultra-low-power / small-screen message terminals** such as tighter `nRF52 + SX1262` class devices, where monochrome UI, status viewing, simplified chat, and BLE bridging matter more than feature breadth

When evaluating hardware, the project currently prioritizes:

- Reliable LoRa / Sub-GHz radio capability, or at least a clear path to integrate it cleanly
- A device that can handle basic input, viewing, and configuration independently, with phones serving as optional extensions
- Reasonable power, battery, and field portability characteristics
- A stable enough ecosystem, documentation base, or supply chain to justify long-term maintenance

The project stays as **hardware-agnostic** as practical. Protocol logic, storage, and UI/business behavior remain decoupled from board-specific code. Embedded and Linux targets share the established Meshtastic, MeshCore, Reticulum, and TAK capabilities and retain one product model.

---

<a id="build-methods"></a>

## 🛠️ Build Methods

Trail Mate currently uses two main toolchain paths: **PlatformIO** and **ESP-IDF**. The commands below are intended to be run from the **repository root**.

### PlatformIO

PlatformIO covers both the ESP32 Arduino targets and the current nRF52 Arduino targets. The root [platformio.ini](platformio.ini) keeps only shared configuration, while the actual target environments live under `variants/*/envs/*.ini`.

Common build commands:

```bash
# Primary targets
platformio run -e tlora_pager_sx1262
platformio run -e tlora_pager_lr1121
platformio run -e tdeck

# nRF52 / simplified targets
platformio run -e gat562_mesh_evb_pro
platformio run -d builds/pio_nrf52 -e t-echo-lite

# Other integrated targets
platformio run -e wio_tracker_l2
platformio run -e tdeck_pro_a7682e
platformio run -e tdeck_pro_pcm512a
platformio run -e lilygo_twatch_s3
```

If you want more verbose diagnostics, the repository also provides these debug environments:

```bash
platformio run -e tlora_pager_sx1262_debug
platformio run -e tdeck_debug
platformio run -e lilygo_twatch_s3_debug
```

Generic upload form:

```bash
platformio run -e <env> --target upload
```

If you need to select the serial port explicitly, add `--upload-port COMx`. For example:

```bash
platformio run -e tlora_pager_sx1262 --target upload --upload-port COM6
```

Notes:

- Running `platformio run` with no explicit environment uses the root default environment, currently **`tlora_pager_sx1262`**
- If you only want to sanity-check whether a target still builds, start with **`tlora_pager_sx1262`**, **`tdeck`**, **`gat562_mesh_evb_pro`**, or **`t-echo-lite`**
- For very tight-RAM nRF52 simplified targets such as GAT562 and T-Echo-Lite-KeyShield, use release-like or low-log validation and enable extra debug output only for a specific diagnostic need

### ESP-IDF

ESP-IDF is currently used mainly for the newer shared-shell path. The officially wired targets right now are `tab5`, `t_display_p4_tft`, and `t_display_p4_amoled`. The repository root already contains the top-level `CMakeLists.txt`, so you can invoke `idf.py` directly from the root directory.

`tab5` example:

```bash
idf.py -B build.tab5 -DTRAIL_MATE_IDF_TARGET=tab5 reconfigure build
idf.py -B build.tab5 -DTRAIL_MATE_IDF_TARGET=tab5 -p COM6 flash
idf.py -B build.tab5 -DTRAIL_MATE_IDF_TARGET=tab5 monitor
```

`t_display_p4_tft` example:

```bash
idf.py -B build.t_display_p4_tft -DTRAIL_MATE_IDF_TARGET=t_display_p4_tft reconfigure build
idf.py -B build.t_display_p4_tft -DTRAIL_MATE_IDF_TARGET=t_display_p4_tft build
```

`t_display_p4_amoled` example:

```bash
idf.py -B build.t_display_p4_amoled -DTRAIL_MATE_IDF_TARGET=t_display_p4_amoled reconfigure build
idf.py -B build.t_display_p4_amoled -DTRAIL_MATE_IDF_TARGET=t_display_p4_amoled build
```

### Notes

- ESP-IDF generated `sdkconfig` state now lives inside the selected build directory such as `build.tab5`, `build.t_display_p4_tft`, or `build.t_display_p4_amoled`, giving every target isolated configuration output
- For **Tab5**, prefer running `monitor` separately after flashing; chaining `flash monitor` can leave ESP32-P4 in ROM download mode after auto-reset
- VS Code already provides split **Tab5**, **T-Display-P4 TFT**, and **T-Display-P4 AMOLED** `Reconfigure / Build / Flash / Monitor` tasks via `tools/vscode/run_idf_task.ps1`
- If your goal is release validation or routine regression checks, prefer the main PlatformIO path that CI already covers; ESP-IDF targets are still more useful for board bring-up and shared-shell evolution

---

## 🌐 Documentation Language

English documentation is maintained in this repository.

---

## 📝 Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history. The [Roadmap](https://github.com/vicliu624/trail-mate/wiki/16.-Roadmap) records the established product direction and maintenance boundary.

---

## 📄 License

This project is licensed under the **GNU Affero General Public License v3.0 (AGPLv3)**.

The license is intended to ensure that:

* Source code remains available when the project is modified, deployed, or offered as a network service
* The core system cannot be incorporated into closed-source or proprietary products without authorization

### Commercial Licensing

A separate **commercial license** may be provided for the following use cases:

* Commercial or closed-source products
* Hardware vendors integrating or pre-installing the firmware
* Commercial applications unable or unwilling to comply with AGPLv3

For such use cases, please contact the project author to discuss licensing terms.
Publication of this repository does **not** grant any default commercial rights.

See the [LICENSE](LICENSE) file for details.

---

## 🔐 Project Scope

This repository contains Trail Mate's open-source edge-device implementations, including:

* Embedded firmware for ESP32-, nRF52-, and related targets
* Linux portable-terminal applications and shared UI/business capabilities
* Offline maps, positioning, tracks, and on-device TAK capabilities
* Meshtastic, MeshCore, and Reticulum/LXMF communication paths and local storage
* HostLink, board integrations, tests, and development tooling

This project **does not include**:

* Separately distributed commercial desktop software
* Mobile applications (iOS / Android)
* Commercial services or platform products

Any surrounding tools or services may follow different licensing strategies and are outside the scope of this repository.

---

## 🤝 Contributing

Trail Mate's embedded product boundary is now defined. Contributions focus on making the existing capabilities more trustworthy, understandable, and usable on real devices.

### Contribution & Copyright

Unless explicitly stated otherwise, all contributions to this repository are released under the **AGPLv3 license**.

The project is currently author-driven and does not accept contributions that alter core architecture or licensing terms.
For commercial collaboration or deep involvement, please contact the author directly.

The most useful contributions include:

* Private vulnerability reports through [SECURITY.md](SECURITY.md)
* Reproducible defect reports with device, firmware, protocol, network conditions, and exact steps
* Real test results from off-grid, weak-link, low-power, and harsh environments
* Interoperability verification against upstream Meshtastic, MeshCore, and Reticulum/LXMF implementations
* Measurements of power, memory, storage, concurrency, and long-running behavior
* Reliability, security, hardware-port, test, and documentation improvements that preserve the product boundary
* Specific reports of misleading status, unclear operations, or ambiguous privacy boundaries

Pull Requests remain welcome, but changes to the core architecture, product boundary, or licensing strategy should be discussed with the author first. Even without code, a report that clearly states the conditions, action, expected result, and actual result is valuable.

> **Every existing Trail Mate promise should be worthy of trust.**

---

## ✅ Current Capability Index

This section provides a searchable implementation index. The [Trail Mate Wiki](https://github.com/vicliu624/trail-mate/wiki) is the user-documentation source for configuration, keyboard shortcuts, file formats, and operational procedures.

### 🧭 GPS Navigation & Tracks

- Offline map rendering (north-up, no rotation)
- Runtime base layer switching: OSM / Terrain / Satellite
- Contour visibility through a dedicated Map management dialog
- SD-hosted TMAP packages with independent labels, POIs and place search
- Real-time position marker and coordinate display
- Discrete zoom levels and low-power tuning
- Track recording, route and navigation cards with selected-item hydration
- Foot-route calculation from current/searched origin to a searched destination
- Persistent `.tmroute` plans, indexed viewport reads, geometric turn hints,
  confirmed off-route replanning and arrival/Stop/Finish while Map is open
- Tracker View/New/Current submenus and Map load/unload management
- KML route overlays and focus
- GPX tracks exportable via USB Mass Storage

### 📝 Decentralized Messaging (Meshtastic / MeshCore / Reticulum)

- LoRa text messaging with Chinese support
- Meshtastic public mesh compatibility (LongFast/PSK)
- MeshCore network compatibility (native MeshCore packet path)
- On-device Reticulum/LXMF runtime with LoRa, AutoInterface, and TCP gateway interfaces
- SD-backed Reticulum network/LXMF settings and a device-managed persistent contact directory
- Standalone controls and optional USB tooling; no supported Meshtastic / MeshCore companion-app workflow
- Message history and conversation list
- Routing confirmations and reliability diagnostics
- Unishox2 decompression for incoming messages

### 🤝 TAK / Team Mode (ESP-NOW Pairing + LoRa Ops)

- Close-range ESP-NOW pairing with key distribution and team ID setup
- Member list and leader/member roles
- Team chat (text/location)
- Team map view with member position updates
- Team waypoint / assembly-point sharing
- Team track and status rebroadcast

### 📷 SSTV Receiver (Slow-Scan TV)

- SSTV audio reception and on-device image decoding
- Real-time decode progress and image preview
- Lightweight pipeline for low-power hardware

### 👥 Contacts

- Node discovery and contacts list
- Node metadata (ID/short name/device info)
- Online/offline status and recent activity
- Quick jump to direct or team conversations

### 💻 Data Exchange (PC Link / HostLink)

- USB CDC-ACM transport
- HostLink frames/events/config support
- Live forwarding of LoRa/team/GPS data
- APRS/iGate-oriented metadata output

### 🎙️ Walkie Talkie

- FSK + Codec2 voice walkie talkie
- Half-duplex PTT (press to talk / release to listen)
- Jitter buffering and fixed playback cadence

### ⚙️ System Settings & Status

- Display/sleep controls and basic settings
- GPS and network-related configuration
- Status bar icons and system notifications
- Screenshot capture (ALT double-press, saved to SD /screen)

### 💾 USB Mass Storage

- Device mounts as a USB drive
- Direct management of exported tracks and files

### 🔌 System Management

- Graceful shutdown
- Low-power management
- Runtime status monitoring

### 📻 Trail-mate Simplified Edition (nRF52 / GAT562 Mesh EVB Pro / T-Echo-Lite-KeyShield)

- The simplified edition currently supports two nRF52 devices: `GAT562 Mesh EVB Pro` and `T-Echo-Lite-KeyShield`
- It already covers Meshtastic and MeshCore dual-protocol LoRa RX/TX, including Text / NodeInfo / Position handling and the corresponding persistence paths
- Device-side protocol switching and Meshtastic / MeshCore air-parameter editing are available for standalone operation without relying on a phone app
- Device-side parameters can already be edited locally and persisted
- The monochrome UI can be used to view time, GPS, and radio status

---
## 🙏 Acknowledgements

Trail Mate has benefited from real support from the community and hardware vendors.

- Special thanks to **LILYGO** for providing development hardware.
  Their open hardware ecosystem and stable ESP32 product line have enabled continuous iteration and validation in real devices.

- Special thanks to **M5Stack** for supporting the Cardputer Zero adaptation with hardware support.
  Their support helped validate the Cardputer Zero environment against real device constraints and move Trail Mate's Linux portable-device path forward.

- Special thanks to **[Seeed Studio](https://www.seeedstudio.com/)** for supporting Trail Mate with development hardware.
  Their support helps broaden device compatibility and enables continued development and validation on real hardware.

- Special thanks to **Shenzhen GAT-IOT Technology Co., Ltd.** (https://github.com/gat-iot) for providing hardware support to this project.
  The real devices they supplied have helped Trail Mate carry out development, debugging, and validation on actual hardware, further advancing the implementation and refinement of related features.

These contributions lowered the barrier to prototyping and allowed Trail Mate to receive real-world feedback much earlier.

If other hardware vendors resonate with the project's design philosophy and wish to explore its potential in offline outdoor scenarios, feel free to get in touch.
When feasible, I am happy to adapt the software to additional devices and provide feedback based on real usage.


Special thanks to **dawsonjon** (https://github.com/dawsonjon) for the open-source **PicoSSTV** project:
https://github.com/dawsonjon/PicoSSTV. Our SSTV receiver borrows from its decoding approach.
For algorithm details, see: https://101-things.readthedocs.io/en/latest/sstv_decoder.html

---

**Built with care for the outdoor community ❤️**

# NOTICE

## About This Project

**Trail Mate** is an edge-first personal communication and situational-awareness system designed for anonymous operation, decentralization, and offline use. It runs on personally held embedded devices and Linux portable terminals, using Meshtastic, MeshCore, Reticulum, and on-device TAK capabilities for communication, position, navigation, and coordination.

Trail Mate makes the Internet an optional connectivity resource and places basic communication directly on personally held devices. Identity, contacts, messages, position, maps, and tracks remain as close as possible to the user, with explicit relationships and choices whenever data is shared.

This repository is an actively developed and maintained engineering project containing practical implementations for evaluation, porting, integration, and deployment.

If you are evaluating, integrating, porting, modifying, or using this codebase in any product, research, deployment, or internal tooling — you are encouraged to contact the author.

---

## Author

**Vic Liu**

The system architecture, communication protocols, embedded firmware, Linux edge implementation, and reference implementations are primarily maintained by the original author.

---

## Contact

* Email: **[vicliu@outlook.com](mailto:vicliu@outlook.com)**
* Discord community: **[Trail Mate Discord](https://discord.gg/shkueG4zfc)**
* Discord personal contact: **`vicliu_63660`** — add this username as a friend in Discord.
* WeChat: **vicliu890624**

You may contact me for:

* Hardware adaptation or porting
* Protocol clarification
* Integration into existing radio systems
* Commercial licensing discussions
* Collaboration or joint development
* Field deployment guidance
* Bug reports that are difficult to describe via Issues

Discord or WeChat is preferred for real-time technical discussion.
Email is preferred for formal or long-form communication.

---

## For Organizations / Companies

If your organization is testing or evaluating this repository internally:

You are welcome to reach out even if you are only in the research or feasibility stage.
Early technical discussion usually saves significant engineering time.

Customization, hardware adaptation guidance, and technical consulting are available.

---

## Licensing Reminder

This project is open-source and distributed under the terms specified in the LICENSE file.

If you are using the code in a network service, device firmware distribution, or any redistributable system, please ensure that your usage complies with the license requirements.

If you are unsure, please contact the author before deployment.

---

## Intent

Trail Mate aims to reduce the number of centers a person must trust in order to communicate, navigate, and coordinate. It provides autonomy with explicit, verifiable boundaries: the device remains with its user and continues to work when cloud accounts, phone applications, and stable public networks are all absent.

The embedded feature surface is now defined. Defect reports, interoperability testing, field validation, deployment experience, and improvements to reliability, security, resource efficiency, hardware carriage, and documentation clarity are especially welcome.

You are not required to open Issues before contacting the author directly.

Thank you for taking the time to examine and use this project.
