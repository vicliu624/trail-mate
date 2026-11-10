# CardputerZero Store submission

The store listing is maintained in `../../app-builder.json`. Its package name
is `trailmate`, while its launcher name remains `TrailMate`. The store package
replaces/provides `trailmate-cardputer-zero`; executable and user-data paths
stay unchanged. Do not install both packages simultaneously.

Release order is mandatory: successful tagged CI and package validation, then
a published stable GitHub Release in `vicliu624/trail-mate`, then a Store
submission. A CI artifact, successful individual build or draft release is not
a substitute for that published release.

The Linux release-package workflow builds both the original device DEB and the
Store DEB. The release job publishes both only after its required checks pass;
`SHA256SUMS` covers the resulting assets. The Store package uses the same
runtime files and user-data paths.

For offline adapter verification, create the Store package from a checksum-verified
ARM64 package on Linux:

```sh
python3 apps/linux_cardputer_zero/tools/package_store.py \
  --deb trailmate-cardputer-zero_1.1.0_arm64.deb \
  --sha256 48f1d4546e54ef2dfb87e91826a7b353dea83a7fbdcddf0668a884f1b093ba4e \
  --output trailmate_1.1.0_arm64.deb
```

The adapter verifies the source checksum, removes unused TMAP development files,
replaces the udev `RUN` permission hook with a package-owned systemd-tmpfiles
configuration, and includes the project license. It verifies that every retained
runtime file has the same SHA256 as the original package. Published release
assets are never modified. Use APT to install this replacement package so that
it resolves the declared conflict and removes the old package transactionally.

Run the current official `install_path_policy.py` and `store_meta_policy.py`
before upload. Verify the replacement package on the device, including the
Cap's non-root EXT5V access, APPLaunch startup and preserved SQLite identity and
messages. Screenshots are unmodified 320 x 170 framebuffer captures from device
qualification; the icon is the existing 256 x 256 product icon.

From an exact checkout of the published release tag, use the guarded publisher:

```sh
python3 apps/linux_cardputer_zero/tools/publish_store_release.py --tag v1.1.1
python3 apps/linux_cardputer_zero/tools/publish_store_release.py \
  --tag v1.1.1 --publish --app-builder /path/to/AppBuilder
```

The first command performs the checks without submitting. The publisher requires
a published stable release, the same tagged commit and listing version, successful
tagged full CI and Zero builds, and a release-owned `trailmate` ARM64 DEB matching
the release checksum and GitHub asset digest. Drafts, incomplete/failed builds,
different source commits, dirty listing resources and mismatched artifacts block
submission. It accepts no local-DEB or CI-artifact override.

The publisher retains the official AppBuilder policy and ownership checks, uses
the existing `gh` login, and points the submission manifest at our immutable
Release asset. Metadata and native screenshots are committed through the GitHub
Git API, including removal of obsolete gallery files. This avoids Windows Git
partial-clone incompatibilities and a second copy of the binary in a buffer
release. Windows package inspection uses WSL's `dpkg-deb`; Python runs in UTF-8
mode for the translated listing.

Do not invoke `czdev publish` directly from this repository: it does not enforce
TrailMate's upstream-release prerequisites. Metadata-only maintenance retains
the currently published version and binary; it does not authorize bypassing the
release gate for a new application version.

Add the real-device demo to the resulting PR's comments as required by the
[submission guide](https://cardputer.cc/#/documents/cp0-submit):
<https://youtu.be/eBIRhOy7cJc>. First submissions require a store maintainer's
review and are not automatically listed.

## Listing maintenance

The Store and website display `pool/main/trailmate/meta.json` from the packages
repository through the generated registry. A pull request description does not
update that listing. Keep the upstream metadata and this project's
`app-builder.json` synchronized when updating product text or screenshots.

Maintain `summary`, `description`, `locales`, `screenshots` and
`author.website`. The description is plain text in the current website detail
view; Markdown images belong in the PR, while the listing uses its separate
screenshot gallery. The project website is linked through `author.website`.
The English summary must be no longer than 80 characters, and the gallery must
contain one to six native 320 x 170 PNG captures.

The gallery uses real Zero captures: launcher, Reticulum messaging, OSM map,
route elevation, offline Geocaching and online TMAP area selection. Frames from
the rehearsal's RGB565 captures are decoded directly at native resolution.
Team is described as a project capability, with the Zero update scope explicit.
Do not substitute screenshots from other hardware or present demo cache data
as a physical hide or an established GNSS fix.
