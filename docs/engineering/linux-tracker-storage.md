# Native Linux recording storage

The Linux Tracker adapter implements the shared recording API. Storage comes
from `resolve_paths().sd_root`; neither a board name nor a fixed device or mount
path enters the adapter. CM0 without a fitted GNSS receiver does not generate
location fixes. The regression fixture injects coordinates and clocks and does
not claim physical GNSS verification.

## Recording ownership and formats

Each start creates an exclusive filename containing UTC seconds, the process
ID and a process-local sequence. Repeated starts within one second cannot
truncate previous recordings. The recorder retains its file descriptor and an
exclusive advisory lock until stop. Each append checks the configured media
root and named file identity. A renamed/replaced recording stops rather than
writing to the replacement. Changing the requested format takes effect at the
next recording; it cannot change the current file's encoding.

GPX uses the shared point writer and a GPX 1.1 namespace. The closing tags are
present after creation and each successful point append. A failed append attempts
to restore the previous end and then stops. This does not promise recovery from
power loss during an overwrite or filesystem failure.

CSV uses the formal IDF header
`time,lat,lon,alt_m,speed_mps,satellites`, with UTC ISO timestamps and a classic
numeric locale. As in the IDF format, CSV does not encode altitude/speed validity;
the existing browser converter preserves those values as recorded metadata.

Binary uses `TMTRK1` followed by explicitly encoded little-endian 20-byte points:

| Offset within a point | Encoding |
| --- | --- |
| 0 | UTC epoch seconds, unsigned 32-bit |
| 4 | Latitude E7, signed 32-bit |
| 8 | Longitude E7, signed 32-bit |
| 12 | Altitude metres, signed 16-bit |
| 14 | Speed centimetres/second, unsigned 16-bit |
| 16 | Satellites, unsigned byte |
| 17 | Validity flags: altitude bit 0, speed bit 1 |
| 18 | Two zero padding bytes |

Non-finite or unrepresentable optional binary values have cleared validity
flags and zero values. Coordinates must be finite and within geographic bounds.
Old native `TMTK1` and old native CSV files are not silently rewritten; the new
writer aligns future recordings with the production reader.

The first valid fix is sampled even at uptime zero. Subsequent sampling uses
unsigned elapsed milliseconds across clock wraparound and clamps oversized
interval settings before multiplication can overflow. Distance-only mode uses
the formal IDF five-metre geographic distance threshold.

## Listing and deletion

The list accepts GPX, CSV and BIN suffixes without case sensitivity. It excludes
dotfiles, reserved `active.bin` metadata, symbolic links, multiply linked files
and unrelated files. Result storage is bounded by the requested count.

Deletion accepts a direct filename or the absolute path beneath the configured
tracks directory, matching the shared Tracker page's contract. Nested paths,
outside-root paths, links, metadata and an actively locked recording are refused.
The rooted descriptor adapter resolves path components without following
symlinks and synchronizes the parent directory after unlink.

## Verification

`linux_tracker_storage` compiles the production adapter with injected GNSS and
clock providers. It covers same-second preservation, absolute and relative
deletion, a forked process attempting to delete an active recording, GPX closing
tags, active-format ownership, exact binary fields/padding, invalid optional
values, uptime zero/wraparound, the distance threshold, file replacement and
listing/path restrictions.

When `TRAIL_MATE_TEST_WEB_CLIENT=ON`, `linux_tracker_web_client` invokes that
fixture and feeds its real CSV and binary output to the unchanged production
`site/tracks/src/recording-convert.js`. It asserts decoded coordinates, altitude,
speed, point counts and format profiles. These tests passed on native Linux.
The ARM fixture also passed on CM0, where Python's XML parser accepted the GPX
with two recorded points. The full native suite passed 124/124, and Cardputer
Zero passed 14/14 after rebuilding the shared adapter. The exact standard deb
passed Lintian, installation/integrity checks, original-logo verification, a
1232 x 568 P4 render and compositor-confirmed fullscreen Alt+F4 exit with code 0
in 0.983 seconds. Artifact and installed executable hashes are recorded in the
[CM0 integration ledger](t-display-cm0-hardware-integration.md).
