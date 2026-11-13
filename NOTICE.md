# NOTICE

## About This Project

**Trail-mate** is an offline-first field communication and situational-awareness system built around low-power radio devices (LoRa and compatible sub-GHz radios).
The project focuses on reliable human-to-human coordination in environments where cellular networks are unavailable, unstable, or undesirable.

This repository is an actively developed engineering project, not an abandoned code drop and not a code archive.

If you are evaluating, integrating, porting, modifying, or using this codebase in any product, research, deployment, or internal tooling — you are encouraged to contact the author.

---

## Copyright and source attribution

Liu Weikai (Vic Liu) maintains Trail Mate. Original maintainer-owned material is
available under MIT within the scope of [LICENSE](LICENSE).

The RNS/LXMF implementations are derived from Mark Qvist's reference works,
including source-derived generated and regenerated code:

* [Reticulum](https://github.com/markqvist/Reticulum):
  Copyright (c) 2016-2026 Mark Qvist, unless otherwise noted.
* [LXMF](https://github.com/markqvist/LXMF):
  Copyright (c) 2020-2026 Mark Qvist, unless otherwise noted.

Those portions retain the **Reticulum License**, with the complete
[derivative notice](LICENSES/Reticulum-Derivatives.txt) and original
[RNS](LICENSES/Reticulum-RNS.txt) / [LXMF](LICENSES/Reticulum-LXMF.txt) notices.
The additions/modifications line covers only original contributions the
maintainer actually owns.

Earlier blanket AGPL descriptions and maintainer-ownership claims were mistakes
and are void for those portions, including in earlier revisions and releases.
The original copyright and Reticulum License grants remain applicable.
Valid prior grants for material a licensor had authority to license are preserved.

The C++ port aims to faithfully preserve upstream protocol behavior for supported
features while adapting memory ownership, storage and work scheduling to ESP32
resource limits alongside Trail Mate's application workloads. These adaptations
retain the upstream-derived material's copyright and license. See the
[embedded implementation approach](LICENSING.md#embedded-implementation-approach).

## Other component notices

TMAP's eligible owner-held originals have an additional
[MIT grant](LICENSES/TMAP-MIT.txt) within its
[recorded scope](docs/licensing/TMAP_RELICENSING.md). The fixed historical package
and third-party notices are preserved.

MessagePack vendor material retains Copyright (c) 2013-2020 vsergeev /
Ivan (Vanya) A. Sergeev and its
[MIT notice](LICENSES/u-msgpack-python-MIT.txt). Other crypto, protocol, library
and data authors retain their own copyrights and licenses.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the
[RNS/LXMF source inventory](docs/licensing/reticulum-sources.csv) for component
scope and notice locations. Applicable complete copyright and permission
notices must accompany covered distributions.

LXST requires separate authorization for the Trail Mate derivative. Affected
releases remain paused as described in [LICENSING.md](LICENSING.md).

---

## Contact

* Email: **[vicliu@outlook.com](mailto:vicliu@outlook.com)**
* WeChat: **vicliu890624**

You may contact me for:

* Hardware adaptation or porting
* Protocol clarification
* Integration into existing radio systems
* Commercial services and permissions for maintainer-owned material
* Collaboration or joint development
* Field deployment guidance
* Bug reports that are difficult to describe via Issues

WeChat is preferred for real-time technical discussion.
Email is preferred for formal or long-form communication.

---

## For Organizations / Companies

If your organization is testing or evaluating this repository internally:

You are welcome to reach out even if you are only in the research or feasibility stage.
Early technical discussion usually saves significant engineering time.

Customization, hardware adaptation guidance, and technical consulting are available.

---

## Licensing Reminder

The maintainer's MIT grant covers only material for which the maintainer holds
the necessary rights. Preserve each upstream license and copyright notice.
See [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md) for scope and current
distribution restrictions.

---

## Intent

The goal of Trail-mate is to enable practical, human-centered, off-grid communication and coordination.
Constructive feedback, real-world testing reports, and implementation experiences are especially appreciated.

You are not required to open Issues before contacting the author directly.

Thank you for taking the time to examine and use this project.
