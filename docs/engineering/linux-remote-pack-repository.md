# Linux remote pack repository

The Linux adapter now consumes the product's version-1 pack catalog and
`trailmate-pack-v1` archives through the existing shared Extensions page.
Catalog refresh and installation run on owned workers; UI reads return cached
snapshots. A revision counter refreshes the page after catalog changes. Shutdown
cancels and joins both workers before dependent services are torn down.

The default catalog is `https://vicliu624.github.io/trail-mate/data/packs.json`.
`TRAIL_MATE_PACK_CATALOG_URL` and `TRAIL_MATE_PACK_BASE_URL` override the source.
Setting the catalog URL to `off` disables remote access. An explicit
`TRAIL_MATE_PACK_ROOT` without a catalog override preserves local-only operation.
No CM0 board identity, interface name or I2C path is needed by this adapter.

Validated catalog text is cached under the configured cache root, keyed by URL
and base URL. A fresh process can display that cache during a network failure.
Installed packages remain visible without a catalog, including uninstall support.
Archive digest metadata distinguishes an updated remote package from an installed
version. JSON parsing uses the existing SQLite JSON1 dependency; ZIP extraction
uses LibArchive, declared in the Linux build and package dependencies.

## Download and publication

Catalog documents are bounded to 2 MiB and 2,048 package records. A remote archive
must declare its identifier, version, layout, positive byte length and SHA-256.
Downloads enforce the declared length and digest with a maximum of 128 MiB.
HTTP/HTTPS use the existing curl port and system certificate verification.

Each install owns a private temporary directory outside the installed store.
Extraction rejects traversal, absolute paths, duplicate normalized members,
symlinks, hard links, special files, encrypted entries and reserved metadata
paths. It limits entry count to 50,000 and expanded data to 1 GiB, and checks
declared member sizes and archive integrity. The root manifest is mandatory.

The existing resource store rechecks compatibility, identity, manifest version
and dependencies before atomic publication. Installed archive digests are stored
as store-owned metadata. Cancellation checks cover download, extraction and store
copy work; the final atomic publication section completes without interruption.
Failures preserve the previously published resource tree and remove staging data.
These digest checks establish consistency with the catalog, not author signatures.

## Verification on 21 October 2026

The complete native suite passed 113/113 tests and the Cardputer Zero suite passed
14/14. The new repository fixture also passed as an armhf executable on CM0. It
covers malformed catalogs, incompatible packages, length/digest failures,
malicious ZIP members, manifest mismatch, cancelled installation, bounded worker
shutdown, persisted metadata and a fresh-process offline catalog reload.

The installed P4 UI listed an isolated test package, opened its details, installed
it, displayed its installed version and Uninstall action, and loaded the added
locale. A fresh application process rediscovered the installation; resource bytes
and archive digest matched. These checks used isolated resource/settings roots.
The device also fetched the official catalog over HTTPS (schema 1, seven packages)
and displayed an official package's details and Install action. Public packages
were not installed during this check.

The resulting `trailmate-cm0_1.0.0_armhf.deb` has SHA-256
`9c93e9c283c69522dc9840036c724a805bf4b495c6feee645f53f2731a489d83`.
It passed Lintian without diagnostics, installation and `dpkg -V`. Its application
icon matches the existing logo. The installed application rendered at 1232 by
568; the compositor confirmed fullscreen and focus, and Alt+F4 exited with code
zero in 0.982 seconds. No display-driver replacement or reboot was performed.

Signed installed-package route-image admission is implemented separately in
[the signed route image adapter](linux-signed-route-package-images.md).
Physical Ethernet, acoustic audio and the full physical input matrix have not
been verified. Cardputer Zero has no physical test device. AHT20 temperature and
humidity collection and Bluetooth support are excluded by user direction.
