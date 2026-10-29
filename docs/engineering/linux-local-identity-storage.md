# Linux local identity storage consistency

The local identity port keeps caller-supplied namespace/public/private selectors.
Its existing 32-byte public/private BLOB layout is unchanged. The working source
now uses the native settings transaction seam rather than independent operations
for a single key pair.

`save()` commits both BLOBs in one transaction. Failure at either write rolls
back the complete pair. Transient mutation payloads containing private bytes are
wiped before their strings are released. `load()` reads both keys through one
`SettingsReadView` snapshot and publishes the caller's output only after complete
validation. Missing, corrupt and unavailable storage remain distinct: only two
missing entries return NotFound; unavailable reads return IoError, never an
invitation to treat an existing identity as absent. Partial results remain invalid
and their temporary private bytes are wiped.

`clear()` removes the pair atomically and returns an I/O failure when deletion
is rejected. It no longer returns unconditional success. Empty/aliased key
selectors are rejected as invalid arguments. The transaction remains subject to
the native settings reset/import fence. No board ID, bus name or device path is
introduced into the common store.

## Evidence

Four fixtures failed against the preceding implementation:

- An injected second-key write failure left the new public key with the old
  private key.
- A rejected private-key deletion still returned successful clear.
- A deliberately unreadable settings view was reported as missing identity.
- A separate writer process committed between the public/private reads, causing
  a mixed pair.

After the fix all four pass. The snapshot fixture uses SQLite statement tracing
and pipes to place an actual second-process commit between the two key reads;
it checks the complete old pair, then observes the complete new pair on a fresh
load. It targets the business key selector rather than a specific SELECT column
count. The store unit is instrumented with ASan/UBSan.

The focused gate passed 6/6, including existing verified voice-contact key
derivation and actual MT/MC MQTT application/private-chat regression. ESP stack
hygiene and Reticulum scope guards also pass. These tests use generated temporary
roots and fixture data, with no physical radio or user identity mutation.

This correction is included in the delivered CM0 package recorded by the
[CM0 acceptance audit](cm0-adaptation-acceptance.md), alongside
formal radio operations 7-9. All four atomic-store scenarios also passed on the
actual ARM device. The full native gate passed 234/234 and the compact gate 19/19.
Formal author-identity operations 4-6 remain a separate coordinator sharing the
same frontend lifecycle bridge. See [wireless identity storage](linux-radio-identity-storage.md).
