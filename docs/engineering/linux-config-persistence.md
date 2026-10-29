# Linux application configuration ownership

An installed CM0 form test exposed Reticulum group edits disappearing after Save.
The SDL input path displayed the exact mixed-case/punctuation text, but the
group did not survive the apply operation. The service regression reproduced
the same loss without the UI: `applyMeshConfig()` reloaded the legacy TSV file
over the just-edited application configuration.

The application snapshot is now the runtime owner of Reticulum groups, matching
the production configuration model. Applying mesh configuration never reloads
the old file. Snapshot version 3 records this ownership, including an intentionally
empty group list. Version 2 snapshots remain readable. At startup, an old empty
snapshot can import the legacy TSV once; an already populated snapshot takes
precedence. The migration also works while another mesh protocol is active.
The legacy source is retained, and an unavailable load is not marked migrated
by the startup loader. A version 3 empty list does not resurrect deleted old groups.

The explicit SDL and Cardputer loops call `updateCoreServices()` rather than the
convenience `tick()` entry point. Debounced persistence now runs in that common
update path, so both loop styles save while the app is running. Shutdown retains
the existing immediate-flush behavior.

On 22 October 2026 native loading was changed to distinguish a missing working
value from invalid or unavailable storage. Only a missing value admits the
first-start defaults and legacy migration. Wrong kind/size, unsupported header
or version, and storage errors stop initialization without replacing the
working value. Supported version-2 migration remains covered. Reset-marker
reads also use the strict boolean port, preventing malformed metadata from
authorizing a discarded group source. See
[strict native settings reads](linux-strict-settings-reads.md) for the current
regressions, installed package and incomplete TMS integration scope.

## Regression verification

The new service test first failed because applying settings restored the old
group name. It now verifies live edits, durable storage before shutdown through
the explicit loop, process-owner reconstruction, modern snapshot precedence,
intentional deletion, and version 2-to-3 migration. The rebuilt native suite
passed 110/110 tests and Cardputer passed 14/14. Platform UI boundaries and ESP
stack hygiene passed. The standard armhf deb uses the target Raspbian C++ ABI
overlay; its SHA-256 is
`a0bb12f044bdd7d12a952c6280ce3dd7781a09d4aba74fe7b8ce6e54152ba636`.

## Installed CM0 form verification

The exact package was installed and passed `dpkg -V`. In an isolated local-mode
Wayland session, Tab/Enter opened Contacts, pointer actions selected Groups and
Add Group, and virtual keyboard input entered `CM0 Ab!9` plus a test destination
hash. The actual form displayed the expected case and punctuation. Before the
fix, Save closed the form but the record disappeared and the isolated database
contained no matching name. After the fix, the list displayed the new record and
the isolated SQLite database contained the exact name. A fresh application process
with no diagnostic preload displayed the same saved group again. Settings, cache
and SD roots were isolated for the successful save/reload test. No chat or radio
message was sent. This verifies the installed compositor/input/editor/config
path, not physical keyboard switches or touchscreen electrical behavior.

The saved group then opened the shared Chat compose screen, where `Draft Ab!9`
was entered and visually confirmed in the actual text area without pressing
Send. Final focused fullscreen Alt+F4 exited with status zero in 1.132 seconds.
RTC readiness/system-source remained valid with a zero-second UTC difference;
no device reboot was performed for this change.
