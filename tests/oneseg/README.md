# CM0 1seg receive-only configuration tests

```sh
cmake -S tests/oneseg -B .codex-build/oneseg -G Ninja
cmake --build .codex-build/oneseg
ctest --test-dir .codex-build/oneseg --output-on-failure
```

The test runs the production receive setup with a fake status-checked port.
It checks the Japanese UHF raster, ROM and ownership preflight, SPI wire-rate
lower bound, RX command order, exact tuning bytes, all eight write failure
boundaries and chip errors. No RF is received or transmitted, no board is
opened, and no media is decoded. See the
[CM0 television integration status](../../docs/engineering/t-display-cm0-oneseg-tv.md)
for the remaining continuous reception, decoding, menu and physical gates.

`oneseg_stream` runs the production continuous bridge and pinned RX engine
against a fake packet HAL. It covers empty/active FIFO cancellation, queue
rejection, FIFO faults, eight setup failures, pre-cancelled startup and restoration
of the original packet profile. `oneseg_transport` extracts unchanged production
FIFO, SPI-clock and SPI-transfer methods; fake boundaries check exact framing,
both status bytes, failed clock setting/readback, temporary 16 MHz operation,
and restoration of packet clock/timing. No host test qualifies physical RF or
sustained SPI throughput.

`oneseg_rf_switch` checks explicit/default profile parsing, exact DIO function
and mode commands, rejection of unknown profiles, and all four write failure
boundaries. `oneseg_rf_lifecycle` extracts the production cold-start, GFSK,
LoRa-return and IQ-reset methods. Its fake chip loses RF-switch state on every
initialization and refuses RX until the table has been reapplied. It injects
all four table failures into each path and verifies that no RX starts, the
driver is offline, and later commands are not issued. The default `none`
profile performs no switch writes.

On UNIX, `oneseg_worker` exercises the production threaded worker with bounded
queue overflow, restart generations, cancellation with an empty FIFO, physical
handoff failure, sink failures and radio restoration errors. It also moves
5120 exact bytes through the production pipe sink and checks cleanup ordering.
`oneseg_pipe` checks descriptor ownership, nonblocking flags, generation/index
validation, broken-pipe signal handling, write deadlines and cancellation using
real OS pipes. These add two tests to the five portable tests above.

The main Linux application's `linux_oneseg_iq_probe` test separately checks the
real engineering executable: a missing hardware profile emits no IQ and fails
explicitly, while simulated composition is rejected before capture. These tests
do not claim live samples, television lock or playback.

`oneseg_programs` adds portable production PAT/PMT parsing tests: CRC, split
sections, PUSI pointers, multiple sections, complete-table publication, version
changes, service selection, malformed descriptors, continuity/duplicate handling,
capacity limits and 2,000 bounded untrusted section inputs. Its explicit ARIB
partial-reception tests discover services from actual PMTs without a PAT, check
the standard service/PID relationship, primary preference, manual selection and
fallback to a received PAT. Media packet counters are not playback evidence.
The parser has fixed member storage and must not be a large ESP automatic local.

`oneseg_process` executes production process ownership against a clearly named
child fixture. It checks startup readiness/failure/timeout/cancellation, real pipe
fragmentation, programme parsing, inherited-descriptor and process-group
isolation, stderr flooding, malformed/partial TS, rejected TS queues, forced
termination, child reaping, descriptor counts and session generations. Its
worker composition test verifies that decoder failure cancels an empty FIFO and
reaps the child before radio restoration; failed startup does not acquire radio.

There are six portable tests and nine tests on UNIX. Supplying
`-DTRAIL_MATE_ONESEG_BACKEND_PREFIX=/absolute/private/prefix` adds
`oneseg_decoder` and `oneseg_process_native`, for eleven tests. The former
executes the actual GNU Radio backend through
`apps/linux_lvgl/tests/oneseg_decoder_test.py`. Backend dependencies are never
downloaded or installed by ordinary CMake configuration. The optional recorded
fixture test and private dependency build commands are documented in the
integration status above.

The process test optionally accepts an explicitly supplied signed 576 ksample/s
offline fixture after its decoder and prefix arguments. This tests actual C++
ownership, native decoding and C++ programme parsing together; it cannot prove
live LR2021 reception. Fixtures belong in the local artifact directory and are
not part of the firmware or ordinary device profile.

`-DTRAIL_MATE_TEST_ONESEG_MEDIA=ON` additionally builds the native FFmpeg media
tests and the real SDL PCM adapter tests. Use matching native development
packages or explicitly supply `TRAIL_MATE_ONESEG_FFMPEG_PREFIX`. The PCM CTest
uses SDL's named dummy backend, with actual focus/device lifecycle and no audible
output. Native media and IQ-pipeline executables take explicitly supplied offline
fixtures; ordinary configuration does not fetch or generate broadcast content.

`oneseg_media_test` checks real video/PCM decoding, PTS and RGB565 geometry,
generation/capacity guards, startup cancellation, failed audio begin/write,
throwing cleanup, queue overflow and recovery. A synthetic, clearly labeled
44.1 kHz fixture compares final PCM sample counts to the FFmpeg reference,
including resampler EOF delay. `oneseg_media_pipeline_test` connects the real
C++ process owner, GNU Radio backend, native media decoder, RGB565 retrieval and
SDL dummy device, then checks focus release and child reaping. These tests prove
neither LVGL presentation/A/V synchronization nor audible onboard reception.

`oneseg_tv_session_test` takes four explicit arguments: child-fixture script,
production decoder script, private native backend prefix and recorded signed
IQ path. It runs the asynchronous application session with a clearly labeled
fake radio and SDL dummy output, while GNU Radio, TS parsing and FFmpeg remain
real. It checks start/stop latency, startup errors/cancellation, generation and
media-epoch guards, actual decoded frame acknowledgment, same-programme
selection, restart, child reaping and audio-focus release. It does not qualify
multi-programme switching, LVGL rendering, AV synchronization or board RF/audio.

The optional native-media Linux application also builds
`trailmate_oneseg_tv_page_test`. Its four explicit arguments are the production
decoder script, private backend prefix, recorded signed IQ and output directory.
It drives production TV controls and connects the real asynchronous session,
GNU Radio, FFmpeg, LVGL image and SDL dummy audio. Only the radio input is a
clearly named recorded-input fixture. It checks missing-radio failure, channel
and volume commands, actual nonblank decoded pixels, frame acknowledgment,
320 x 240 normal/fullscreen viewports without upscaling, same-programme requests,
stop/hide and nonblocking page-exit cleanup. Captures visibly label the fixture.
The clock/scheduler follow-up adds a real PCM queue-time estimate and bounded
timed frame selection. `oneseg_audio_clock` checks queued-time arithmetic,
underrun clamping, stale generations, missing/invalid PTS, reset and concurrent
publication. The SDL adapter test checks clock anchoring, discontinuity and
unknown-PTS invalidation. Media tests check due-frame selection and verify that
future/stale/wrong-generation rejection leaves destination pixels unchanged.
The production-page harness also waits for exhausted input, requires an invalid
audio estimate and hidden obsolete video, and checks accepted-frame PTS bounds.
Physical RF, partial-PMT programme expiry and audible end-to-end AV alignment
remain separate acceptance requirements.

`oneseg_tv_programmes_test` exercises programme transitions through the real
session, child ownership, PSI parser, FFmpeg and SDL dummy output. Generate its
explicitly synthetic TS inputs with `make_transport_fixtures.py OUTPUT`, then
pass `decoder_transport_fixture.py` and that output directory to the executable.
The transport child drains synthetic IQ and emits TS at its declared mux rate;
it bypasses OFDM and is never selected by the production runtime factory.
Each programme's SDT name explicitly says `TEST FIXTURE - NO RF`.

The harness selects two programmes with distinct decoded colours, changes a
selected programme's elementary PIDs, removes it from a complete PAT and checks
fallback to the received remaining programme with a new media epoch. Old-epoch
frames are rejected. It also exercises video-only presentation and forward/back
PTS jumps in the same media epoch, and checks child/focus/radio cleanup.
Its append-only control file ignores incomplete last lines, avoiding WSL DrvFS
rename-overwrite visibility races. This does not establish live RF switching,
partial-PMT programme expiry or device A/V latency.
