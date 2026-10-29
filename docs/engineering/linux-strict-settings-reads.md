# Strict native settings reads

The Linux settings port now implements the existing strict signed/unsigned
integer, byte, boolean and string readers alongside the strict blob reader.
These are required by the formal TMS settings extension. They distinguish
`Ok`, `Missing`, `Invalid` and `Unavailable`; only a missing authoritative value
permits a default. Existing permissive getters retain their compatibility
behavior for existing callers.

The native readers share one bounded SQLite row lookup. Values must retain
their expected one-byte kind and BLOB storage representation. Signed/unsigned
numbers require complete base-10 parsing and range checks, without whitespace,
suffixes or unsigned-negative coercion. Byte reads admit unsigned native values
up to 255. Booleans use the existing native boolean kind with exactly `0` or
`1`. Strings preserve their bytes, require room for a terminator and reject
embedded NUL. Failure resets output values and lengths. No new persistent
format or table schema is introduced.

The SQL projection evaluates the BLOB payload only after type and size checks.
Nested CASE expressions provide the required lazy evaluation, and BLOB lengths
can be measured without reading the full value. These properties follow
[SQLite CASE semantics](https://www.sqlite.org/lang_expr.html#the_case_expression)
and [SQLite length semantics](https://www.sqlite.org/lang_corefunc.html#length).
The regression stores rejected 8 MiB BLOB and TEXT values and checks SQLite's
largest allocation while reading into a small output buffer. The rejected
payload is not materialized as one allocation.

Application startup now uses the strict blob result. A missing working value
retains the established defaults and one-time group migration. Wrong kind,
wrong length, unsupported header/version/protocol, or unavailable storage stops
service initialization and preserves the working value. A failed migration
write also fails initialization. Supported version-2 snapshots still migrate
to version 3 through the existing path. Reset-marker reads use the strict
boolean port, so malformed metadata cannot authorize old SD group imports.

This strengthens the existing legacy object-blob admission checks; it is not
full semantic validation of every field in that object representation. The
complete canonical TMS import/export, explicit recovery UI and full settings
snapshot composition still need integration. A subsequent native transaction
view now supports consistent profile snapshots; see
[Wi-Fi profile storage](linux-wifi-profile-storage.md). Standalone strict reads
retain their serialized row lookup.

The regression exercises integer limits, overflow, invalid suffixes/signs,
boolean kind/value errors, UTF-8 byte preservation, capacity limits, NUL in
string/kind metadata, wrong SQLite storage classes, empty blobs and rejected
large payloads. Real application services also reject malformed working values
without replacing them from an old migration file. A broken settings schema
returns unavailable rather than missing. A malformed reset marker suppresses
legacy import. Temporary roots isolate every test from normal configuration.

On 22 October 2026 the full native suite passed 153/153 and Cardputer Zero
passed 18/18. The strict reader implementation was instrumented with address
and undefined-behavior sanitizers in its native fixture, which passed ten
consecutive runs. Strict reads, reset, multi-endpoint settings and legacy
configuration migration ARM fixtures passed on CM0. Full native TMS support
remains unfinished.

The standard armhf deb was installed on CM0, SHA-256
`40f4c107273bb89f5ac44f4ef362924af02cee16e3d3f58d088bfc8185f3e35e`.
Lintian produced no diagnostics; `dpkg -V`, the original logo and installed
Crypto license checks passed. The actual installed application was launched
with an isolated malformed working blob and a valid old group migration file.
It exited with code 1, reported invalid configuration, retained the original
bytes and did not import the old group. Normal homepage and active packaged-map
sessions passed fullscreen/focus and Alt+F4 exit with code zero in 0.782 and
0.681 seconds. The preceding package is retained as
`trailmate-cm0-before-strict-settings.deb` in the validation cache.
