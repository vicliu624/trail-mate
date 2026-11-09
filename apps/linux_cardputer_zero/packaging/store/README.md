# CardputerZero Store submission

The store listing is maintained in `../../app-builder.json`. Its package name
is `trailmate`, while its launcher name remains `TrailMate`. The store package
replaces/provides `trailmate-cardputer-zero`; executable and user-data paths
stay unchanged. Do not install both packages simultaneously.

Create the store package from the verified ARM64 release asset on Linux:

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

From `apps/linux_cardputer_zero`, publish with the official AppBuilder CLI:

```sh
/path/to/AppBuilder/czdev publish --deb /path/to/trailmate_1.1.0_arm64.deb
```

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
