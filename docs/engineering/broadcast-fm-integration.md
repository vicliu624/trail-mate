# Independent broadcast FM integration

FM is a separate application for the LR2021 targets, not a Protocol Probe or
1seg TV mode. The user selected the P4 TFT LR2021 variant and **100.8 MHz** as
the first physical listening test. No successful physical listening result has
been recorded in this worktree.

## Evidence and receive profile

The [upstream range measurements](https://github.com/SAMS0N1TE/lr2021-sdr/blob/0a44fc69beb34274d6e86f29c1c064b3d720c48d/research/rx-range-and-gain/README.md)
report experimental reception from about 100 MHz on one P4 LR2021 ROM 1.24
board. IMG_CAL may remain flagged near that lower edge. This does not establish
complete 87.5–108 MHz coverage or performance on the user's board.

The shared `FmLr2021Rx` profile therefore accepts only 100–108 MHz, checks ROM
1.24 and exclusive ownership, and configures the receive path independently of
RadioLib's packet-frequency setter. Ordinary packet/TX guards remain intact.
It stages the wide FSK converter input code 17 for raw source 0 and requests
250,000 signed IQ8 pairs/s. The [raw-route report](https://github.com/SAMS0N1TE/lr2021-sdr/blob/0a44fc69beb34274d6e86f29c1c064b3d720c48d/docs/host-sdr.md#raw-receive-routes)
describes that converter input and programmable source-0 output. Its measured
576 ksample/s results are not a measurement of this new 250 ksample/s path.

The 4 MHz SPI check is a wire-rate lower bound, not a throughput qualification.
The board adapter temporarily owns its checked 16 MHz SPI mode and must measure
actual FIFO service timing and rate. RF passband, spectral orientation, effective
sample precision and board matching still need physical acceptance.

Every command failure and chip error stops setup by default. An explicit
`allow_image_calibration_warning` request can permit only IMG_CAL (`0x0080`),
while retaining both the raw error bits and a calibration-warning flag. It never
permits a failed command or another chip error. The eventual page must expose
that warning; it cannot present the exception as calibrated RF acceptance.

## Current code

- Existing `BroadcastFmDecoder`, `FmIqConsumer` and `FmReceiver` provide mono
  250 ksample/s IQ to 48 kHz PCM processing and worker-owned application logic.
- `FmIqQueue` supplies 16 bounded 1280-byte slots between a single capture
  producer and a single DSP consumer. Publication uses lock-free 32-bit atomic
  indices; payloads retain session, generation, rate and first-pair metadata.
  Invalid input and a full queue reject entire blocks. The runtime owner
  cancels on overflow, allocates the queue and consumer scratch persistently,
  and joins both workers before destruction. Host tests cover full/empty behavior, ownership of
  copied payloads and 100,000 blocks with concurrent producer/consumer reuse.
- `fm_iq_stream.h` defines independent request/result metadata; it has no TV
  channel semantics and does not expose the optional C capture-library headers
  through every Linux packet-radio consumer.
- `run_fm_lr2021_stream` uses the checked FIFO engine, caller-provided staging,
  cancellation and bounded sink acceptance. It saves and restores the packet
  PHY, rejects unowned/invalid sessions before mutation, preserves calibration
  diagnostics, and extends accepted-byte/sample indices to 64 bits.
- The Linux LR2021 driver exposes this path only for its explicit CM0 RF-switch
  profile. The coordinator requires a foreground lease, excludes concurrent
  packet operations and quarantines failed restoration. Capability reports an
  available backend path; it does not attest device readiness or an FM station.
- `FmPcmOutput` opens the actual Linux SDL mono 48 kHz output under exclusive
  `FmRadio` audio focus. Session tags reject stale PCM after retuning, volume
  scaling uses persistent scratch, and writes have bounded backpressure and
  cancellation. Closing discards queued audio and closes the device before
  releasing focus. It is compiled with Linux media, independently of TV media.
- `FmSession` composes the real packet-radio stream, bounded queue, mono DSP and
  physical PCM adapter behind UI-safe tune/stop/volume/snapshot methods. An owner
  thread serializes radio acquisition and restoration while a consumer thread
  handles DSP and PCM. Pending retunes coalesce to the latest requested station;
  restoration failure suppresses pending retunes. A new queue and decoder reset
  isolate each station. Its `PcmQueued` phase only attests accepted PCM. Requested
  calibration exceptions are visible from startup, with final raw radio results
  preserved after stopping. Hardware endpoint checks run on the owner thread,
  not in UI tune callbacks.
- `FmRadioApp` is a target-owned independent Menu application in the Linux
  CM0 LR2021 composition when Linux media and LR2021 support are enabled.
  It connects tuning in 0.1 MHz steps, Start/Stop, independent volume, explicit
  stopped-only IMG_CAL exception, Help and asynchronous exit to `FmSession`.
  Demo/simulated radio modes get no synthetic receiver. The real composition
  uses the foreground coordinator and physical packet-receive handoff barrier.
- The P4 codec already has an independent FM owner. The shared receive profile
  now connects through `Lr2021Radio::streamFmIq` to its real checked command,
  single-frame FIFO, reset and packet-mode restoration methods. Singleton-owned FIFO
  scratch is copied through the existing internal DMA buffers. The entry holds
  the physical mutex and requires a powered/online packet radio, a foreground owner,
  paused AppTasks and no transmission. Receive switching precedes raw SetRx.
  It retains the configured 10 MHz SPI device; this is not measured throughput.
  `platform::ui::fm` now composes dedicated P4 receive and DSP tasks, the bounded
  IQ queue, mono decoder, actual codec begin/write and checked audio close. The
  independent P4 page now calls this service; physical acceptance remains pending.

The P4 service pins the producer to core 1 and DSP/audio to core 0, uses 6144-byte
task stacks and keeps all IQ/DSP/PCM buffers in persistent storage. Start rejects
an active or unclean session; Stop is asynchronous and also retries failed codec
cleanup. It joins the DSP consumer before destroying its queue or closing audio.
Radio restoration and release occur before publishing inactive. Queue overflow,
discontinuity, allocation/task, stream, audio and recovery failures are explicit.
`PcmSubmitted` only attests codec acceptance, never RF station lock or audible
playback. The continuous FM entry also accepts a successfully committed FSK
profile. Under the same physical mutex it applies a real LoRa staging profile
for the legacy IQ engine, then cold-resets and restores the original FSK
frequency, bitrate, deviation, bandwidth, power, preamble, sync word and CRC,
including the prior RX/standby state. It does not relabel an FSK profile as LoRa.
Failed staging/capture still attempts restoration; failed restoration marks the
driver offline. A failed FSK configuration cannot replace the last committed
profile. Physical mode handoff remains unqualified.

The 2026-10-30 exact-source mode test compiles the actual FSK configuration,
staging and restoration method bodies against explicit hardware boundaries.
It verifies all saved settings, RX/standby, capture and staging failure,
restoration failures and commit-on-success behavior. The complete native suite
passed 19/19 and the existing IQ contracts passed 8/8. Invalid callback/buffer or
frequency requests are rejected before mode changes; immediate cancellation
returns without changing modes.
This is software restoration evidence, not a physical mesh/Walkie/FM acceptance
result. Finite Probe IQ retains its separate LoRa-only entry contract.

IDF foreground radio admission now rejects nested/concurrent acquisition and
acquisition during release immediately. IDF Walkie uses this same admission
and release path instead of bypassing it via the physical driver. Existing
external task pauses are preserved. Direct production-method extraction tests
verify one winner, failed acquisition without release, no unmatched release,
configuration-before-release ordering and mutual exclusion with Walkie.

The P4 runtime test executes the actual service on real host threads with
clearly isolated radio/codec/RTOS boundaries. It covers PCM submission through
the real mono decoder, startup/task/audio refusal, cancellation while recovery
is held, session changes, discontinuity, audio/capture/recovery errors, blocked
PCM causing an explicit full-queue stop, cleanup failure/retry without another
radio acquisition, and repeated immediate stop. The focused native suite passed
16/16 on 2026-10-30. This is software lifecycle evidence; it does not measure
P4 task scheduling, RF reception, DAC output or speaker audio.

P4 audio now exposes `trail_mate_t_display_p4_audio_end_checked`. Physical close
failure retains the owner and audio focus, quarantines PCM/read/configuration,
and rejects reopening until the owner retries cleanup successfully. An open or
initial configuration failure whose rollback also fails retains the same
quarantined reservation instead of falsely reporting a free codec. The legacy
void close API delegates to this checked path. An FM worker must always attempt
checked close after any begin attempt, including a failed begin, and publish a
cleanup error if it cannot release the session. This is required before the P4
FM worker can honestly report stopped/free resources. The P4 FM service now
uses this path, attempts bounded cleanup retries, retains `cleanup_failed` if
unsuccessful, and allows Stop to retry cleanup without reacquiring the radio.
Checked cleanup succeeds without side effects when that owner is already
absent, including when another function owns the codec. It never closes the
other function's session. The 2026-10-30 P4 hardware tests passed 7/7, including
mutex timeout, physical close failure, failed-open/configuration rollback,
quarantine, preserved focus, successful cleanup retry and idempotence. The TFT
LR2021 firmware build also passed. No physical audio acceptance was performed;
the P4 FM worker uses this checked lifecycle.

The P4 runtime uses a 100 Hz RTOS tick and monitors both idle tasks. At the
requested 250 ksample/s IQ8 rate, a 1024-byte FIFO holds only about 2 ms.
`P4IqWait` now supplies an ESP one-shot timer and static binary semaphore: the
ESP stream path blocks for a requested 100 us between FIFO service iterations,
checking cancellation before and after waiting. The normal interval does not
depend on the 10 ms tick. A tick-based timeout is only a failure bound; it stops
capture and follows checked restoration. The timer and semaphore remain idle
in the process-lifetime singleton between sessions. Callback generations reject
stale semaphore tokens; a late callback never retains a task handle. Re-entry
is refused until any timed-out timer has completed. This uses bounded retained
resources rather than allocating per service iteration.

The adapter is exposed by the independent P4 FM page. Actual timer dispatch
latency, FIFO service margin, watchdog behavior and RF/audio remain unqualified.
The pacing implementation passed 8/8 IQ contract tests (including 10,000 timer
waits and timeout/late-callback cases), 14/14 native FM/TV/media tests, and the
real TFT LR2021 ESP-IDF build on 2026-10-30. Physical scheduling acceptance
remains pending.
The P4 production FIFO method passed direct source-extraction boundary tests on
2026-10-30, including status/transport failures and CS/bus cleanup; the full IQ
suite passed 7/7. The TFT LR2021 firmware also compiled successfully with the new
stream adapter. These FIFO tests alone do not prove a worker, page or physical
FM acceptance.

Before enabling P4 continuous FM, the RX overlay now removes the unconditional
1280-byte fallback from the ESP stream stack. ESP continuous capture accepts
only checked sessions with caller-owned staging; unbuffered legacy stream APIs
fail before HAL locking or I/O. The host fallback and finite diagnostic capture
remain available. The 2026-10-30 host ESP-path storage contract and IQ suite
passed 7/7, FM/1seg/media tests passed 14/14, and the TFT LR2021 ESP-IDF firmware
build completed successfully with `ESP_PLATFORM` active for the C engine. The
512-byte stack-frame contract uses the host compiler and does not establish
the complete RISC-V task stack budget. No device was flashed or accessed.

The Linux and Cardputer compositions must be rebuilt with normal CMake
dependency handling after the public packet-radio interface changes. `/fast`
builds are not valid header-ABI verification.

## Remaining work

### P4 page and software acceptance (2026-10-30)

The shared P4 `fm_radio` page is registered as an independent Menu app and calls
the production FM runtime for start, stop, volume and snapshots. Retuning waits
for asynchronous stop/restoration and coalesces requests to the latest selected
frequency. A restoration or cleanup error cancels automatic restart. Stop can
retry quarantined audio cleanup without reacquiring the radio.

P4 does not use the Pager navigation implementation for this page's Help. The
page opens the shared shortcut dialog directly; the P4 dialog has a touch close
button, H toggles Help, and Escape closes Help before returning to Menu. This
avoids exposing a Help action backed by a no-op Pager implementation.

The normal Linux dependency build and five focused tests passed: P4 landscape
(1168 by 540), P4 portrait (540 by 1168), Linux FM page, FM session and PCM output.
The UI tests exercise the actual shared page with isolated runtime inputs,
including latest-request retuning, cleanup retry, touch Help close, keyboard
behavior and control bounds. Cardputer Zero Help regressions passed 13/13 after
the shared catalog/dialog changes. Captures in
`.codex-build/cm0/p4-fm-page-review` are actual LVGL rendering, explicitly marked
`UI CONTRACT FIXTURE - NO RF / NO AUDIO`. Session counters and errors in those
captures are controlled test inputs, not receiver measurements.

The TFT LR2021 ESP-IDF build completed successfully. The resulting
`trail-mate.bin` is 5,379,328 bytes, SHA-256
`3e30912fcfcfb8fad9a3d1c400f082aceef8b902e6cd81f259e38de18b5b3a6e`
after the checked Probe TX/RX fixes and saved-FSK FM handoff implementation.
The linked ELF contains the FM page, production runtime, LR2021 IQ stream and
checked codec output/cleanup symbols. These checks establish software
composition, not physical reception or audible output. No device was flashed.
CM0 and its LR2021 remain reserved for the user; device access is deferred until
the user releases them.

Validate native device deployment and physical FM reception/playback.
Qualify the actual rate and passband
before treating the input as qualified. Validate RF
restoration with mesh, Walkie and Probe, and perform the requested 100.8 MHz
listening test. PCM acceptance is audio-output progress, not station-lock
evidence. Scan/seek, stereo and RDS are not delivered by the current mono core.

Host tests inject SPI/FIFO behavior or synthetic IQ. They verify control flow,
ownership, flags, error handling, cancellation and restoration, not physical RF
or audible playback. The production runtime does not select those fixtures.

The Linux `linux_fm_pcm_output` test runs the production PCM adapter with SDL's
dummy audio driver. It verifies exclusive focus against Walkie/Television,
nonzero session ownership, stale-session rejection, volume requests,
cancellation, idempotent cleanup and reopening with a new session. It does not
verify the device DAC, volume acoustics or reception. The normal CM0 dependency
build and this test passed on 2026-10-29.

On 2026-10-30, the normal Linux dependency build compiled `FmSession` and the
`linux_fm_session` integration test passed. A clearly identified fake radio
feeds IQ into the production queue, DSP and SDL dummy PCM output. The test
covers PCM submission, exclusive focus, retuning while radio restoration is
held, coalescing to the latest frequency, responsive commands/snapshots,
calibration-diagnostic retention, discontinuity failure, handoff refusal,
restoration failure and repeated immediate stop of pending requests. Radio
acquisitions and releases balance and music focus is released. This is software
composition evidence, not a physical radio or speaker acceptance result.

The Linux CM0 composition now registers `FM Radio` as an independent Menu app.
On 2026-10-30, the full normal main/UI build completed and the focused FM page,
FM session, PCM and existing TV keyboard/touch Help tests passed (5/5). The page
test uses a deliberately unavailable radio, verifies tune/volume/exception
controls and the actual error, opens FM-specific Help and checks asynchronous
exit. Captures under `.codex-build/cm0/fm-page-review` are actual LVGL output,
marked `NO HARDWARE - ERROR PATH REVIEW`. Unknown chip flags are shown as not
read, never as a successful zero-error measurement. Help fits its 320-pixel
dialog without obscuring rows. A separate main-executable run in explicit demo
and simulated-radio mode verified catalog registration and opening `fm_radio`;
it deliberately provides no FM receiver. Cardputer Zero's full normal build and
13/13 Help regression tests also passed after the shared catalog change.
