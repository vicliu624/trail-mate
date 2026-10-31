# T-Display CM0 standalone 1seg television

## Requested product

The user requested a separate CM0 television application on 2026-10-29 and
explicitly prioritized the onboard LR2021 receiver. It belongs in the CM0 app
menu with its own page, Help and controls. It is separate from Protocol Probe
and FM Radio. Existing Cardputer Zero and P4 applications must not acquire a
nonfunctional television entry as a side effect.

The intended path is onboard LR2021 continuous IQ -> OFDM synchronization,
TMCC and FEC -> MPEG transport stream -> programme selection -> video/audio
decode -> LVGL video and the CM0 audio output. A successful command, FIFO read
or generated test picture must never be displayed as received television.

The requested video viewport is 320 x 240 pixels, centered within the page.
Preserve the decoded picture's aspect ratio without upscaling. Fullscreen hides
surrounding information but retains this viewport; it must not stretch video
across the CM0 panel.

## Current source and installed-device status

The current source defaults the TV session and its real decoder subprocess to
automatic waveform acquisition. Explicit research-profile selection remains
available through configuration. Host process and actual-page regressions pass
with offline IQ; this does not qualify onboard LR2021 reception or ARM timing.
At the user's request for hands-on receiver testing, CM0 now has application
`1.1.0~test1` and backend `1.1.0+gitb576783021fb-1`. The automatic receiver,
scan/list/cancellation and retry workflow is installed. Native ARM package and
no-RF UI checks passed before installation; independent device verification
confirmed package versions, installed payload hashes, dynamic dependencies,
automatic TMCC capability and the real decoder ready/cancel lifecycle. SPI/GPIO
node permissions are available to pi. The application was left stopped and no
RF receive command was issued by the agent. Physical reception/audio acceptance
is for the user's current test, not an achieved claim.

Channel persistence, direct retuning, saved-station forward/back selection and
last-service restoration are connected in source and verified through the real
page. The automatic channel scanner and channel-list menu are connected in
source; scan cancellation, channel-selector retuning and full-range scheduling
have passed actual-page regression with recorded IQ. Current local source also
renders converted SDT/EIT text, saves received station names and provides a
scrollable Programme info page. Its bundled Japanese font contains 6,960
characters, including the full-width space. Read-only installed font discovery
and user overrides have passed host tests. These additions are not installed
on the device's test1 package. The newest scheduled-end, running-status and
undefined/invalid-time UI cases have passed regression. The test2 ARM package
has also passed package, glyph-loader and no-RF page startup checks; the complete
workflow still requires device replacement and physical reception/audio acceptance.

The following paragraphs retain successive implementation checkpoints and their
original limitations. An earlier statement that a component is pending is
historical when a later checkpoint explicitly records its implementation and
verification. None of the offline fixtures establishes live LR2021 reception.

### Acceptance ledger for the next device package

| Requirement | Current authoritative evidence | Remaining acceptance |
| --- | --- | --- |
| Onboard LR2021 reception and automatic waveform acquisition | Installed test1 backend capability and no-RF lifecycle checks; host recorded-IQ decoding | Actual broadcast reception on CM0 |
| Automatic scan, cancellation, station list and restoration | Actual-page recorded-IQ regression; full 50-channel scheduler fixture; transactional scan/storage tests | Device scan against available broadcasts and subsequent restart |
| Fixed 320 x 240 video, audio and retry | Actual decoded-frame page regression, audio worker tests, starvation/retry regression | Continuous clean video and audible CM0 output |
| Received station names and present/following programme information | SDT/EIT parser, ARIB conversion and actual-page Japanese metadata assertions | Updated ARM package and broadcast metadata on device |
| Programme schedule and running status | Undefined/invalid BCD, JST/MJD and signed-64-bit helper tests; actual-page dynamic time/status regression | Updated ARM package and device interaction |
| Readable Japanese content and bundled assets | All 6,960 glyph mappings verified from installed host and packaged ARM assets; system-root and user-override tests | Device rendering with received metadata |
| Menu, details scrolling, Help and service switching | Actual-page scrolling/close/ESC checks; keyboard/touch Help; actual decoded red-to-blue service switch without a new radio acquisition | ARM page startup and hands-on device interaction |
| Reproducible application/backend delivery | Test1 installation receipt; current host regression receipt with source hashes; test2 ARM package, private backend import and no-RF page startup checks | Device replacement and independent postflight |

The latest complete host run is recorded in the local
`.codex-build/cm0/programme-final-receipt.json`, with logs in
`programme-final.log` and `programme-final.err`. It passed six focused core
tests, four installed-font/root tests, the actual-page regression and both Help
tests. Its synthetic SI and transport-stream scenarios are explicitly labelled;
they do not establish real broadcast content. The recorded-IQ scenario still
reports initial H.264 acquisition errors, so a clean-picture claim remains open.
Test2 is not installed while the user is testing test1.

Application `1.1.0~test2` has now completed native Raspbian armhf/GCC14 build and
CPack DEB packaging. Its package SHA-256 is
`751f074d6919895f4d493f2319f69cd2547bd9fce9caac17ff598925cfaa74dc`.
Qualification extracts the actual package, verifies ELF hard-float architecture,
dependencies, content-font hash and provenance, and imports the paired backend
through the package's own automatic decoder. The real 32-bit ARM LVGL loader
verifies all 6,960 glyph mappings: retained payload 471,976 bytes, peak payload
499,824 bytes, ten allocations. The packaged application discovers the font at
`/usr/share/trailmate/packs/fonts/ja-broadcast-cjk/font.bin` and starts the 1seg
page and touch Help with radio access disabled. Its Help screenshot was reviewed
without overlap. All build and qualification processes exited zero; this does
not establish physical receiver throughput or audible output. Local evidence is
`.codex-build/cm0/native-armhf/test2-package-receipt.json` and
`test2-finish.log`. Host regression input hashes still match the packaged source
checkpoint. Device update timing is pending the user's current reception test.

The scan controller and real-session adapter are connected to Menu, the saved
channel selector and the Scan channels / Cancel scan action. The controller stages results, enumerates
received programmes, waits for release before retuning and commits only after
the final release. Empty, cancelled, failed and unwritable scans retain the old
in-memory channel list. The adapter requires valid received PMT mapping and
decoded media from the current decoder generation/media epoch; IQ counts and
old frames alone do not qualify. Automatic decoder exit 4 is classified as no
signal only after shutdown and only without a radio/queue/restoration failure.
Three focused CTests (channel storage, scan transaction, session observation)
pass on the host. Their synthetic observations explicitly do not qualify RF.
The actual LVGL page with the real automatic decoder also passes cancellation
back to the previously watched channel, muted scanning, volume-command guarding,
old-image hiding and saved-list retention using recorded IQ. A screenshot review
found the initial cancel control below the viewport; the corrected layout hides
the video viewport while Menu is open. Fresh page regression and keyboard/touch
Help checks pass, and the reviewed progress screenshot shows the cancel control
within the viewport. Hidden video does not receive presentation acknowledgements.
The no-signal adapter defers error classification until child/RF cleanup ends.
No later package has been installed on CM0.

The full-range host fixture repeats the same recorded waveform for each requested
frequency. The real automatic decoder and page scan all 50 channels, persist
50 received-media-confirmed fixture entries in ascending order, and restore
CH14/service8 and volume70 after the final release. A 50-bit request mask verifies
13-62 were visited. The fixture does not demonstrate 50 stations on air. The
channel dropdown defaults to the watched service and closes to video on selection;
actual-page tests verify CH14 -> CH15 -> CH14 with fresh decoded frames. Final
`tv-full-scan.exit` is 0; keyboard/touch Help checks also pass. The restored-picture
screenshot still shows initial H.264 acquisition damage from the recorded signal;
this is not a clean-picture or physical-radio acceptance claim. General metadata,
signal recovery and ARM/backend packaging and deployment remain outstanding.

Manual recovery is now connected through Retry / R. Once previously decoded
video or IQ progress becomes stale for two seconds, the page hides the picture
and reports an interrupted signal. Retry preserves the selected service, waits
for the old receiver to release and starts a new acquisition of the same channel.
It is unavailable during scanning, stopping or pending retuning and cannot bypass
a radio restoration failure. Natural automatic-decoder exit4 is presented as no
supported TV signal, distinct from radio failures. The recorded-IQ actual-page
regression exhausts input, verifies the interrupted-picture state, retries and
requires a new generation with a fresh service8 frame on CH14 at volume70. It
also attempts retry after restoration failure and verifies no new acquisition.
The app/page build, page regression and keyboard/touch Help checks pass with
`tv-workflow.exit=0`. Reviewed starvation/recovered screenshots have no overlap;
the recovered screen now reports Reception recovered after a current frame is
actually presented. Page regression requires the prior retry notice to disappear.
This does not qualify changing live RF conditions.

Backend package source now uses version `1.1.0+gitb576783021fb-1` and provides
`trailmate-oneseg-backend-auto-v1`. Packaging checks the TMCC source adaptation,
message schema1 and Viterbi repair hashes in addition to the original provenance.
Its relocated real-decoder check uses the automatic profile and requires both
parameter autodetection and observation capabilities. CM0 application metadata
requires backend >=1.1.0, automatic capability and the existing Raspbian C++ ABI
capability. The cross-package preflight rejects older versions or missing
capabilities before preparing a sysroot or starting a build.

The new amd64 backend package has passed extraction, clean private binding import,
automatic capability, ready-descriptor and cancellation tests. Six altered/missing
manifest checks fail before output creation; CM0 metadata tests reject wrong
architecture, wrong ABI, missing automatic capability and an old claimed-capable
version. `auto-package.exit` is 0. This is host packaging evidence; the retained
ARM backend and installed CM0 packages still need rebuilding and replacement.

The decoder process now feeds actual TS packets into a worker-owned bounded
SDT/EIT reader. It selects the received PMT service and clears metadata on a
service/transport change or a new decoder generation. Actual SDT (PID0x11) and
present/following EIT (PIDs0x12/0x26/0x27) are accepted only after section assembly,
continuity, descriptor bounds and CRC checks; future or other-transport tables
are not published. The snapshot carries raw STD-B24 name/provider, present and
following title/description/language, event IDs and encoded start/duration fields.
An empty present-event section clears the previous present event.

The field layout follows [ARIB STD-B10 v5.13-E1, tables5-6/5-7 and service/short
event descriptors](https://www.arib.or.jp/english/html/overview/doc/6-STD-B10v5_13-E1.pdf).
Synthetic boundary tests and an actual child-process TS fixture both pass,
including generation-reset clearing. These tests do not prove received broadcast
metadata. STD-B24 character decoding, metadata expiry and UI/store integration
remain outstanding; raw bytes are deliberately not rendered as UTF-8.

Metadata snapshots now apply a 30-second monotonic reception-freshness policy
independently to SDT, present and following information. Expiry clears text and
event data, and a non-running decoder exposes no old service information. This
policy does not validate the event's encoded wall-clock end time. Multiple
short-event descriptor languages are accepted, preferring Japanese and otherwise
the first received language; duplicates of the same language invalidate the
section. Tests cover language order, duplicate retention, independent expiry at
the exact boundary, backwards timestamps and cleared metadata after process stop.
The focused parser and subprocess tests pass with `service-information.exit=0`.

A standalone private-prefix probe of Ubuntu libaribb24 1.0.3-2.1build2 validates
generic-SI alphanumeric, Kanji, Hiragana, Katakana and GR conversion. ARIB
alphanumeric correctly produces full-width Unicode, as confirmed against the
[upstream decoder implementation](https://github.com/nkoriyama/aribb24/blob/master/src/decoder.c).
The malformed-input gate fails: an incomplete two-byte Kanji sequence produces no
explicit decoder error. The retained probe deliberately remains failing
(`arib-text/check.exit=134`) until input-completeness validation or a suitable
alternative addresses it. The library is not integrated or installed system-wide.
Production text decoding/UI/store integration therefore remains incomplete.

The new production-source `oneseg_arib_text` module now guards the native decoder
with bounded SI character-state validation. It checks designation/locking/single
shifts, GL/GR graphic widths and complete two-byte characters before conversion.
Invalid or unsupported fields return an explicit error and no partial text.
The wrapper initializes generic SI character sets, disables DRCS generation and
private-use conversion, and rejects native decoder errors. It currently supports
the native library's common Kanji/alphanumeric/Hiragana/Katakana designations;
DRCS and other unsupported control/character formats remain explicit failures.
This is not a claim of complete STD-B24 coverage.

The native wrapper CTest passes with full-width alphanumeric, Japanese characters,
GL/GR designation and shift examples, all 255 prefix boundaries of a repeated
two-byte string, maximum input size and 2000 bounded deterministic descriptors.
`arib-text/guarded.exit` is 0; the original unguarded-library failure stays as
comparison evidence. The native-media CI installs libaribb24 development files
and runs the new test; CI YAML parses locally, but no remote job was dispatched.
The module is now linked into the native 1seg media component and its owner-thread
cache publishes converted service information in the session snapshot outside
the UI lock. Repeated content and failed conversions are cached; service changes,
stopping and expiry clear text. An in-flight programme selection immediately
hides the old programme's metadata. Four focused CTests, a real native-backend
recorded-IQ session and package dependency checks pass (`service-text.exit=0`).
The metadata session CTest uses explicit synthetic IQ/TS with two programmes and
asserts text alone never produces playback/frame evidence. UI fonts/rendering,
channel-name saving, event end-time checks and full character coverage remain
required. The CM0 runtime now depends on libaribb24-0t64 with checked SONAME0.
At the earlier raw-snapshot checkpoint, the full host app and actual-page harness build
and pass viewing, retuning, menu selection, scan cancellation, retry recovery,
failed-restoration guarding and keyboard/touch Help regression. The final
`tv-workflow.exit` is 0. Recovery screenshot review confirms the updated notice
and unchanged 320x240 viewport without overlap; recorded startup damage remains.

The installed test package has been rebuilt natively for Raspbian armhf/GCC14,
with a reviewed no-RF 1seg Help screenshot. It does not yet render decoded SDT/EIT
text in the page. Rollback packages for the prior app1.0.1 and backend1.0.0 remain
under `/home/pi/trailmate-deployment-20261030-1seg-test1-eedf80e8/rollback/`.
The deployment only upgraded those two Trail Mate packages. The user is operating
the receiver now; do not replace this test version or acquire LR2021 during their
test without a further request. Continue outstanding UI/metadata work offline.

## Reference evidence and boundaries

The local OneSegCast checkout was inspected at base commit `073c72d`, including
its uncommitted `docs/LR2021_VALIDATION.md` research. Those local additions are
not assumed to exist at the published commit. They describe CM0 ROM 1.24,
control-command tests at 1–16 MHz SPI, finite TX experiments, and a two-minute
symbol-slotted MMIO TX run. They do not prove continuous RX, decoded television
or receiver interoperability. The experimental root/MMIO SPI backend and TX
operations must not be adopted as the default receiver transport.

The neighboring OneSegView working copy contains a streaming RTL-SDR -> GNU
Radio/gr-isdbt -> TS -> FFmpeg receiver and records earlier CM4 RF acceptance.
Its current `stream_receiver.py` has a fixed Mode3/GI1/16, QPSK 2/3,
time-interleave-4 profile and hard-coded service/PID handling. That evidence
does not establish a general channel scanner or an LR2021 input adapter.
Its original-code distribution license is not selected in `NOTICE.md`; no
OneSegView implementation is copied into Trail Mate. OneSegCast declares MIT.
GNU Radio, gr-isdbt, FFmpeg and their packaging remain separate dependencies
with their own licenses; their availability on CM0 must be checked.

The radio-command reference is the pinned MIT lr2021-sdr source
`0a44fc69beb34274d6e86f29c1c064b3d720c48d`, specifically the RX configuration
and raw-receive-route documentation. Current Trail Mate radio ownership and
checked SPI transport remain the integration boundary.

## Receive configuration implemented

`platform/linux/common/include/platform/linux/oneseg_lr2021_rx.h` implements
the first CM0 receive setup. The actual Linux LR2021 backend now calls it through
the optional continuous `IPacketRadio::streamOneSegIq` port. The engineering
receiver connects the worker, decoder and optional native media component.
The standalone TV menu/page and presentation lifecycle are implemented; current
workflow and device acceptance limits are listed above.

* Japanese UHF channels 13–62 map to `473142857 + (channel - 13) * 6000000` Hz.
* Candidate input is source 0 IQ8, route 1, input code 17, at 576000 pairs/s.
  This is a candidate for board qualification, not a measured CM0 RX rate.
* The private 1seg decoder's native rate is 64000000/63 samples/s. Rational
  conversion from 576000 pairs/s is interpolation 1000, decimation 567.
* Source 0 uses signed I/Q bytes, with six significant bits observed in the
  reference research. RTL's unsigned cu8 offset conversion is unsuitable.
  Gain-tagged source 6 must not be treated as ordinary IQ8.
* IQ data alone requires 1152000 bytes/s, or 9216000 SPI wire bits/s before
  command/status overhead. Slower clocks are rejected before I/O. Passing this
  lower-bound check does not qualify FIFO service latency or sustained rate.
* The port must attest a real exclusive lease and a held transport mutex.
  ROM 1.24 is checked before changing the modem. All configuration commands
  are status checked; all reported chip errors, including image calibration,
  are retained as failures.
* Setup leaves the radio in standby. Routing, FIFO checks and RX startup belong
  to the continuous stream engine. TCXO and board RF-switch configuration stay
  with the board adapter; P4 RF-switch settings must not be substituted.
* Once a state-changing command is attempted, `restore_required` remains true
  even on failure. The caller must reset/reapply saved packet settings before
  releasing the lease. This helper does not perform cleanup on its own.

`tests/oneseg` covers channel limits, all eight command failure boundaries,
command order and tuning bytes, ownership, wire-rate rejection, ROM mismatch,
and chip-error propagation. It opens no radio or audio device. CI runs these
tests separately from FM DSP and the existing IQ engine contracts.

## Continuous radio port implemented

The foreground coordinator keeps its physical mutex for the complete stream,
including reset/recovery; mesh physical I/O cannot interleave with it. Only the
foreground client exposes the optional TV IQ capability. Requests require a
valid channel, caller-owned staging of at least 1280 bytes, a nonblocking queue
sink and an independent thread-safe cancellation callback. The UI worker must
cancel and join before releasing its foreground lease. Cached status remains
nonblocking; competing foreground admission and quick-control power changes
return false when the mutex is busy, rather than waiting on an indefinite stream.

`oneseg_lr2021_stream.h` bridges the existing packet HAL to source-0 IQ8. It
preserves the original packet snapshot separately, configures TV RX inside the
checked engine's recovery boundary, starts raw RX without RadioLib's cached
packet-mode staging, and restores the original snapshot after reset. A request
already cancelled before startup performs no hardware I/O. Queue rejection,
FIFO errors and recovery failures are reported separately from cancellation.
Previously validated byte counts remain diagnostic evidence even if a later
block or recovery fails; they do not establish a successfully received programme.

The LR2021 HAL temporarily requests 16 MHz through the kernel SPI speed ioctl
and validates the configured readback. The per-transfer speed changes with it.
The original packet clock is restored before reset and packet reconfiguration.
Packet transfers retain the existing 100 us post-transfer wait. IQ transfers
retain a 1 us minimum interval, and IQ-only requests of at most 100 us use a
bounded steady-clock wait instead of scheduler sleeps. FIFO reads use persistent
258-byte transport buffers and validate both returned status bytes before
publishing the payload. No MMIO, governor, affinity or scheduler policy is changed.
SPI configuration readback is not an independent clock or throughput measurement.

Native tests exercise the production bridge with the actual pinned stream
engine, including valid blocks, empty FIFO, queue rejection, FIFO failure,
eight setup failures, cancellation before startup and saved-PHY restoration.
Additional tests extract the unmodified production FIFO/clock/transfer methods
to check framing, status rejection, kernel clock failures, per-mode transfer
speed and restoration of packet timing. Their transport is fake; on-board
qualification is still required. The Linux coordinator test exercises real
threads, competing mesh I/O, UI-safe admission/power checks and recovery faults.

## CM0 RF-switch profile implemented

`boards/t_display_cm0/device.conf` explicitly selects
`radio_rf_switch_profile=cm0_16e8`. The default is `none`; selecting a non-default
profile with another radio backend, or an unknown profile value, is rejected.
The optional TV IQ capability and stream admission require the CM0 profile.
An unprofiled generic LR2021 instance does not advertise TV support.

The table follows the official local CM0 checkout at commit
`e170a07c2e19d9225cd6a852cddd8f0bb2dc5039`, with the reference file unchanged, under
`lora_lib/modules/LR20xx/LR20xx.cpp`: DIO7 is assigned the RF-switch function
with mode mask `0x10`, DIO6 with mask `0x18`. Both are low for LF RX/TX/standby.
Each function and mode write uses the checked transport. This is independent
command encoding from board facts; no vendor driver implementation is copied.
P4 wiring is not used.

The driver reapplies the selected profile after cold initialization, GFSK
initialization, switching back to LoRa, and IQ reset, before reception resumes.
A failed table write prevents RX startup and marks the driver offline. Host
tests execute the extracted production lifecycle methods and inject each of
the four table failures into each of those four paths. They also verify zero
RF-switch writes for the default profile. These tests do not establish antenna
switch voltage levels, RF sensitivity or programme reception on the board.

## Continuous receive worker and decoder transport

`OneSegReceiveWorker` connects the continuous port to a fixed eight-slot queue
of 1280-byte blocks. The producer copies validated signed IQ8 blocks without
allocation, blocking I/O or a mutex. Overflow, discontinuous pair indices and
sink failures stop the session explicitly. Each start has a new generation.
Cancellation works with an empty FIFO. Cleanup joins the consumer, finishes the
sink and restores/releases the radio before publishing the final state.

The application factory uses the physical receive-release barrier and the real
foreground radio client. `OneSegPipeSink` writes to an owned nonblocking pipe
with bounded waits and cancellation. It handles a closed decoder without
changing the process-wide SIGPIPE disposition. `OneSegDecoderProcess` owns
decoder startup, cancellation and reaping as described below.

The Linux engineering executable `trailmate-linux-oneseg-iq` accepts `--profile`,
`--channel` (13 through 62), `--seconds` (zero means until interrupted) and
optional `--decoder` with an absolute decoder-script path. Without `--decoder`,
stdout must be a pipe and contains raw signed IQ8 only. With it, the tool starts
the real named-profile backend, consumes TS and reports received programme
metadata. All status goes to stderr. It requires the real CM0 LR2021 profile and rejects simulated radio
composition. A clean stop or forwarded bytes establishes neither decoder lock
nor television playback. This executable is not a television menu page.

The existing local OneSegCast GNU Radio prefix imports `isdbt`, but does not
expose the five 1seg synchronization, TMCC, deinterleaving and demapping blocks
required by the OneSegView streaming backend. OneSegView pins gr-isdbt revision
`b576783021fb5c18f1903c534075146c31b21e90` with its PR70 patch. Its Debian ARM64
installer cannot serve as an armv7 CM0 deployment artifact. Decoder dependency
integration and ABI validation remain necessary.

## Programme metadata and named-profile decoder

`TransportStreamPrograms` now reads CRC-checked PAT/PMT sections from aligned
188-byte packets, including fragmented sections, pointer fields, multiple PAT
sections, version updates, duplicate packets and PSI continuity loss. It bounds
the table and stream counts, rejects malformed descriptors, distinguishes
scrambled packets, and invalidates selected media metadata when the programme
mapping changes. Its counters describe transport packets, not decoded media.
Store this fixed-storage worker object outside ESP task stacks.

The explicit `AribPartial` discovery mode also reads PMTs at `0x1fc8..0x1fcf`
without requiring a PAT. Service numbers come from the received PMT; their low
three bits must agree with the PMT PID assignment. At most three services are
discovered, primary-service preference yields to explicit user selection, and
the metadata source remains `AribPartialPmt` until a real PAT is received. No TS
identifier, PAT packet or elementary-stream PID is fabricated. This follows the
partial-reception rules in
[ARIB TR-B14 v2.8, Vol. 7 section 5.2.9](https://www.arib.or.jp/english/html/overview/doc/8-TR-B14v2_8-3p3-E2.pdf).
The generic PAT mode remains separate. MPEG packet and section syntax follows
[ITU-T H.222.0](https://www.itu.int/rec/T-REC-H.222.0-202308-S).

`apps/linux_lvgl/tools/oneseg_decode.py` is a real GNU Radio engineering decoder
for signed IQ8 at 576 ksample/s. Its rational conversion is 1000/567. It requires
explicit selection of `onesegcast-mode3-gi16-qpsk23-ti4`; this is a named research
profile, not TMCC parameter autodetection. It writes raw TS to stdout and status
to stderr, redirects upstream native prints away from TS, uses bounded input
queues and output waits, handles incomplete IQ pairs and cancellation, and does
not emit RF-lock or playback states. It does not synthesize PAT packets.

The reproducible private dependency builder is
`apps/linux_lvgl/tools/build_oneseg_backend.py`. It verifies the pinned source
archive and source-file hashes, preserves unexpected local modifications, and
adapts the upstream synchronization block to pair `new[]` with `delete[]`, free
the scattered-pilot workspace, bound tracking searches, and initialize failed
acquisition outputs. It installs only beneath the supplied work directory,
clears the upstream `PYBOMBS_PREFIX` override, and records the host architecture,
Python version and runtime search paths. It uses existing Linux GNU Radio 3.10
development dependencies and does not install system packages. The dependency
is [gr-isdbt at the pinned revision](https://github.com/dragoon2014/gr-isdbt/tree/b576783021fb5c18f1903c534075146c31b21e90),
under its upstream GPL-3.0-or-later terms; source and notices remain in the
private source/prefix. The successful x86_64 build is not an armv7 deployment.

```sh
python3 apps/linux_lvgl/tools/build_oneseg_backend.py \
  --work-dir "$PWD/.codex-build/oneseg/gr-isdbt"
python3 apps/linux_lvgl/tests/oneseg_decoder_test.py \
  --decoder apps/linux_lvgl/tools/oneseg_decode.py \
  --backend-prefix "$PWD/.codex-build/oneseg/gr-isdbt/prefix" \
  --artifact-dir "$PWD/.codex-build/oneseg/linux/decoder-review"
```

Host verification includes native decoder startup, empty/zero IQ, incomplete
pairs and cancellation with an empty input pipe. An optional explicit
`--recorded-rtl` argument adds the named 1.024 MHz unsigned RTL fixture, conversion
to signed 576 kHz IQ, TS decoding, and closed/blocked output tests. This fixture
is not checked into Trail Mate and is not a CM0 capture. The local five-second
recording produced 1,321 TS packets with zero TEI, and its unmodified PMT identified
service 8, H.264 PID 256 and AAC PID 257 without a PAT. FFmpeg decoded 61 video
frames and processed audio; the sample also logged initial missing PPS and a
macroblock decoding error. This is offline interoperability evidence, not clean
media, on-device reception, audible output or UI playback acceptance.

## Decoder process ownership implemented

`OneSegDecoderProcess` implements the IQ sink used by the real receive worker.
It launches Python with an argument vector and environment overrides through
`posix_spawn`, without a shell. It isolates the process group, clears the signal
mask, and closes inherited application descriptors after mapping stdin, TS,
diagnostics and a private ready descriptor. Closing inherited descriptors uses
glibc 2.34 or newer; an unsupported runtime fails explicitly rather than leaking
radio or application handles. The private one-byte ready handshake occurs only
after the native graph and its input thread have started. The radio is not
acquired until sink startup succeeds.

The monitor reads stdout and stderr independently of IQ writes. It reassembles
188-byte TS packets from arbitrary pipe boundaries, checks alignment, feeds the
real programme parser and supports a bounded-queue TS consumer callback. The
diagnostic tail has fixed storage. Programme selection is an asynchronous reader
request; snapshots hold a short mutex and never wait for the child. Child startup
and `Running` mean decoder-process readiness, not RF lock or media playback.

The worker's FIFO cancellation callback checks a lock-free sink-health flag, so
an exited decoder or rejected TS queue also cancels an empty FIFO. Error text is
copied only after the consumer joins, outside the producer hot path. Finish
closes IQ input, allows a short EOF drain, then escalates to SIGTERM and SIGKILL
when necessary. It reaps the owned child before the worker releases the radio.
Signal delivery and reaping serialize access to the owned PID to avoid reuse
races. Forced termination, nonzero exits, incomplete TS and sink faults remain
explicit errors. No playback state is generated by this controller.

Process tests cover three generations, actual OS-pipe fragmentation, programme
discovery, startup failure and cancellation, empty-FIFO decoder death, descriptor
isolation, stderr flooding, malformed/partial TS, queue rejection, forced kill,
reaping and descriptor-count stability. A native test uses the real GNU Radio
backend. A separately supplied historical IQ fixture also traversed the complete
C++ process controller, Python decoder and C++ metadata parser, recovering its
actual PMT without a PAT. That remains offline RTL evidence, not onboard LR2021
capture or physical television acceptance.

## Native video and PCM component implemented

`OneSegMediaWorker` consumes complete TS packets through a fixed 512-slot SPSC
queue. Its producer copies one packet and publishes lock-free atomics; full,
stale-generation and invalid input fail explicitly. A cancellable custom FFmpeg
input reads this queue. Selected video/audio PIDs come from received programme
metadata, including the no-PAT partial-reception path. No PAT is inserted.

The MPEG-TS header scan uses a 32 KiB prefix and a separate stream-analysis
budget. FFmpeg scans the header budget before enabling stream guessing, so a
large budget can delay live no-PAT startup. The validated FFmpeg 6.1 path also
leaves PMT version merging disabled; enabling it prevented discovery on the
retained no-PAT sample. See the
[upstream MPEG-TS header implementation](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavformat/mpegts.c).
These are byte/media-time limits, not a wall-clock signal timeout. Cancellation
remains available with an empty input queue.

Video decoding rejects corrupt frames and bounds input geometry. Conversion
fits the configured viewport while preserving sample aspect ratio and produces
RGB565LE with original presentation timestamps. Fixed worker scratch and latest
frame storage live on the heap. The single UI reader uses a try-lock to copy a
frame without waiting for conversion. Superseded frames are counted. Copying a
frame is not evidence that LVGL displayed it; A/V presentation timing is pending.

Audio converts to mono PCM16 at 48 kHz with bounded scratch and write chunks.
Timestamps account for resampler delay and EOF flushes remaining samples.
Partially accepted PCM remains counted after a later write failure. Failed audio
startup/output is cleaned up; native decoder resources are freed even when audio
cleanup throws. Native states are `Opening`, `Decoding`, `Stopped` and `Error`,
never `Playing`.

`OneSegPcmOutput` binds to the existing real SDL PCM output with a distinct
exclusive `Television` focus owner. It closes the physical output before
releasing focus, bounds the playback queue and provides its own atomic volume.
It does not reuse the FM, Walkie or notification owner. SDL dummy tests cover
device lifecycle, focus refusal/rollback, notification/FM exclusion, volume,
cancellation, stale generations, missing-device errors and music resumption.
They do not prove audible output.

The opt-in `TRAIL_MATE_LINUX_ENABLE_ONESEG_MEDIA` build flag links this component
into the real engineering receiver. `--decoder SCRIPT --media` selects received
PIDs and connects TS to native video decoding and PCM. A programme mapping change
stops this engineering session explicitly; automatic switching belongs to the
application. Unsupported builds reject `--media`. Matching native FFmpeg
development/runtime libraries are required; `TRAIL_MATE_ONESEG_FFMPEG_PREFIX`
may select a private development prefix, not a device deployment artifact.

Historical IQ now traverses C++ child ownership, real GNU Radio, real FFmpeg,
generation-tagged RGB565 retrieval and SDL dummy PCM output. It yielded 60 valid
video frames and 208,896 accepted PCM samples, released focus and left no child.
One corrupt video frame was skipped; initial missing-PPS diagnostics remain
visible. A separately named synthetic 44.1 kHz fixture verifies resampling and
EOF: 146,008 samples matched the FFmpeg reference. Its content is explicitly
marked `TEST FIXTURE - NO RF`. Neither fixture is onboard LR2021 or physical
display/audio acceptance.

## Asynchronous application session

`OneSegTvSession` now owns receive, decoder, media and PCM lifecycle on a
background owner thread. UI commands enqueue start, stop and programme requests;
snapshots and bounded RGB565 copies do not wait for RF cleanup or child reaping.
The real-device factory uses the foreground packet-radio coordinator and the
physical receive-release barrier, and rejects demo/simulated composition.

Public session generations and media epochs reject stale commands and frames.
Actual received PMT mappings select elementary streams; changed mappings restart
the media epoch. Selecting the current programme does not interrupt decoding.
The state distinguishes incoming IQ, decoding and a frontend-acknowledged frame.
That acknowledgment attests submission to the display widget, not audible audio
or physical RF lock. Signal/frame ages prevent stale progress from sustaining
the presented-frame phase.

The native session harness passes recorded IQ through the real GNU Radio and
FFmpeg components, with an explicitly named fake radio and SDL dummy output.
It checks asynchronous commands, absent hardware, decoder startup failure,
startup cancellation, frame acknowledgment, stale generations, same-programme
selection, restart, child reaping and audio-focus release. Multi-programme
switching, real LVGL presentation and physical board acceptance remain pending.

## Remaining integration and acceptance

On 2026-10-30, the standalone production transport-program parser test passed
with AddressSanitizer and UndefinedBehaviorSanitizer enabled, assertions enabled
and warnings treated as errors. Coverage includes fragmented PAT/PMT sections,
complete-PAT removal, PID/version updates, continuity recovery, malformed tables,
ARIB partial reception and 2,000 randomized CRC-correct section inputs. No memory
or undefined-behavior diagnostic was reported. This is parser boundary evidence;
it does not qualify partial-PMT ageing, real RF service transitions or playback.

The software presentation path now uses a generation-scoped PCM queue clock.
Only accepted PCM writes with valid PTS anchor it. The estimate subtracts SDL's
reported queue duration (including its output-period estimate) from the accepted
sample endpoint, advances only through queued samples, and becomes invalid on
underrun or cleanup. Timestamp discontinuities discard obsolete queued PCM;
unknown timestamps do not imply a valid clock. This is not a hardware DAC
timestamp or proof of audible A/V alignment.

The media worker retains twelve bounded RGB565 frames, allocated for the
configured viewport. It selects the newest frame within 30 ms ahead / 250 ms
behind the current audio estimate. Future, stale, missing-PTS and wrong-epoch
frames cannot overwrite the display buffer. Video-only programmes use a
monotonic video anchor. The frontend refreshes at 33 ms, acknowledges actual
widget submission, reports clock availability, and hides obsolete presentation.
Those admission limits are software policy, not a measured end-to-end sync SLA.

Programme removal is actionable only when a complete, validated received PAT
omits the manually selected service. The controller then releases that selection
constraint, follows the parser's actual remaining programme, starts a new media
epoch and reports the removal. It does not infer that an ARIB partial-PMT
programme disappeared merely from silence. A received selected-PMT mapping/version
change also starts a new media epoch, isolating obsolete PID frames and PCM.
Video-only PTS discontinuities beyond the presentation lateness window reanchor
the monotonic clock without reopening the RF session.

1. The opt-in native-media application now registers a target-owned `1seg TV`
   descriptor for the LR2021 `cm0_16e8` profile. Its page invokes the asynchronous
   session for channel/start/stop, received-programme selection and TV volume,
   copies actual RGB565 frames into an LVGL image and acknowledges submission.
   Stale frames are hidden. Fullscreen and keyboard/touch Help are connected;
   page exit requests asynchronous stop. A standalone production-page harness
   now supplies recorded IQ through a clearly named fake radio, while real
   GNU Radio, FFmpeg, LVGL and SDL dummy audio remain connected. It verifies
   actual decoded RGB565 presentation, fixed 320 x 240 geometry without
   upscaling in normal/fullscreen modes, channel guards, volume commands,
   same-programme selection, stop/hide and asynchronous page-exit cleanup.
   The harness also checks that exhausted input invalidates the audio estimate
   and hides stale video. This is not onboard RF, audible audio or physical A/V
   timing acceptance. The engineering CLI still uses its
   earlier manual composition.
2. Qualify the kernel SPI path at the required throughput on the actual CM0.
   Record rate, FIFO flags, worst service gap, drop counts and recovery. Earlier
   research used an armv7l CM0 system; verify the deployment ABI instead of
   assuming the host x86_64 or an ARM64 build is a device acceptance artifact.
   Verify the implemented `cm0_16e8` RF-switch profile on the user's board,
   including cold boot and reset recovery, rather than treating table writes
   or host fixtures as proof of RF continuity.
3. Qualify programme transitions on actual RF, including ARIB partial-PMT
   ageing and signal recovery. The synthetic transport harness covers manual
   programme selection, PID remapping, complete-PAT removal fallback and
   video-only forward/back PTS recovery through real media decoding.
   Add actual TMCC parameter
   reporting and adaptive configuration. Keep the fixed OneSegCast waveform
   as a named test profile, not a general-TV claim.
4. Qualify the implemented queue-estimated A/V scheduling on physical hardware,
   including output latency, timestamp discontinuities and video-only sources. Track
   discontinuity, TEI, continuity-counter errors, video-frame progress and
   audio-write progress separately. Do not infer playback from valid TS alone.
5. Test the implemented CM0 menu entry and page callbacks, channel selection,
   start/stop, programme selection, volume, fullscreen and Help. Test page exit,
   Help and menu navigation during RX,
   signal loss, recovery, device error and audio failure.
6. Perform actual broadcast reception and record visible changing video and
   audible audio, then verify return to mesh, Walkie and Probe. Offline IQ/TS
   fixtures must be labeled and cannot satisfy this physical acceptance gate.

The TV page requires `TRAIL_MATE_LINUX_ENABLE_ONESEG_MEDIA=ON`. It is not yet
enabled in the routine CM0 package recipe; native ARM dependencies and backend
deployment remain pending. The decoder script defaults to the configured
installation's libexec directory; `TRAIL_MATE_ONESEG_DECODER` may explicitly
select another production script. Missing dependencies produce real startup
errors, not substitute content. Host-rendered idle/Help screens do not establish
physical received-programme playback. FM remains a separate ongoing
implementation; this addition does not complete the broader remediation goal.

Enabling the TV composition also requires a Linux target and
`TRAIL_MATE_LINUX_BUILD_APP=ON`, `TRAIL_MATE_LINUX_ENABLE_LR2021=ON`, and
`TRAIL_MATE_LINUX_ENABLE_MEDIA=ON`. CMake now rejects contradictory settings
instead of silently omitting the requested TV target or failing later at link
time. On 2026-10-30, three isolated negative configurations each failed with the
corresponding prerequisite diagnostic. This guard does not enable TV in the
routine Debian package or supply native ARM decoder dependencies.

### Native backend package

`apps/linux_lvgl/tools/package_oneseg_backend.py` creates a separate
`trailmate-oneseg-backend` Debian package from the pinned native builder's work
directory. It verifies the pinned provenance, current machine, Python binding
ABI and ELF machine type. `dpkg-shlibdeps` derives native shared-library
dependencies; Python minor-version bounds prevent installing an incompatible
binding. Packaging does not install anything or access a radio.

Build and package on a Linux environment matching the deployment distribution,
architecture, GNU Radio ABI and Python ABI:

```sh
CC=gcc-14 CXX=g++-14 python3 apps/linux_lvgl/tools/build_oneseg_backend.py --cm0 --work-dir BACKEND_WORK
python3 apps/linux_lvgl/tools/package_oneseg_backend.py \
  --backend-work BACKEND_WORK --output PACKAGE_OUTPUT
python3 apps/linux_lvgl/tests/oneseg_backend_package_test.py \
  --package PACKAGE_OUTPUT/trailmate-oneseg-backend_VERSION_ARCH.deb \
  --decoder apps/linux_lvgl/tools/oneseg_decode.py
```

The package owns a private `oneseg-backend` sidecar next to the installed
application decoder under `/usr/libexec/trailmate`. The decoder restarts once
with that sidecar's Python and dynamic-library paths before importing native
blocks. The ready descriptor and process identity survive this exec. No global
Python installation or loader configuration is modified. When the sidecar is
absent, existing system/private-development backend behavior remains available;
missing modules still produce real startup errors.

`--cm0` requires native armhf Raspbian Trixie, explicitly selects ARMv7-A /
VFPv3-D16 hard float and checks the actual compiled C++ ABI plus always-lock-free
bool atomics. This is a board target selection, not a private libstdc++ macro
override. Generic host validation omits this option. The manifest records the
target and CPU flags. An incompatible architecture or distribution is rejected
before source download or build-directory creation.

Backend provenance now includes the C++ ABI measured by preprocessing
`ext/concurrence.h` with the actual backend translation unit's compile command.
It records the libstdc++ header release, default lock policy and C++11 ABI; no
private macro is overridden. A backend declares
`trailmate-oneseg-backend-cm0-abi-v1` only for armhf, libstdc++ release 14, mutex
lock policy and C++11 ABI 1, matching the inspected CM0 configuration described
in [the target ABI ledger](linux-armhf-cxx-abi.md). The CM0 application depends on
this virtual capability, so the constraint also applies to manual package
installation. Unknown ABI metadata requires rebuilding the backend manifest.
The package revision is now `-1` to distinguish the ABI-described payload.

On 2026-10-30, the amd64 host package was built and extracted without installation.
With private development path variables cleared, the actual decoder imported
the extracted binding, preserved the ready handshake across exec and stopped
cleanly on SIGTERM. Package permissions and ownership of the decoder path were
checked. The ordinary decoder regression cases also passed. This validates the
packaging mechanism on the host only. The CM0 armhf backend build and physical
deployment remain outstanding.

The CM0 application recipe now has an explicit TV packaging mode:

```sh
TRAIL_MATE_PACKAGE_ONESEG=1 \
TRAIL_MATE_ONESEG_BACKEND_DEB=/absolute/path/to/native-armhf-backend.deb \
bash apps/linux_lvgl/tools/build_cm0_deb.sh
```

It rejects a missing backend, wrong package name, non-armhf architecture or
missing CM0 ABI capability
before preparing the sysroot. TV mode adds the five FFmpeg development packages
to the target sysroot and explicitly enables the media composition. The device
package declares the separate backend dependency and derives FFmpeg runtime
package names from the selected target libraries' SONAMEs. The supplied backend
package is copied alongside the application package for joint installation.
Base mode explicitly disables TV, so a reused cache cannot silently retain it.
The TV mode is opt-in until the native armhf backend and full application package
are built and qualified; this is not yet the routine device release default.

On 2026-10-30, isolated CMake checks evaluated the real CM0 package metadata and
confirmed it contains the backend and all five actual FFmpeg SONAME dependencies.
Non-ELF libraries were rejected. The existing amd64 backend package was rejected
by the CM0 preflight before sysroot preparation. Both shell recipes passed syntax
checks. These are metadata and preflight checks, not a completed armhf build,
dependency installation on CM0 or physical reception test.

The ABI follow-up rebuilt the actual host backend and package. The measured host
profile is libstdc++ 13 / atomic / C++11 ABI 1; the resulting amd64 package does
not advertise the CM0 capability. Clean-environment extraction, private module
loading, ready-descriptor preservation and cancellation passed again. A separate
empty metadata fixture with armhf architecture but no matching ABI capability
was rejected before sysroot preparation. Positive and negative capability checks
also cover lock policy, header release and C++11 ABI changes. This does not
substitute for producing and validating a native Raspbian armhf backend.

### Automated native validation

The `oneseg_native_media` CI job uses Ubuntu 24.04 and its system Python to build
the pinned native backend, verify its source adaptation, build the native FM/TV
boundary suite, decode explicitly synthetic transport streams through FFmpeg,
test programme/PID/removal/timestamp transitions, package the backend and check
extracted-package startup plus CM0 dependency metadata. It does not claim ARM
or physical RF acceptance. Its retained artifacts identify test input and native
backend provenance rather than publishing a host package as CM0 firmware.

On 2026-10-30, the workflow YAML and all five shell blocks passed local parsing.
Using the existing local dependencies, the native suite passed 16/16, the source
adaptation check passed, and the synthetic media test decoded 180 video frames
and 577,536 PCM samples with zero reported decode errors. The separate synthetic
programme-transition test completed successfully. This validates those commands
locally; the new clean-runner CI job has not yet been executed remotely.

The initial local inventory found QEMU but no armhf compiler or running Docker
engine. A later private Linux-filesystem root was bootstrapped from signed
Raspbian Trixie packages under isolated mount/PID namespaces. It reports Raspbian
13.6, armhf/armv7l, GCC 14.2.0-19+rpi1 and Python 3.13.5. A real target compile
and QEMU execution verified libstdc++ release 14 / mutex / C++11 ABI 1 with
ARMv7 always-lock-free cancellation. The default ARMv6 compiler target was
rejected by the same static cancellation contract. Updated self-signatures from
the official archive-keyring package allow strict APT metadata verification;
no crypto policy was relaxed.

The subsequent native ARM backend build and packaging completed with exit 0.
The actual compile-command probe measured release 14 / mutex / C++11 ABI 1 /
always-lock-free boolean atomics. The generated armhf package passed the
relocated decoder import check and provides
`trailmate-oneseg-backend-cm0-abi-v1`. Its SHA-256 is
`d5ab6d1038296df3e2632dd9bb5702d8a21ba1bc2026c1496c1b233efb3c5ed1`.
This proves native backend compilation and packaging, not successful RF decoding.
The TV-enabled full ARM application compiled and linked successfully. Initial
packaging failed because the isolated root lacked `file`; installing that tool
inside the root allowed CPack to complete. Joint package verification then
detected missing device dependencies in native packaging. The generic recipe
now preserves device-owned dependencies alongside native shlibdeps scanning.
The corrected `trailmate-cm0_1.0.1_armhf.deb` has SHA-256
`78d05eb21b22b8de62f914564a785ec986464632317dcf1ad8199267cc308004`
after the shared Help sizing and maintainer-script corrections described below.
This package also includes the original FM/TV menu icons and shared 32-entry
catalog/launcher capacity. The actual LVGL pointer regression passed with 24
entries, vertical app scrolling through the last row, and forward/back horizontal
widget paging without moving the app grid. The test uses the same monotonic
LVGL tick callback as the real application. Native ARM recompilation/CPack,
actual isolated installation and four installed-app Help runs all completed
with exit 0 on this exact package hash. Device replacement is still pending
working SSH authentication; no physical deployment is claimed.
Joint extraction verified both packages' armhf metadata, the application ELF32
ARM hard-float executable, actual backend/ABI and FFmpeg dependencies, and the
application-owned decoder's clean-environment private backend import.
Physical CM0 installation, complete device runtime and hardware acceptance remain
pending. Extraction/import alone does not exercise package maintainer scripts.
Actual installation in the isolated Raspbian root subsequently exposed CRLF
maintainer scripts: the postinst shebang resolved to `/bin/sh\r` and package
configuration failed. The device recipe now generates all three control
scripts with explicit Unix newlines, with matching Git attributes. Python
entry scripts were independently checked to contain no CRLF. The rebuilt DEB
was unpacked through dpkg and configured through APT; both application and
backend reached `install ok installed`, `dpkg --audit` was empty, and the
installed `/usr/libexec/trailmate/trailmate-linux-oneseg-decode` passed the
private-backend import check. Installed control scripts passed `sh -n`.
The original broken-script upgrade fallback warnings are retained. Services
were blocked by policy-rc.d and SYSTEMD_OFFLINE in a private mount/PID namespace
without device nodes; this qualifies isolated installation, not physical CM0
deployment or service operation.
The installed `/usr/bin/trailmate-linux` and installed CM0 profile subsequently
passed four bounded startup/touch-Help checks for TV, FM, Probe and Walkie in
the same no-radio/no-demo SDL-dummy environment. All runs exited 0 and their
1232 x 568 captures were reviewed in
`.codex-build/cm0/native-armhf/installed-help-review/`; Help final rows are
visible. These runs use installed paths rather than the earlier extracted copy.
The package metadata regression also now checks generated control-script LF,
shebangs and shell syntax and passed alongside its dependency/ABI checks.
The packaged ARM application also completed a bounded QEMU startup smoke test
with SDL dummy video/audio, local runtime, disabled radio and all demo data
disabled. Its actual catalog opened `oneseg_tv`, logged application entry,
rendered the stopped page and exited with code 0. The retained
`.codex-build/cm0/native-armhf/oneseg-tv-arm.png` capture is 1232 x 568 and shows
the 320 x 240 picture area without control overlap. No received picture or
audio is claimed: IQ, video and PCM counters are zero. This test neither starts
reception nor exercises package maintainer scripts or device peripherals.
The same packaged ARM application then opened touch Help for `oneseg_tv`,
`fm_radio`, `energy_sweep` and `walkie_talkie`. Initial screenshots exposed
bottom-row clipping in Probe and Walkie: shared Help requested a fixed 192 px
height. The pager now requests height from its actual row count, while the
modal still clamps to available screen space. Incremental ARM compilation and
packaging passed, all four touch-Help runs exited 0, and reviewed screenshots
in `.codex-build/cm0/native-armhf/help-review/` show their final rows completely.
The Cardputer Zero target then recompiled the changed shared pager and passed
all 13 existing page-Help regressions (exit 0, 93.16 seconds). These exercise
keyboard/touch Help entry, close/focus restoration and page-specific contracts,
including unavailable Probe and Walkie behavior; they do not prove every
scroll position visually or validate a physical keyboard. Joint package
metadata/private import was also rechecked successfully on this latest ARM DEB.
The current Cardputer screenshot target was subsequently relinked and captured
Probe and Walkie Help at both initial and bottom scroll positions, all at
320 x 170. Reviewed images in
`.codex-build/cardputer-zero/help-sizing-review/{top,bottom}/` confirm the panel
fits the screen and the final close/focus hint is reachable and fully visible
after scrolling. The initial viewport necessarily shows only part of the list;
this is scrollable content, not a claim that every row fits simultaneously.
An ARMv7 build of the unchanged `test_media.cpp`, linked against the full
application's actual `libtrailmate_linux_oneseg_media.a`, subsequently passed
under QEMU with exit 0. The explicitly synthetic TS produced 180 video frames,
564 audio frames and 577,536 PCM samples with zero decode errors. The test also
checked timestamps, RGB565 output, stale-generation rejection and failure paths.
It exercises native ARM FFmpeg/media code without RF or a real audio device;
it does not establish onboard IQ throughput or end-to-end television reception.
The unchanged synthetic programme-transition test was then linked against the
actual ARM application libraries and executed under QEMU. It verified programme
8 to 9 selection (epochs 1/2), programme 9 PID remapping (epoch 3), and fallback
to programme 8 after complete-PAT removal (epoch 4). The subsequent video-only
session at epoch 5 failed the existing 15-second frame predicate and aborted
with exit 134. H.264 missing-PPS diagnostics preceded the failure. This full
test remains failed; successful earlier transitions do not qualify video-only
startup or timestamp-jump handling. Logs are retained in
`.codex-build/cm0/native-armhf/programmes.stderr.log`; no fixture child remained
after the private PID namespace terminated. No RF hardware was involved.
A diagnostic-only copy adding failure-state logging, without changing predicates
or deadlines, subsequently completed with exit 0. Recompiling and rerunning the
unchanged original test also completed with exit 0, including epoch-5 video-only
startup and both timestamp jumps. The initial failure is therefore intermittent
and remains unresolved; a successful repeat is not a demonstrated fix. Initial
failure and diagnostic logs are preserved separately as
`programmes-original-test.stderr.log` and `programmes-diagnostic-pass.stderr.log`.
Five further single-namespace ARM runs with additional failure-only media
diagnostics produced two failures (134) followed by three passes (0). One
epoch-5 failure had 3 decoded video frames, no audio frames and timing value 4
(`VideoMonotonic`); another epoch-1 failure had 4 decoded video frames, no audio
frames and timing value 2 (`AudioQueueEstimate`). Both reported zero media
decode errors. An earlier interpretation of these numeric values as audio
starvation/waiting was incorrect. An independent
FFprobe check confirms `video.ts` contains only video PID 0x100. These snapshots
narrow investigation to programme mapping/audio readiness/presentation timing;
they do not prove which component is responsible. The overall five-run job
failed. Per-run evidence is `programmes-single-{1..5}.{log,exit}`. A separate
nested-PID-namespace harness segfaulted before reaching test stages; that harness
was discarded and its failures are not counted as product-test failures.
A subsequent five-run group with PMT/clock/generation diagnostics yielded four
passes and one epoch-5 failure. The failure had receive/decoder generation 2,
media generation 5, clock generation 0, and one PMT stream (PID 256, type 27).
This confirms a video-only mapping and excludes the suspected stale audio-track
mapping for that failure. The presentation path reported `VideoMonotonic` but
did not deliver a frame before the original deadline. Target/available frame
timestamp selection still needs direct observation; no production fix is yet
established.
The subsequent investigation found a deterministic defect in the test's
`await` helper: after the loop observed a successful predicate, the final
failure check invoked that predicate again. Frame retrieval uses nonblocking
locks and is not guaranteed to succeed on two consecutive calls. Extracting
the actual helper control flow reproduced rejection of a one-shot successful
predicate (two calls). The helper now retains the observed success and the
test includes that one-shot regression; its 15-second deadline and media
assertions are unchanged. The deterministic reproduction now accepts success
with one call. With diagnostic session overrides removed, five complete ARM
runs against the actual application libraries all passed (exit 0 each and
overall 0), including video-only startup and both timestamp jumps. This fixes
the demonstrated test false-negative; it does not establish device throughput,
physical timing or universal absence of presentation faults. Initial failure
logs remain retained rather than being relabeled as successful runs.
The unchanged extracted-package readiness test failed its 15-second deadline
under QEMU. A separate diagnostic observed readiness at 15.19 seconds, followed
by successful cancellation; GNU Radio reported shared-memory buffer failures
before falling back. This is also relevant to application startup: both
`OneSegDecoderProcess::Config` and `OneSegTvSession::Config` default to a
15,000 ms startup timeout, and the launcher waits for the private ready byte
after the decoder starts its graph. The diagnostic therefore does not qualify
application startup. After compilation and packaging ended, the unchanged
extracted-backend test passed private import, permissions, ready-FD preservation
within the original 15-second deadline, and cancellation with exit 0. No timeout
was relaxed. The initial failure remains evidence of startup sensitivity under
concurrent emulated compilation; native device timing and buffer initialization
still need measurement after the hardware reservation ends.
The user subsequently authorized CM0 package deployment and supplied working
device authentication. This releases application installation and non-RF
verification; it does not authorize taking over the LR2021 for reception tests
or interfering with the separate OneSegCast processes. Physical RF/audio/video
acceptance remains with the user.

## Physical CM0 deployment, 2026-10-30

The application was upgraded from 1.0.0 to **1.0.1** on the actual Raspbian
Trixie armhf device. The private backend package
**1.0.0+gitb576783021fb-1** and its GNU Radio dependencies were installed in the
same operation. Both packages report `install ok installed`. The detached
installer and independent postflight each exited 0; `dpkg --audit` is empty.
The installed executable matches the application DEB payload and has SHA256
`6b6e31d621facb11e3ded466ecbc331fd4efac0b6895afe23adda356bb34a9b7`.
There are no unresolved dynamic libraries. The native private decoder check
reports `backend_ready` for `onesegcast-mode3-gi16-qpsk23-ti4`, with parameter
autodetection explicitly false. Application `--help` also exits successfully.

The existing desktop launcher points at the upgraded executable. Comparing the
old and new device profiles shows only the addition of
`radio_rf_switch_profile=cm0_16e8`; previous settings are retained. Before
installation, the old executable, package metadata, installed files and profile
were backed up and a 1.0.0 rollback DEB was generated. The deployment receipt,
hashes and installer/postflight logs are retained locally under
`.codex-build/cm0/native-armhf/`, with the package manifest also on the device.

No application or receiver was started for RF testing. This establishes actual
package deployment and dependency availability, not successful FM listening,
1seg reception, live video/audio, touch behavior or return-to-mesh acceptance.
The user will perform physical testing from the existing Trail Mate launcher.

## Remaining television workflow completion contract

Deployment of 1.0.1 did not complete the television product workflow. The user
requires the following before considering 1seg development complete:

- First-use setup with real automatic channel discovery and manual tuning.
  Discovery must establish a supported decoded service, rather than treating
  RF energy, IQ traffic or decoder process readiness as a television station.
- A persistent received-service list, explicit scan progress and cancellation,
  and retention of the previous list after empty, cancelled or failed scans.
- Selecting saved services and switching directly between them while watching,
  without requiring the user to stop, change physical channel and start again.
- Restoring the last selected service on a subsequent launch.
- A viewing interface centered on the fixed 320 x 240 picture, real service and
  programme information where available, and understandable reception states.
  Diagnostics belong in the feature menu rather than the primary viewing area.
- Clear signal-loss/retry behavior and checked radio release on stop or exit.
- Backend parameter discovery suitable for the claimed reception scope. Cycling
  physical channels with the old fixed waveform is not general automatic tuning.

The new `OneSegChannelStore` provides transactional channel-list and last-service
persistence. Its Linux regression covers restart, rescans, corrupt input, invalid
and duplicate services, filesystem failures and temporary-file cleanup. It is
now connected to the production viewing page: a programme is remembered after
its valid mapping and current picture have actually reached display submission.
This is not a station-name or broadcast-parameter discovery claim.

The page now allows physical channel changes during reception, plus previous/next
saved-service selection. It stops the old session asynchronously and starts the
replacement only after the old session is inactive, following worker cleanup.
An RF restoration failure cancels that automatic replacement. Saved-service
selection within the same transport requests programme selection without an RF
restart. Re-entering the feature requests the persisted last service; a picture
from a different service is hidden while the requested service is unavailable.

Actual Linux application and page-harness builds passed. The updated real-page
harness, using explicitly labeled recorded IQ and dummy audio, passed physical
channel 14-to-15-to-14 transitions, saved-station forward/back transitions,
current decoded pictures after each transition, corresponding radio-release
counts, and restoration from disk in a newly constructed page/session. Injected
radio-release failure prevented another acquisition/generation. Persistence
regression passed; keyboard and touch Help CTests passed 2/2. Final combined job
exit was 0 (`.codex-build/cm0/tv-workflow.*`). Rendered controls wrap without
overlapping the fixed 320 x 240 viewport. Decoder warnings during initial
mid-stream H.264 acquisition are retained in stderr; the test establishes later
picture submission, not artifact-free reception or audible output.

Automatic discovery, backend parameter handling, real station/programme metadata,
a channel-list interface, final viewing/recovery integration and ARM/device
qualification remain required. No updated package has been deployed for this
workflow expansion. The existing 1.0.1 installation must not be described as
having these additions.

### TMCC parameter observation

The pinned private gr-isdbt builder now applies a separately hashed TMCC source
adaptation in addition to the existing OFDM safety adaptation. The native
`tmcc_decoder_1seg` block publishes a schema-1 `tmcc_parameters` message only
inside its successful BCH parity-check branch. The message carries the native
mode and 204 received TMCC bits; arbitrary upstream console output is not parsed.
Exact original/adapted hashes, idempotence and preservation of locally modified
sources are checked. The backend build manifest records the adapted TMCC hash
and message schema.

The decoder observes this optional port and reads current layer-A parameters.
It checks all 16 synchronization bits, rejects malformed/reserved fields and
unsupported layer compositions, and reports a parameter set after two matching
validated messages. Mode and guard interval still belong to the explicitly
configured acquisition hypothesis; TMCC does not independently discover those
values. The named-profile decoder remains compatible with the earlier backend;
`--check-backend` now reports whether parameter observation is available, while
`parameter_autodetection` remains false. This message is currently an observation
event, not a station-found or playback-success signal and not yet an application
configuration command.

The actual host backend was rebuilt successfully. The semantic test passes 720
combinations of mode, guard interval, supported modulation, code rate and time
interleaving, plus malformed/unsupported rejection. Those constructed semantic
fixtures do not constitute native BCH or RF tests. A separate actual GNU Radio
test passes with the offline signed-IQ recording: two matching parity-validated
messages report mode 3, GI 1/16, QPSK, rate 2/3 and interleaving 4, while the
decoder emits 1,321 aligned transport packets. Zero IQ produces no valid
parameter event and no transport stream. CTest passes 3/3 for parameter semantics,
existing decoder behavior and native-message reception. Evidence is retained in
`.codex-build/oneseg/tmcc*` and `linux/tmcc-review/`; no LR2021 was opened.

This does not implement automatic OFDM acquisition, automatic layer-chain
configuration, channel scanning or broadcast service identification. Those
remain completion requirements. The new backend has not been rebuilt as ARM or
deployed. A release requiring this interface must identify the changed backend
payload/version and enforce its capability rather than silently accepting the
older installed backend as supporting automatic acquisition.

### Blind mode/GI and acquired-parameter chain validation

`ofdm_candidates` now ranks all 12 mode/guard hypotheses at the backend's native
64 MHz / 63 sample rate using periodic cyclic-prefix correlation. Detection uses
at most 131,072 samples and 64 symbol periods per hypothesis. It subtracts
outside-prefix correlation to reject continuous carriers; candidate scores are
not RF-lock or station-found claims. Thirty-six seeded synthetic cases cover
all 12 hypotheses with arbitrary starting offset, carrier offset, multipath and
8 dB additive noise. Silence, DC, a continuous tone, insufficient/nonfinite input
and 20 pure-noise cases return no candidates. This is bounded software evidence,
not a measured false-positive rate for arbitrary real RF.

`acquire_tmcc_parameters` replays a bounded native-rate capture through the actual
GNU Radio OFDM/TMCC blocks for candidate hypotheses. It requires two matching
parity-validated messages, checks cancellation, bounds the number of hypotheses,
and stops each probe before another starts. Wrong FFT mode, insufficient symbols
and a pre-cancelled probe do not return parameters. `build_decoder_chain` builds
the actual deinterleaving/demapping/FEC chain from the resulting parameter set;
the existing named-profile runtime now uses this factory with its original
explicit settings.

The recorded-IQ test reads a one-second prefix, detects mode 3 / GI 1/16 without
supplying those values, confirms QPSK / rate 2/3 / interleaving 4 from native TMCC,
and configures the real decoder from that result. Its output contains 149 aligned
TS packets. The production `TransportStreamPrograms` parser independently
confirms programme 8 through ARIB partial PMT PID 0x1FC8, valid PMT, video/audio
streams and 71/13 selected video/audio packets, with zero table CRC errors.
FFprobe independently identifies video and audio streams. Initial H.264
mid-stream acquisition warnings remain retained; this does not establish
artifact-free video, decoded PCM or physical presentation.

An initial test incorrectly required FFprobe's ordinary programme association
to contain both media streams. It failed on this no-PAT partial-reception input;
its log/exit are retained as `ofdm-acquisition.first-failure.*`. The corrected
test uses the production partial-PMT parser for service association and the
same short no-PAT probe configuration as the media worker for independent stream
identification. The IQ duration, acquired parameters and positive media/service
requirements were not relaxed. The final five focused CTests all pass.

Evidence is `.codex-build/oneseg/ofdm-acquisition.*` and
`linux/ofdm-acquisition-report.json`. The desktop candidate computation took
about 11 ms and the native TMCC probe about 380 ms in that run; these exclude
the one-second capture interval and do not establish CM0 timing or throughput.
The code has not yet been wired into the live stdin/ready-handshake acquisition
lifecycle, the application scanner, channel-list UI or scan cancellation flow.
The only public decoder profile remains the explicit research profile. ARM
qualification and deployment are still outstanding for these additions.

### Automatic decoder process lifecycle and native reset repair

The decoder CLI now accepts `--profile auto`. It requires the native TMCC
message capability, signals private readiness when it can accept IQ, collects a
bounded one-second IQ prefix, resamples it with the production GNU Radio filter,
acquires parameters and builds the full chain from them. Readiness establishes
input acceptance only. It is not programme discovery or playback success. The
prefix is replayed before later queued IQ, with no intentional sample dropping.
The automatic input queue is bounded to 2,048 blocks of at most 1,280 bytes;
the existing 250 ms full-queue failure remains in effect. Prefix arrival,
native probe shutdown, duration cancellation and child cleanup are bounded.
The explicit research profile retains its original smaller queue and settings.

The real subprocess test covers cancellation before input, during prefix capture
and during probing; a duration bound with no incoming IQ; empty and incomplete
input; zero IQ; and a paced recorded-IQ stream. The positive case consumes all
5,760,000 original bytes exactly once, including the replay prefix, emits
248,348 TS bytes and confirms programme 8 through the production PSI parser,
with video/audio packets and no table CRC errors. Private readiness precedes
IQ input, preventing a startup handshake deadlock. These are offline process
tests, not onboard RF or CM0 performance acceptance.

The first combined run passed the new automatic-process scenarios but exposed a
native SIGSEGV in the acquisition/full-chain regression. GDB reproduced the
crash. The pinned upstream Viterbi reset omitted initialization of `store_pos`;
allocator-pattern testing captured an initial value of -1,010,580,541 with a
traceback depth of 9. The subsequent signed modulo produced a negative history
index and writes before the native traceback array. The builder now applies a
separately hashed one-line reset of this counter, covering construction and
resynchronization in both native implementation branches. Modified upstream
files remain protected by exact original/adapted hashes.

An exact-source compiled reset regression deterministically fails for the old
body and passes for poisoned and reused counters after repair. The actual host
backend was rebuilt. All six focused CTests then passed. Under allocator-pattern
testing, automatic-process and acquisition tests each passed three repeats;
five further debugger runs observed initial `store_pos=0` and exited 0. Initial
failure logs are retained as `auto-decoder.native-crash.*`,
`acquisition-native-first-crash.log` and `viterbi-counter-before.log`; final
job evidence is `.codex-build/oneseg/auto-safety.*` (exit 0). No diagnostic
process remains running. This does not establish absence of unrelated native
defects or replace ARM qualification.

The application C++ launcher still selects the explicit research profile at
this checkpoint. Automatic-profile selection, scan progress/cancellation,
channel-list UI, metadata, final viewing/recovery behavior and updated ARM
packages/deployment remain required before the full 1seg workflow is complete.

### Application automatic-profile integration

`OneSegDecoderProcess::Config` and `OneSegTvSession::Config` now default to
automatic parameters. The session forwards that setting to the real subprocess
and retains it across valid and invalid start/reset paths. Explicit research
configuration still selects the original profile. The page reports the actual
configured acquisition mode rather than displaying a fixed waveform as though
it had been detected.

The automatic decoder reserves exit 4 for failure to validate supported
parameters in the captured IQ; other initialization, transport and decoding
failures retain their separate error exits. Parent-process regression confirms
that true no-signal exit 4 remains available after cleanup. Explicit cancellation
signals a healthy automatic child before closing IQ input, preventing EOF from
being mistaken for no signal. Already-failed children are allowed to finish so
cleanup does not replace their actual exit code with a termination signal. The
initial stop-order and failure-exit race logs remain retained.

The actual page initially failed before its first frame because the IQ worker
queue filled during acquisition. A local interpreter-heartbeat measurement found
a 156 ms scheduling gap while GNU Radio's vector bindings converted large Python
sequences. The acquisition path now hands complex bytes through anonymous
in-memory files to native source/sink blocks, preloads bindings before private
readiness, and avoids converting the resampler output into a large Python tuple.
This is transient process-owned data, not saved IQ or an executable payload.
The same desktop measurement then observed a 7.7 ms maximum heartbeat gap;
resampling/probing returned the same parameters. Neither the radio-worker queue,
the automatic decoder queue, overflow handling nor the original frame deadline
was relaxed. These measurements do not qualify Raspberry Pi timing.

Final evidence is `.codex-build/oneseg/app-auto-recheck.*` (exit 0): seven decoder
and parent-process CTests pass; the actual page passes automatic first-picture,
continuous channel changes, saved-station changes, new-instance restoration,
audio-clock checks, stop/exit and failed-radio-restoration checks. Keyboard and
touch Help CTests pass 2/2. The current rendered page retains the 320 x 240 viewport
and its fixture label. Earlier failures remain in `app-auto-profile.*`,
`native-profile.exit-race-before.log` and
`.codex-build/cm0/tv-auto-page.iq-starvation-before.log`. Initial H.264 acquisition
warnings are retained; this is not an artifact-free video or audible-output claim.
No new debugger, physical radio, firmware or device deployment was used for this
integration verification. Scanner/menu/metadata/viewing recovery and updated
ARM package acceptance remain required.

### Broadcast schedule time semantics

The EIT's five-byte start field represents MJD and BCD **Japan Standard Time**,
not UTC. An all-ones start or duration denotes an undefined value and must not
be rendered as a real timestamp or a zero-length programme. See
[ARIB STD-B10, section 5.2.7 and Annex C](https://www.arib.or.jp/english/html/overview/doc/6-STD-B10v5_13-E1.pdf).

Schedule start and duration are planned times. Broadcasts can keep the original
schedule during delays, so a nominal end time alone must not erase a fresh EIT
present event that is still reported as running. The same standard's
present/following guidance allows programme gaps and schedule slippage. This
must remain distinct from the existing 30-second monotonic reception-freshness
policy. Schedule conversion, undefined/invalid-time presentation and running-state
tests are still required before claiming complete programme information.

### Offline metadata UI checkpoint after the test1 handoff

The local TV page now reads `Snapshot.service_text` for station, present and
following titles. Three fixed-height lines keep the 320 x 240 viewport and the
reception controls visible. Missing information, unsupported text and unavailable
fonts have explicit states. The broadcaster's decoded text is never replaced
with a plausible station or programme name.

The separate `packs/ja-broadcast` content font covers 6,960 baseline characters,
including the fullwidth space. Real LVGL loading verifies every advertised glyph
and tracks font allocation cleanup. Its 64-bit retained payload is 472,132 bytes
and the load peak is 499,980 bytes; the manifest reserves 655,360 bytes and the
package targets the extended memory profile. This is not an ARM memory result or
complete ARIB additional-symbol/DRCS coverage.

Unbuffered Linux POSIX reads initially stalled the UI during font loading and
failed the unchanged frame deadline. Linux LVGL file reads now use a 4 KiB cache.
The same recorded-IQ page workflow subsequently passed with the font loaded,
including an assertion that the visible station name matches received metadata,
channel/station changes, retry, scan cancellation, restoration and cleanup.
Evidence is `.codex-build/cm0/broadcast-final.*` (exit 0), alongside the preserved
`broadcast-loaded*` failures. The native font coverage and memory result is also
at the beginning of `broadcast-final.log`. Locale pack validation passes.

The recording contains a station name but does not qualify nonempty EIT-title
presentation. Programme details and schedule presentation, saved station-name
updates, ARM resource packaging and device qualification remain required.
Initial H.264 acquisition warnings remain. The CM0 application and radio were
not accessed or updated during this work; the user retains the deployed test1
version for physical reception testing.
