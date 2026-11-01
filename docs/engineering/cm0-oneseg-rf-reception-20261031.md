# CM0 1seg RF reception investigation, 2026-10-31

This investigation is still in progress. Finite onboard LR2021 recordings have
produced real video and audio on CM0, but continuous live playback in the Trail
Mate page has not passed acceptance.

## Transmitter reference

The user confirmed Sony A919 programme discovery, normal picture and normal
sound during the full-video Pluto transmission starting at 21:45:09 UTC+08:00.
The source is the user's local `test.mkv`, SHA-256
`4336df21a48e2ff693a81624964e0c46d280e8a6860c8d5d20b791f482b80ee0`,
duration 1712.543 seconds. The reproducible reference and user observation are
retained in the sibling OneSegCast workspace's local
`build/cm0-rx-reference-20261031/` directory.

Reference parameters are Japanese CH13, nominal 473142857 Hz, Pluto LO
473144734 Hz, 3 MS/s, 1 MHz RF bandwidth, TX gain 0 dB, Mode 3, GI 1/16,
one QPSK layer-A segment, code rate 2/3 and time interleaving 4. Media is
320x240 H.264 and 48 kHz stereo AAC. The startup amplitude reference peak is
0.5674009919166565 with fixed target 0.8.

The first full-video attempt lost its IIO connection at 22:07:33. A fresh
connection subsequently read TX powerdown as 0; the agent explicitly set it
to 1 and confirmed the readback. The next complete run submitted 5,143,396,352
samples over 1714.419579 seconds and reported transport cleanup complete.
Fresh readback after this run confirmed powerdown 1. These are transmitter
software/readback observations, not a full-film receiver BER measurement.

## Capture failures and measured improvements

The installed test2 application originally stopped after a few thousand IQ
pairs with `TV IQ stream rejected: -4`. A bounded SPI trace showed FIFO level
0x0404 and sticky error flags 0x1c after a service gap of about 2.2 ms.

The hot FIFO-level and FIFO-flag queries now use two checked HAL frames. The
HAL retains its actual BUSY wait before each frame. Bad command status or I/O
failure does not publish output. Other register/configuration reads retain
the existing RadioLib path.

The 1seg owner thread now enters scoped FIFO70 scheduling and binds to its
last allowed CPU after the consumer has been created. Scheduling and CPU
affinity are restored after capture and on admission failure. A caller needs
an appropriate realtime limit or capability; an unprivileged desktop process
without that admission must report the missing permission explicitly.

With 2048-byte caller staging, the checked raw stream can drain up to 1020
payload bytes in one SPI frame. Worker slots and pipe writes accept up to
2048 bytes. Existing 1280-byte engineering callers retain 256-byte reads.
FIFO flags, continuous pair indices, bounded queues and cancellation remain
checked; an overflow is still an error.

| Measurement | Result |
| --- | --- |
| Installed engineering baseline | 2746 forwarded pairs; capture error -4 |
| Larger-frame scoped capture, 60-second owner budget | 32,255,086 pairs over 56.000857 s, then FIFO error; maximum service gap 2317 us |
| Fixed gain 5, 20-second owner budget | 11,296,962 pairs over 19.614353 s; capture/recovery errors 0; queue high-water 2; maximum FIFO 850 bytes; maximum service gap 741 us; normal coordinator release |

The owner budget includes setup and teardown, so it is not the requested
on-air sample duration. The longer failed recording is not continuous-playback
acceptance.

## Gain and actual received media

Automatic gain produced significant six-bit IQ clipping in this setup and
failed native TMCC parity checks. Controlled fixed-gain recordings at steps
5, 9 and 13 yielded valid TMCC frames without correction. Step 1 was too weak.
A device-owned `radio_oneseg_gain_step` option accepts 0 for chip AGC or 1..13
for explicit gain. Unspecified configurations retain AGC; the local receive
test profile selects 5.

The real installed ARM decoder recovered 5420 TS packets (1,018,960 bytes)
from the 19.614353-second gain-5 recording, with zero reported TEI packets or
decoder errors. CM0 FFmpeg subsequently decoded a clean 320x240 frame and
384,000 bytes of stereo 48 kHz signed-16 PCM, representing two seconds.
Initial H.264 PPS/header acquisition errors preceded the first received key
frame. No reference video or TS bytes were used to synthesize receiver output.

This validates finite real-RF decoding, not audible speaker output or the
actual television page. The frame and logs are local investigation artifacts
under `.codex-build/cm0-rx-probe/`.

## Remaining performance work

The installed decoder took approximately 37.24 seconds to process that
19.61-second recording. The installed VOLK library reports only the
`generic_orc` machine. A private NEON VOLK build and a separate native ARMv7
NEON backend are being evaluated; neither replaces system libraries.

The Python IQ source batches already available data into native scheduler
buffers instead of publishing at most one 640-pair read per callback. Replay
ordering, partial signed pairs, cancellation and EOF are regression tested.
Real-coefficient resampling produced the same recovered TS as the original
complex-coefficient path in the finite gain-5 comparison. Enlarging automatic
acquisition storage to eight seconds allowed actual TMCC acquisition on ARM,
but its full queue still exposes the steady-state throughput deficit.

A separately built Mode-3 geometry prototype tests a 576-point FFT at
4000000/7 samples/s. Conversion from LR2021's 576000 pairs/s is exactly
125/126, and a 36-sample GI gives the same 1.071 ms OFDM symbol duration as
the 1024-point reference. Its pilot-table lookup remains selected by
transmission mode, independently of FFT geometry. It recovered 5548 TS
packets with zero TEI from the gain-5 recording, finishing 20.85 seconds after
the ready handshake. Cold setup took 17.88 seconds during the background
native build. This is close to the sample duration but does not qualify live
headroom or page playback. The prototype remains isolated.

The updated GUI application has completed an ARM target-ABI build and device
dependency/profile checks. Its unstripped executable SHA-256 is
`aecad5cad51bb6df2582cda8f0ef37d7c35f81567f82bff103d914b85024c58b`.
It has no build-host RPATH/RUNPATH and has not replaced the installed package.
An actual-page attempt stopped before IQ admission because the decoder's cold
startup exceeded the original 15-second handshake budget. The CM0 composition
now uses the process controller's supported 30-second startup budget. The
replacement private test executable SHA-256 is
`166edefd79a48ecfb5e5cabf69f14130375a30b56438440dcebabcca3bbb9e04`.

The updated standard decoder's host lifecycle and actual-RF-recording
automatic-acquisition regressions pass. The latter recovered programme 8,
PMT PID 8136, video and audio, with zero service-table CRC errors. Cancellation
before input, during the prefix and during acquisition passed; zero IQ produced
no programme. These tests do not open the physical receiver.

Remaining acceptance is continuous LR2021 capture with sufficient decoder
headroom, successful automatic acquisition, fresh live frames on the actual
320x240 page, audible CM0 output, and verified stop/recovery. Software queue
progress or a finite decoded frame must not be presented as completion.

## November 1 capture isolation

The user requested metric-based verification without screenshots. Optional
telemetry now distinguishes decoded frames from frames acknowledged by the
video widget, and PCM production from an active, draining audio device queue.
The current GUI tests have not qualified playback: presented frames and PCM
remain zero when capture fails.

At 500000 signed-IQ pairs/s, a finite 19.621914-second hardware recording
contained 9809866 pairs, with no capture or restoration errors. The isolated
compact automatic decoder recovered 4063 TS packets with zero reported TEI or
decoder errors in approximately 19.65 seconds after its ready handshake.
This remains a finite recording result, not a live playback result.

SPI tracing confirmed an arrival rate of approximately 1000000 bytes/s and
variable kernel transfer latency. A 1000-us SPI polling threshold combined
with bounded low-water pacing passed a 30-second headless capture, but GUI
capture still overflowed. Moving capture affinity to CPU 2 did not resolve it.

A private CM0-only MMIO experiment reuses the verified SPI0/GPIO addresses and
CS0 pin configuration from the existing MIT-licensed hardware experiments.
The preload must export `__ioctl_time64` for the target executable's ABI;
initial tests exporting only `ioctl` did not exercise MMIO. Its transfer clock
must also be set explicitly: setting the kernel's requested speed alone does
not update the hardware divider before a kernel transfer. With those controls
corrected, a 15-second headless MMIO capture passed with 7308354 forwarded
pairs, zero capture/recovery errors, and queue high-water 2. GUI capture still
failed before TS output.

Per-thread resource accounting then measured 11 major page faults during a
short failing GUI capture. A separate experiment now preloads and locks
executable mappings and the capture thread stack before starting RX. Results
are pending. The MMIO adapter is not a product implementation or installed
package; abnormal-exit register restoration and privilege handling still need
qualification before promotion.

The queue itself is now resident during the receive operation: the worker
locks its bounded storage before handing off or acquiring the radio and
unlocks it at cleanup. If the OS denies this admission, it reports an explicit
error before opening the decoder or touching the radio. A regression with a
zero `RLIMIT_MEMLOCK` verifies those boundaries; the worker suite passes.
With queue residency and selected runtime mappings resident, one failing GUI
attempt recorded zero major and minor faults, but still overflowed. Another
attempt measured a 1286-us direct SPI frame for 822 bytes, while the largest
short sleep was only 145 us. Residency fixes one measured source of latency
but does not qualify continuous capture.

No-GUI capture plus the actual decoder and PCM path also failed before TS
output, so the UI alone is not established as the cause. The standalone
engineering launcher must explicitly forward the 500000-pair input rate to
the decoder; its initial isolation attempt omitted that environment override
and must not be used as a decoder correctness result.

An IRQ trace was too intrusive for throughput qualification and reported
dropped records. Separately, `/proc/interrupts` showed approximately 1.34
billion architectural timer interrupts on CPU 2 versus approximately 2.5
million on each other CPU. The abnormal counter continued increasing with
no surviving perf recorder. Capture had been pinned to CPU 2 in these tests;
an isolated CPU-1 comparison is being built. This observation is not yet a
proven final cause. Global governor was restored to `ondemand` after an
unsuccessful `performance` comparison. An attempted USB IRQ affinity change
was rejected by the kernel and read back its original mask `f`.

## SIMD decoder and controlled thread isolation

The receive owner and IQ-pipe threads now have descriptive Linux thread names.
A private affinity harness initially let the owner inherit the main thread's
restricted mask, which made its preferred CPU unavailable. Earlier experiments
using that harness do not prove isolation on CPU 1. The corrected harness
prints and verifies the actual realtime thread affinity. CPU-1 and CPU-3
isolation comparisons still failed the continuous-capture gate.

The fixed-profile Python input previously retained only eight 1280-byte
blocks, approximately ten milliseconds at the supported rates. It now uses
the same bounded 7200-block startup buffer as automatic acquisition. The
worker and production IQ batching/remainder/cancellation regressions pass.
This is startup headroom, not permission for a growing steady-state queue.

The ARM native backend selected its scalar Viterbi implementation. A separate
library in `/home/pi/cm0-rx-neon-viterbi` adapts the existing vector path with
SIMDe 0.8.2 and retains the compact OFDM object. Original source/header hashes,
compiler/linker commands and output hash are recorded in its receipt; the
baseline files and installed package are unchanged. All 15 deterministic
comparisons (three constellations and five FEC rates) produced byte-identical
scalar and vector outputs. Combined Viterbi graph runtime was 3.9121 seconds
scalar versus 1.5220 seconds vector, approximately 2.57 times faster. For the
reference QPSK 2/3 case the times were 0.1166 and 0.0435 seconds.

The full fixed-reference decoder then processed the 19.621914-second actual
500-kpair/s RF recording in 10.5303 seconds, reporting 3999 TS packets,
751812 bytes, zero TEI/errors and exit 0. This test used CPUs 0, 2 and 3 and
does not itself prove live headroom with the UI and audio output.

Finite live headless tests recovered programme 8 and valid PMT, and one test
produced 26 decoded frames and 104448 accepted PCM samples before capture
overflow. A preceding scalar/buffered test produced 13 frames and 87040 PCM
samples. These do not qualify sustained playback, the actual video widget,
or audible speaker output. Initial H.264/demux errors remain to be assessed
after continuous IQ capture is established.

A packed SPI FIFO prototype was investigated from the Raspberry Pi driver's
DMAEN and transfer-reset logic. Its actual packed path did not pass protocol
and timeout checks and has been removed from the active preload. An earlier
10-second raw result used the byte fallback because the frame-length guard
did not admit the observed FIFO frames; it must not be cited as evidence for
packed transfer correctness or improvement. The failed source is retained as
an investigation artifact. The active preload again uses the earlier byte
PIO implementation. The root launcher now opens `/dev/mem` with `O_SYNC`;
that mapping change did not resolve the failed packed experiment.

The reference transmitter lost its IIO connection at 17:30:36 UTC on October
31 and could not confirm cleanup through the old context. A fresh context
read TX LO powerdown as 0, explicitly set it to 1, and verified 1 at 17:31:09
UTC. No transmitter process remains at that checkpoint. Continuous playback
acceptance must exclude the interrupted interval; fresh-context cleanup on
transport failure remains an outstanding launcher improvement.

## Subsequent measurements: November 1

Playback verification now uses timestamped JSON telemetry rather than screen
captures. Decoded frames, frames acknowledged by the actual TV presentation
path, PCM output activity, starvation, and audio/video clock difference are
separate gates. Error cleanup resets presentation counters, so acceptance must
inspect the active interval rather than only the final stopped snapshot.
For GUI PID 9573, generation 1, the active history contains 81 presented
frames and 12 records with an active audio clock; the observed audio/video
difference was 10525–28621 microseconds. Capture lasted approximately 19.7
seconds before its bounded IQ queue filled. This proves brief real GUI
playback, but does not qualify continuous playback.

The timer interrupt source was identified as the keyboard backlight's
software PWM at 20000 Hz. Temporarily disabling that channel reduced its
CPU's timer interrupt rate from approximately 40000/s to tens/s. Screen
hardware PWM is separate and was left untouched. The experiment saves the
original keyboard state and restores it after testing. A permanent policy
has not yet been implemented.

The TS reader now has its own thread name, preventing the private isolation
harness from mistaking its inherited capture-owner name for a second
realtime producer. The private byte-burst SPI adapter passed a 60-second
hardware capture under independent recorded-DSP load with zero capture or
recovery errors; this was not live programme playback. It still requires
privilege and abnormal-exit recovery work before product integration.

Partial SDL texture updates passed exact pixel comparisons at four rotations
with both dummy/software and actual Wayland/OpenGLES2 renderers. These are
synthetic in-memory readbacks, not screenshots or evidence of received video.

The frozen reference transmitter now holds a scoped Windows system-awake
request and performs shutdown verification using a fresh IIO context while
its exclusive transmitter lock remains held. A subsequent full-file run
submitted 5143396352 samples at 3 MS/s in approximately 1714.43 seconds of
output time. This qualifies the reference transmitter's full-file run, not
CM0 reception for that duration. Automatic profile acquisition and sustained
GUI playback remain unqualified.

A subsequent live headless test with the corrected thread names, verified
CPU-3 realtime capture, DSP on CPUs 0–2, and keyboard PWM disabled forwarded
26054664 pairs at 450 kpair/s before the IQ queue filled. It decoded 668
video frames and accepted 2247680 PCM samples; capture and recovery errors
remained zero. The run did not reach its 90-second budget. At 500 kpair/s,
a separately verified isolated run instead failed after 5964834 pairs when
the media worker's 512-packet TS queue filled. Its IQ queue high-water was
only 11; it decoded 104 frames and accepted 415744 PCM samples. These failures
require downstream throughput and burst/backpressure investigation as well
as physical FIFO analysis. The temporary affinity and keyboard settings were
restored after both tests.

A separate bounded silent-PCM test measured the actual SDL pulseaudio queue
draining 143872 mono frames in 3.000268 seconds, approximately 47953 frames/s,
with the application's requested 48-kHz S16 and 512-frame buffer. The idle
SDL rate is consistent with its configured rate; this does not establish
its behavior under simultaneous RF, DSP and video load.

## Bounded media backpressure

Direct replay isolated a media-side burst handling failure: the same 3999-packet
RF recording passed with real PCM output when paced at approximately real time,
but failed when fed as a burst. FFmpeg buffers packets during format probing,
then the media worker plays the retained prefix through its paced audio adapter
before requesting more TS. Immediate rejection of a temporarily full 512-packet
queue incorrectly terminated that pipeline.

The default media input remains nonblocking. Background TV and engineering-tool
TS callbacks now request up to three seconds of cancellable backpressure, with
the queue size unchanged. The TV owner requests media cancellation before
waiting for in-flight callbacks during shutdown. Shorter experimental bounds
of 250 and 1000 milliseconds did not pass the isolated burst replay; the
three-second bound accepted all 3999 packets, produced 200 frames and 661504
PCM samples in 14.090 seconds, and reported no media error. One decode error
was already present in the recorded input. The same input paced at 3000 us per
TS packet produced identical frame/sample counts through both pulseaudio and
ALSA outputs. These are recorded-input results, not continuous RF acceptance.

The CM0 media regression suite passes immediate full-queue rejection, recovery
after a 350-ms blocked consumer, bounded persistent-stall failure, and rapid
cancellation of a producer allowing a three-second wait. Both application
targets built successfully. A subsequent 500-kpair/s live run no longer failed
the media queue, but still hit physical capture overflow following a 1495-us
SPI frame; this remains a separate unresolved requirement.

The temporary affinity observer was found consuming roughly one third of a CPU
core while continuously inspecting thread state. It now verifies initialization
and exits once the capture, IQ-pipe and audio threads have been assigned and
stable for one second. Separate per-second decoder metrics are retained in a
private device log so that buffered native diagnostic output cannot obscure
the final error in the parent's bounded stderr tail.

The retained diagnostics identified a mismatched upstream timeout: a later
450-kpair/s run exited after 102.777 seconds with `TS consumer remained blocked
for 250 ms`, despite the media input permitting longer backpressure. The Python
TS sink now allows four seconds of pipe blockage, above the media-side bound,
while checking cancellation every 25 milliseconds. A real nonblocking-pipe
regression passes recovery after 350 ms, persistent failure at 4.018 seconds,
and cancellation in 51 ms. No packet is silently dropped.

Direct ALSA output (`SDL_AUDIODRIVER=alsa`, `AUDIODEV=plughw:1,0`) removed the
pulseaudio client/server overhead from the qualification run. The actual CM0
hardware parameters were stereo S16_LE, 48000 Hz, period 512, buffer 1024. A
180-second headless session, PID 15251, exited normally with status 0: 80125974
IQ pairs, 2498 video frames and 8032256 PCM samples. Capture/recovery errors
were zero. The decoder processed every received IQ pair and reported 49520 TS
packets, zero TEI and no errors; its fixed-size input queue high-water was 49
of 1125 batches, without sustained accumulation. This uses the explicitly fixed
Sony-verified modulation profile, not automatic acquisition qualification.

The real GUI remains unqualified. PID 15473 first failed foreground radio
acquisition during startup. A manual retry started reception but overflowed
after 839420 IQ pairs, before producing media. The GUI was stopped and temporary
CPU affinity and keyboard PWM state restored. The contrast with the successful
headless run motivates separating physical capture into an exec-launched
process with explicit hardware ownership handoff and recovery. Neither a
screenshot nor decoded-frame counters can substitute for sustained actual
presentation and PCM output in that future verification.

## Capture process and direct display experiments

An explicitly enabled capture-helper path now retains the foreground lease,
releases physical SPI/GPIO ownership, execs the real raw-IQ engineering tool,
and restores the saved packet PHY after reaping it. IQ and a versioned final
capture receipt use separate pipes. The parent validates receipt size/version,
sample rate and byte count. Cancellation terminates the child process group;
an unresponsive child is killed and reaped. Normal transmission, odd pipe-read
boundaries, device errors, missing/corrupt/truncated/extra receipts, rate mismatch,
downstream rejection, cancellation and hung children pass the no-RF process
regression. The ordinary in-process path remains the default.

The private register adapter now sets its inherited capability close-on-exec
at process initialization. A no-radio test confirms ordinary exec children
cannot inherit it. Runtime inspection confirms only the explicitly launched
capture child receives it; the decoder does not. The helper's capture thread
runs FIFO priority 70 on CPU 3 while reporting and pipe-consumer threads use
other CPUs. The private adapter remains experimental; its ioctl ABI handling
and abnormal-exit register restoration still require qualification.

Two related correctness fixes were made. The helper restoration initially
started receive twice: the shared LoRa configuration method already arms it.
Removing the duplicate command eliminated the observed restoration error.
Separately, foreground admission uses a try-lock to protect UI callers. The
background TV owner now retries only failures without an error message for up
to 250 ms, checking cancellation every 2 ms. Transient, persistent and cancelled
admission regressions pass, and later GUI attempts acquired on their first
generation.

The application GLES2 renderer is hardware accelerated: its reported renderer
is Broadcom VC4 V3D 2.1. The desktop compositor is instead forced to pixman by
the installed Raspberry Pi session launcher and consumed roughly half a CPU
core during playback. A temporary KMS/DRM launcher therefore stops the desktop,
uses the application's direct display path, and automatically restarts the
desktop in ExecStopPost. No firmware or permanent display configuration changed.

The 450-kpair/s direct-display run, PID 16987, presented 348 frames over a
sampled 28.235-second playback interval, with audio active in 27 of 29 records
and a maximum sampled audio/video difference of 29509 microseconds. It still
failed physical FIFO capture after 13331294 IQ pairs, and decoder buffering was
growing. The 500-kpair/s run, PID 19052, had no restoration error but overflowed
after 2005290 pairs. These are partial playback evidence, not sustained success.

The pinned RX engine already supports conservative FIFO-level prediction from
the configured input clock, reducing actual level queries while continuing to
check sticky overflow/underflow flags before publication. It is now selectable
only in the experimental capture child, with every fourth pass measuring the
real level. Its regression verifies that either sticky error prevents data
publication. A subsequent 500-kpair/s run, PID 19951, still overflowed after
1954896 pairs. Prediction therefore does not resolve the physical service
margin. That GUI was stopped, the desktop restarted, and temporary affinity
and keyboard PWM settings restored. Sustained real GUI playback remains open.

## Kernel DMA qualification

An experimental kernel-driver DMA path now uses a 10-MHz requested SPI clock
with the verified 500-kpair/s input. It reads samples that arrive while DMA is
running, rather than limiting each transfer to the FIFO's initial contents.
The current read length derives from the measured FIFO level: at most 4092
bytes, with a conservative 128-byte reserve and a 512-byte minimum start level.
Every entire DMA block requires clean sticky FIFO flags before publication;
it is then split into the existing bounded callback chunks. Linux-only radio
member storage holds the buffers; the receive queue size is unchanged.

The clock/FIFO model covers normal data order, a CPU pause during DMA, excessive
delay causing overflow, a too-fast wire clock causing underflow, downstream
rejection, setup failure, clock mismatch, cancellation and PHY restoration.
CMake tests for DMA, the process protocol, the original stream and the worker
pass. One CMake run exposed an error-precedence race when downstream rejection
terminated a child before its receipt arrived. The process adapter now preserves
the original rejection error; deterministic tests cover both present and absent
cleanup receipts.

The CM0 experiment uses the ordinary kernel SPI/GPIO interfaces and no MMIO
preload or inherited register capability. Kernel debug counters confirmed actual
DMA: the counter rose from 836 to 857 during the first short attempt. Fixed
1020-byte and 2044-byte reads still overflowed, and the adaptive variant also
failed. These failures are not successful playback qualification.

An identity-checked temporary SPI-worker scheduler helper was added to test a
possible priority inversion: the SPI0 kernel worker normally uses ordinary
scheduling while the capture producer uses FIFO priority 70. The experiment
sets only that worker to FIFO 75 on CPU 3, preserves its original policy and
affinity, and restores them automatically at service exit. Normal DSP can use
all four CPUs while DMA is in progress. This improved the observed partial run
but did not eliminate failure: GUI PID 24700 presented 130 frames, with all nine
sampled presentation records reporting active audio and no starvation, before
capture overflowed after 5437738 IQ pairs.

A fixed eight-entry in-memory timing history now prints only after capture
stops. Representative transfers took 54–144 microseconds for the FIFO query,
roughly 1.8–2.6 milliseconds for the payload, and 79–121 microseconds for flags.
The failing 2420-byte payload call took 8568 microseconds and returned overflow
flags. Its payload call includes both the pre-transfer BUSY wait and the SPI
ioctl, so the trace does not yet identify which phase stalled. Splitting those
phases is the next diagnostic requirement before choosing another transport
change. The desktop, keyboard PWM and SPI-worker scheduling were restored after
the failed test; sustained real GUI playback remains unproven.

## Later transport diagnosis and capture-only qualification

The HAL now records preparation, BUSY waiting, ioctl, tail delay, CPU time,
fault counts and context switches in a bounded postmortem ring. BUSY waits were
approximately 2 microseconds. Private, temporary SPI tracepoints and driver
probes further separated descriptor preparation, DMA completion and thread
resumption. In the measured window, descriptor preparation peaked at 70 us,
completion-to-resumption at 219 us, and 7500 tasklet samples had a maximum
raise-to-entry delay of 150 us. Those results did not establish delayed ordinary
softirq scheduling as the primary cause. All probes and private trace instances
were removed after measurement. The installed kernel uses voluntary preemption
and HZ=100; no kernel or boot configuration was changed.

The experimental DMA path subsequently requested 8.35 MHz, using the maximum
requested clock as a conservative bound on FIFO depletion. It derives each read
from the actual measured level, retains a 32-byte reserve, caps each transfer at
16380 bytes and starts at 320 measured bytes. A 32-KiB member staging area holds
data until sticky flags are checked; callback chunks remain bounded at 2 KiB.
The kernel character-buffer parameter is temporarily increased from 4096 to
32768 only after verifying the sole SPI endpoint, an unused module, and matching
module/kernel versions. Automatic cleanup restores the original parameter,
worker scheduling, keyboard PWM and desktop. The parameter roundtrip was tested.

One failure exposed two major faults and an approximately 2-ms delay in a
nominal 1-us tail wait. Data buffers had been resident, but executable/runtime
pages and the capture stack had not. A new scoped admission component locks
the executable, selected C/C++ runtime mappings and the hot stack before RX,
with a 48-MiB cap and rollback on failure. The measured CM0 admission was
7708672 bytes, plus the separately locked transport/receive storage. Tests cover
denied admission, normal admission and repeated calls.

A delayed completion can also leave too much data pending to spend time on
flags, publication and a new level query. The capture path can now stage one
bounded catch-up read, calculated from the configured clock and the short
elapsed interval since the last real level measurement, retaining a 128-byte
estimation reserve. It checks sticky flags for both reads together before
publishing any of that batch. Estimated occupancy is not reported as a measured
FIFO level. The clock model verifies recovery after a delayed completion,
preserved byte order, and rejection on excessive delay or underflow.

Capture-only GUI PID 4782 completed its two-minute budget with 56601378 IQ pairs
delivered, zero hardware capture/recovery errors and no decoder errors. A separate
coordinator cleanup error was then identified: restoring LoRa configuration
already armed RX, and the public startReceive wrapper issued StartRx again.
The wrapper now transitions through standby before starting a fresh RX generation.
The subsequent normal-stop test, PID 6154, finished in phase Stopped with all
session/receive/capture/recovery errors zero, 26578574 pairs delivered, queue
high-water 9 and maximum measured FIFO level 778. Its diagnostic reported zero
catch-up reads, so actual hardware coverage of that branch remains limited to
the model rather than established by this run. Five native CMake suites and the
Linux IQ transport contract passed. These tests used noise IQ without a valid
programme transmitter and do not qualify video/audio playback.

The transmitter independently rebooted during the next reference run at
20:53:07 UTC on October 31. A later serial check reported only four minutes of
uptime and explicitly confirmed TX powerdown 1. A fresh IIO connection confirmed
OFF again at 21:19:34 UTC. No-programme results after that interruption were
excluded from playback acceptance. The host test launcher now refuses programme
qualification unless a live reference process has a fresh active-RF health record;
capture-only mode is explicitly labelled. A physical USB/power check was requested.

A read-only Windows USB alternative was prepared: the installed official IIO
tools discovered the same serial-numbered Pluto at usb:1.31.5 and opened it
directly, without driver changes. The installed libiio is 0.21; the PyPI wheel
labelled 0.21 unexpectedly required a missing label API, so the unmodified
official v0.21 Python binding at commit 565bf68eccfdbbf22cf5cb6d792e23de564665c7
was used instead. The probe confirmed both signed little-endian 16-bit TX formats,
the exact device serial, TX powerdown 1, and that the existing station transport
imports under native Windows Python. It did not create a buffer or transmit.
Using that transport through a private binary pipe is a possible next experiment
to avoid the RNDIS path; it has not yet been implemented or RF-qualified.

### Direct USB experiment and automatic reception (22:18 UTC)

The private pipe bridge is now implemented. WSL retains the original encoder,
modulator, sample packing, reference profile and station lock. A native Windows
helper uses the original `PlutoTransport` with the exact serial-numbered USB
device. Length, sequence and SHA-256 checks protect each binary block before
submission; acknowledgements and a final digest verify transport accounting.
Mock tests covered all byte values, chunk boundaries, cancellation and malformed
frames. Mock throughput was 207 MB/s against the required 12 MB/s; this is a pipe
benchmark, not a hardware throughput claim. The helper validates the frozen
source hash and existing approval before opening hardware and performs a fresh
independent TX powerdown readback on cleanup.

The live run started at approximately 22:15 UTC with the unchanged corrected
473144734 Hz LO, 3 MS/s rate and gain 0. At 22:18 UTC the CM0 acquired programme 8
using the normal automatic profile/TMCC path. Its first 28.282 seconds of sampled
playback contained 425 presented frames and 1,469,440 PCM samples, with audio
output active throughout, no starvation, and a maximum reported absolute A/V
delta of 29,696 microseconds. Transport, continuity, CRC, capture, recovery and
media errors were zero. The initial decode-error count of 29 remained constant
after acquisition; the receiver joined an already-running file. This is early
live-programme evidence, not a completed full-file endurance result.

Monitoring uses presentation and audio counters rather than screenshots. A
bounded watchdog observes transmitter health and receiver progress, stopping
the experiment on source loss, stale status, receiver failure or a prolonged
presentation stall. A PowerShell guard was corrected to preserve the UTC kind
of JSON timestamps parsed as `DateTime`; reparsing their culture strings had
incorrectly added eight hours of apparent age.

The experiment subsequently stopped on a real capture failure. Across 33.343
seconds of sampled presentation, the receiver presented 501 frames, decoded 517
and produced 1,742,848 PCM samples. All 34 presenting status records had active
audio without starvation; transport/continuity/CRC errors stayed zero and the
startup decode-error count stayed at 29. The capture then returned -4 with sticky
FIFO flags 28 decimal (0x1c, including the overflow bit 0x10). Its final batch started at level 814 and staged 29,120 bytes,
including a 12,740-byte catch-up read, in 28,670 microseconds. The SPI trace
reported no page faults or involuntary context switches. All bytes from the
faulted batch were rejected. This remains a receiver service-latency failure
under real playback load, not a completed endurance qualification.

The watchdog stopped the receiver and transmitter. Independent USB cleanup
verified TX powerdown 1 at 22:18:58 UTC. The transmitter's original waveform and
USB transport remained live until cancellation; this run did not reproduce the
earlier RNDIS source loss.

### Follow-up timing regressions and remaining headroom (22:29 UTC)

Two deterministic DMA-model regressions now cover a completion leaving just
below the former 512-byte catch-up threshold, followed by a 250-microsecond
next-transfer startup delay, and a further 500-microsecond delay on the catch-up
itself. The former threshold failed the first case. A single catch-up read
failed the second case. Catch-up now starts at an estimated 256 bytes and can
continue within the existing 32 KiB private staging capacity, with each read
bounded to 16,380 bytes and no tiny reads below 2,944 bytes. The whole batch still
requires clean sticky flags before publication. Five related test suites pass.

Real hardware has not passed. Follow-up runs still failed with FIFO flags 28
decimal (0x1c). A private kernel probe measured DMA preparation at 20 and 6
microseconds, but a final 6,210-byte transfer took 497 microseconds from DMA
completion callback to SPI transfer-stop. The callback itself arrived about
6,015 microseconds after preparation. The probe group and trace instance were
removed after collecting the evidence.

The decoder wrapper was also expanding its inherited CPU 0–2 affinity to all
four cores. It now preserves CPU 0–2, keeping CPU 3 available to capture. Another
real run still failed, so affinity alone is not an adequate root-cause claim.
The latest bounded-catch-up run processed successive 32 KiB batches in 32,843,
32,886 and 32,994 microseconds: at 1 MB/s input, FIFO occupancy can increase
despite maximum-sized staged batches. This demonstrates inadequate service
headroom under the observed load. More catch-up reads cannot by themselves
correct sustained throughput below input rate.

Both cross-builds completed successfully and the latest binaries were deployed.
The final experiment was stopped by the watchdog; independent cleanup verified
TX powerdown 1. The receiver unit is inactive, spidev buffer size is restored to
4,096, the display manager is active and keyboard PWM is restored. Programme
qualification remains incomplete.

### 450 kpair/s DMA and decoder startup deadline (22:38 UTC)

The experimental DMA path now accepts the already-supported 450 kpair/s radio
configuration as well as 500 kpair/s. It programs the requested rate into the
route command and uses conservative reserve factors of 7 and 23 respectively
at the unchanged requested SPI clock. The arrival model checks both rates,
including delayed DMA start/completion; all five related suites pass. The actual
450 kpair/s compact resampler taps measured +0.00729 dB at 214,285.7 Hz and
-55.71 dB at 225 kHz. This checks the input filter, not RF playback acceptance.

A real 450 kpair/s run received programme 8 PMT and 59,220 transport bytes, then
failed after about 4.9 seconds. A 2,058-byte SPI read took 3,841 microseconds,
about 1.8 milliseconds beyond nominal wire time. The subsequent catch-up could
not recover the already-faulted FIFO.

A bounded scheduling experiment kept a low-priority FIFO thread on CPU 3 while
capture waited for DMA, temporarily disabling RT bandwidth throttling. A
separate systemd cleanup stopped capture and restored the original 950,000 us
RT runtime; the reservation was limited to 70 seconds. Its first attempt exposed
a decoder startup bug: the three-second acquisition-prefix deadline included
radio initialization after the ready handshake. Only about 0.7 seconds of IQ
had arrived when it expired. The decoder now waits at most ten seconds for the
first IQ block, then allows three seconds to collect its one-second prefix.
Cancellation and explicit duration remain bounded.

The real decoder subprocess lifecycle suite passes all nine cases, including
a new 2.5-second delayed radio-start case. Immediate and delayed input both
produced 514,368 transport bytes, automatically acquired the expected TMCC
parameters, and decoded programme 8 with video and audio. This is an offline
process test. The correction was deployed to the private experimental backend.

Repeating the scheduling experiment with the deadline fixed still failed after
about 7.5 seconds: a 2,018-byte SPI read took 3,729 microseconds. This rules out
ordinary CPU-3 userspace contention as a sufficient explanation. Further
measurement must distinguish actual DMA time from cross-core completion and
resume latency. Both installed v7 kernels use voluntary preemption and offer
no runtime dynamic-preemption switch; no kernel or boot changes were made.
All experiments are stopped, temporary RT policy is restored, and independent
USB cleanup verified TX powerdown 1 at 22:37:48 UTC. Stable live-programme
qualification is still incomplete.

### Monitor-induced BPF cleanup latency (22:48 UTC, experiment ongoing)

The 450 kpair/s capture-only experiment reproduced the failure without RF. A
2,382-byte DMA completed normally 2,333 microseconds after preparation, but SPI
transfer-stop followed the completion callback by 2,862 microseconds. A second
private trace added workqueue execution and RT scheduler switches. For its
failed 2,242-byte read, DMA completed after 2,201 microseconds, then waited another
1,055 microseconds before transfer-stop. CPU 3 had entered
`bpf_prog_free_deferred` at monotonic 20481.994884; completion ran on CPU 1 at
20481.996833; the CPU-3 worker only yielded to capture at 20481.997873. An SSH
authentication process had been running on CPU 3 immediately before that work.
This identifies a concrete non-preemptible post-DMA delay associated with the
monitor's repeated SSH connection lifecycle. All private probes were removed.

The diagnostic monitor now keeps one SSH connection open. A single remote
process pushes status every five seconds after reading only the bounded log
tail; it no longer creates an SSH connection for each sample. Source health,
receiver errors, status age and presentation progress remain monitored, with
bounded connection and observation timeouts. A subsequent capture-only test
completed normally with 47,686,224 validated bytes across 19,192 batches, no
catch-up reads, flags 0x04, and zero capture/recovery errors. No busy guard or
RT-runtime override was used. This is supporting I/O evidence, not programme
qualification.

The next real RF run is using automatic TMCC acquisition and the persistent
monitor. At 22:48:14 UTC, CM0 had presented 902 frames and produced 2,959,360 PCM
samples, with active audio, no starvation, and no transport, continuity, CRC,
capture or media errors. The startup decode-error count remained 18. The full
file endurance result is pending; these are interim live readings.

The preceding RF attempt was independently invalidated by a Pluto reboot.
USB disappeared, then re-enumerated at a new address; COM12 reported uptime
under one minute. Serial control and a fresh USB context both confirmed TX
powerdown 1 afterward. The current run retains the same approved RF parameters
and source file. No firmware, USB driver or network changes were made.

### Subsequent failures and prepared preemptible kernel (23:05 UTC)

The persistent-monitor RF run subsequently failed after 185.626 seconds of
sampled presentation. The source was still healthy and was cancelled by the
watchdog. Its last capture batch began at FIFO level 844; sticky flags later
included overflow. Avoiding repeated SSH teardown removes a demonstrated
trigger, but does not establish a general latency bound on the stock kernel.
Independent cleanup confirmed TX powerdown 1 at 22:50:39 UTC.

A smaller catch-up minimum for 450 kpair/s passed a targeted timing model but
failed on hardware: numerous short reads added enough overhead that a 32 KiB
batch took 36,759 microseconds. That experiment was reverted, including its
targeted test expectation. The canonical minimum remains 2,944 bytes.

Combining persistent monitoring with a bounded CPU-3 reservation instead
exposed automatic-acquisition backpressure. Hardware capture completed cleanly,
but the 512 KiB software queue filled. The backend log measured about 3.8 seconds
of parameter probing followed by 1.2 seconds of graph setup; it eventually
confirmed TMCC. The Linux receive queue now reserves 2 MiB on the heap and keeps
its explicit full/error policy. The overflow fixture now supplies more than
the configured capacity instead of a hardcoded 1,024 blocks. All five related
suites pass, including overflow and cancellation. The new binaries are deployed.

The next real run reached queue high-water 307/1,024 with no hardware capture
error, then stopped because `OneSegPipeSink` permits only 250 milliseconds per
write. This is the next startup contract to address: automatic probing needs a
bounded initial write grace while normal playback still requires prompt stall
detection and cancellation. No pipe-timeout change has been made yet. All tests
are stopped; receiver and guard units are inactive, RT runtime is 950,000 us,
spidev is restored to 4,096, and independent cleanup verified TX powerdown 1
at 23:04:02 UTC.

A separate same-version kernel experiment is fully built locally. It uses the
checksum-verified official 6.18.50 source package, the device's stock config,
full kernel preemption, and an independent release name
`6.18.50-trailmate-preempt-v7`. The kernel, normal modules, DTBs and all ten CM0
vendor modules built successfully; module version tags include preemption and
display/Wi-Fi dependency resolution passed. The build follows the official
[Raspberry Pi kernel workflow](https://www.raspberrypi.com/documentation/computers/linux_kernel.html).
It has not been installed or booted. A future trial still needs a device-built
initramfs, a complete artifact manifest and a concrete one-shot boot setup,
preserving the existing CM0 DTB and overlays. The firmware's
[tryboot mechanism](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html)
provides an alternate one-shot configuration; the original boot configuration
has been saved for preparing that trial.

### Bounded acquisition startup and actual PREEMPT trial (23:40 UTC)

Automatic decoder pipes now opt into an initial grace window. A write may wait
up to three seconds inside that window, capped by its end; ordinary pipes and
post-startup operation retain the 250 ms limit. Cancellation polling remains
10 ms. Real-pipe tests cover a 350 ms reader pause, expiry back to the normal
limit, prompt cancellation and a permanently absent reader bounded by a 600 ms
test window. Decoder-process tests also pass. The Linux fixed receive queue is
now 4 MiB to hold roughly four seconds at 1 MB/s, with explicit full/error
handling. Five additional memory/capture/worker/stream suites pass.

The 450 kpair/s acquisition resampler now factors its exact conversion through
576 ksample/s. Its real GNU Radio passband regression measured less than 0.005
dB variation through 214,285.7 Hz and correctly acquired the saved RF prefix.
Under the same desktop/PWM conditions used for playback, CM0 measured 1.40
seconds for resampling and 0.69 seconds for TMCC confirmation. A separate private
wrapper bug was then identified: it constructed a large native resampler only
to replace it with the compact resampler. The wrapper now skips that unused
construction and inserts the compact stage once. A real chain construction
regression verifies one resampler, correct connections and environment
restoration; CM0 measured about 0.25 seconds. Live logs subsequently showed
automatic parameters confirmed and the runtime graph started 0.16 seconds later.

The same-version PREEMPT kernel was staged with an independent module directory,
image and device-built initramfs. Hashes and critical module version tags were
checked before a one-shot tryboot. CM0 booted successfully into
`6.18.50-trailmate-preempt-v7`; Wi-Fi/SSH, DSI panel, touch/keyboard modules,
SPI and the board audio card were detected. Stock boot configuration hashes
remain unchanged. The current kernel is a trial: the next ordinary reboot uses
the original configuration. It has not been promoted as a stable fix.

Live reception remains unqualified. At 500 kpair/s, the latest run acquired
programme 8 and produced video/PCM without transport, continuity, CRC or decode
errors, then stopped on capture overflow after 30,967,876 validated IQ bytes.
The expanded 32-entry diagnostic ring includes short control transactions;
the last controls had normal BUSY and ioctl times. Several 30–32 KiB batches
took almost as many microseconds as bytes, leaving little headroom beyond
the 1 MB/s source once publication and control work are included. FIFO levels
rose to 848 before failure. A faster transport clock with a correctly derived
reserve factor is a possible next controlled experiment, not an implemented
or proven solution.

Pluto also rebooted independently during an earlier run. Subsequent continuous
COM12 monitoring showed stable uptime and approximately 444 MiB available
memory; later sources remained healthy until receiver-triggered cancellation.
No device firmware, network, USB driver or RF waveform changes were made.
All current tests and monitors are stopped. Fresh independent USB cleanup
verified TX powerdown 1 at 23:39:23 UTC; SPI scheduling, character-buffer size,
display manager, keyboard PWM and RT runtime are restored. The PREEMPT trial
kernel remains running.

### Explicit DMA IRQ threading trial (November 1, 01:11 UTC)

The user reported that the full application was upside down. The KMS rotation
has been corrected from 90 to 270 degrees in both test profiles and their
launcher. Physical confirmation of the corrected direction is still pending.
The user also confirmed that Pluto has an independent power supply; previous
device reboots do not establish a PC USB power problem.

Subsequent diagnostics at 16 MHz ruled out the short idle wait and publication
as the cause of the latest overflow: their maxima were 77 and 5 microseconds,
while the loss occurred during DMA transfers. The previous PREEMPT kernel did
not create DMA IRQ threads despite the threadirqs command line option.

A separate same-version kernel, `6.18.50-trailmate-irq-v7`, now explicitly
threads BCM2835 DMA completion and makes the BCM2836 GPU interrupt rotation
respect the default IRQ CPU mask. All 17,766 existing exported symbol CRCs
match the previous build. Its build and staging completed successfully, and
the device booted it using one-shot tryboot. The original boot configuration
and command line hashes remain unchanged. This is still an experimental
kernel, not a qualified permanent deployment.

Runtime inspection confirms both SPI DMA IRQ threads exist and peripheral
hard IRQ counts on CPU3 are zero. An identity-checked policy places the two
SPI DMA threads on CPU3 at FIFO priority 80 and other IRQ threads on CPUs0-2.
Configuration and restoration of all 13 IRQ threads passed a round trip.
SPI, the DSI DRM device and the ES8389 audio card remain available.

A new full-file RF test is running with the unchanged CH13 waveform, corrected
270-degree KMS orientation, 500 kpair/s and 16 MHz SPI. By 01:11:26 UTC it had
presented 1,843 frames, with active audio, no audio starvation, and zero
capture, transport, continuity or CRC errors. Eighteen acquisition-stage decode
errors have remained constant since playback started. This is partial evidence
only; full-file reception is not yet qualified. Monitoring uses counters and
timestamps without screenshots.

### User acceptance and planned stop (November 1, 01:29 UTC)

The user explicitly confirmed the corrected screen direction, normal playback
on CM0, and that the test passed. Sony A919 reception had already been accepted
by the user with normal picture and audio. On that acceptance, the receiver
was stopped deliberately; this was not a full-file endurance pass.

The PID1664 application history contains 1,196 playback statuses spanning
1,205.449 seconds (approximately 20 minutes). All playback statuses report zero
capture, recovery, transport, continuity, CRC, media and audio-starvation errors.
The acquisition decode count remains 18 throughout playback. The last observed
playback status reports 18,056 presented frames and 57,962,496 PCM samples.
The terminal decode count is 18, with 18,089 decoded frames. The frozen source
controller reports successful cancellation after the deliberate stop, rather
than natural source EOF. Do not represent this run as complete-file playback.

An important limitation is retained: stopping the service at 01:29:30 UTC
coincides with a terminal capture overflow (`capture_error=-4`, FIFO flags63).
The preceding playback status has no errors. The stop-time event is preserved
in the raw capture summary and acceptance evidence; it has not been reclassified
as clean capture termination or proven unrelated to shutdown.

The receiver service exited successfully, its watchdog reports no cleanup
errors, and both local process handles are gone. A fresh independent Pluto
context verified `tx_powerdown_after=1` at 01:29:39 UTC. All 13 IRQ thread
settings, the SPI worker scheduling, spidev buffer4096, keyboard PWM, memory
policy1, CPU ondemand policy and the display manager were restored.

The accepted receiver configuration uses `6.18.50-trailmate-irq-v7`, 500 kpair/s,
16 MHz SPI, the scoped DMA IRQ policy, and KMS rotation270. This kernel remains
a one-shot trial: an ordinary reboot returns to the original boot configuration.
Acceptance establishes the requested physical reception demonstration; it does
not establish a permanent default-kernel deployment or clean-stop qualification.
Local evidence is retained in `.codex-build/cm0-rx-probe/run30-acceptance.json`
and `run30-app.log`; transmitter evidence is in the sibling OneSegCast build
directory `trailmate-usb-video-20261101-090841`.
