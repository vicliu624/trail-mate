# Broadcast FM DSP verification

Integration status and physical acceptance boundaries are tracked in
[the independent FM engineering record](../../docs/engineering/broadcast-fm-integration.md).

`fm_lr2021_rx` now tests the independent receive-only configuration at the user's
100.8 MHz test frequency, supported experimental boundaries, ownership/clock/ROM
guards, all ten setup I/O failures and explicit IMG_CAL handling. It uses a
test-only command port, never actual radio hardware. `fm_lr2021_stream`, built
with the existing `tests/oneseg` native capture-engine harness, additionally
checks the real portable checked FIFO engine at nominal 250 ksample/s IQ8,
empty-FIFO cancellation, loss/sink rejection, invalid HAL, strict/explicit
calibration policy and packet-PHY restoration. Source setup and nominal rate
metadata do not establish board RF bandwidth or a received station.

`BroadcastFmDecoder` is a platform-independent, allocation-free mono DSP core.
It takes channel-centred signed IQ16 at exactly 250,000 complex samples/second
and emits signed mono PCM16 at 48,000 samples/second. Each `push` consumes one
IQ pair and returns whether an output sample was written. Store the decoder in
the receiver owner's persistent storage, not on an ESP task stack.

The independently implemented chain uses a quadrature phase discriminator
normalised to 75 kHz deviation, a 30 Hz DC blocker, selectable 50/75 microsecond
de-emphasis, a 127-tap Blackman-windowed 15 kHz audio low-pass, and fractional
output scheduling. The filter is evaluated at output instants; adjacent filtered
input samples are interpolated to locate each 48 kHz instant. There is no heap
allocation, queued output or per-block timing reset.

Call `reset()` before the first sample of a new capture generation, after a
retune, or after lost/discontinuous IQ. The acquisition layer must detect those
events. The decoder cannot infer a lost block from a continuous sample stream.

`FmIqConsumer` implements that consumer-side boundary for checked IQ16 blocks in
little-endian order or signed IQ8 blocks of at most 1,280 bytes. IQ16 remains the
default; pass `Format::Iq8` explicitly for raw source-0/1 bytes. It validates the
input rate, byte alignment,
sample-index overflow and output capacity before consuming a block. A generation
change, duplicate/backward index, forward gap or format change resets the decoder.
Rejected blocks also break continuity. Reserve 62 PCM samples for a maximum IQ16
block, or 123 for IQ8; `kMaxPcmSamples` covers both. The format values are not chip
route numbers. Pair indices count actual complex samples, not four-byte words.
Signed IQ8 is expanded by multiplication by 256, avoiding negative left shifts;
the actual output count depends on the retained fractional sample phase.
Run it in a separate consumer task, never inside the radio's FIFO callback.

The pinned upstream [raw-route contract](https://github.com/SAMS0N1TE/lr2021-sdr/blob/0a44fc69beb34274d6e86f29c1c064b3d720c48d/docs/host-sdr.md#raw-receive-routes)
distinguishes source-0/1 IQ8 from source-6 gain-tagged IQ6. The latter carries
gain information in the low bits and is explicitly rejected as
`UnsupportedFormat`; it must not be cast to ordinary IQ8. Unknown formats are
also rejected and break continuity.

The source-2 route in the pinned RX engine derives sample rate from four times
the configured LoRa bandwidth. A 250 ksample/s clock alone does not prove that
the selected RF route preserves a broadcast FM channel. The hardware adapter
must establish both RF bandwidth and sustained FIFO throughput before selecting
this decoder. Unsupported rates are rejected rather than silently mis-timed.

Run the independent host tests with:

```sh
cmake -S tests/broadcast_fm -B .codex-build/broadcast-fm -G Ninja
cmake --build .codex-build/broadcast-fm
ctest --test-dir .codex-build/broadcast-fm --output-on-failure
```

Synthetic IQ tests check exactly 48,000 output samples from 250,000 input pairs,
1 kHz recovered amplitude at 20 kHz and full 75 kHz deviation, carrier-offset DC
rejection, both de-emphasis constants, harmonic distortion, greater than 40 dB
rejection of a 40 kHz demodulated component aliasing to 8 kHz, exact continuity
across irregular chunks, and reset/silence/resume behavior.

IQ8 tests compare 100 maximum-sized blocks against exactly equivalent IQ16
samples, yielding 12,288 identical PCM samples. They cover signed extrema,
two-byte pair indexing, format-switch reset, odd input, index overflow,
unsupported formats/rates and atomic output-capacity rejection. A separate
synthetic 1 kHz FM signal at full 75 kHz deviation uses only six significant IQ8
bits and recovers the expected de-emphasised amplitude. This verifies DSP and
format handling, not RF reception.

The current RadioLib packet API [rejects LF tuning below 150 MHz](https://github.com/jgromes/RadioLib/blob/2325a38a89dd0f0e402c19f67fe910111da1e246/src/modules/LR2021/LR2021_config.cpp#L88).
Broadcast FM therefore needs a separately owned, receive-only tuning path and
RF/filter validation rather than routing the FM band through ordinary packet
configuration. Packet frequency validation remains intact. At 250 ksample/s,
IQ8 needs 500,000 bytes/s versus 1,000,000 for IQ16; the current 1 MHz CM0 SPI
bus still cannot sustain either. The input code, passband, measured FIFO
service timing and actual frequency range must be qualified before a receiver
can be enabled.

This does not provide an LR2021 raw-IQ driver, RF channel filtering, stereo or
RDS, audio focus/playback, measured target throughput or real-station reception.
The current firmware does not expose an operational FM receiver. Device
integration still needs bounded capture buffers with generation/loss reporting,
exclusive radio ownership and mesh restoration, target performance measurements,
audio ownership, and physical frequency-range/reception acceptance.
# Independent FM receiver service

`platform/shared/radio/fm_receiver.h` provides a worker-owned service for the
independent FM application. It composes the IQ consumer with explicit radio and
audio ownership ports. It is separate from Protocol Probe and does not call the
finite Probe capture API.

The receiver validates the 87.5–108 MHz application band, requests qualified
250 kpair/s reception and 48 kHz mono output, rejects stale session blocks, and
stops on subsequent IQ discontinuities or short audio writes. `Receiving` means
the backend started; `Playing` requires PCM accepted by the audio port. Neither
state proves an audible station or a measured RF passband. Stop restores the
radio PHY, flushes audio, and releases both owners; a cleanup failure remains
visible alongside the originating error.

`fm_receiver_test` verifies startup failure unwinding, ownership and cleanup
order, station changes, stale data rejection, discontinuity handling, invalid
rates, short writes, and repeated stop. These are host tests with a fake port.
No device implements this port yet: continuous LR2021 reception, actual speaker
output, the standalone menu/page, tuning/seek controls, and hardware acceptance
remain required. Do not expose a working FM launcher based on this test alone.
