# SD transport strategies and WIO L2 memory budget

The Arduino SD runtime retains one file API and one filesystem ownership lock.
Boards supply wiring; transport selection is a firmware build choice, not a
board-name branch inside file reads, writes, or the block driver.

## Responsibilities

| Layer | Responsibility |
| --- | --- |
| Board | Power sequencing and typed wiring/host configuration |
| SD runtime | Files/directories, filesystem mutex, application/external raw owner, errors and flush semantics |
| Transfer policy | Maximum file request slice for the selected transport |
| Operation scope | Transport-specific context within the filesystem lock |
| SPI driver hooks | Existing shared coordinator leases at physical transactions |
| SDMMC block device | Host/card lifetime, DMA compatibility, multi-sector batches and owned bounce buffer |

`SharedSpiSdTransferPolicy` keeps the existing 512-byte file slice. The SPI
operation scope preserves policy, deadline input, owner label, failure status,
and nested context restoration. The coordinator, SPI mount/recovery sequence,
bus pin restoration and physical transaction hooks keep their existing behavior.
File slicing alone does not release a physical bus lease.

`SdmmcSdTransferPolicy` uses 16 KiB file requests, at most 32 sectors per direct
DMA call, and a 4 KiB (8-sector) internal DMA bounce buffer. File requests and
DMA batch sizes are deliberately independent. SdFat still controls metadata,
unaligned file offsets, cluster boundaries and fragmented extents.

The SDMMC scope does not create SPI context or acquire the shared SPI bus.
Both transports retain the filesystem lock and external raw-access ownership
checks. Chunk boundaries do not release the filesystem lock or permit an
ownership handoff in the middle of an operation.

## SDMMC configuration and lifecycle

New boards use `mount_sd_card(const SdmmcSdConfig&)`. The config describes slot,
CLK/CMD/data pins, 1-bit or 4-bit width, permitted clock ceiling and pullups.
WIO supplies its three pins and retains 1-bit, 20 MHz maximum configuration.
The maximum is not a measured or negotiated bus frequency. No high-speed
clock change is part of this optimization; 4-bit support requires actual
wiring and validation on that board. The old three-pin entry point delegates
to the same typed implementation for compatibility.

The block device owns a DMA buffer allocated once at initialization and freed
on initialization failure or unmount. It is non-copyable. Direct transfers
require an aligned buffer whose entire address range is conservatively within
the target's internal DMA-capable region. PSRAM and unaligned callers use the
bounce buffer. Each hardware submission has a bounded sector count.

Zero-length operations perform no I/O. Range/size overflow is rejected before
transfer. A failed batch stops the operation; there is no implicit retry after
a possibly partial write. The bool block-device API cannot report how many
sectors were committed before a failure. A failed bounced read is not copied
to the caller. Earlier successful batches may already have changed the caller
buffer or card.

## RAM changes

- WIO release firmware opts into the existing build-private TinyUSB DFU trim.
  The framework package remains untouched. Hardware USB serial/JTAG and ROM
  download behavior are independent of the removed application DFU classes.
- A version-checked, idempotent prebuild transformation adds an opt-out around
  audio-driver 0.3.1's global codec instances in WIO's environment-local
  dependency. WIO defines that opt-out and owns one ES8311 codec instance.
  Codec classes and their implementation remain upstream. The transformation
  does not run for T-Deck/Pager and refuses unknown library versions/layouts.
- Removing the 512-byte static SD scratch buffer lowers static BSS, but its
  replacement consumes 4096 bytes of internal heap while mounted. This is a
  throughput/memory tradeoff, not a 4096-byte memory saving.
- Touch IME button-map pointer arrays are immutable as well as their strings.
  The six arrays can live in flash instead of consuming 872 bytes of WIO's
  static internal RAM. This is a shared UI fix, not a board-specific bypass.
- The WIO board singleton follows T-Deck's placement policy: prefer PSRAM,
  fall back to internal heap, and keep only the pointer in static storage.
  The current OPI SDK initializes PSRAM before global constructors. This
  relocates the 1,256-byte object; it does not eliminate its memory use.
  Radio handling is polled in task context. RTOS locks and peripheral DMA
  buffers remain separately owned and are not moved to PSRAM.
- WIO uses its own Arduino pin variant. The generic ESP32-S3 variant declared
  a GPIO48 NeoPixel even though WIO uses GPIO48 for I2C SCL and its user LED
  is on the I/O expander. Correcting the variant removes the unintended
  GPIO -> RGB -> RMT dependency. Compile-time checks keep the UART/I2C/SPI
  defaults consistent with the board profile and reject built-in LED aliases.
  This does not disable RMT globally or change another board's variant.

## Verification

The first successful WIO build after these changes reports 100,236 bytes of
static RAM and 4,069,033 bytes of flash, versus 108,156 and 4,137,609 bytes in
the pre-change build. The reductions are 7,920 bytes and 68,576 bytes. The new
4 KiB mounted DMA heap allocation must be accounted for separately. The ELF
no longer contains `_dfu_ctx`, the old `s_sector_scratch`, or unused global
codec instances; the board-owned ES8311 instance remains.

All three firmware builds completed successfully:

| Environment | Static RAM | Flash | Comparison |
| --- | ---: | ---: | --- |
| `wio_tracker_l2` | 100,236 B | 4,069,033 B | RAM -7,920 B; flash -68,576 B |
| `tdeck` | 95,080 B | 3,982,017 B | RAM unchanged; flash +16 B |
| `tlora_pager_sx1262` | 98,728 B | 4,179,165 B | Successful regression build; no same-revision pre-change size baseline |

The first T-Deck attempt stopped before C++ compilation because SCons could
not import `SCons.Tool.FortranCommon`. A subsequent direct import succeeded,
and the queued retry completed successfully without a firmware-source change
or a modification to the shared tool package.

The second RAM pass reduces WIO to **97,652 bytes static RAM** and
**4,059,745 bytes flash**: another 2,584 bytes of static RAM and 9,288 bytes
of flash below the first pass, or 10,504 / 77,864 bytes below the original
build. ELF inspection confirms all six updated IME maps are flash-mapped
(`0x3c...`), the board singleton is a four-byte pointer rather than a
1,256-byte static object, and `g_rmt_objects`, `rmt_contex`, `neopixelWrite`
and `rmtWriteBlocking` are absent. The 4 KiB mounted SDMMC DMA allocation
and transfer policies are unchanged by this pass. Static RAM is not a
measurement of total runtime memory consumption.

Second-pass firmware verification completed with exit code zero for all
three environments (T-Deck succeeded on a queued retry after the same
pre-compilation SCons import failure described above):

| Environment | Static RAM | Flash | Change from first pass |
| --- | ---: | ---: | --- |
| `wio_tracker_l2` | 97,652 B | 4,059,745 B | RAM -2,584 B; flash -9,288 B |
| `tdeck` | 95,080 B | 3,982,017 B | Unchanged |
| `tlora_pager_sx1262` | 98,728 B | 4,179,165 B | Unchanged |

WIO's remaining static-RAM gap to T-Deck is 2,572 bytes. Its display, audio,
SDMMC and synchronization state is not identical to T-Deck's; exact equality
is not an acceptance criterion. Check runtime internal heap and PSRAM on
hardware rather than treating a smaller static number as sufficient proof.

The real-LVGL touch editor host regression also passes with the immutable
maps, alongside all four SD transport/filesystem host tests and the three
build-configuration tests.

Host tests run the production block driver with injected SDMMC/heap failures,
and run the repository's real SdFat FAT16/FAT32/exFAT sources over the same
driver with a sparse in-memory card. They check actual multi-sector calls,
data after close/remount, non-sector-sized tails, unaligned/PSRAM-like buffers,
unaligned file seeks, interleaved file allocations, direct DMA batches, bounds,
partial failures and cleanup. Policy tests cover
both build selections and nested SPI operation status restoration.

For the 71,717-byte test file, changing the file slice from 512 bytes to 16 KiB
reduced fake-hardware read submissions from 142 to 37 (FAT16), 142 to 20
(FAT32), and 141 to 19 (exFAT). FAT16's smaller clusters cap batches at four
sectors in this fixture; FAT32/exFAT reach eight. These are request-count
measurements with the real filesystem/driver, not real-card throughput claims.

```sh
cmake -S tests/sd_transport -B build-sd-transport -G Ninja
cmake --build build-sd-transport
ctest --test-dir build-sd-transport --output-on-failure
python tests/sd_transport/test_build_configuration.py
```

Use GCC/Clang for these standalone host tests. Firmware verification covers
`wio_tracker_l2`, `tdeck` and `tlora_pager_sx1262`, plus the repository's
`check_shared_spi_*` boundary checks. Builds alone do not prove electrical
stability or real-card throughput.

On-device acceptance can use the existing SD card/filesystem and measure
sequential 4 KiB/64 KiB/1 MiB/16 MiB reads and writes, checking readback hashes.
Repeat with UI, LoRa and BLE activity. Record throughput, operation latency,
minimum internal free heap and largest free block after mount and after
repeated unmount/remount. Retain configuration-save flush behavior. Real-card
MB/s, tail latency, audio and USB behavior must be reported as hardware results
only after those checks have actually run.

For this change, hardware acceptance is performed by the device owner. Useful
checks, in order:

1. WIO boots, mounts the existing card, reads map tiles, saves/reloads config,
   and plays/records audio using ES8311.
2. Exercise file operations on the candidate firmware, with per-chunk serial
   tracing disabled for throughput measurements. Check data after close and
   remount; do not count only bytes accepted into a write cache. The owner has
   declined A/B flashing; it is not required by this verification plan.
3. Repeat during map scrolling and radio traffic. Check internal heap after
   several mount/unmount cycles, accounting for the mounted 4 KiB DMA buffer.
4. On T-Deck/Pager, repeat map scrolling, radio traffic and SD save/load together;
   check display/radio recovery after SD unmount/remount and USB raw ownership
   transitions where that feature is enabled.
5. Confirm WIO USB serial and the ROM BOOT/RESET download path. Application
   TinyUSB DFU is intentionally absent from the release firmware.

The subsequent map pipeline reliability work is documented in
`map-tile-pipeline-reliability.md`. Its phase timings distinguish storage work,
completion waiting, decode and UI updates without a board-specific map path.
