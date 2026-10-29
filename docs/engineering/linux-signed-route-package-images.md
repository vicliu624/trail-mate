# Linux signed route package images

The native image-cache worker now handles the existing installed signed-route
layout. It uses the shared `verifyManifest`, `AttachmentPackageFileCheck`,
`ManifestFileCheck`, `FileIntegrityCheck` and `ImageSourceBindingStore` contracts.
Ed25519 verification delegates to the existing Linux OpenSSL crypto adapter.
There is no board identity, interface name or fixed host data directory in this
implementation.

Shared GPS and Tracker pages recognize package routes as direct children of the
platform's configured route root. They no longer require a literal `/routes/`
host prefix. The native boundary maps a valid physical route to the existing
logical package identity; storage access resolves that identity under the
configured SD root with canonical containment checks.

## Admission and publication

The installed `.rpk` binding is a CRC-protected locator, not author proof. Each
batch rereads and verifies the original signed manifest, derives object/revision
hashes, checks the binding's KML and attachment-index metadata, streams the entire
index and all declared available resources, and verifies the installed KML hash
and geometry. Required missing attachments fail the package. Image reference
mappings remain private until all these checks succeed.

Available JPEG/PNG entries are selected by the exact decoded reference's SHA-256.
Their filesystem names derive only from the authenticated resource hash. Relative
references and HTTP URLs never become package filesystem paths and are not
downloaded by this adapter. A matching image is copied to a unique staged source,
checked against the authenticated size and digest, reread for integrity, and
published with the standard source binding under the image lease. Derived BMPs
are invalidated by publication and rebuilt by the existing bounded decoder.

Package mappings are limited to 64 image entries. Existing shared limits bound
the manifest, KML, index, resources and package size. Cancellation is checked
through proof work and copying. Owned I/O and temporary files are cleaned up on
failure; allocation/filesystem exceptions produce a failed image batch. Invalid
proofs cannot promote an existing unverified source into a package image.

## Verification on 21 October 2026

The new fixture creates a real Ed25519-signed version-2 manifest, KML, attachment
index, JPEG and installed binding in an isolated temporary root. It exercises
complete asynchronous materialization and BMP-cache creation through the public
route-storage API using a configured physical root. It checks corrupted
signatures, KML, index and resource bytes, damaged bindings, CRC-valid forged
object/index locators, missing required resources via the availability mask,
wrong references, cancellation, mismatched asset identity and escaped/symlinked
sources. Rejected proofs preserve the previously published source bytes.

The fixture passed natively and as armhf code on CM0. The complete Linux suite
passed 114/114 tests; Cardputer Zero Wayland and framebuffer builds and 14/14 tests
passed. The shared platform boundary and ESP stack checks passed.
The production-source `image_source_ui` and `image_source_linux` host contracts
also passed (2/2), including configurable-root package identity in the shared
GPS producer and the existing Tracker package queue behavior.

The standard development deb has SHA-256
`b8d4dbd4f25b6becc4f229c2d60146d80a0542cf44912396353de7aeab0e6efa`.
It passed Lintian without diagnostics, installation and `dpkg -V`. The original
logo and 1232 by 568 P4 render were preserved. Focused fullscreen Alt+F4 exited
with code zero in 0.649 seconds. No kernel/display change or reboot was performed.

This implements image admission from an existing installed package. The signed
route-package import/installation transport is provided separately by
[native HostLink](linux-hostlink-package-install.md). Neither adapter proves an
author is trusted by the user or establishes directory publication/current-head status.
The automated public-worker test is not a physical touchscreen route-preview test.
