# Trail Mate 1.1.0 release

Version 1.1.0 is a minor release: it adds two installable Linux device targets
and online map preparation while retaining existing configuration, chat and
radio-identity formats. `CHANGELOG.md` is the version source for ordinary
builds; release tags take precedence through `scripts/project_version.py`.

## Linux assets

- `trailmate-cardputer-zero_1.1.0_arm64.deb`: Cardputer Zero application,
  APPLaunch entry, framebuffer/Wayland backends and self-contained ARM64 TMAP
  producer. The package targets the stock Bookworm-compatible ARM64 userspace.
- `trailmate-cm0_1.1.0_armhf.deb`: T-Display-CM0 application, native television
  media integration and self-contained ARM TMAP producer. It targets Raspbian
  Trixie ARMHF and its GCC 14 mutex C++ ABI.
- `trailmate-oneseg-backend_1.1.0+gitb576783021fb-3_armhf.deb`: matching CM0
  native decoder backend. This backend has its own source/build version; it
  is not an alternative application package.
- `SHA256SUMS`: hashes of the published firmware and Debian archives, using
  the same filenames as the downloadable release assets.

The existing ESP32/nRF52 firmware assets remain part of the release matrix.
Use the package for the actual device and operating-system architecture.

## Installation

Download the files from the GitHub Release and verify their hashes against
`SHA256SUMS`. On Cardputer Zero:

```sh
sudo apt update
sudo apt install ./trailmate-cardputer-zero_1.1.0_arm64.deb
```

Open `TrailMate` in APPLaunch. Both Linux launchers use this product name.
Automatic backend selection uses an existing
Wayland session, otherwise the detected ST7789V framebuffer or configured
framebuffer path. Existing explicit display/input environment overrides are
retained. The LoRa/GNSS Cap is detected separately from display selection.

On T-Display-CM0, install both archives together so APT resolves the native
television dependency and distribution libraries:

```sh
sudo apt update
sudo apt install ./trailmate-oneseg-backend_1.1.0+gitb576783021fb-3_armhf.deb ./trailmate-cm0_1.1.0_armhf.deb
```

Use the existing CM0 desktop/profile integration. Its bundled kernel backlight
patch is documented under `/usr/share/doc/trailmate-cm0/kernel`; application
installation does not replace the running kernel.

Both application packages retain private settings and radio identities. They
do not attempt to reconstruct previously corrupted chat databases. Online map
creation requires network access; installed maps remain available offline.

On the Recent Activity widget, use A/D or Left/Right to select a conversation
and Enter to open it; touch users can tap either populated row. Chat owns the
read-state update. Back/Home returns through the message list to the launcher;
V reopens the same widget
page, showing updated badges while preserving unread messages in other threads.
W/S and the wheel continue to switch widget pages.

## Build provenance and publication

`linux-device-packages.yml` publishes Center's self-contained producers from
commit `efa12462a526aabad7212cbadfa537ace0b7dbd6`, with the Center license and a
per-file SHA-256 manifest. ARM64 packaging uses the existing Bookworm Docker
builder. CM0 bootstraps signed Raspbian ARMHF repositories, builds its pinned
native decoder and packages the application in the same target userspace.
Generic Debian ARMHF toolchains do not substitute for the CM0 C++ ABI.

The CI release job waits for the firmware/platform checks and Linux package
jobs, then checks application versions, ELF architectures, map dependencies,
producer hashes and CM0 backend capabilities before publishing `.deb` assets.
The final tag must identify the verified source revision. Release metadata
refreshes Pages after publication.

The [physical acceptance record](linux-paired-mesh-validation-20261108.md)
distinguishes real Zero/CM0 RF and UI tests from host regressions and unperformed
ESP32 hardware qualification. Earlier dated development-package versions are
device evidence, rather than the final release asset names.
