# Geocaching local-read performance reproduction

## Current evidence (2026-10-01, catalogue-scoped row invalidation)

With `TRAIL_MATE_TEST_IO_DELAY_MS=25` and
`TRAIL_MATE_TEST_IO_PROFILE=1`, the production-default overlay polling interval
is 100 ms. Do not set the diagnostic polling override for acceptance runs.
The current browse runtime test passes its unchanged three-second gates:

| Scenario | Simulated elapsed | I/O operations | Result |
| --- | ---: | ---: | --- |
| Two local markers, direct Map cold start | 1,795 ms | 50 | Passed |
| Discover to local lists | 1,840 ms | — | Passed |
| Local cold-start acceptance | 2,515 ms | — | Passed |

This is a simulated storage sensitivity result, not an L2 device measurement.
The older measurements below describe intermediate implementations.

An unrelated download now preserves the loaded publication catalogue's row
token. The LVGL page tests also change status text and button availability while
holding the row token fixed: labels update without reading or recreating rows.
The journal integration test removes completed task shards during background
publication recovery, which must finish without those reads; restoring the
shards then permits the exact confirmed-receipt lookup.

### Current cache-head generations and legacy history

The production writer now replaces table 2 shards with current-only generations.
The history test also exercises two colliding cache heads in one transaction,
40 repeated updates, retention of the preceding committed generation, deletion
of one and then both keys, legacy conversion while retaining an unchanged key,
and failed read-back that must leave both committed shard heads intact. After
repeated updates, scanning two live heads reads exactly two 152-byte references
from the selected generation and none from older generations. A current-only
single-row fixture reads 152 bytes even when its obsolete append file is corrupt.

Untouched legacy cache-head buckets are not converted on read. They still need
conversion, and obsolete generation files currently await exclusive slot cleanup.
These remain incomplete parts of history-independent operation on existing cards.

Run `geocaching_index_scan_history_test` to reproduce two colliding keys,
60 obsolete versions and a tombstone, leaving one live row. It exercises the
same `SdIndexScan` backend used by Downloaded, with table 11 test records; it
is not a direct measurement of a Downloaded page.

| Scanner workspace | Live rows | I/O operations | Shard bytes read | Steps |
| --- | ---: | ---: | ---: | ---: |
| 144 bytes, lookup fallback | 1 | 413 | 25,688 | 741 |
| 8,192 bytes, adjacent-reference optimization | 1 | 236 | 16,720 | 269 |

The legacy scanner still reads obsolete references and validates their CRCs.
Consequently the two-marker latency gate does not prove history-independent
pagination. Changing only the page size, timer or adjacent-reference shortcut
cannot satisfy that requirement. A current-record traversal needs a committed
index representation that omits superseded references, while preserving pinned
old roots and crash recovery. The authoritative journal and signature history
must remain separate from that read projection.

The native browse runtime links the production local stores and storage worker.
Its SD seam charges a configurable simulated duration per I/O operation. This is
a sensitivity model, not a measurement of device latency.

After building `geocaching_browse_runtime_test` from
`modules/core_geocaching/tests`, set `TRAIL_MATE_TEST_IO_DELAY_MS=25` and
`TRAIL_MATE_TEST_IO_PROFILE=1`, then run the executable with
`modules/core_geocaching/tests/fixtures` as its argument. Add `local-draft-only`
as a second argument to isolate an unpublished draft. The default delay remains
5 ms. The three-second assertion is unchanged at every delay.

The profile includes both streaming file reads and whole-file metadata reads.
Direct Map prints every file's bytes and the total I/O operation count.

On 2026-09-30, the 25 ms model failed the existing budget:

| Scenario | Simulated elapsed | Result |
| --- | ---: | --- |
| Single unpublished draft, direct Map | 3,045 ms | Failed |
| Two local markers, direct Map cold start | 6,160 ms | Failed |

The two-marker run performed 199 I/O operations and read 2,296 bytes from the
28-byte volume header: 82 header reads. The current 5 ms regression passing
does not establish acceptable device performance. Repeated volume checks are
an optimization target, but media replacement detection must remain intact.

The first session-based value-reader optimization reduces this run to 195 I/O
operations and 76 volume-header reads (2,128 bytes), taking 6,130 ms in the same
25 ms model. This remains a failed performance gate. The reader validates the
volume before reading and rejects completion after a media-session change,
including a remount with an identical header. It no longer reopens the header
to complete that same value read. No global volume cache is introduced.

Sharing a verified volume session across each indexed get/scan reduces the same
run further to 178 operations and 54 header reads (1,512 bytes), at 6,115 ms.
Subreaders still validate shard checksums, sequence boundaries and record shape.
The session belongs to that one operation; a new query revalidates the header.
The three-second target remains unmet. Fewer I/O operations have not yet removed
the dominant end-to-end delay, so this is not a completed latency fix.
