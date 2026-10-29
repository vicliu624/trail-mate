# Linux network readiness and route images

Shared pages use `platform::ui::network::status()` for connectivity hints. Wi-Fi
settings retain their own radio/credential port. The ESP adapter projects its
existing Wi-Fi status; the Linux adapter samples all OS interfaces on a background
worker and returns a cached snapshot to UI callers. No board ID or interface name
is selected in shared UI. Active non-loopback interfaces with usable IPv4 or IPv6
addresses qualify, including renamed Ethernet, wireless, tether and virtual
interfaces. Unspecified, loopback, link-local and multicast addresses do not
qualify. This is a readiness hint, not an Internet reachability proof.

Tracker route-image downloads use that generic status rather than requiring
Wi-Fi. Extensions shows generic connectivity and distinguishes an unavailable
catalog from missing Wi-Fi credentials when a network link is already active.

## Native HTTP and storage

The Linux implementation of the existing HTTP port uses libcurl and the system
route selection. HTTP/HTTPS are the only transfer and redirect protocols. TLS
peer and hostname verification remain enabled with the system trust store.
Transfers enforce request timeouts and byte limits; a request without its own
limit has a 16 MiB host bound. Text reads discard partial output on failure.
HTTP errors, truncated responses, rejected writers and cancellation propagate as
failures rather than successful payloads. See the upstream
[protocol restrictions](https://curl.se/libcurl/c/CURLOPT_PROTOCOLS_STR.html) and
[write callback contract](https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html).

Route images stream into unique staging files under the configured route root.
Limits follow the shared image-source contract (at most 8 MiB; the route API
defaults to 4 MiB). The existing `FileIntegrityCheck` rereads the staged bytes,
and `ImageSourceBindingStore` publishes the image with the standard reference/file
SHA-256 binding. A process-local mutex and per-asset advisory file lease protect
commit/cache operations across threads and application instances. Non-regular
files, traversal and symlink escapes are rejected. Failed network transfers leave
the previous published source intact; incomplete staging files are removed.
The binding is not an author's signature or an independent signed-package proof.

The same owned worker performs source-bound cache builds. JPEG and PNG inputs
are checked against the stored file digest before decoding. Native decoders use
bounded image dimensions and allocations, retain aspect ratio, and write the
existing 200 by 120 thumbnail and 320 by 180 view BMP formats. Cache files are
staged and renamed. Linux LVGL now enables its BMP decoder. Application shutdown
cancels active HTTP work and joins the worker before service teardown.

Signed installed-package image admission now uses fresh original-author and file
proofs; see [signed route package images](linux-signed-route-package-images.md).
Remote extension-catalog/archive integration is implemented separately; see
[the remote pack repository](linux-remote-pack-repository.md).

## Installed package path prerequisites (2026-10-21)

Linux now accepts the shared reserved `p-<64 lowercase hexadecimal digits>` asset
identifier used by signed route packages, in addition to ordinary identifiers up
to 48 characters. Dot and parent-directory identifiers are rejected. Asset and
image-directory creation checks canonical containment; a pre-existing symlink
cannot redirect creation outside its storage root. Saved-image counting checks
containment and ignores symlink entries.

The initial HTTP/image fixture exercises valid package identifiers, malformed or oversized
identifiers, directory escapes and symlink image entries. The extended fixture
passed both natively and as armhf code on CM0; the native suite passed 113/113.
This package was a prerequisite for signed image support, not evidence of signature or
package authorization. The subsequent implementation freshly verifies the
original manifest signature and object/revision, installed binding, complete
attachment index and available resource hashes, and the installed KML hash and
geometry, using the same shared contracts as the ESP stored-package loader.
Only after that proof succeeds may reference digests select cached resources.

The resulting development deb SHA-256 is
`47ba62df43130719e1d20a0c587ab90abe2681ee076f2a38a7a5e8ee6ab32843`.
It passed Lintian package checks and installation with `dpkg -V`; the original
logo and 1232 by 568 render were preserved. Focused fullscreen Alt+F4 exited with
code zero in 0.749 seconds. No kernel or display configuration was changed.

## Verification scope

The loopback HTTP/TLS fixture covers success, redirects, binary chunked bodies,
HTTP errors/304, size limits, short bodies, blocked protocol redirects, untrusted
certificates, writer rejection, timeout, cancellation and worker cleanup. Storage
checks cover source binding, digest tampering, busy leases, path escape, special
files, JPEG/PNG caches and a complete asynchronous download/cache batch. The test
also renders a generated cache through a real LVGL display buffer and checks its
red/green RGB565 pixels. Link classification fixtures cover interface renaming,
multiple interfaces, wired/wireless-style names, IPv6, offline links and excluded
addresses.

On the inspected CM0, JPEG/PNG runtime libraries were already installed. The
current physical state is Wi-Fi connected and Ethernet unavailable. This does
not establish physical wired-link interoperability. No network device is toggled
or reconfigured by these readiness/download adapters.

## Network integration package and device checks (2026-10-21)

The following records the network integration package. The subsequent remote-pack
package and its additional checks are recorded in the linked repository document.

The final native suite passed 112/112 tests; Cardputer's no-media Wayland and
framebuffer builds and its 14/14 tests also passed. The new ESP status adapter
passed a C++17 syntax check, and platform boundaries/ESP stack hygiene passed.
The armhf fixture ran on CM0 and passed the same HTTP, binding, cache, special-file,
lease, cancellation and LVGL pixel checks. The unprivileged hardware probe returned
`network_supported=1 network_connected=1`, valid RTC/system-source flags, battery,
keyboard and Wi-Fi status.

The installed development deb SHA-256 is
`0155dd8f2a42905a192d27bd47143a424557a805cbaaff95cb41ffcd97fea6af`.
It passed Lintian with no diagnostics and has no build-host RPATH/RUNPATH. Runtime
JPEG/PNG and systemd-helper dependencies are declared. Audio-probe documentation
is installed as a compressed man page, and RTC removal/purge hooks use Debian's
systemd helpers. Package installation and `dpkg -V` passed; the original logo and
1232 by 568 P4 frame were confirmed. The installed Extensions page showed Online
and allowed keyboard switching to Uninstalled and reopening its filter menu.
An unavailable catalog was displayed as a catalog/package condition rather than
a request for Wi-Fi credentials. Focused fullscreen Alt+F4 exited with status zero
in 0.649 seconds. RTC remained enabled/active and its UTC difference was zero;
no kernel/display driver replacement or reboot was performed.
