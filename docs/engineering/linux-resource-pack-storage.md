# Linux resource pack storage

Linux compositions pass their installed resource root to the shared localization
registry before its first access. The registry knows neither Linux device names
nor XDG conventions. Embedded compositions retain their existing default paths.
Root changes are rejected once registry records may have escaped to UI consumers.
Repeated configuration with the same root remains valid for successive UI sessions.

The installed font, locale and IME directories are under
`<resolved data root>/trailmate/packs/{fonts,locales,ime}`. The data root follows
the existing runtime-path composition and `TRAIL_MATE_SD_ROOT` override.
`TRAIL_MATE_RESOURCE_PACK_ROOT` overrides the complete installed resource root.
Both the P4 touch Linux entry and Cardputer Zero shell supply this path before
starting shared UI services.

The local extension catalog is separate from installed resources. It defaults to
`<resolved data root>/pack-catalog`; `TRAIL_MATE_PACK_ROOT` selects an alternative
catalog containing package directories and their `package.ini` manifests. A
deployed executable no longer uses a source-code build-machine path as its catalog.

Local directory packages now install actual resources through `ResourcePackStore`.
Each snapshot contains the flattened resource tree, complete installed package
payloads under `.packages`, and a preserved `.base` for resources deployed manually
before the first managed install. Installed records come from this snapshot rather
than legacy settings IDs. They remain readable when the original catalog is offline.

An installation clones the current snapshot into a sibling staging directory,
replaces the selected package, merges resource ownership, validates required files
and locale/IME dependencies, and synchronizes files and directories. Identical
shared resource files can have multiple owners; conflicting bytes reject the
transaction. Uninstall regenerates the merged tree from remaining packages and
the manual baseline. Removing a dependency still needed by another locale fails.

Publication uses an atomic directory rename for the first snapshot and Linux
`renameat2(RENAME_EXCHANGE)` for replacements. Filesystems without exchange support
reject replacement without falling back to a partially visible multi-step move.
The parent directory is synchronized after publication. These operations follow
the Linux [rename](https://man7.org/linux/man-pages/man2/renameat2.2.html) and
[fsync](https://man7.org/linux/man-pages/man2/fsync.2.html) contracts.
An advisory lock serializes publication across processes and protects record reads.

Before-publication failures leave the active snapshot intact. A process killed
while staging can leave an unreferenced sibling stage, but cannot publish half a
package. A synchronization error after publication can leave the complete new
snapshot visible while returning an error; callers must reload installed records.
The backend bounds snapshot input to 50,000 entries and 1 GiB of copied data,
rejects links/special files and rejects manifest paths escaping their resource.
Snapshot cloning and staging require additional free disk space; this is not an
in-place delta installer.

The UI's installation entry queues a worker and publishes mutex-protected status.
The worker makes no LVGL calls; the UI reloads localization after completion.
Uninstall retains the existing synchronous API and reloads localization on the UI
owner after success. Large uninstall snapshots can still delay the UI and require
a separate asynchronous contract. This implementation accepts local package
directories, not downloaded archives or a remote catalog.

Compatibility uses the project build version (CI tag before Changelog) and the
memory-profile name supplied by each Linux UI composition from the shared runtime.
The repository computes display flags, while the transaction re-evaluates the
captured `package.ini` instead of trusting caller flags. Missing constraints are
unrestricted. A declared minimum with an invalid/unknown version is incompatible;
a restricted profile list with no configured profile is incompatible. Version
precedence follows [SemVer 2.0](https://semver.org/spec/v2.0.0.html), including
numeric prerelease identifiers and ignored build metadata. Configuring a profile
does not grant extra font RAM: compatibility follows actual shared-UI budgets.
Policy configuration is immutable and idempotent for matching application facts.

`scripts/project_version.py` resolves CI tags before using the existing Changelog
reader, including historical BOM-marked encodings. CMake generates one private
build-identity header; version changes rebuild its small source rather than adding
a version define to every common source. Runtime device metadata and default CM0
and Cardputer Zero package versions use this source. Cardputer container builds
forward CI ref metadata. Explicit package-version overrides remain supported;
existing CMake cache values remain explicit until cleared with the appropriate
`-U...PACKAGE_VERSION` option. The version identifies the shared project source;
hardware readiness remains target-specific as recorded in the device ledger.

The resource-root regression creates a real locale under a custom path containing
spaces and verifies discovery through LVGL's POSIX file port and the shared
localization registry. It also checks default/override path resolution and rejects
root mutation after registry initialization. It also installs a real local bundle,
reloads the registry, switches to its translated strings, uninstalls it and verifies
that only the managed locale disappears. The store regression covers reopen in a
child process, identical shared-resource ownership, conflicting payloads,
publication failure, process termination before publication, missing files and
dependencies, forbidden links, offline-catalog removal and worker completion.
