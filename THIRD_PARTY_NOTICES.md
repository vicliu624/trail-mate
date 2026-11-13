# Third-party notices and license inventory

Updated: 2026-11-13. Third-party material retains its authors' copyright and
applicable license grants. The scoped MIT grant in [LICENSE](LICENSE) does not
replace these notices. See [LICENSING.md](LICENSING.md) for mixed-file boundaries,
the correction of the previous blanket AGPL description, and the publication hold.

The paths identify component areas. File-specific copyright and license notices
remain applicable, including when a directory contains material from several
sources. The maintainer's MIT grant does not replace those notices.

## Reticulum, LXMF and LXST

Trail Mate includes implementations derived from Mark Qvist's reference works,
including code generated from those implementations and subsequent regeneration
of derived code. Their upstream copyrights and applicable terms are retained.

The complete derivative notice supplied by Mark Qvist, with only the maintainer's
name filled in, is in [Reticulum-Derivatives.txt](LICENSES/Reticulum-Derivatives.txt).
The original upstream reference notices in the table remain unchanged. The
additions/modifications line does not claim copyright in Mark Qvist's generated
or regenerated material and supplies no LXST authorization.

| Work and source | Attribution and retained terms | Scope and status |
| --- | --- | --- |
| [Reticulum / RNS](https://github.com/markqvist/Reticulum) | Copyright (c) 2016-2026 Mark Qvist. [Full Reticulum License](LICENSES/Reticulum-RNS.txt). | RNS-derived portions in shared and platform code, wherever located. The exact source revisions and mixed-file boundaries remain under review. No MIT/GPL/AGPL alternative is granted for those portions. |
| [LXMF](https://github.com/markqvist/LXMF) | Copyright (c) 2020-2026 Mark Qvist. [Full Reticulum License](LICENSES/Reticulum-LXMF.txt). | LXMF-derived portions in shared code, adapters, runtime and persistence. Exact source revisions remain under review; separate LXST portions retain their separate status. |
| [LXST](https://github.com/markqvist/LXST) | Mark Qvist. [Upstream CC BY-NC-ND 4.0 text](LICENSES/LXST-CC-BY-NC-ND-4.0.txt). Preserve any more specific upstream per-file notices. | Existing LXST-derived telephony code requires separate authorization for redistribution. No direct Trail Mate license is recorded as granted. This reference text does not supply that permission. |

The [source inventory](docs/licensing/reticulum-sources.csv) identifies shared
implementations, platform adapters, local integration and separate-source
boundaries. Preserve complete applicable notices in covered distributions.
See [LICENSING.md](LICENSING.md) for current distribution restrictions.

## Mesh protocols and external application libraries

| Component and source | Local scope or declared version | Retained notices and review status |
| --- | --- | --- |
| [Meshtastic firmware](https://github.com/meshtastic/firmware) and [protobuf schemas](https://github.com/meshtastic/protobufs) | `modules/core_chat/generated/meshtastic/`, shared Meshtastic code and platform integration | Meshtastic contributors retain their copyright. GPL-covered portions retain their applicable GPLv3 grants; [full GPLv3 text](LICENSES/GPL-3.0.txt). Exact imported revisions, file-level provenance and any only/or-later wording remain to be verified. Nanopb's generator license does not replace the source schemas' terms. |
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | Shared `infra/meshcore/` and platform integration | Upstream-derived portions retain [MIT, Copyright (c) 2025 Scott Powell / rippleradios.com](LICENSES/MeshCore-MIT.txt), together with any additional applicable per-file notices. Exact source revision remains unresolved. |
| [Orson Peters' Ed25519 implementation](https://github.com/orlp/ed25519) | `modules/core_chat/src/infra/meshcore/crypto/ed25519/` | Copyright (c) 2015 Orson Peters. Preserve the separate [zlib-style notice](modules/core_chat/src/infra/meshcore/crypto/ed25519/license.txt); it is not replaced by the project's MIT notice. |
| [u-msgpack-python in the RNS reference](https://github.com/markqvist/Reticulum/blob/16621a78e35e2ba5156c72af7fe492458ba092d5/RNS/vendor/umsgpack.py) | Compatibility/source review for `modules/core_chat/include/chat/infra/lxmf/lxmf_msgpack.h`; actual port extent unconfirmed | Copyright (c) 2013-2020 vsergeev / Ivan (Vanya) A. Sergeev. Any derived portions retain the [full vendor MIT notice](LICENSES/u-msgpack-python-MIT.txt). This is not Mark Qvist's or the maintainer's copyright, and compatibility alone does not prove code copying. |
| [Unishox2](https://github.com/siara-cc/Unishox2) | Shared Meshtastic `compression/unishox2.*` | Copyright (C) 2020 Siara Logics (cc); author and port credits remain in the source. Apache-2.0 as stated in the [source header](modules/core_chat/src/infra/meshtastic/compression/unishox2.cpp), not a blanket GPL assignment to the directory. |
| [TMAP](https://github.com/vicliu624/tmap) | `platformio.ini` pins `d74b084acb61653149dec1ac161df58a92278cff`; installed package declares version 0.2.0 | Liu Weikai (Vic Liu) additionally grants [MIT](LICENSES/TMAP-MIT.txt) for eligible owner-held originals at this unchanged source pin; [scope and retained third-party notices](docs/licensing/TMAP_RELICENSING.md). The old package still declares `AGPL-3.0-only`; valid prior grants and its own provenance/notices are retained. Other authors' material is excluded from the additional grant. |
| [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio/tree/2.0.0) | Declared dependency 2.0.0; use depends on target and selected sources | Inspected `AudioOutputI2S.h` credits Copyright (C) 2017 Earle F. Philhower, III and grants GPL-3.0-or-later. Preserve each selected file's own terms and credits; do not assume every bundled decoder has identical terms. |
| LVGL, RadioLib, XPowersLib and SensorLib | `platformio.ini`: 9.4.0, 7.4.0, 0.3.1 and 0.3.3 respectively | Installed manifests declare MIT. Preserve the upstream packages' actual copyright and license notices; they are not replaced by Copyright (c) 2026 Vic Liu. Target-specific and transitive packages still require review. |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | Declared dependency 1.0.3 | Copyright (C) 2008-2013 Mikal Hart, with additional credits in `TinyGPS++.h`. Its header grants LGPL-2.1-or-later; [LGPL 2.1 text](LICENSES/LGPL-2.1.txt). |
| [nanopb](https://github.com/nanopb/nanopb) | Declared range `^0.4.8`; generated Meshtastic headers identify generator 0.4.9.1 | Keep the installed runtime and generator's upstream notices. Its own terms do not establish the license of schema-derived outputs. Exact resolved versions and notice packaging require review per target. |

## Vendored libraries and hardware support

Existing license files and source-header notices are retained in their current
locations. Different files within a vendored component can have different terms.

| Local component | Retained attribution, license and evidence |
| --- | --- |
| `third_party/codec2/` | David Rowe and other authors credited per file. [Root license](third_party/codec2/LICENSE) is GPLv3; [codec2.h](third_party/codec2/src/codec2.h) specifies LGPL version 2.1; [kiss_fft.c](third_party/codec2/src/kiss_fft.c) retains Mark Borgerding's BSD-3-Clause notice. Review the selected source set in [library.json](third_party/codec2/library.json); do not flatten this mixed tree to MIT, GPL or an LXST license. |
| `third_party/minimp3/` | [CC0 1.0 Universal](third_party/minimp3/LICENSE); preserve source credits. |
| `third_party/rweather_crypto/` | Copyright (C) 2015 Southern Storm Software, Pty Ltd. [MIT-style license](third_party/rweather_crypto/LICENSE). |
| `third_party/sdfat/` | Copyright (c) 2011..2020 Bill Greiman. [MIT](third_party/sdfat/LICENSE.md). |
| `third_party/yxml/` | Copyright (c) 2013-2014 Yoran Heling. [MIT-style license](third_party/yxml/COPYING). |
| `third_party/bzip2/` | Copyright (C) 1996-2019 Julian R Seward. [bzip2 1.0.8 license](third_party/bzip2/LICENSE). |
| `third_party/route_png/` | Preserve [MINIZ-LICENSE.txt](third_party/route_png/MINIZ-LICENSE.txt), which contains a public-domain/Unlicense dedication, and any other source-specific notices. The miniz notice alone does not license unrelated wrapper code. |
| `third_party/lilygo_adafruit_epd/` | Dean Miller / Adafruit Industries credits and BSD notice in [Adafruit_EPD.cpp](third_party/lilygo_adafruit_epd/src/Adafruit_EPD.cpp). Exact inherited version and completeness of the BSD notice require review. |
| `platform/esp/third_party/lilygo_cpp_bus_driver/` | Retain [existing GPLv3 text](platform/esp/third_party/lilygo_cpp_bus_driver/LICENSE) and upstream per-file credits. Exact imported revision and selected-file terms remain to be established. |
| `platform/esp/idf_components/m5stack_tab5/` | Retain [Apache-2.0 license](platform/esp/idf_components/m5stack_tab5/LICENSE) and upstream credits. |
| `modules/core_sstv/` DSP material | Preserve [MIT notice and Jonathan P Dawson / dawsonjon credits](modules/core_sstv/LICENSE-DSP) and any other applicable per-file notices. |

## Website, fonts and data

The website has its own detailed [third-party credits](site/THIRD_PARTY.md). These
include Animal Island UI, Naive Icons, retained CC BY-NC artwork, React, Noto fonts,
Leaflet, seek-bzip, buffer and map/data attribution. Website code also uses
`@reticulum/core` and `@reticulum/lxmf` 0.9.0 from
[reticulum-js](https://github.com/bergie/reticulum-js), with EUPL-1.2 notices in
[site/geocaching/licenses/EUPL-1.2.txt](site/geocaching/licenses/EUPL-1.2.txt).
That is a separate package provenance record; it does not settle the licensing of
Trail Mate's other RNS/LXMF implementations or all generated site artifacts.

Keep existing font and data notices, including those under `tools/fonts/`,
`packs/`, `tests/site/fixtures/` and `site/geocaching/data/`. The applicable asset
licenses and attribution are not replaced by the MIT grant for original code.

## License sources and distribution notices

The machine-readable [component-licenses.csv](docs/licensing/component-licenses.csv)
records these review areas. The pinned license-text sources and hashes are in
[license-sources.json](docs/licensing/license-sources.json); those references do
not identify the source revisions actually imported into Trail Mate.

SDKs, operating-system packages, board-specific libraries and transitive
dependencies retain the notices required by the actual bundled versions.
The inventory does not grant a combined-release license or LXST authorization.
