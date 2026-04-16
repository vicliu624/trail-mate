# 🗺️ Trail Mate

![trail mate page](docs/images/ChatGPTImage.png)

> A low-power, offline-first handheld device for outdoor navigation and communication

[English](README.md) | [中文](README_CN.md) | [Join Discord](https://discord.gg/87PVMVUP)

---

## 📋 Overview

![logo](docs/images/logo_big.png)

Outdoor activities often take place in environments where cellular networks are unreliable or completely unavailable.
In these conditions, people still need to **exchange short text messages, understand relative positions, and maintain basic orientation** — without depending entirely on smartphones or complex infrastructure.

**Trail Mate** is a low-power, offline-first handheld device project built on ESP32-class hardware, designed specifically to address these constraints.

It focuses on two core needs in offline outdoor scenarios:

* **Simple and reliable self-positioning**, using a fixed north-up GPS map to avoid unnecessary visual complexity
* **Direct LoRa text communication**, allowing users to send free-form messages to Meshtastic or MeshCore mesh networks **without relying on a smartphone**

Trail Mate prioritizes **stability, efficiency, and interoperability** over feature density or visual polish, making it suitable for long-term use on constrained hardware in real outdoor environments.

---

## ✨ Core Features

### 🧭 Main Menu Overview

![main menu](docs/images/main.png)

The main menu provides quick access to GPS, LoRa chat, tracker, and system utilities,
designed for fast navigation on a physical keyboard without deep menu nesting.


### 🧭 GPS Map (Performance-First)

| Layer Menu | OSM Base Map |
| --- | --- |
| ![map menu](docs/images/map_menu.png) | ![map osm](docs/images/map_osm.png) |

| Terrain Base Map | Satellite Base Map |
| --- | --- |
| ![map terrain](docs/images/map_terrain.png) | ![map satellite](docs/images/map_satellite.png) |

* Fixed **North-Up** map orientation (no rotation)
* Fully **offline** map rendering from SD card tiles
* Three switchable base layers: **OSM / Terrain / Satellite**
* Optional contour overlay for terrain shape awareness
* Real-time position marker for the current GPS fix
* Discrete zoom levels optimized for embedded systems
* Simple breadcrumb trails for path awareness
* Fast in-page layer switching via map layer menu (no page restart)

Expected SD card tile layout:

```text
/maps/base/osm/{z}/{x}/{y}.png
/maps/base/terrain/{z}/{x}/{y}.png
/maps/base/satellite/{z}/{x}/{y}.jpg
/maps/contour/major-500/{z}/{x}/{y}.png
/maps/contour/major-200/{z}/{x}/{y}.png
/maps/contour/major-100/{z}/{x}/{y}.png
/maps/contour/major-50/{z}/{x}/{y}.png
/maps/contour/major-25/{z}/{x}/{y}.png
```

### 🛰️ GNSS Sky Plot

![skyplot](docs/images/SkyPlot.png)

* Real-time sky plot of visible satellites (azimuth/elevation)
* SNR status and constellation coloring (GPS/GLONASS/Galileo/BeiDou)
* Clear indication of satellites used in the current fix
* Summary of USE/HDOP/FIX for fast diagnostics

### 📶 Energy Sweep (Sub-GHz Scan)

![sub-ghz scan](docs/images/subGScan.png)

Energy Sweep provides a fast Sub-GHz occupancy view for channel planning in the field.

* Real-time RSSI sweep bars across the configured Sub-GHz band
* Cursor readout for exact frequency, RSSI, and noise floor
* Best-channel recommendation with cleanliness/SNR hint
* `STOP/SCAN` control for pause/resume
* `AUTO` applies the current best channel and moves cursor to the recommended frequency
* Sweep range follows the currently configured region (Meshtastic region or MeshCore region preset)

### 📡 LoRa Chat (Meshtastic + MeshCore Compatible)

![message compose page](docs/images/screenshot_20260118_200651.png)

![messages](docs/images/messages.png)

Messages view shows recent conversations and history for quick review.

* LoRa-based text messaging
* Chinese text support
* Compatible with the **Meshtastic public mesh** (LongFast/PSK)
* Compatible with **MeshCore networks** (native MeshCore packet path)
* Bluetooth connectivity to Meshtastic / MeshCore companion apps
* Broadcast-based communication (no central infrastructure)
* Designed for high latency, low bandwidth, and packet-loss environments
* Minimal protocol implementation optimized for ESP32-class devices

### 📷 SSTV Receiver (Slow-Scan TV)

![sstv page](docs/images/sstv_page.jpg)

![sstv result](docs/images/sstv_image_result.jpg)

* Receive SSTV audio and decode to images on-device
* Real-time decode progress and image preview
* Designed for low-power, embedded decoding

### 👥 Contacts

![contacts](docs/images/contacts.png)

Contacts shows discovered nodes, recent activity, and quick actions
to jump into direct or team conversations.

### 💻 Data Exchange (PC Link)

![data exchange](docs/images/data_exchange.png)

PC Link connects the device to a host computer and exposes a structured HostLink stream
for real-time APRS/iGate integration, diagnostics, and data capture.

* Live forwarding of LoRa messages, team updates, and GPS snapshots
* APRS-oriented metadata for external gateways and dashboards
* USB CDC-ACM transport with deterministic framing

### 🤝 Team Mode (ESP-NOW Pairing + LoRa Ops)

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

![tracker](docs/images/tracker.png)

![tracker](docs/images/tracker1.png)

* Track recording and storage (record/route modes)
* Track list browsing and route focus
* KML route overlay support
* GPX tracks exportable via USB Mass Storage

### 🎙️ Walkie Talkie

![walkie talkie](docs/images/walkie_talkie.png)

* FSK + Codec2 voice walkie talkie
* Half-duplex PTT (press to talk / release to listen)
* Jitter buffering and fixed playback cadence for stability

---
## 💡 Design Philosophy

Trail Mate is **not** a smartphone replacement, and it does not attempt to hide the real limitations of offline communication.

Instead, it focuses on:

* ✅ Honest representation of uncertainty
* ✅ Deterministic and predictable system behavior
* ✅ Long-term reliability on constrained hardware

> 💬 **Designed for environments where simplicity and robustness matter more than visual refinement.**

---

## 📱 Planned Supported Devices

Trail Mate's long-term goal is not simply to support as many boards as possible. The priority is to support the kinds of devices that actually make sense for off-grid outdoor communication. This section describes the **hardware direction**, not a promise that every device listed here is already fully supported in the current release.

Current priority device categories include:

- **Keyboard-first LoRa handhelds** such as `T-LoRa-Pager`, `T-Deck`, `T-Deck Pro`, and `Cardputer`-style devices, where users can enter free-form text directly without depending on a phone
- **Large-screen touch navigation terminals** such as `M5Stack Tab5` and `T-Display P4`, which are better suited to maps, team situational awareness, and HostLink / companion-computer workflows
- **Ultra-low-power / small-screen message terminals** such as tighter `nRF52 + SX1262` class devices, where monochrome UI, status viewing, simplified chat, and BLE bridging matter more than feature breadth

When evaluating hardware, the project currently prioritizes:

- Reliable LoRa / Sub-GHz radio capability, or at least a clear path to integrate it cleanly
- A device that can handle basic input, viewing, and configuration on its own rather than depending heavily on a phone
- Reasonable power, battery, and field portability characteristics
- A stable enough ecosystem, documentation base, or supply chain to justify long-term maintenance

The project still aims to stay as **hardware-agnostic** as practical. Protocol logic, storage, and UI/business behavior are kept as decoupled as possible from board-specific code so future ESP32- and nRF52-class targets can continue to reuse Meshtastic / MeshCore-related capabilities instead of forking into separate applications per board.

---

## 🧩 Current Device Support & Development Status

The table below describes the **real build targets that exist in the repository today**, not the broader long-term hardware direction.

| Device / Target | Build Target | Stack | Current Status |
| --- | --- | --- | --- |
| **LILYGO T-LoRa-Pager (SX1262)** | `tlora_pager_sx1262` | PlatformIO / Arduino | Current default environment and still the most complete day-to-day validation target |
| **LILYGO T-Deck** | `tdeck` | PlatformIO / Arduino | Primary validation target; keyboard, chat, maps, and shared UI paths are actively used |
| **GAT562 Mesh EVB Pro** | `gat562_mesh_evb_pro` | PlatformIO / Arduino (nRF52) | Resource-constrained target focused on monochrome UI, Meshtastic, BLE, and persistence paths; some features are intentionally trimmed to fit RAM limits |
| **LILYGO T-LoRa-Pager (SX1280)** | `tlora_pager_sx1280` | PlatformIO / Arduino | Integrated Pager RF variant for alternate hardware versions |
| **LILYGO T-Deck Pro** | `tdeck_pro_a7682e` / `tdeck_pro_pcm512a` | PlatformIO / Arduino | Separate environments exist, but this line is still in active bring-up / adaptation work |
| **LILYGO T-Watch S3** | `lilygo_twatch_s3` | PlatformIO / Arduino | Experimental target used more for system and UI validation than for full feature coverage |
| **M5Stack Tab5** | `TRAIL_MATE_IDF_TARGET=tab5` | ESP-IDF | Main large-screen IDF bring-up target; the shared shell runs and hardware-specific work is still being filled in |
| **LILYGO T-Display P4** | `TRAIL_MATE_IDF_TARGET=t_display_p4` | ESP-IDF | Early integrated target used to validate the shared IDF shell and large-screen path |

### How To Choose A Target Today

- If you want the most stable daily development path right now, start with **`tlora_pager_sx1262`** or **`tdeck`**
- If you are debugging a resource-constrained monochrome target or Meshtastic / BLE behavior, start with **`gat562_mesh_evb_pro`**
- If you are working on the newer large-screen ESP-IDF path, start with **`tab5`**
- **`tdeck_pro_*`**, **`lilygo_twatch_s3`**, and **`t_display_p4`** are better treated as bring-up, layout, or device-adaptation targets than as the highest-maturity feature-validation path
- “The repository has a build target” does not mean every page or capability is equally mature on that device; some features are enabled or hidden dynamically based on capabilities, RAM budget, and input hardware
- GitHub Actions currently keeps building the main path through **`tlora_pager_sx1262`**, **`tdeck`**, and **`lilygo_twatch_s3`**

---

## 🛠️ Build Methods

Trail Mate currently uses two main toolchain paths: **PlatformIO** and **ESP-IDF**. The commands below are intended to be run from the **repository root**.

### PlatformIO

PlatformIO covers both the ESP32 Arduino targets and the current nRF52 Arduino target. The root [platformio.ini](platformio.ini) keeps only shared configuration, while the actual target environments live under `variants/*/envs/*.ini`.

Common build commands:

```bash
# Primary targets
platformio run -e tlora_pager_sx1262
platformio run -e tdeck

# nRF52 / resource-constrained target
platformio run -e gat562_mesh_evb_pro

# Other integrated targets
platformio run -e tlora_pager_sx1280
platformio run -e tdeck_pro_a7682e
platformio run -e tdeck_pro_pcm512a
platformio run -e lilygo_twatch_s3
```

If you want more verbose diagnostics, the repository also provides these debug environments:

```bash
platformio run -e tlora_pager_sx1262_debug
platformio run -e tlora_pager_sx1280_debug
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
- If you only want to sanity-check whether a target still builds, start with **`tlora_pager_sx1262`**, **`tdeck`**, or **`gat562_mesh_evb_pro`**
- For very tight-RAM targets such as GAT562, prefer release-like or low-log validation instead of enabling excessive debug output by default

### ESP-IDF

ESP-IDF is currently used mainly for the newer shared-shell path. The officially wired targets right now are `tab5` and `t_display_p4`. The repository root already contains the top-level `CMakeLists.txt`, so you can invoke `idf.py` directly from the root directory.

`tab5` example:

```bash
idf.py -B build.tab5 -DTRAIL_MATE_IDF_TARGET=tab5 reconfigure build
idf.py -B build.tab5 -DTRAIL_MATE_IDF_TARGET=tab5 -p COM6 flash
idf.py -B build.tab5 -DTRAIL_MATE_IDF_TARGET=tab5 monitor
```

`t_display_p4` example:

```bash
idf.py -B build.t_display_p4 -DTRAIL_MATE_IDF_TARGET=t_display_p4 reconfigure build
idf.py -B build.t_display_p4 -DTRAIL_MATE_IDF_TARGET=t_display_p4 build
```

### Notes

- ESP-IDF generated `sdkconfig` state now lives inside the selected build directory such as `build.tab5` or `build.t_display_p4`, so different targets do not fight over stale config output anymore
- For **Tab5**, prefer running `monitor` separately after flashing; chaining `flash monitor` can leave ESP32-P4 in ROM download mode after auto-reset
- VS Code already provides **`IDF Tab5: Reconfigure`**, **`IDF Tab5: Build`**, **`IDF Tab5: Flash`**, and **`IDF Tab5: Monitor`** tasks via `tools/vscode/run_idf_task.ps1`
- If your goal is release validation or routine regression checks, prefer the main PlatformIO path that CI already covers; ESP-IDF targets are still more useful for board bring-up and shared-shell evolution

---

## 🌐 Languages

* [English](README.md) ← You are here
* [中文](README_CN.md)

---

## 📝 Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history and planned updates.

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

This repository contains the **core system implementation** of the Trail Mate project, including but not limited to:

* Device-side firmware
* Offline navigation and GPS processing logic
* LoRa-based communication protocols and mesh behavior
* System interaction and state management for constrained hardware

This project **does not include**:

* Commercial desktop software
* Mobile applications (iOS / Android)
* Commercial services or platform products

Any surrounding tools or services may follow different licensing strategies and are outside the scope of this repository.

---

## 🤝 Contributing

All code in Trail Mate is **100% generated by AI under human guidance**.
The project itself is a long-term experiment in **human–AI collaboration for real engineering systems**.

Here, **contribution does not equal writing code**.

### Contribution & Copyright

Unless explicitly stated otherwise, all contributions to this repository are released under the **AGPLv3 license**.

The project is currently author-driven and does not accept contributions that alter core architecture or licensing terms.
For commercial collaboration or deep involvement, please contact the author directly.

### Who Are the Most Important Contributors?

**The most important contributors are people who actually spend time outdoors.**

We especially welcome:

* Hikers, campers, cyclists, off-road travelers, anglers
* Users operating in **no-network, low-power, harsh environments**
* People who may **not write code**, but have strong intuition about what is useful and what is not

Their judgments, frustrations, and decisions are the starting point for this system’s evolution.

### What Can Contributions Be?

* 🧭 **Real-world usage scenarios and problem descriptions**

  > In what environment? What went wrong? What behavior felt unreliable?
* 🧠 **Intuitive judgments about feature trade-offs**

  > What information matters? What becomes noise?
* 🧪 **Failure cases and boundary feedback**

  > When does the system stop being trustworthy?
* 🔑 **Token resources** to support AI generation, verification, and iteration

Even if you **never submit code**, your judgment can still be transformed — through AI — into **executable, verifiable system behavior**.

### How Do We Collaborate?

* Humans (especially outdoor users) decide:
  **what deserves to exist**
* AI translates those decisions into:
  **consistent, runnable implementations**

Pull Requests are welcome, but they are neither the only nor the most important form of contribution.
Trail Mate values **judgment quality and real-world feedback** over lines of code.

> If a feature has no value outdoors, it should not exist.

---

## ✅ Implemented Features

### 🧭 GPS Navigation & Tracks

- Offline map rendering (north-up, no rotation)
- Runtime base layer switching: OSM / Terrain / Satellite
- Contour overlay toggle in map layer menu
- SD tile layout support for OSM/Terrain PNG and Satellite JPG
- Real-time position marker and coordinate display
- Discrete zoom levels and low-power tuning
- Track recording and route modes (record/route list)
- KML route overlays and focus
- GPX tracks exportable via USB Mass Storage

### 📝 LoRa Messaging (Meshtastic + MeshCore Compatible)

- LoRa text messaging with Chinese support
- Meshtastic public mesh compatibility (LongFast/PSK)
- MeshCore network compatibility (native MeshCore packet path)
- Bluetooth connectivity to Meshtastic / MeshCore companion apps
- Message history and conversation list
- Routing confirmations and reliability diagnostics
- Unishox2 decompression for incoming messages

### 🤝 Team Mode (ESP-NOW Pairing + LoRa Ops)

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

### 📻 Trail-mate Relay Edition (GAT562 Mesh EVB Pro)

- A dedicated firmware path exists for `GAT562 Mesh EVB Pro`, positioned as a **relay device inside the Trail-mate system**, not as a normal handheld endpoint
- The relay edition already covers Meshtastic-compatible LoRa RX/TX, Text / NodeInfo / Position handling, and the corresponding persistence paths
- It already supports BLE interaction with the Meshtastic app for message, node-information, and configuration-related synchronization
- Relay-side parameters can already be updated remotely and persisted
- The monochrome UI is usable for time, GPS, radio status, and runtime diagnostics, which fits field deployment and maintenance for relay use

---
## 🚀 Planned Features

### 🔗 Enhanced Meshtastic Compatibility

* [x] **Position sharing** (`POSITION_APP`) for team awareness (see Team Mode)
* [ ] **Native Meshtastic waypoint interoperability** (`WAYPOINT_APP`, `meshtastic_Waypoint`) for POIs, camps, and hazards; Trail-mate team waypoints / assembly points already exist, but protocol-level interoperability with native Meshtastic waypoints is still missing
* [ ] **Store-and-forward messaging** for unstable networks
* [ ] **Network diagnostics** (`TRACEROUTE_APP`)
* [x] **Meshcore network compatibility** (selectable adapter)

### 🧭 GPS Enhancements

* [x] Real-time position markers (see GPS Map / Team Mode)
* [x] **Track export** (GPX via USB Mass Storage)
* [ ] **Track playback & stats** (replay, distance/elevation summaries)

### 📝 Messaging Enhancements

* [ ] **Unishox2 compression** for outgoing messages
* [ ] **Reticulum support** for interoperable offline messaging experiments

### 🔌 System Enhancements

* [ ] Language switching (EN / ZH)
* [ ] Firmware updates via USB or wireless

### 📻 Trail-mate Relay Edition Integration

* [ ] **Remote relay-parameter management from regular devices** - the `GAT562 Mesh EVB Pro` relay firmware already supports remote parameter updates and persistence on the relay side, but normal Trail-mate handheld devices do not yet expose the matching entry point, interaction flow, or management page

---
## 🙏 Acknowledgements

Trail Mate has benefited from real support from the community and hardware vendors.

Special thanks to **LILYGO** for providing development hardware.
Their open hardware ecosystem and stable ESP32 product line have enabled continuous iteration and validation in real devices.

Special thanks to **Shenzhen GAT-IOT Technology Co., Ltd.** (https://github.com/gat-iot) for providing hardware support to this project.
The real devices they supplied have helped Trail Mate carry out development, debugging, and validation on actual hardware, further advancing the implementation and refinement of related features.

These contributions lowered the barrier to prototyping and allowed Trail Mate to receive real-world feedback much earlier.

If other hardware vendors resonate with the project’s design philosophy and wish to explore its potential in offline outdoor scenarios, feel free to get in touch.
When feasible, I am happy to adapt the software to additional devices and provide feedback based on real usage.


Special thanks to **dawsonjon** (https://github.com/dawsonjon) for the open-source **PicoSSTV** project:
https://github.com/dawsonjon/PicoSSTV. Our SSTV receiver borrows from its decoding approach.
For algorithm details, see: https://101-things.readthedocs.io/en/latest/sstv_decoder.html

---

**Built with care for the outdoor community ❤️**

# NOTICE

## About This Project

**Trail-mate** is an offline-first field communication and situational-awareness system built around low-power radio devices (LoRa and compatible sub-GHz radios).
The project focuses on reliable human-to-human coordination in environments where cellular networks are unavailable, unstable, or undesirable.

This repository is an actively developed engineering project, not an abandoned code drop and not a code archive.

If you are evaluating, integrating, porting, modifying, or using this codebase in any product, research, deployment, or internal tooling — you are encouraged to contact the author.

---

## Author

**Vic Liu**

The system architecture, protocols, firmware design, and reference implementations are primarily maintained by the original author.

---

## Contact

* Email: **[vicliu@outlook.com](mailto:vicliu@outlook.com)**
* Discord: **[Trail Mate Discord](https://discord.gg/87PVMVUP)**
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

The goal of Trail-mate is to enable practical, human-centered, off-grid communication and coordination.
Constructive feedback, real-world testing reports, and implementation experiences are especially appreciated.

You are not required to open Issues before contacting the author directly.

Thank you for taking the time to examine and use this project.

