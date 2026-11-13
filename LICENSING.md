# Licensing

Trail Mate contains material under several licenses. [LICENSE](LICENSE) defines
the maintainer's grant; component and file-specific terms remain applicable.

## Original Trail Mate material

Original material in which Liu Weikai (Vic Liu) holds the necessary rights is
available under [MIT](LICENSES/MIT.txt). Third-party works, upstream-derived
portions and other authors' contributions are excluded unless their own license
permits the relevant use. A mixed file does not acquire one blanket MIT license.

## Correction of earlier copyright and license statements

Earlier blanket AGPL descriptions, and any claim of maintainer copyright over
Mark Qvist's RNS/LXMF-derived material, were made in error and are void for those
portions. This correction applies to **earlier revisions and releases containing
that material**, including generated and subsequently regenerated derivatives.

Those portions retain Mark Qvist's original copyright and the applicable
Reticulum License grants. They are not licensed under MIT, GPL or AGPL by the
maintainer. A Git author or committer field records a contribution to the
repository; it does not establish ownership of included upstream works.

This correction does not rewrite Git history or revoke valid grants for material
the relevant licensor had authority to license. The former root AGPL text is
preserved unchanged in [LICENSES/AGPL-3.0.txt](LICENSES/AGPL-3.0.txt).

## Reticulum and LXMF

The implementations are derived from these reference works:

* [Reticulum](https://github.com/markqvist/Reticulum):
  Copyright (c) 2016-2026 Mark Qvist, unless otherwise noted.
* [LXMF](https://github.com/markqvist/LXMF):
  Copyright (c) 2020-2026 Mark Qvist, unless otherwise noted.

The full [Reticulum derivative license](LICENSES/Reticulum-Derivatives.txt)
preserves the notice supplied by Mark Qvist, with the maintainer's name filled
in. Its additions/modifications copyright line applies only to original
contributions the maintainer actually owns. Generated copies of Mark's work do
not become maintainer-owned originals through copying or regeneration.

The separate [RNS](LICENSES/Reticulum-RNS.txt) and
[LXMF](LICENSES/Reticulum-LXMF.txt) upstream notices are retained. The license's
conditions concerning harmful systems, AI/ML training datasets and preservation
of copyright/permission notices remain in force.

[reticulum-sources.csv](docs/licensing/reticulum-sources.csv) identifies source
areas and mixed-file boundaries. RNS/LXMF protocol-derived portions retain their
terms wherever moved or renamed. Application scheduling, storage adapters and
other original contributions retain their applicable terms; the inventory does
not grant a new license for material whose rights are not established.

MessagePack vendor material retains its own
[u-msgpack-python MIT notice](LICENSES/u-msgpack-python-MIT.txt), including Ivan
A. Sergeev's copyright. Crypto implementations likewise retain their individual
notices. The maintainer's and Mark's notices do not replace those copyrights.

## Embedded implementation approach

The intent of Trail Mate's C++ port is to faithfully preserve the reference
implementations' protocol behavior and wire compatibility for the features it
supports. ESP32 devices must share limited memory and CPU time with Trail Mate's
radio, UI, mapping and storage workloads. The principal adaptations therefore
focus on bounded resource use: explicit buffer ownership, PSRAM-backed storage
where available, incremental work with per-poll budgets, and separate protocol,
transport and persistence responsibilities.

These resource and structural adaptations remain derivatives wherever they are
based on the upstream source. Modular boundaries do not transfer upstream
copyright, make generated code independently authored, or create permission to
combine otherwise incompatible licenses. The port does not claim support for
every upstream feature; target-specific limits do not redefine the upstream
protocol. Mark Qvist's reference implementations remain the source of the
protocol behavior being ported.

## Component terms

| Component | Applicable terms |
| --- | --- |
| Eligible original Trail Mate material | [MIT](LICENSES/MIT.txt), within the scope above. |
| RNS/LXMF-derived portions | [Reticulum License](LICENSES/Reticulum-Derivatives.txt), with original upstream notices. |
| LXST-derived portions | Separate authorization required. The [upstream CC BY-NC-ND 4.0 text](LICENSES/LXST-CC-BY-NC-ND-4.0.txt) does not authorize sharing adapted material. |
| Meshtastic-derived material | Applicable upstream GPL terms and any distinct per-file notices; [GPLv3 text](LICENSES/GPL-3.0.txt). |
| MeshCore and bundled Ed25519 | Preserve [Scott Powell's MIT notice](LICENSES/MeshCore-MIT.txt) and [Orson Peters' separate notice](modules/core_chat/src/infra/meshcore/crypto/ed25519/license.txt). |
| TMAP originals | [Additional MIT grant](LICENSES/TMAP-MIT.txt) for eligible owner-held material at the [fixed dependency revision](docs/licensing/TMAP_RELICENSING.md); third-party terms and valid prior grants remain available. |
| Audio/codecs, hardware libraries, website and data | Preserve each component's terms and attribution, as listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). |

The [component inventory](docs/licensing/component-licenses.csv) records license
and notice locations. [license-sources.json](docs/licensing/license-sources.json)
identifies fixed sources for retained license texts; those references do not
establish every implementation's historical import revision.

## Distribution notices and current restrictions

Affected source and binary distributions must preserve the applicable original
copyright notices, complete license/permission texts and component attribution.
An approved distribution relying on TMAP's additional MIT grant must include
both its full text and scope record alongside applicable dependency notices.

Linux application installs and the Cardputer Zero Store adapter include these
project notices, the complete `LICENSES/` directory and `docs/licensing/` under
`usr/share/doc/trailmate/`. The store adapter preserves runtime files and other
authors' notices while correcting the project's own outdated notice copies.
This packaging does not update historical downloads or supply permission to
release an otherwise unauthorized or incompatible combined work.

Affected releases remain paused. LXST authorization, compatibility of combined
GPL/AGPL and Reticulum-licensed works, and the treatment of historical release
artifacts remain unresolved. Correcting source notices does not repair previously
distributed packages or supply a missing license.

The exact source history of generic vendor-derived MessagePack portions and
RNode air framing is not fully established. Those areas retain their separate
status in the source inventory; they are not newly assigned MIT or the Reticulum
License by this notice.
