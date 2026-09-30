# Geocaching memory ownership audit

Audited on 2026-09-29 for `wio_tracker_l2`, against the Geocaching branch.

## Allocation contract

Geocaching application objects and workspaces use PSRAM on ESP. Allocation
failure must return an error or retry without falling back to internal RAM.
`platform::memory::PsramPtr` owns objects with matching construction,
destruction, and heap-capability allocation. Host builds preserve nothrow-new
fault injection.

This covers session state, catalogues, index readers and writers, checkpoint
jobs, publication and download jobs, response mailboxes, record details, page
state, editors, map visits, and the local marker cache. Existing protocol and
record byte buffers retain their PSRAM-only allocation policy.

While the Geocaching page or its map overlay is alive, LVGL allocations use
PSRAM, including small labels, text areas, keyboard state, and deferred layout
allocations. Nested lifetimes are reference counted. Reallocation of an
existing PSRAM allocation stays in PSRAM after the page closes. This policy
also applies to other LVGL work performed during that lifetime.

The Reticulum adapter object uses a PSRAM allocator compatible with its
existing polymorphic ownership. SD file and iterator objects and auxiliary
buffers cannot fall back to internal RAM on PSRAM-equipped targets. Targets
without PSRAM retain the existing SD internal-memory path. The TCP settings
rollback copy is allocated in PSRAM rather than placed on the task stack.

## Root cause of blocked local reads

The index workspace requested 28,736 bytes of PSRAM, but its admission check
also demanded 4,096 bytes of internal RAM above a 40,960-byte reserve. On the
reported device, internal free memory was 38,904–40,112 bytes while over 7 MB
of PSRAM remained available. The check therefore rejected local work forever.

The workspace now charges only its actual PSRAM requirement. This changes an
incorrect admission condition; it does not itself reclaim 44 KB of RAM.

Repeated pending draft reads also rebuilt focus membership and focused the
bottom Back control on every poll. Pending polls now preserve focus and scroll;
only the initial transition configures the loading view.

## Device compiler measurements

Selected object sizes from the L2 firmware DWARF data:

| Object | Bytes | Storage |
| --- | ---: | --- |
| LocalMapOverlay | 5,920 | PSRAM |
| CacheDetail | 2,864 | PSRAM |
| Session | 1,656 | PSRAM |
| DraftCatalog | 1,336 | PSRAM |
| QueryClient | 1,224 | PSRAM |
| SavedCacheCatalog | 1,000 | PSRAM |
| PageState | 496 | PSRAM |
| DownloadClient | 384 | PSRAM |
| Publication | 376 | PSRAM |
| MapVisit | 240 | PSRAM |
| Announcement | 228 | PSRAM, only while queued |
| Editor | 192 | PSRAM |

These are object sizes, not simultaneous peak usage or measured heap savings.
Previously, ordinary allocation could select internal RAM; the new policy
makes the memory domain explicit. Dynamic payloads and LVGL children are
additional PSRAM allocations.

The firmware links with 99,084 bytes of static internal RAM for the whole
application. Symbols containing `geocaching`, plus the UI lease counter,
account for 147 bytes in DRAM: pointers, counters, flags and small control
state. This symbol-based subtotal excludes shared infrastructure and runtime
heap allocations. The 12,288-byte Geocaching icon is in mapped Flash, not
internal RAM.

## Remaining internal memory

This change does not claim zero internal RAM for the entire feature. Task
stacks, RTOS synchronization, driver DMA buffers, and Wi-Fi/lwIP allocations
remain internal. Geocaching reuses the existing storage-owner task; it does
not add a task or enlarge its 8 KB stack. The SD DMA scratch buffer remains
hardware-accessible memory. The three configured TCP endpoints are retained;
their SDK connection costs require runtime measurement.

Compiled Geocaching function stack frames reach 1,088 bytes in the legacy
download-install preparation path and 992 bytes in the runtime dispatcher.
These are individual frame sizes, not maximum call-chain usage. Reducing a
frame does not release an already allocated RTOS stack. Stack-size reductions
require device high-water measurements and are not inferred from these sizes.

No comparable before/after device heap capture was available. An older local
ELF was not built from the exact preceding revision, so it is not used to
claim total bytes reclaimed.

## Verification

- All 85 core/device-session native tests pass.
- All three Geocaching UI viewport tests pass.
- A cold local-draft reopen with only 38,904 bytes of simulated internal free
  memory loads its map marker within 1,500 simulated milliseconds and its
  editor within the three-second test limit, without network traffic or writes.
- ESP allocator fakes reject any non-PSRAM capability request for application
  objects. Allocation and reallocation failure tests verify no internal fallback,
  preserved old allocations, balanced UI lifetimes, and complete release.
- Pending-read UI tests verify that repeated polls preserve scroll and focus.
- The ESP stack hygiene check passes; the L2 firmware build completes.

Native timing is a regression bound, not an on-device latency measurement.
