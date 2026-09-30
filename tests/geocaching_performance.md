# Geocaching local-read performance reproduction

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
