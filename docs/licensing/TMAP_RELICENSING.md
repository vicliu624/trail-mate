# Additional MIT grant for original TMAP material

Effective date: 2026-11-13.

## Grant and exclusions

Liu Weikai (Vic Liu), the owner of the original TMAP implementation, hereby offers
the original code, documentation and other copyrightable material in which he
holds the necessary rights under the full [MIT License](../../LICENSES/TMAP-MIT.txt).
This includes his eligible originals imported from Trail Mate and later original
TMAP contributions. The grant covers his eligible original material at commit
`d74b084acb61653149dec1ac161df58a92278cff`, the fixed source consumed by Trail Mate
as TMAP 0.2.0, as well as the local TMAP source checkpoint
`4e8297d11bc48cb0f427b7c58395cda00f9349ef`.

The grant excludes third-party works, contributions owned by others without their
permission, and portions subject to another upstream license. Preserve any more
specific notices. It does not relicense external tools, dependencies or map/data
assets. Valid earlier AGPL grants for material the licensor had authority to
license remain available; no existing valid grant is revoked.

The TMAP `LICENSE`, `COPYRIGHT`, `NOTICE.md` and `THIRD_PARTY_NOTICES.md` retain
the grant and source provenance. This additional grant applies to the specified
source revisions without replacing the historical release archive.

## Immutable dependency and distribution notices

The existing Git commit, 0.2.0 source archive URL and SHA-256 remain unchanged.
The historical archive and installed dependency manifest still declare
`AGPL-3.0-only`. They are not rewritten. This additional grant supplies MIT as an
option for the owner's eligible original material at that exact source revision.

Keep this scope document and `LICENSES/TMAP-MIT.txt` together with the dependency's
own copyright, provenance and third-party notices in any separately approved
distribution that relies on the additional MIT grant. The exact dependency and
license paths are recorded in [tmap-dependency-license.json](tmap-dependency-license.json).

## Retained third-party provenance

* `src/calendar_condition.cpp` retains Howard Hinnant's attribution and the
  public-domain status described in his
  [civil calendar algorithms](https://howardhinnant.github.io/date_algorithms.html).
* Optional host producer dependency Shapely 2.1.2 retains its upstream
  [BSD-3-Clause terms](https://github.com/shapely/shapely/blob/2.1.2/LICENSE.txt).
  Its transitive libraries and any bundled GEOS distribution retain their own
  licenses and notices. These are not assigned the maintainer's MIT notice.
* External datasets and other tools retain their applicable version-specific
  terms. The component list is not an exhaustive dependency or data audit.

## Publication status

This owner-held MIT grant addresses TMAP's original material. It does not resolve
Meshtastic or other GPL-covered components, combined-work compatibility, LXST
authorization or historical release compliance. Publication remains paused;
the grant does not authorize a combined release with unresolved permissions.
